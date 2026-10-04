#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "macro_mode.h"
#define TAP_DANCE_OWNED_ACTIONS
#define TAPDANCE_ENABLE
#define MOUSEKEY_ENABLE
#define MOUSE_SHARED_EP
#define EXTRAKEY_ENABLE
#define _USE_HW_WS2812
#define TOTAL_EEPROM_BYTE_COUNT 128U
#define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR 256U
#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE TOTAL_EEPROM_BYTE_COUNT
#define DYNAMIC_KEYMAP_MACRO_COUNT 16U
#define DYNAMIC_KEYMAP_MACRO_DELAY 10U
#ifdef MACRO_POSITIVE_TAP_DELAY
#define TAP_CODE_DELAY 5U
#else
#define TAP_CODE_DELAY 0U
#endif
#define TAP_HOLD_CAPS_DELAY 80U
#define PROGMEM
#define pgm_read_byte(address) (*(const uint8_t *)(address))
#define dprintf(...) ((void)0)
#define dprint(...) ((void)0)
#include "keycode.h"
#include "report.h"
#include "host_driver.h"
#include "process_keycode/process_tap_dance.h"
#include "era_macro.h"

#ifdef ERA_MACRO_ENABLE
enum { ACTION_MACRO_PERSISTENT, ACTION_MACRO_TEMPORARY };
uint8_t action_macro_set_owner(uint8_t);
void action_macro_restore_owner(uint8_t);
bool action_macro_has_outputs(void);
bool action_macro_is_emitting(void);
void action_macro_clear_owner(uint8_t);
void action_macro_cancel_outputs(void);
uint16_t action_owned_usage(uint8_t, uint16_t);
void host_extra_reconcile(void);
#endif
#define TD_USAGE_SYSTEM 0U
#define TD_USAGE_CONSUMER 1U

void register_code(uint8_t);
void unregister_code(uint8_t);
void register_mouse(uint8_t, bool);
void tap_code_wait(uint16_t, uint16_t);
void tap_code_delay(uint8_t, uint16_t);
void tap_code(uint8_t);
void clear_keyboard(void);
void clear_keyboard_but_mods(void);
void clear_keyboard_but_mods_and_keys(void);
void wait_ms(uint32_t);
void send_char_with_delay(char, uint8_t);
void send_string_with_delay(const char *, uint8_t);
void dynamic_keymap_macro_send(uint8_t);
uint8_t dynamic_keymap_macro_get_count(void);
uint16_t dynamic_keymap_macro_get_buffer_size(void);
void dynamic_keymap_macro_get_buffer(uint16_t, uint16_t, uint8_t *);
void host_keyboard_send(report_keyboard_t *);
void host_mouse_send(report_mouse_t *);
void host_system_send(uint16_t);
void host_consumer_send(uint16_t);
void host_keyboard_delay(uint16_t);
uint16_t host_last_system_usage(void);
uint16_t host_last_consumer_usage(void);
uint8_t get_mods(void);
uint8_t get_weak_mods(void);
void send_keyboard_report(void);
void mousekey_send(void);
typedef struct { struct { bool pressed; } event; } keyrecord_t;
bool process_record_via(uint16_t, keyrecord_t *);
void qmkUpdate(void);

typedef struct { uint32_t generation; bool valid, suspended; } usb_hid_session_t;
typedef struct { uint32_t time; bool bound; report_keyboard_t report; } captured_t;
static captured_t capture[1024];
typedef struct { uint32_t time; uint16_t length; uint8_t data[sizeof(report_mouse_t)]; } extra_captured_t;
static extra_captured_t extra_capture[2048];
static unsigned extra_capture_count;
static uint8_t storage[TOTAL_EEPROM_BYTE_COUNT];
static unsigned eeprom_reads;
static bool logical_keys[256];
static report_keyboard_t logical_report, accepted_keyboard;
static report_mouse_t logical_mouse, accepted_mouse;
static uint16_t accepted_system, accepted_consumer;
static uint32_t fixture_now, release_time, due_release, first_service_time;
static unsigned capture_count, service_calls, hal_waits, host_intervals, rejected_epoch, extra_sends;
static uint16_t host_interval;
static bool macro_press_pending, release_pending, inject_reset;
static usb_hid_session_t session = {1U, true, false};
static host_driver_t *driver;
static bool debug_keyboard;
static uint16_t last_system_usage, last_consumer_usage;
static uint32_t key_update_scan_token, key_update_count;
static uint8_t key_update_usage;
static bool key_update_pressed;

#ifdef ERA_MACRO_ENABLE
static uint32_t timer_read32(void) { return fixture_now; }
static usb_hid_session_t usbHidGetSession(void) { return session; }
#endif
static void HAL_Delay(uint32_t ms) { ++hal_waits; fixture_now += ms; }
void delay(uint32_t);
static uint8_t eeprom_read_byte(const uint8_t *address) {
    ++eeprom_reads;
    uintptr_t index = (uintptr_t)address - DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR;
    assert(index < sizeof(storage));
    return storage[index];
}
static bool command_proc(uint8_t code) { (void)code; return false; }
bool is_key_pressed(uint8_t code) { return logical_keys[code]; }
void add_key_to_report(uint8_t code) { logical_keys[code] = true; }
void del_key_from_report(uint8_t code) { logical_keys[code] = false; }
static void clear_keys(void) { memset(logical_keys, 0, sizeof(logical_keys)); }
void send_keyboard_report(void) {
    memset(&logical_report, 0, sizeof(logical_report));
    logical_report.mods = get_mods() | get_weak_mods();
    unsigned slot = 0;
    for (unsigned code = 1U; code < 256U && slot < KEYBOARD_REPORT_KEYS; ++code)
        if (logical_keys[code]) logical_report.keys[slot++] = (uint8_t)code;
    host_keyboard_send(&logical_report);
}
#ifdef ERA_MACRO_ENABLE
static bool accepted_key(uint8_t code) {
    for (unsigned slot = 0; slot < KEYBOARD_REPORT_KEYS; ++slot)
        if (accepted_keyboard.keys[slot] == code) return true;
    return false;
}
#endif
static bool record_keyboard(uint8_t *data, uint16_t length, bool bound) {
    assert(length == sizeof(report_keyboard_t) && capture_count < 1024U);
    if (!session.valid) return false;
    memcpy(&accepted_keyboard, data, length);
    capture[capture_count++] = (captured_t){fixture_now, bound, accepted_keyboard};
    return true;
}
static bool usbHidSendReport(uint8_t *data, uint16_t length) { return record_keyboard(data, length, false); }
static bool usbHidSubmitKeyUpdate(uint8_t *data, uint16_t length, uint32_t token, uint8_t usage, bool pressed) {
    (void)token; (void)usage; (void)pressed; return usbHidSendReport(data, length);
}
static bool usbHidSendReportEXK(uint8_t *data, uint16_t length) {
    if (!session.valid) return false;
    ++extra_sends;
    assert(extra_capture_count < 2048U && length <= sizeof(extra_capture[0].data));
    extra_captured_t *entry = &extra_capture[extra_capture_count++];
    *entry = (extra_captured_t){.time = fixture_now, .length = length};
    memcpy(entry->data, data, length);
    if (data[0] == REPORT_ID_MOUSE) {
        assert(length == sizeof(report_mouse_t)); memcpy(&accepted_mouse, data, length);
    } else {
        assert(length == sizeof(report_extra_t));
        report_extra_t extra; memcpy(&extra, data, length);
        if (extra.report_id == REPORT_ID_SYSTEM) accepted_system = extra.usage;
        else if (extra.report_id == REPORT_ID_CONSUMER) accepted_consumer = extra.usage;
        else assert(false);
    }
    return true;
}
static void usbHidDelayKeyboardReport(uint16_t interval) { ++host_intervals; host_interval = interval; }
#ifdef ERA_MACRO_ENABLE
static bool same_epoch(uint32_t generation) {
    if (inject_reset) {
        inject_reset = false; ++session.generation;
        memset(&accepted_keyboard, 0, sizeof(accepted_keyboard));
        memset(&accepted_mouse, 0, sizeof(accepted_mouse));
        accepted_system = accepted_consumer = 0U;
    }
    if (!session.valid || generation != session.generation) { ++rejected_epoch; return false; }
    return true;
}
static bool usbHidSendReportForGeneration(uint8_t *data, uint16_t length, uint32_t generation) {
    return same_epoch(generation) && record_keyboard(data, length, true);
}
static bool usbHidSendReportEXKForGeneration(uint8_t *data, uint16_t length, uint32_t generation) {
    return same_epoch(generation) && usbHidSendReportEXK(data, length);
}
static void usbHidDelayKeyboardReportForGeneration(uint16_t interval, uint32_t generation) {
    if (same_epoch(generation)) usbHidDelayKeyboardReport(interval);
}
#endif
static void mousekey_on(uint8_t code) {
    if (code >= KC_MS_BTN1 && code <= KC_MS_BTN8) logical_mouse.buttons |= 1U << (code - KC_MS_BTN1);
}
static void mousekey_off(uint8_t code) {
    if (code >= KC_MS_BTN1 && code <= KC_MS_BTN8) logical_mouse.buttons &= ~(1U << (code - KC_MS_BTN1));
}
void mousekey_send(void) { host_mouse_send(&logical_mouse); }
report_mouse_t mousekey_get_report(void) { return logical_mouse; }
static void mousekey_clear(void) { memset(&logical_mouse, 0, sizeof(logical_mouse)); }
static void tap_dance_cancel_all(void) { /* TD runtime cancellation is exercised by its own production fixture. */ }
static void keyboard_task(void) {
    if (macro_press_pending) {
        macro_press_pending = false;
        keyrecord_t record = {.event.pressed = true};
        assert(!process_record_via(QK_MACRO_0, &record));
    } else if (release_pending && fixture_now >= due_release) {
        unregister_code(KC_A);
        release_pending = false;
        release_time = fixture_now;
    }
}
static void ws2812Task(void) { if (!service_calls) first_service_time = fixture_now; ++service_calls; }
static void via_hid_task(void) { ++service_calls; }
static void eeprom_task(void) { ++service_calls; }
static void idle_task(void) { ++service_calls; }

#include "macro_production.inc"

static void load(const uint8_t *data, size_t size) {
    assert(size <= sizeof(storage));
    memset(storage, 0, sizeof(storage)); memcpy(storage, data, size);
}
static void reset_fixture(const uint8_t *data, size_t size) {
    ++session.generation; session.valid = true; session.suspended = false;
#ifdef ERA_MACRO_ENABLE
    era_macro_session(session.generation, true, false);
#endif
    clear_keyboard();
    fixture_now = capture_count = service_calls = hal_waits = host_intervals = rejected_epoch = extra_sends = 0U;
    extra_capture_count = 0U;
    first_service_time = release_time = UINT32_MAX;
    macro_press_pending = release_pending = inject_reset = false;
    last_system_usage = last_consumer_usage = accepted_system = accepted_consumer = 0U;
    memset(&accepted_keyboard, 0, sizeof(accepted_keyboard));
    memset(&accepted_mouse, 0, sizeof(accepted_mouse));
    load(data, size);
    eeprom_reads = 0U;
}
static int liveness(void) {
    const uint8_t macro[] = {SS_QMK_PREFIX, SS_DELAY_CODE, '1','0','0','0','|', 0};
    reset_fixture(macro, sizeof(macro));
    register_code(KC_A);
    due_release = 50U; release_pending = macro_press_pending = true;
    qmkUpdate();
    while (release_time == UINT32_MAX && fixture_now < 60U) { ++fixture_now; qmkUpdate(); }
    if (release_time == UINT32_MAX) qmkUpdate();
    printf("LIVENESS: due=50, release=%lu, service-first=%lu, HAL-waits=%u\n",
           (unsigned long)release_time, (unsigned long)first_service_time, hal_waits);
    if (release_time > 60U || first_service_time > 60U) {
        fprintf(stderr, "FAIL: production macro blocks ordinary release/service\n"); return 1;
    }
    assert(release_time == 50U && first_service_time == 0U && hal_waits == 0U && service_calls >= 50U * 4U);
    return 0;
}

#ifdef ERA_MACRO_ENABLE
static void at(uint32_t time) { fixture_now = time; qmkUpdate(); }
static bool busy(void) { era_macro_stats_t stats; era_macro_get_stats(&stats); return stats.active || stats.queued; }
static void drain(unsigned max_ms) {
    for (unsigned step = 0U; busy() && step <= max_ms; ++step) { qmkUpdate(); ++fixture_now; }
    assert(!busy());
}
static void trace_parity(uint8_t value, bool command) {
    const uint8_t empty[] = {0};
    reset_fixture(empty, sizeof(empty));
    if (command) { tap_code(value); wait_ms(DYNAMIC_KEYMAP_MACRO_DELAY); }
    else send_char_with_delay((char)value, DYNAMIC_KEYMAP_MACRO_DELAY);
    uint32_t end = fixture_now;
    unsigned count = capture_count, extras = extra_capture_count, intervals = host_intervals;
    captured_t reference[16]; extra_captured_t extra_reference[16];
    assert(count <= 16U && extras <= 16U);
    memcpy(reference, capture, count * sizeof(reference[0]));
    memcpy(extra_reference, extra_capture, extras * sizeof(extra_reference[0]));
    const uint8_t text[] = {value, 0};
    const uint8_t code[] = {SS_QMK_PREFIX, SS_TAP_CODE, value, 0};
    reset_fixture(command ? code : text, command ? sizeof(code) : sizeof(text));
    dynamic_keymap_macro_send(0);
    for (unsigned step = 0U; busy() && step < 100U; ++step) {
        qmkUpdate();
        if (busy()) ++fixture_now;
    }
    if (hal_waits) {
        printf("FAIL: %s %u calls HAL wait %u; simulated time=%lu\n", command ? "TAP" : "ASCII", value,
               hal_waits, (unsigned long)fixture_now); fflush(stdout);
    }
    assert(hal_waits == 0U && !busy() && fixture_now == end);
    assert(capture_count == count && extra_capture_count == extras && host_intervals == intervals);
    for (unsigned report = 0U; report < count; ++report) {
        assert(capture[report].time == reference[report].time);
        assert(memcmp(&capture[report].report, &reference[report].report, sizeof(report_keyboard_t)) == 0);
    }
    for (unsigned report = 0U; report < extras; ++report) {
        assert(extra_capture[report].time == extra_reference[report].time);
        assert(extra_capture[report].length == extra_reference[report].length);
        assert(memcmp(extra_capture[report].data, extra_reference[report].data, extra_reference[report].length) == 0);
    }
    assert(!action_macro_has_outputs());
}
static void all_taps_nonblocking(void) {
    /* CR is the independent review witness: KC_NO used to HAL-wait 10ms. */
    trace_parity(13U, false);
    for (unsigned ascii = 1U; ascii < 128U; ++ascii)
        if (ascii != SS_QMK_PREFIX) trace_parity((uint8_t)ascii, false);
    for (unsigned code = 1U; code <= UINT8_MAX; ++code) trace_parity((uint8_t)code, true);
    const uint8_t caps[] = {SS_QMK_PREFIX, SS_TAP_CODE, KC_CAPS_LOCK, 0};
    reset_fixture(caps, sizeof(caps)); dynamic_keymap_macro_send(0); at(0);
    assert(host_intervals == 1U && host_interval == 80U && hal_waits == 0U);
    at(10); assert(!busy());
    puts("PASS: ASCII1..127 except prefix and TAP1..255 match synchronous output/timing with macro HAL waits=0; Caps80 remains host interval");
#ifdef MACRO_NONKEYBOARD_LAYOUT
    puts("PASS: custom KC_NO+AltGr+dead, consumer+shift+AltGr and mouse+shift+dead LUTs preserve cooperative dwell/cleanup");
#endif
#ifdef MACRO_POSITIVE_TAP_DELAY
    puts("PASS: positive TAP_CODE_DELAY=5 nonkeyboard command release is cooperative; keyboard remains host interval");
#endif
}
static void order_and_lifetime(void) {
    const uint8_t sequence[] = {SS_QMK_PREFIX, SS_DOWN_CODE, KC_B,
        SS_QMK_PREFIX, SS_DELAY_CODE,'1','0','0','0','|',SS_QMK_PREFIX,SS_UP_CODE,KC_B,0};
    reset_fixture(sequence, sizeof(sequence));
    dynamic_keymap_macro_send(0); at(0); at(10); at(1010); at(1020); at(1030);
    assert(capture_count == 2U && capture[0].time == 0U && capture[1].time == 1020U && !busy());
    assert(!accepted_key(KC_B) && capture[1].bound && hal_waits == 0U);
    const uint8_t text[] = {'A','a',0};
    reset_fixture(text, sizeof(text)); dynamic_keymap_macro_send(0);
    at(0); at(10); at(20); at(30); at(40);
    assert(capture_count == 6U && host_interval == 10U && host_intervals == 2U);
    assert(capture[0].report.mods == MOD_BIT(KC_LEFT_SHIFT));
    assert(capture[1].time == 10U && capture[2].time == 10U && capture[3].time == 20U);
    assert(capture[4].time == 30U && capture[5].time == 30U && !busy());

    const uint8_t held[] = {SS_QMK_PREFIX,SS_DOWN_CODE,KC_A,SS_QMK_PREFIX,SS_DOWN_CODE,KC_LEFT_SHIFT,0,
                           SS_QMK_PREFIX,SS_UP_CODE,KC_A,SS_QMK_PREFIX,SS_UP_CODE,KC_LEFT_SHIFT,0};
    reset_fixture(held, sizeof(held));
    register_code(KC_A); register_code(KC_LEFT_SHIFT);
    dynamic_keymap_macro_send(0); drain(40);
    unregister_code(KC_A); unregister_code(KC_LEFT_SHIFT);
    assert(accepted_key(KC_A) && accepted_keyboard.mods == MOD_BIT(KC_LEFT_SHIFT));
    uint8_t previous = tap_dance_action_set_owner(0U); register_code(KC_A); register_code(KC_LEFT_SHIFT);
    tap_dance_action_set_owner(previous);
    dynamic_keymap_macro_send(1); drain(40);
    assert(accepted_key(KC_A) && accepted_keyboard.mods == MOD_BIT(KC_LEFT_SHIFT));
    previous = tap_dance_action_set_owner(0U); unregister_code(KC_A); unregister_code(KC_LEFT_SHIFT);
    tap_dance_action_set_owner(previous);
    assert(!accepted_key(KC_A) && accepted_keyboard.mods == 0U);

    reset_fixture(text, sizeof(text)); register_code(KC_LEFT_SHIFT); dynamic_keymap_macro_send(0); drain(60);
    assert(accepted_keyboard.mods == MOD_BIT(KC_LEFT_SHIFT));
    puts("PASS: wire order, 1000+10ms timing, string/transport intervals and ordinary/TD/macro key+modifier lifetime");
}

static void queue_and_snapshot(void) {
    uint8_t data[] = {SS_QMK_PREFIX,SS_DELAY_CODE,'1','0','0','|','a',0,
                      'b',0,'c',0,'d',0,'e',0,'f',0,'g',0,'h',0,'i',0,'j',0};
    reset_fixture(data, sizeof(data));
    assert(era_macro_request(0)); at(0);
    for (uint8_t id = 1; id <= ERA_MACRO_QUEUE_CAPACITY; ++id) assert(era_macro_request(id));
    era_macro_stats_t before, after; era_macro_get_stats(&before);
    assert(!era_macro_request(9)); era_macro_get_stats(&after);
    assert(after.queued == 8U && after.rejected_full == before.rejected_full + 1U);
    storage[6] = 'z'; storage[8] = 'k'; /* active retains a; queued ID 1 resolves new k */
    drain(300);
    uint8_t pressed[9]; unsigned count = 0;
    for (unsigned i = 0; i < capture_count; ++i)
        if (capture[i].report.keys[0]) pressed[count++] = capture[i].report.keys[0];
    const uint8_t expected[] = {KC_A,KC_K,KC_C,KC_D,KC_E,KC_F,KC_G,KC_H,KC_I};
    assert(count == sizeof(expected) && memcmp(pressed, expected, sizeof(expected)) == 0);
    const uint8_t invalid[] = {0}; reset_fixture(invalid, sizeof(invalid));
    storage[sizeof(storage)-1] = 0xFF;
    assert(era_macro_request(0) && !era_macro_request(16));
    era_macro_get_stats(&before); at(fixture_now); era_macro_get_stats(&after);
    assert(!busy() && after.rejected_invalid == before.rejected_invalid + 1U);
    puts("PASS: FIFO8 newest rejection, active snapshot and queued execution-time snapshot, invalid marker/id");
}

static void malformed_and_clear(void) {
    const uint8_t bad[] = {SS_QMK_PREFIX,SS_DOWN_CODE,KC_B,
                          SS_QMK_PREFIX,SS_DELAY_CODE,'1','2','3','4','5','|',0};
    reset_fixture(bad, sizeof(bad)); dynamic_keymap_macro_send(0); drain(40);
    assert(accepted_key(KC_B) && !busy());
    const uint8_t ends[] = {SS_QMK_PREFIX,SS_TAP_CODE,0};
    reset_fixture(ends, sizeof(ends)); dynamic_keymap_macro_send(0); drain(10);
    assert(!busy() && capture_count == 0U);
    const uint8_t shift[] = {'A',0}; reset_fixture(shift, sizeof(shift));
    register_code(KC_LEFT_SHIFT); dynamic_keymap_macro_send(0); at(0);
    assert(action_macro_has_outputs());
    assert(era_macro_request(0)); clear_keyboard();
    assert(!busy() && !action_macro_has_outputs() && accepted_keyboard.mods == 0U);
    puts("PASS: bounded malformed abort, explicit DOWN retained, clear retires active/queue/temp modifiers");
}

static void reader_bounds(void) {
    const uint8_t empty[] = {0};
    for (uint8_t id = 0U; id < DYNAMIC_KEYMAP_MACRO_COUNT; ++id) {
        reset_fixture(empty, sizeof(empty));
        assert(era_macro_request(id)); drain(1);
        assert(capture_count == 0U);
    }
    reset_fixture(empty, sizeof(empty)); memset(storage, 'a', sizeof(storage)-1U);
    assert(era_macro_request(1U)); drain(1);
    assert(capture_count == 0U);
    assert(era_macro_request(0U)); drain(2000);
    assert(capture_count == (sizeof(storage)-1U)*2U);
    const uint8_t commands[] = {SS_TAP_CODE,SS_DOWN_CODE,SS_UP_CODE,SS_DELAY_CODE};
    for (unsigned command = 0; command < sizeof(commands); ++command) {
        for (unsigned remaining = 1; remaining <= 6U; ++remaining) {
            reset_fixture(empty, sizeof(empty)); memset(storage, 'a', sizeof(storage)-1U);
            unsigned start = sizeof(storage)-1U-remaining;
            storage[start] = SS_QMK_PREFIX;
            if (remaining > 1U) storage[start+1U] = commands[command];
            assert(era_macro_request(0)); drain(2000);
            assert(hal_waits == 0U);
        }
    }
    puts("PASS: all empty IDs, missing Nth macro, full buffer/final sentinel and truncated commands at every 1..6 byte boundary");
}

static void suspend_wrap_and_epoch(void) {
    const uint8_t delayed[] = {SS_QMK_PREFIX,SS_DELAY_CODE,'1','0','0','0','|','b',0};
    reset_fixture(delayed, sizeof(delayed)); dynamic_keymap_macro_send(0); at(0);
    fixture_now = 100; session.suspended = true; qmkUpdate();
    at(1100); assert(capture_count == 0U && busy());
    session.suspended = false; at(1100); at(1999); assert(capture_count == 0U);
    at(2000); at(2010); assert(capture_count == 2U && capture[0].time == 2010U);
    reset_fixture(delayed, sizeof(delayed)); fixture_now = UINT32_MAX - 500U;
    uint32_t start = fixture_now; dynamic_keymap_macro_send(0); qmkUpdate();
    at(start + 999U); assert(capture_count == 0U);
    at(start + 1000U); at(start + 1010U); assert(capture_count == 2U);

    const uint8_t held[] = {SS_QMK_PREFIX,SS_DOWN_CODE,KC_A,0};
    reset_fixture(held, sizeof(held)); dynamic_keymap_macro_send(0); drain(20);
    register_code(KC_B);
    inject_reset = true; register_code(KC_C); /* IRQ reset between producer provenance and admission */
    assert(rejected_epoch == 1U && !accepted_key(KC_A));
    qmkUpdate();
    assert(!action_macro_has_outputs() && accepted_key(KC_B) && accepted_key(KC_C) && !accepted_key(KC_A));
    assert(!capture[capture_count-1U].bound);

    reset_fixture(held, sizeof(held)); register_code(KC_A); dynamic_keymap_macro_send(0); drain(20);
    memset(&accepted_keyboard, 0, sizeof(accepted_keyboard)); ++session.generation;
    qmkUpdate(); assert(accepted_key(KC_A) && !action_macro_has_outputs());
    reset_fixture(delayed, sizeof(delayed)); dynamic_keymap_macro_send(0); at(0); assert(era_macro_request(0));
    session.valid = false; qmkUpdate(); assert(!busy());
    session.valid = true; ++session.generation; qmkUpdate(); assert(capture_count <= 1U);
    puts("PASS: same-session pause with remaining delay, timer wrap, epoch admission rejection and physical union reissue");
}

static void extra_ownership(void) {
    const uint8_t held[] = {SS_QMK_PREFIX,SS_DOWN_CODE,KC_SYSTEM_SLEEP,
                           SS_QMK_PREFIX,SS_DOWN_CODE,KC_AUDIO_VOL_UP,
                           SS_QMK_PREFIX,SS_DOWN_CODE,KC_MS_BTN1,0};
    reset_fixture(held, sizeof(held));
    register_code(KC_SYSTEM_SLEEP); register_code(KC_AUDIO_VOL_UP); register_code(KC_MS_BTN1);
    dynamic_keymap_macro_send(0); drain(50);
    unregister_code(KC_SYSTEM_SLEEP); unregister_code(KC_AUDIO_VOL_UP); unregister_code(KC_MS_BTN1);
    assert(accepted_system == SYSTEM_SLEEP && accepted_consumer == AUDIO_VOL_UP && accepted_mouse.buttons == 1U);
    uint8_t previous = tap_dance_action_set_owner(0);
    register_code(KC_SYSTEM_SLEEP); register_code(KC_AUDIO_VOL_UP); register_code(KC_MS_BTN1);
    tap_dance_action_set_owner(previous);
    logical_mouse.x = 9; logical_mouse.y = -7; logical_mouse.v = 3; logical_mouse.h = -2;
    accepted_system = accepted_consumer = 0U; accepted_mouse.buttons = 0U; ++session.generation;
    unsigned sends = extra_sends; qmkUpdate();
    assert(extra_sends >= sends+3U && accepted_system == SYSTEM_SLEEP && accepted_consumer == AUDIO_VOL_UP && accepted_mouse.buttons == 1U);
    assert(!action_macro_has_outputs());
    assert(accepted_mouse.x == 0 && accepted_mouse.y == 0 && accepted_mouse.v == 0 && accepted_mouse.h == 0);
    assert(logical_mouse.x == 9 && logical_mouse.y == -7 && logical_mouse.v == 3 && logical_mouse.h == -2);
    previous = tap_dance_action_set_owner(0);
    unregister_code(KC_SYSTEM_SLEEP); unregister_code(KC_AUDIO_VOL_UP); unregister_code(KC_MS_BTN1);
    tap_dance_action_set_owner(previous);
    assert(accepted_system == 0U && accepted_consumer == 0U && accepted_mouse.buttons == 0U);
    puts("PASS: ordinary/TD/macro shared system+consumer+mouse lifetime, force EXK reissue after generation retirement");
}
#ifdef MACRO_CUSTOM_LAYOUT
static void custom_layout_parity(void) {
    const uint8_t empty[] = {0};
    const char chars[] = {'~', '^'};
    for (unsigned character = 0U; character < sizeof(chars); ++character) {
        reset_fixture(empty, sizeof(empty));
        send_char_with_delay(chars[character], DYNAMIC_KEYMAP_MACRO_DELAY);
        captured_t reference[16]; unsigned count = capture_count;
        assert(count <= 16U); memcpy(reference, capture, count * sizeof(reference[0]));
        uint32_t end = fixture_now; unsigned intervals = host_intervals;
        const uint8_t macro[] = {(uint8_t)chars[character],0};
        reset_fixture(macro, sizeof(macro)); dynamic_keymap_macro_send(0); drain(80);
        assert(capture_count == count && host_intervals == intervals && hal_waits == 0U);
        for (unsigned report = 0U; report < count; ++report) {
            assert(capture[report].time == reference[report].time);
            assert(memcmp(&capture[report].report, &reference[report].report, sizeof(report_keyboard_t)) == 0);
        }
        assert(end == (character == 0U ? 50U : 40U));
    }
    puts("PASS: custom AltGr/dead-key executor trace matches actual synchronous send_char timing/LUT/action reports");
}
#endif
#endif

#ifdef ERA_MACRO_ENABLE
static void activation_bounds(void) {
    const uint8_t empty[] = {0}; reset_fixture(empty, sizeof(empty));
    for (unsigned request = 0U; request < ERA_MACRO_QUEUE_CAPACITY; ++request) assert(era_macro_request(0));
    assert(eeprom_reads == 0U);
    assert(!era_macro_request(0));
    for (unsigned step = 0U; step < ERA_MACRO_QUEUE_CAPACITY; ++step) {
        unsigned before = eeprom_reads; qmkUpdate();
        assert(eeprom_reads == before + sizeof(storage));
        era_macro_stats_t stats; era_macro_get_stats(&stats);
        assert(stats.queued == ERA_MACRO_QUEUE_CAPACITY - step - 1U);
    }
    assert(!busy());
    puts("PASS: request performs zero EEPROM reads, FIFO saturation and one activation per task");
}
#endif

int main(int argc, char **argv) {
    if (liveness()) return 1;
    if (argc == 2 && strcmp(argv[1], "--liveness-only") == 0) return 0;
#ifdef ERA_MACRO_ENABLE
    activation_bounds(); all_taps_nonblocking(); order_and_lifetime(); queue_and_snapshot(); malformed_and_clear(); reader_bounds(); suspend_wrap_and_epoch(); extra_ownership();
#ifdef MACRO_CUSTOM_LAYOUT
    custom_layout_parity();
#endif
#endif
    puts("PASS: production macro executor and caller/owner/host boundaries");
    return 0;
}
