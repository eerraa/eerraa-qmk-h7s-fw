#include "era_macro.h"

#ifdef ERA_MACRO_ENABLE
#include "action.h"
#include "action_util.h"
#include "dynamic_keymap.h"
#include "send_string.h"
#include "timer.h"

#ifndef DYNAMIC_KEYMAP_MACRO_DELAY
#    define DYNAMIC_KEYMAP_MACRO_DELAY TAP_CODE_DELAY
#endif

/* The storage module owns its layout. Its runtime-sized macro region cannot
 * exceed the board's complete EEPROM image; no format constants are copied. */
static uint8_t snapshot[TOTAL_EEPROM_BYTE_COUNT];
static uint8_t pending_ids[ERA_MACRO_QUEUE_CAPACITY];
static uint8_t pending_head, pending_count;
static uint16_t cursor, limit;
static bool active, session_known, session_valid, paused, waiting;
static uint32_t session_generation, wait_stamp, wait_remaining;
static era_macro_stats_t counters;

typedef enum {
    MACRO_READ,
    MACRO_COMMAND_INTERVAL,
    MACRO_COMMAND_TAP_UP,
    MACRO_CHAR_SHIFT,
    MACRO_CHAR_ALTGR,
    MACRO_CHAR_TAP,
    MACRO_CHAR_TAP_UP,
    MACRO_CHAR_ALTGR_UP,
    MACRO_CHAR_SHIFT_UP,
    MACRO_CHAR_DEAD,
    MACRO_CHAR_DONE
} macro_phase_t;
static macro_phase_t phase;
static uint8_t char_key;
static bool char_shift, char_altgr, char_dead;

static void macro_wait(uint32_t delay_ms) {
    wait_remaining = delay_ms;
    wait_stamp = timer_read32();
    waiting = delay_ms != 0U;
}

static void macro_elapsed(uint32_t now) {
    if (!waiting) return;
    uint32_t elapsed = now - wait_stamp;
    wait_stamp = now;
    if (elapsed >= wait_remaining) {
        wait_remaining = 0U;
        waiting = false;
    } else {
        wait_remaining -= elapsed;
    }
}

static void macro_finish(bool malformed) {
    /* Only string helper state is temporary. Explicit DOWN remains owned
     * across ordinary completion and malformed-command abort until its UP. */
    action_macro_clear_owner(ACTION_MACRO_TEMPORARY);
    active = waiting = false;
    if (malformed) ++counters.malformed;
}

static bool macro_start(uint8_t id) {
    uint16_t size = dynamic_keymap_macro_get_buffer_size();
    if (id >= dynamic_keymap_macro_get_count() || size == 0U || size > sizeof(snapshot)) return false;
    dynamic_keymap_macro_get_buffer(0U, size, snapshot);
    if (snapshot[size - 1U] != 0U) return false;
    uint16_t position = 0U;
    while (id > 0U && position < size) {
        if (snapshot[position++] == 0U) --id;
    }
    if (id != 0U || position >= size) return false;
    cursor = position;
    limit = size;
    phase = MACRO_READ;
    active = true;
    waiting = false;
    return true;
}

bool era_macro_request(uint8_t id) {
    if (!session_known || !session_valid || id >= dynamic_keymap_macro_get_count()) {
        ++counters.rejected_invalid;
        return false;
    }
    /* Dispatch records an ID only. Snapshot/selection belongs to the task. */
    if (pending_count == ERA_MACRO_QUEUE_CAPACITY) {
        ++counters.rejected_full;
        return false;
    }
    pending_ids[(pending_head + pending_count) % ERA_MACRO_QUEUE_CAPACITY] = id;
    ++pending_count;
    ++counters.accepted;
    return true;
}

void era_macro_cancel(void) {
    if (active || pending_count || action_macro_has_outputs()) ++counters.canceled;
    active = waiting = false;
    pending_head = pending_count = 0U;
    action_macro_cancel_outputs();
}

void era_macro_session(uint32_t generation, bool valid, bool suspended) {
    uint32_t now = timer_read32();
    if ((session_known && generation != session_generation) || (session_valid && !valid)) era_macro_cancel();
    session_known = true;
    session_generation = generation;
    session_valid = valid;
    if (suspended && !paused) macro_elapsed(now);
    if (!suspended && paused) wait_stamp = now;
    paused = suspended;
}

bool era_macro_report_generation(uint32_t *generation) {
    if (!session_known || (!action_macro_is_emitting() && !action_macro_has_outputs())) return false;
    *generation = session_generation;
    return true;
}

static uint8_t macro_read(void) {
    return cursor < limit ? snapshot[cursor++] : 0U;
}

static void macro_code(uint8_t code, uint8_t command) {
    uint8_t owner = command == SS_TAP_CODE ? ACTION_MACRO_TEMPORARY : ACTION_MACRO_PERSISTENT;
    uint8_t previous = action_macro_set_owner(owner);
    if (command == SS_TAP_CODE) tap_code(code);
    else if (command == SS_DOWN_CODE) register_code(code);
    else unregister_code(code);
    action_macro_restore_owner(previous);
}

static void macro_char_edge(uint8_t code, bool down) {
    uint8_t previous = action_macro_set_owner(ACTION_MACRO_TEMPORARY);
    if (down) register_code(code);
    else unregister_code(code);
    action_macro_restore_owner(previous);
}

static bool macro_command(void) {
    uint8_t command = macro_read();
    if (command == 0U) return false;
    if (command == SS_TAP_CODE || command == SS_DOWN_CODE || command == SS_UP_CODE) {
        uint8_t key = macro_read();
        if (key == 0U) return false;
        if (command == SS_TAP_CODE && TAP_CODE_DELAY != 0U &&
            !IS_BASIC_KEYCODE(key) && !IS_MODIFIER_KEYCODE(key)) {
            char_key = key;
            macro_char_edge(key, true);
            phase = MACRO_COMMAND_TAP_UP;
            if (active) macro_wait(TAP_CODE_DELAY);
        } else {
            macro_code(key, command);
            if (active) macro_wait(DYNAMIC_KEYMAP_MACRO_DELAY);
        }
    } else if (command == SS_DELAY_CODE) {
        uint32_t ms = 0U;
        uint8_t digits = 0U;
        for (;;) {
            uint8_t digit = macro_read();
            if (digit == '|') break;
            if (digit < '0' || digit > '9' || digits == 4U) return false;
            ms = ms * 10U + digit - '0';
            ++digits;
        }
        phase = MACRO_COMMAND_INTERVAL;
        macro_wait(ms);
    } else {
        /* QMK ignores an unknown command but retains its ordinary interval. */
        macro_wait(DYNAMIC_KEYMAP_MACRO_DELAY);
    }
    return true;
}

void era_macro_task(void) {
    if (!session_valid || paused) return;
    macro_elapsed(timer_read32());
    if (waiting) return;
    /* Copy/selection is a separate large cost: at most one activation, then
     * at most sixteen small execution phases per main iteration. */
    bool activated = false;
    for (uint8_t budget = 0U; budget < 16U && !waiting; ++budget) {
        if (!active) {
            if (activated || !pending_count) return;
            activated = true;
            uint8_t id = pending_ids[pending_head];
            pending_head = (pending_head + 1U) % ERA_MACRO_QUEUE_CAPACITY;
            --pending_count;
            if (!macro_start(id)) {
                ++counters.rejected_invalid;
                continue;
            }
        }
        switch (phase) {
            case MACRO_READ: {
                uint8_t ascii = macro_read();
                if (!ascii) {
                    macro_finish(false);
                    break;
                }
                if (ascii == SS_QMK_PREFIX) {
                    if (!macro_command()) macro_finish(true);
                    break;
                }
                if (ascii >= 128U) {
                    macro_finish(true);
                    break;
                }
                char_key = pgm_read_byte(&ascii_to_keycode_lut[ascii]);
                char_shift = (pgm_read_byte(&ascii_to_shift_lut[ascii / 8U]) >> (ascii % 8U)) & 1U;
                char_altgr = (pgm_read_byte(&ascii_to_altgr_lut[ascii / 8U]) >> (ascii % 8U)) & 1U;
                char_dead = (pgm_read_byte(&ascii_to_dead_lut[ascii / 8U]) >> (ascii % 8U)) & 1U;
                phase = MACRO_CHAR_SHIFT;
                break;
            }
            case MACRO_COMMAND_TAP_UP:
                macro_char_edge(char_key, false);
                phase = MACRO_READ;
                if (active) macro_wait(DYNAMIC_KEYMAP_MACRO_DELAY);
                break;
            case MACRO_COMMAND_INTERVAL:
                phase = MACRO_READ;
                macro_wait(DYNAMIC_KEYMAP_MACRO_DELAY);
                break;
            case MACRO_CHAR_SHIFT:
                phase = MACRO_CHAR_ALTGR;
                if (char_shift) {
                    macro_char_edge(KC_LEFT_SHIFT, true);
                    if (active) macro_wait(DYNAMIC_KEYMAP_MACRO_DELAY);
                }
                break;
            case MACRO_CHAR_ALTGR:
                phase = MACRO_CHAR_TAP;
                if (char_altgr) {
                    macro_char_edge(KC_RIGHT_ALT, true);
                    if (active) macro_wait(DYNAMIC_KEYMAP_MACRO_DELAY);
                }
                break;
            case MACRO_CHAR_TAP: {
                phase = MACRO_CHAR_ALTGR_UP;
                if (IS_BASIC_KEYCODE(char_key) || IS_MODIFIER_KEYCODE(char_key)) {
                    uint8_t previous = action_macro_set_owner(ACTION_MACRO_TEMPORARY);
                    tap_code_delay(char_key, DYNAMIC_KEYMAP_MACRO_DELAY);
                    action_macro_restore_owner(previous);
                } else {
                    /* tap_code_wait sleeps for non-keyboard codes, including
                     * KC_NO. Preserve that dwell with a cooperative release. */
                    macro_char_edge(char_key, true);
                    phase = MACRO_CHAR_TAP_UP;
                }
                if (active) macro_wait(DYNAMIC_KEYMAP_MACRO_DELAY);
                break;
            }
            case MACRO_CHAR_TAP_UP:
                macro_char_edge(char_key, false);
                phase = MACRO_CHAR_ALTGR_UP;
                if (active) macro_wait(DYNAMIC_KEYMAP_MACRO_DELAY);
                break;
            case MACRO_CHAR_ALTGR_UP:
                phase = MACRO_CHAR_SHIFT_UP;
                if (char_altgr) {
                    macro_char_edge(KC_RIGHT_ALT, false);
                    if (active) macro_wait(DYNAMIC_KEYMAP_MACRO_DELAY);
                }
                break;
            case MACRO_CHAR_SHIFT_UP:
                phase = MACRO_CHAR_DEAD;
                if (char_shift) {
                    macro_char_edge(KC_LEFT_SHIFT, false);
                    if (active) macro_wait(DYNAMIC_KEYMAP_MACRO_DELAY);
                }
                break;
            case MACRO_CHAR_DEAD:
                phase = MACRO_CHAR_DONE;
                if (char_dead) {
                    macro_code(KC_SPACE, SS_TAP_CODE);
                    if (active) macro_wait(DYNAMIC_KEYMAP_MACRO_DELAY);
                }
                break;
            case MACRO_CHAR_DONE:
                phase = MACRO_READ;
                break;
        }
        if (!session_valid || paused) return;
    }
}

void era_macro_get_stats(era_macro_stats_t *stats) {
    if (!stats) return;
    *stats = counters;
    stats->active = active;
    stats->queued = pending_count;
}
#endif
