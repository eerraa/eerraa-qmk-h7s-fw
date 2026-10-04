#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "macro_mode.h"
#define MATRIX_ROWS 5
#define MATRIX_COLS 16
#define MOUSEKEY_ENABLE
#define MOUSE_SHARED_EP
#define EXTRAKEY_ENABLE
#define NO_ACTION_ONESHOT
#define _USE_HW_WS2812
#define TOTAL_EEPROM_BYTE_COUNT 128U
#define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR 256U
#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE TOTAL_EEPROM_BYTE_COUNT
#define DYNAMIC_KEYMAP_MACRO_COUNT 16U
#ifdef MACRO_ZERO_DELAY
#define DYNAMIC_KEYMAP_MACRO_DELAY 0U
#else
#define DYNAMIC_KEYMAP_MACRO_DELAY 10U
#endif
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

#include "ownership_api.h"
void host_extra_reconcile(void);

void add_key_to_report(uint8_t);
void del_key_from_report(uint8_t);
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
bool host_keyboard_report_needs_send(void);
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
void send_keyboard_report_force(void);
void mousekey_send(void);
typedef struct { struct { bool pressed; } event; } keyrecord_t;
bool process_record_via(uint16_t, keyrecord_t *);
void qmkUpdate(void);
bool qmkInit(void);

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
report_keyboard_t *keyboard_report = &logical_report;
static report_mouse_t logical_mouse, accepted_mouse;
static uint16_t accepted_system, accepted_consumer;
static bool accepted_keyboard_bound, accepted_mouse_bound, accepted_system_bound, accepted_consumer_bound;
static bool extra_submission_bound;
static uint32_t fixture_now, release_time, due_release, first_service_time;
static unsigned capture_count, service_calls, hal_waits, host_intervals, rejected_epoch, extra_sends;
static uint16_t host_interval;
static bool macro_press_pending, release_pending, ordinary_press_pending, inject_reset;
static usb_hid_session_t session = {1U, true, false};
static host_driver_t *driver;
static bool debug_keyboard;
static uint16_t last_system_usage, last_consumer_usage;
static uint32_t key_update_scan_token, key_update_count;
static uint8_t key_update_usage;
static bool key_update_pressed;

static uint32_t timer_read32(void) { return fixture_now; }
#include "via_query.inc"
#ifdef ERA_MACRO_ENABLE
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
#ifndef ACTION_OWNERSHIP_ENABLE
static void add_key(uint8_t key) { add_key_to_report(key); }
static void del_key(uint8_t key) { del_key_from_report(key); }
#endif
static bool command_proc(uint8_t code) { (void)code; return false; }
bool is_key_pressed(uint8_t code) { return logical_keys[code]; }
static void rebuild_keys(void) {
    memset(logical_report.keys, 0, sizeof(logical_report.keys));
    unsigned slot = 0;
    for (unsigned code = 1U; code < 256U && slot < KEYBOARD_REPORT_KEYS; ++code)
        if (logical_keys[code]) logical_report.keys[slot++] = (uint8_t)code;
}
void add_key_to_report(uint8_t code) { logical_keys[code] = true; rebuild_keys(); }
void del_key_from_report(uint8_t code) { logical_keys[code] = false; rebuild_keys(); }
static void clear_keys(void) { memset(logical_keys, 0, sizeof(logical_keys)); rebuild_keys(); }
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
    accepted_keyboard_bound = bound;
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
        accepted_mouse_bound = extra_submission_bound;
        assert(length == sizeof(report_mouse_t)); memcpy(&accepted_mouse, data, length);
    } else {
        assert(length == sizeof(report_extra_t));
        report_extra_t extra; memcpy(&extra, data, length);
        if (extra.report_id == REPORT_ID_SYSTEM) { accepted_system = extra.usage; accepted_system_bound = extra_submission_bound; }
        else if (extra.report_id == REPORT_ID_CONSUMER) { accepted_consumer = extra.usage; accepted_consumer_bound = extra_submission_bound; }
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
    extra_submission_bound = true;
    bool accepted = same_epoch(generation) && usbHidSendReportEXK(data, length);
    extra_submission_bound = false;
    return accepted;
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
static __attribute__((unused)) void tap_dance_cancel_all(void) { /* TD runtime cancellation is exercised by its own production fixture. */ }
static void keyboard_task(void) {
    if (macro_press_pending) {
        macro_press_pending = false;
        keyrecord_t record = {.event.pressed = true};
        assert(!process_record_via(QK_MACRO_0, &record));
    } else if (ordinary_press_pending && fixture_now >= 25U) {
        register_code(KC_B); ordinary_press_pending = false;
    } else if (release_pending && fixture_now >= due_release) {
        unregister_code(KC_A);
        release_pending = false;
        release_time = fixture_now;
    }
}
static void ws2812Task(void) { if (!service_calls) first_service_time = fixture_now; ++service_calls; }
static void via_hid_task(void) {
    uint8_t command_data[5] = {id_uptime};
    via_uptime_query(command_data);
    uint32_t reported = ((uint32_t)command_data[1] << 24) | ((uint32_t)command_data[2] << 16) |
                        ((uint32_t)command_data[3] << 8) | command_data[4];
    assert(reported == fixture_now); ++service_calls;
}
static void eeprom_task(void) { ++service_calls; }
static void idle_task(void) { ++service_calls; }

/* Board setup and persistence readiness are adapters; qmkInit itself is production. */
static bool fixture_eeprom_ready = true, fixture_eeconfig_enabled = true, fixture_factory_ok = true;
static bool is_suspended;
static unsigned init_calls;
static bool eeprom_is_ready(void) { return fixture_eeprom_ready; }
static bool eeconfig_is_enabled(void) { return fixture_eeconfig_enabled; }
static bool eeprom_apply_factory_defaults(bool save) { assert(save); return fixture_factory_ok; }
static void via_hid_init(void) { ++init_calls; }
static void debounce_profile_init(void) { ++init_calls; }
#ifdef TAPDANCE_ENABLE
static void tapdance_init(void) { ++init_calls; }
#endif
static void mousekey_config_init(void) { ++init_calls; }
static void keyboard_setup(void) { ++init_calls; }
static void keyboard_init(void) { ++init_calls; }
static bool usbIsSuspended(void) { return session.suspended; }
typedef struct { unsigned type, pre_ms, post_ms; } debounce_profile_values_t;
static const debounce_profile_values_t *debounce_profile_current(void) {
    static debounce_profile_values_t profile; return &profile;
}
#define logPrintf(...) do { if (0) printf(__VA_ARGS__); } while (0)
#define cliAdd(...) ((void)0)
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
    macro_press_pending = release_pending = ordinary_press_pending = inject_reset = false;
    last_system_usage = last_consumer_usage = accepted_system = accepted_consumer = 0U;
    memset(&accepted_keyboard, 0, sizeof(accepted_keyboard));
    memset(&accepted_mouse, 0, sizeof(accepted_mouse));
    load(data, size);
    eeprom_reads = 0U;
    accepted_keyboard_bound = accepted_mouse_bound = accepted_system_bound = accepted_consumer_bound = false;
}
static int liveness(void) {
    const uint8_t macro[] = {SS_QMK_PREFIX, SS_DELAY_CODE, '1','0','0','0','|', 0};
    reset_fixture(macro, sizeof(macro));
    register_code(KC_A);
    due_release = 50U; release_pending = macro_press_pending = ordinary_press_pending = true;
    qmkUpdate();
    while (release_time == UINT32_MAX && fixture_now < 60U) { ++fixture_now; qmkUpdate(); }
    if (release_time == UINT32_MAX) qmkUpdate();
    printf("LIVENESS: due=50, release=%lu, service-first=%lu, HAL-waits=%u\n",
           (unsigned long)release_time, (unsigned long)first_service_time, hal_waits);
    if (release_time > 60U || first_service_time > 60U) {
        fprintf(stderr, "FAIL: production macro blocks ordinary release/service\n"); return 1;
    }
    assert(logical_keys[KC_B] && !logical_keys[KC_A]);
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
    assert(!(action_owner_has_outputs(ACTION_OWNER_MACRO_PERSISTENT) || action_owner_has_outputs(ACTION_OWNER_MACRO_TEMPORARY)));
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
    uint8_t previous = action_owner_select(0U); register_code(KC_A); register_code(KC_LEFT_SHIFT);
    action_owner_select(previous);
    dynamic_keymap_macro_send(1); drain(40);
    assert(accepted_key(KC_A) && accepted_keyboard.mods == MOD_BIT(KC_LEFT_SHIFT));
    previous = action_owner_select(0U); unregister_code(KC_A); unregister_code(KC_LEFT_SHIFT);
    action_owner_select(previous);
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
    assert((action_owner_has_outputs(ACTION_OWNER_MACRO_PERSISTENT) || action_owner_has_outputs(ACTION_OWNER_MACRO_TEMPORARY)));
    assert(era_macro_request(0)); clear_keyboard();
    assert(!busy() && !(action_owner_has_outputs(ACTION_OWNER_MACRO_PERSISTENT) || action_owner_has_outputs(ACTION_OWNER_MACRO_TEMPORARY)) && accepted_keyboard.mods == 0U);
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
    assert(!(action_owner_has_outputs(ACTION_OWNER_MACRO_PERSISTENT) || action_owner_has_outputs(ACTION_OWNER_MACRO_TEMPORARY)) && accepted_key(KC_B) && accepted_key(KC_C) && !accepted_key(KC_A));
    assert(!capture[capture_count-1U].bound);

    reset_fixture(held, sizeof(held)); register_code(KC_A); dynamic_keymap_macro_send(0); drain(20);
    memset(&accepted_keyboard, 0, sizeof(accepted_keyboard)); ++session.generation;
    qmkUpdate(); assert(accepted_key(KC_A) && !(action_owner_has_outputs(ACTION_OWNER_MACRO_PERSISTENT) || action_owner_has_outputs(ACTION_OWNER_MACRO_TEMPORARY)));
    reset_fixture(delayed, sizeof(delayed)); dynamic_keymap_macro_send(0); at(0); assert(era_macro_request(0));
    session.valid = false; qmkUpdate(); assert(!busy());
    session.valid = true; ++session.generation; qmkUpdate(); assert(capture_count <= 1U);
    const uint8_t last_up[] = {SS_QMK_PREFIX,SS_DOWN_CODE,KC_A,0,SS_QMK_PREFIX,SS_UP_CODE,KC_A,0};
    reset_fixture(last_up, sizeof(last_up)); register_code(KC_B);
    dynamic_keymap_macro_send(0); drain(20);
    dynamic_keymap_macro_send(1); inject_reset = true; qmkUpdate();
    assert(rejected_epoch == 1U && !accepted_key(KC_B));
    qmkUpdate();
    assert(!busy() && accepted_key(KC_B) && !accepted_key(KC_A));
    assert(!action_owner_has_outputs(ACTION_OWNER_MACRO_PERSISTENT));
    /* Reset immediately after the terminating read also needs forced union publication. */
    reset_fixture(last_up, sizeof(last_up)); register_code(KC_B);
    dynamic_keymap_macro_send(0); drain(20); dynamic_keymap_macro_send(1); drain(20);
    memset(&accepted_keyboard, 0, sizeof(accepted_keyboard)); ++session.generation;
    qmkUpdate(); assert(accepted_key(KC_B) && !busy());
    /* Explicit cancellation republishes survivors even with no macro owner and
     * unchanged, already-unbound host context. A normal dedup send is insufficient. */
    const uint8_t empty[] = {0}; reset_fixture(empty, sizeof(empty)); register_code(KC_B);
    unsigned before_cancel = capture_count;
    assert(!accepted_keyboard_bound && !action_owner_has_outputs(ACTION_OWNER_MACRO_PERSISTENT));
    era_macro_cancel();
    assert(capture_count == before_cancel + 1U && accepted_key(KC_B) && !accepted_keyboard_bound);
    puts("PASS: same-session pause with remaining delay, timer wrap, epoch admission rejection and physical union reissue");
}

static void init_lifetime(void) {
    const uint8_t active_text[] = {SS_QMK_PREFIX,SS_DOWN_CODE,KC_B,'A',0};
    reset_fixture(active_text, sizeof(active_text)); register_code(KC_C);
    dynamic_keymap_macro_send(0); at(0); at(10); assert(era_macro_request(0));
    assert(busy() && action_owner_has_outputs(ACTION_OWNER_MACRO_PERSISTENT));
    assert(action_owner_has_outputs(ACTION_OWNER_MACRO_TEMPORARY));
    unsigned sends = capture_count + extra_sends, calls = init_calls;
    fixture_eeprom_ready = false; assert(!qmkInit()); fixture_eeprom_ready = true;
    assert(busy() && capture_count + extra_sends == sends && init_calls == calls);
    fixture_eeconfig_enabled = false; fixture_factory_ok = false; assert(!qmkInit());
    fixture_eeconfig_enabled = fixture_factory_ok = true;
    assert(busy() && capture_count + extra_sends == sends && init_calls == calls);
    assert(qmkInit() && init_calls > calls && !busy());
    assert(!action_owner_has_outputs(ACTION_OWNER_MACRO_PERSISTENT));
    assert(!action_owner_has_outputs(ACTION_OWNER_MACRO_TEMPORARY));
    assert(accepted_key(KC_C) && !accepted_key(KC_B) && accepted_keyboard.mods == 0U);
    puts("PASS: successful public qmkInit retires active/queued/persistent/temporary macro state; EEPROM guards leave runtime untouched");
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
    uint8_t previous = action_owner_select(0);
    register_code(KC_SYSTEM_SLEEP); register_code(KC_AUDIO_VOL_UP); register_code(KC_MS_BTN1);
    action_owner_select(previous);
    logical_mouse.x = 9; logical_mouse.y = -7; logical_mouse.v = 3; logical_mouse.h = -2;
    accepted_system = accepted_consumer = 0U; accepted_mouse.buttons = 0U; ++session.generation;
    unsigned sends = extra_sends; qmkUpdate();
    assert(extra_sends >= sends+3U && accepted_system == SYSTEM_SLEEP && accepted_consumer == AUDIO_VOL_UP && accepted_mouse.buttons == 1U);
    assert(!(action_owner_has_outputs(ACTION_OWNER_MACRO_PERSISTENT) || action_owner_has_outputs(ACTION_OWNER_MACRO_TEMPORARY)));
    assert(accepted_mouse.x == 0 && accepted_mouse.y == 0 && accepted_mouse.v == 0 && accepted_mouse.h == 0);
    assert(logical_mouse.x == 9 && logical_mouse.y == -7 && logical_mouse.v == 3 && logical_mouse.h == -2);
    previous = action_owner_select(0);
    unregister_code(KC_SYSTEM_SLEEP); unregister_code(KC_AUDIO_VOL_UP); unregister_code(KC_MS_BTN1);
    action_owner_select(previous);
    assert(accepted_system == 0U && accepted_consumer == 0U && accepted_mouse.buttons == 0U);
    const uint8_t identical[] = {SS_QMK_PREFIX,SS_DOWN_CODE,KC_LEFT_SHIFT,
        SS_QMK_PREFIX,SS_DOWN_CODE,KC_SYSTEM_SLEEP,SS_QMK_PREFIX,SS_DOWN_CODE,KC_AUDIO_VOL_UP,
        SS_QMK_PREFIX,SS_DOWN_CODE,KC_MS_BTN1,0};
    reset_fixture(identical, sizeof(identical));
    register_code(KC_LEFT_SHIFT); register_code(KC_SYSTEM_SLEEP); register_code(KC_AUDIO_VOL_UP); register_code(KC_MS_BTN1);
    assert(!accepted_keyboard_bound && !accepted_system_bound && !accepted_consumer_bound && !accepted_mouse_bound);
    for (unsigned cycle = 0U; cycle < 2U; ++cycle) {
        dynamic_keymap_macro_send(0); drain(50);
        assert(accepted_keyboard_bound && accepted_system_bound && accepted_consumer_bound && accepted_mouse_bound);
        era_macro_cancel();
        assert(!accepted_keyboard_bound && !accepted_system_bound && !accepted_consumer_bound && !accepted_mouse_bound);
        assert(accepted_keyboard.mods == MOD_BIT(KC_LEFT_SHIFT) && accepted_system == SYSTEM_SLEEP && accepted_consumer == AUDIO_VOL_UP && accepted_mouse.buttons == 1U);
    }
    dynamic_keymap_macro_send(0); drain(50);
    unregister_code(KC_LEFT_SHIFT); unregister_code(KC_SYSTEM_SLEEP); unregister_code(KC_AUDIO_VOL_UP); unregister_code(KC_MS_BTN1);
    assert(accepted_keyboard.mods == MOD_BIT(KC_LEFT_SHIFT) && accepted_system == SYSTEM_SLEEP && accepted_consumer == AUDIO_VOL_UP && accepted_mouse.buttons == 1U);
    assert(accepted_keyboard_bound && accepted_system_bound && accepted_consumer_bound && accepted_mouse_bound);
    era_macro_cancel();
    assert(accepted_keyboard.mods == 0U && accepted_system == 0U && accepted_consumer == 0U && accepted_mouse.buttons == 0U);
    puts("PASS: ordinary/TD/macro shared system+consumer+mouse lifetime, force EXK reissue and identical-union generation provenance");
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


static void ownership_boundaries(void) {
    const uint8_t empty[] = {0};
    reset_fixture(empty, sizeof(empty));
#ifndef ERA_MACRO_ENABLE
    dynamic_keymap_macro_send(0);
    assert(eeprom_reads == 0U && capture_count == 0U && extra_sends == 0U);
#endif
    /* Ordinary output is bit state, including repeated unscoped firmware DOWN. */
    register_code(KC_A); register_code(KC_A); unregister_code(KC_A);
    assert(!logical_keys[KC_A]);
#ifdef ACTION_OWNERSHIP_ENABLE
    const action_owner_t owner0 = 0U;
    action_owner_t old = action_owner_select(owner0);
    register_code(KC_A); add_mods(MOD_BIT(KC_LEFT_SHIFT)); add_weak_mods(MOD_BIT(KC_LEFT_ALT));
    action_owner_select(old);
    register_code(KC_A); add_mods(MOD_BIT(KC_LEFT_CTRL)); add_weak_mods(MOD_BIT(KC_LEFT_GUI));
    old = action_owner_select(owner0); clear_mods(); action_owner_select(old);
    assert(get_mods() == MOD_BIT(KC_LEFT_SHIFT));
    clear_weak_mods(); assert(get_weak_mods() == 0U);
    clear_keys(); assert(!logical_keys[KC_A] && action_owner_has_outputs(owner0));
    register_code(KC_A); unregister_code(KC_A); assert(logical_keys[KC_A]);
    old = action_owner_select(owner0);
    action_owner_release(owner0);
    assert(action_owner_current() == owner0 && !logical_keys[KC_A] && get_mods() == 0U);
    action_owner_select(old);
    register_code(KC_A); old = action_owner_select(owner0); register_code(KC_A); action_owner_select(old);
    action_ownership_reset_keys(); clear_keys();
    old = action_owner_select(owner0); unregister_code(KC_A); action_owner_select(old);
    assert(!logical_keys[KC_A]);
#ifdef ERA_MACRO_ENABLE
    old = action_owner_select(ACTION_OWNER_MACRO_PERSISTENT);
    action_owner_t middle = action_owner_select(owner0);
    action_owner_select(middle);
    assert(action_owner_current() == ACTION_OWNER_MACRO_PERSISTENT);
    action_owner_select(ACTION_OWNER_MACRO_TEMPORARY); register_code(KC_B);
    action_owner_select(ACTION_OWNER_MACRO_PERSISTENT);
    unsigned sends = capture_count + extra_sends;
    action_owner_release(ACTION_OWNER_MACRO_TEMPORARY);
    assert(action_owner_current() == ACTION_OWNER_MACRO_PERSISTENT);
    assert(capture_count + extra_sends == sends && !logical_keys[KC_B]);
    action_owner_select(old);
#endif
    const action_owner_t owners[] = {ACTION_OWNER_REGULAR, 0U, ACTION_OWNER_COUNT - 1U};
    const uint8_t usages[2][3] = {{KC_SYSTEM_SLEEP, KC_SYSTEM_POWER, KC_SYSTEM_WAKE},
                                 {KC_AUDIO_VOL_UP, KC_AUDIO_VOL_DOWN, KC_AUDIO_MUTE}};
    const unsigned permutations[6][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    for (unsigned order = 0U; order < 6U; ++order) {
        reset_fixture(empty, sizeof(empty));
        for (unsigned i = 0U; i < 3U; ++i) {
            old = action_owner_select(owners[i]);
            register_code(KC_A); register_code(KC_LEFT_SHIFT); register_code(KC_MS_BTN1);
            action_owner_select(old);
        }
        for (unsigned step = 0U; step < 3U; ++step) {
            unsigned released = permutations[order][step];
            old = action_owner_select(owners[released]);
            unregister_code(KC_A); unregister_code(KC_LEFT_SHIFT); unregister_code(KC_MS_BTN1);
            action_owner_select(old);
            assert(logical_keys[KC_A] == (step != 2U));
            assert((get_mods() != 0U) == (step != 2U));
            assert((logical_mouse.buttons != 0U) == (step != 2U));
        }
    }
    for (unsigned page = 0U; page < 2U; ++page) {
        for (unsigned down = 0U; down < 6U; ++down) {
            for (unsigned up = 0U; up < 6U; ++up) {
                reset_fixture(empty, sizeof(empty));
                bool held[3] = {true, true, true};
                unsigned current = 0U;
                for (unsigned step = 0U; step < 3U; ++step) {
                    current = permutations[down][step];
                    old = action_owner_select(owners[current]);
                    register_code(usages[page][current]); action_owner_select(old);
                }
                for (unsigned step = 0U; step < 3U; ++step) {
                    unsigned released = permutations[up][step]; held[released] = false;
                    old = action_owner_select(owners[released]);
                    unregister_code(usages[page][released]); action_owner_select(old);
                    if (!held[current]) current = held[0] ? 0U : held[1] ? 1U : held[2] ? 2U : 3U;
                    uint16_t expected = current == 3U ? 0U : page == 0U ? KEYCODE2SYSTEM(usages[page][current]) : KEYCODE2CONSUMER(usages[page][current]);
                    assert((page == 0U ? accepted_system : accepted_consumer) == expected);
                }
            }
        }
    }
#endif
    puts("PASS: owner feature boundary, ordinary bit state, report-only/reset, real/weak clear, nested scope and 72 system/consumer DOWN/UP permutations");
}

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
    /* A no-delay command stream may do many small phases, but not consume all 128 bytes. */
    reset_fixture(empty, sizeof(empty));
    for (unsigned i = 0U; i + 3U < sizeof(storage); i += 3U) {
        storage[i] = SS_QMK_PREFIX; storage[i+1U] = SS_DELAY_CODE; storage[i+2U] = '|';
    }
    assert(era_macro_request(0)); qmkUpdate();
    assert(busy() && eeprom_reads == sizeof(storage));
#ifdef MACRO_ZERO_DELAY
    assert(cursor == 24U); /* 16 phases alternate READ(3 bytes) and COMMAND_INTERVAL. */
    qmkUpdate(); assert(cursor == 48U && fixture_now == 0U && busy());
#endif
    drain(1000);
    puts("PASS: request performs zero EEPROM reads, FIFO saturation, one activation per task, bounded no-delay phases");
}
#endif

int main(int argc, char **argv) {
    ownership_boundaries();
    if (argc == 2 && strcmp(argv[1], "--ownership-only") == 0) return 0;
#ifdef ERA_MACRO_ENABLE
    if (argc == 2 && strcmp(argv[1], "--activation-only") == 0) { activation_bounds(); return 0; }
#endif
    if (liveness()) return 1;
    if (argc == 2 && strcmp(argv[1], "--liveness-only") == 0) return 0;
#ifdef ERA_MACRO_ENABLE
    activation_bounds(); all_taps_nonblocking(); order_and_lifetime(); queue_and_snapshot(); malformed_and_clear(); reader_bounds(); suspend_wrap_and_epoch(); init_lifetime(); extra_ownership();
#ifdef MACRO_CUSTOM_LAYOUT
    custom_layout_parity();
#endif
#endif
    puts("PASS: production macro executor and caller/owner/host boundaries");
    return 0;
}
