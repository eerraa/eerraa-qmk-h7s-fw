// V260911R1: 판정/입력/RGB task는 실제 소스, matrix·시계·HID·LED 출력만 호스트 대역을 사용한다.
#define RGBLIGHT_ENABLE
#define VELOCIKEY_ENABLE
#define TYPING_SPEED_MAX_VALUE 200
#define RGBLIGHT_USE_TIMER
#define RGBLIGHT_EFFECT_PULSE_ON_PRESS
#define RGBLIGHT_EFFECT_PULSE_OFF_PRESS
#define RGBLIGHT_EFFECT_PULSE_ON_PRESS_HOLD
#define RGBLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD
#define RGBLIGHT_MODE_PULSE_OFF_PRESS 43
#define RGBLIGHT_MODE_PULSE_ON_PRESS 44
#define RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD 45
#define RGBLIGHT_MODE_PULSE_ON_PRESS_HOLD 46
#define RGBLIGHT_EFFECT_PULSE_DURATION_MIN_MS 5
#define RGBLIGHT_EFFECT_PULSE_DURATION_STEP_MS 1
#define TAPDANCE_ENABLE
#define TAP_DANCE_ENABLE
#define TAPDANCE_SLOT_COUNT 8
#define TAPDANCE_ACTION_COUNT 4
#define PACKED __attribute__((packed))
#define MATRIX_ROWS 5
#define MATRIX_COLS 15
#define LT(layer, kc) (QK_LAYER_TAP | ((layer) << 8) | (kc))
#define MT(mod, kc) (QK_MOD_TAP | ((mod) << 8) | (kc))
#define MO(layer) (QK_MOMENTARY | (layer))
#define TD(i) (QK_TAP_DANCE | (i))
#define QK_TAP_DANCE_GET_INDEX(kc) ((kc) & 0xFF)
#define TIMER_DIFF_16(a, b) ((uint16_t)((a) - (b)))
#define TIMER_DIFF_32(a, b) ((uint32_t)((a) - (b)))
#define ac_dprintf(...) ((void)0)
#define debug_event(...) ((void)0)
#define debug_record(...) ((void)0)
#define debug_action(...) ((void)0)
#define layer_debug(...) ((void)0)
#define default_layer_debug(...) ((void)0)
#define dprintln(...) ((void)0)
#define dprint(...) ((void)0)

_Static_assert(sizeof(action_t) == 2, "QMK action requires GCC packed bitfield layout");

typedef uint16_t matrix_row_t;
typedef uint32_t layer_state_t;
typedef int animation_status_t;
static struct { bool enable, velocikey; uint8_t mode, speed, hue, sat, val; uint64_t raw; } rgblight_config;
static struct { uint8_t base_mode; bool timer_enabled; } rgblight_status;
// V260913R1: 실제 rgblight_sethsv_eeprom_helper()를 커밋 경로로 연결한다. 색 계산과 EEPROM만 대역이다.
#define RGBLIGHT_MODE_STATIC_LIGHT 1
#ifndef dprintf
#define dprintf(...) ((void)0)
#endif
static uint8_t mode_base_table[RGBLIGHT_MODE_PULSE_ON_PRESS_HOLD + 1] = {
  [RGBLIGHT_MODE_STATIC_LIGHT] = RGBLIGHT_MODE_STATIC_LIGHT,
  [RGBLIGHT_MODE_PULSE_ON_PRESS] = RGBLIGHT_MODE_PULSE_ON_PRESS,
  [RGBLIGHT_MODE_PULSE_OFF_PRESS] = RGBLIGHT_MODE_PULSE_OFF_PRESS,
  [RGBLIGHT_MODE_PULSE_ON_PRESS_HOLD] = RGBLIGHT_MODE_PULSE_ON_PRESS_HOLD,
  [RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD] = RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD,
};
static uint8_t written_val, visible_val; // 마지막으로 LED 버퍼에 쓴 값과 마지막 프레임이 보여준 값
static void sethsv(uint8_t h, uint8_t s, uint8_t v, rgb_led_t *led) { (void)h; (void)s; led->r = led->g = led->b = v; }
static void eeconfig_update_rgblight(uint64_t raw) { (void)raw; }
static struct { bool matrix; } debug_config;
static bool rgblight_indicator_supported = true;
static bool is_rgblight_initialized = true;
static bool output_suspended, background_on, physical_dispatch;
static bool accept_keypress = true, accept_record = true;
static bool (*fixture_pre_process_record)(uint16_t, keyrecord_t *);
static bool (*fixture_process_record)(uint16_t, keyrecord_t *);
static void (*fixture_post_process_record)(uint16_t, keyrecord_t *);
static struct { uint32_t (*get_generation)(void); bool (*is_complete)(uint32_t); uint32_t (*time_us)(void); } rgblight_driver;
static bool rgblight_pulse_output_visible(void) { return !output_suspended && !indicator_on && rgblight_config.enable; }
static uint32_t irq_mask;
static uint32_t __get_PRIMASK(void) { return irq_mask; }
static void __disable_irq(void) { irq_mask=1; }
static void __set_PRIMASK(uint32_t mask) { irq_mask=mask; }
static uint8_t host_led_raw;
static uint8_t typing_speed;
static uint32_t now_ms, pending_matrix_activity_time, caps_press_time, caps_release_time;
static uint32_t caps_press_count, caps_release_count, color_writes, physical_color_writes, frame_count;
static unsigned blocking_delay_calls, keyboard_delay_calls;
static uint16_t last_keyboard_delay;
// V260911R3: 실제 host 포트까지 연결하고 USB 간격 요청만 기록한다. 물리 전송은 별도 실제 class fixture가 검사한다.
static void usbHidDelayKeyboardReport(uint16_t ms)
{
  keyboard_delay_calls++;
  last_keyboard_delay = ms;
}
static int visible_frame; // 0: OFF, 1: 기본 RGB, 2: Caps 인디케이터
static matrix_row_t matrix_rows[MATRIX_ROWS];
static bool scan_changed;
static uint16_t mapped_keycode = TD(0);
static uint16_t other_keycode = KC_A;
static uint16_t layered_other_keycode, third_keycode;
extern layer_state_t layer_state, default_layer_state;
void layer_state_set(layer_state_t); void layer_on(uint8_t); void layer_off(uint8_t);
uint16_t layer_physical_owner(void); void layer_set_physical_owner(uint16_t); void layer_clear_physical_momentary(void);
void layer_clear(void); void layer_move(uint8_t); void layer_invert(uint8_t);
bool layer_state_cmp(layer_state_t, uint8_t);
void clear_keyboard(void); void clear_keyboard_but_mods(void); void clear_keyboard_but_mods_and_keys(void);
void register_code(uint8_t); void unregister_code(uint8_t); void register_code16(uint16_t); void unregister_code16(uint16_t);
void register_mods(uint8_t); void unregister_mods(uint8_t); void register_weak_mods(uint8_t); void unregister_weak_mods(uint8_t);
void reset_tap_dance(tap_dance_state_t *); void tap_dance_cancel_all(void);
#ifdef TAPDANCE_ENABLE
uint32_t tap_dance_input_epoch(void); bool tap_dance_discard_retired_record(keyrecord_t *);
void tap_dance_run_quantum_keycode(keyrecord_t *record, uint16_t keycode);
void tapdance_storage_apply_defaults(void);
void tapdance_storage_stage_defaults(void);
void tapdance_storage_flush(bool force);
#endif
static uint8_t bitpop(uint8_t value) { return (uint8_t)__builtin_popcount(value); }
static keymap_config_t keymap_config = {.oneshot_enable = true};
static void eeconfig_update_keymap(uint16_t value) { (void)value; }
static bool command_proc(uint8_t code) { (void)code; return false; }
static uint8_t dirty_tapdance, dirty_tapdance_timing;
static void eeconfig_init_tapdance(void) {}
static void eeconfig_init_tapdance_timing(void) {}
static void eeconfig_flush_tapdance_timing(bool force) { (void)force; }
static void eeconfig_flag_tapdance_timing(bool dirty) { (void)dirty; }
static void eeconfig_flush_tapdance(bool force) { (void)force; }
static void eeconfig_flag_tapdance(bool dirty) { (void)dirty; }
void rgblight_indicator_post_host_event(led_t value);
static bool automatic_caps_feedback = true;
static bool report_had_caps, require_shared_a, forbid_a;
static unsigned forbidden_a_reports, a_reports;
static unsigned shared_a_gaps;
static uint8_t last_a_mods;
static bool report_has_key(const report_keyboard_t *report, uint8_t code) {
  for (unsigned i = 0; i < KEYBOARD_REPORT_KEYS; ++i) if (report->keys[i] == code) return true;
  return false;
}
static void fixture_record_keyboard_report(report_keyboard_t *report) {
  if (report_has_key(report, KC_A)) { last_a_mods = report->mods; ++a_reports; }
  if (forbid_a && report_has_key(report, KC_A)) ++forbidden_a_reports;
  if (require_shared_a && !report_has_key(report, KC_A)) ++shared_a_gaps;
  bool caps = report_has_key(report, KC_CAPS);
  if (caps && !report_had_caps) {
    caps_press_time = now_ms; ++caps_press_count;
    if (automatic_caps_feedback) { host_led_raw ^= 2; rgblight_indicator_post_host_event((led_t){host_led_raw}); }
  }
  if (!caps && report_had_caps) { caps_release_time = now_ms; ++caps_release_count; }
  report_had_caps = caps;
}

/* Report construction and host provenance are production code. USB admission
 * is an always-accepting adapter here; the actual HID fixture owns wire order. */
typedef struct { void (*send_keyboard)(report_keyboard_t *); } host_driver_t;
static host_driver_t *driver;
static bool debug_keyboard;
static void (*fixture_keyboard_observer)(const report_keyboard_t *, uint32_t, uint8_t, bool);
static void (*fixture_scan_end_observer)(uint32_t);
static bool usbHidSendReport(uint8_t *data, uint16_t length)
{
  assert(length == sizeof(report_keyboard_t));
  fixture_record_keyboard_report((report_keyboard_t *)data);
  if (fixture_keyboard_observer) fixture_keyboard_observer((report_keyboard_t *)data, 0U, 0U, false);
  return true;
}
static bool usbHidSubmitKeyUpdate(uint8_t *data, uint16_t length, uint32_t scan_token, uint8_t usage, bool pressed)
{
  assert(length == sizeof(report_keyboard_t) && scan_token != 0U);
  fixture_record_keyboard_report((report_keyboard_t *)data);
  if (fixture_keyboard_observer) fixture_keyboard_observer((report_keyboard_t *)data, scan_token, usage, pressed);
  return true;
}
static void usbHidEndKeyScan(uint32_t scan_token)
{
  if (fixture_scan_end_observer) fixture_scan_end_observer(scan_token);
}
void host_keyboard_send(report_keyboard_t *);
void host_keyboard_begin_key_update(uint32_t, uint8_t, bool);
void host_keyboard_end_key_update(void);
void host_keyboard_end_key_scan(uint32_t);
uint32_t host_keyboard_key_update_count(void);
void action_exec(keyevent_t);
void action_exec_physical(keyevent_t, uint32_t);

void process_record(keyrecord_t *record);
void process_record_handler(keyrecord_t *record);
void process_action(keyrecord_t *record, action_t action);
bool process_record_quantum(keyrecord_t *record);
bool pre_process_record_quantum(keyrecord_t *record);
void post_process_record_quantum(keyrecord_t *record);
static void rgblight_request_render(void);
static void rgblight_indicator_restore_pulse_effect(void);
static void rgblight_effect_pulse_evaluate_output(void);
void rgblight_indicator_post_host_event(led_t value);

static uint16_t timer_read(void) { return (uint16_t)now_ms; }
static uint16_t timer_elapsed(uint16_t time) { return (uint16_t)(now_ms - time); }
static uint32_t sync_timer_read32(void) { return now_ms; }
static uint32_t timer_read32(void) { return now_ms; }
static uint32_t timer_elapsed32(uint32_t time) { return (uint32_t)(now_ms - time); }
static bool timer_expired32(uint32_t now, uint32_t due) { return (int32_t)(now - due) >= 0; }
void wait_ms(uint32_t ms);
static void delay(uint32_t ms)
{
  blocking_delay_calls++;
  now_ms += ms + 1U; // V260911R2: HAL은 0ms에도 tick을 더한다. 실제 wait 포트의 차단 여부를 검증한다.
}
/* Mouse engine and extra-report transport are adapters; register_mouse(),
 * usage routing and ownership are the production functions. */
static uint32_t mouse_codes_down;
static uint16_t last_system, last_consumer;
static void mousekey_on(uint8_t code) { mouse_codes_down |= 1UL << (code - KC_MS_UP); }
static void mousekey_off(uint8_t code) { mouse_codes_down &= ~(1UL << (code - KC_MS_UP)); }
static void mousekey_send(void) {}
static void mousekey_clear(void) { mouse_codes_down = 0; }
static void host_system_send(uint16_t usage) { last_system = usage; }
static void host_consumer_send(uint16_t usage) { last_consumer = usage; }
static uint16_t host_last_system_usage(void) { return last_system; }
static uint16_t host_last_consumer_usage(void) { return last_consumer; }
void register_mouse(uint8_t mouse_keycode, bool pressed);
static uint8_t host_keyboard_leds(void) { return host_led_raw; }
static void led_set(uint8_t value) { rgblight_indicator_post_host_event((led_t){value}); }

/* Keymap is an adapter; the production core must request it again after
 * a TD interruption changes the effective layer. */
static bool fixture_dynamic_keymap;
uint16_t dynamic_keymap_get_keycode(uint8_t, uint8_t, uint8_t);
static uint16_t fixture_layer_keycode(uint8_t layer, keypos_t key)
{
  if (fixture_dynamic_keymap) return dynamic_keymap_get_keycode(layer, key.row, key.col);
  if (key.col == 2 && third_keycode != KC_NO) return third_keycode;
  if (key.col != 0 && layer == 1 && layered_other_keycode != KC_NO) return layered_other_keycode;
  return key.col == 0 ? mapped_keycode : other_keycode;
}
static uint8_t fixture_layer(keypos_t key)
{
  return key.col != 0 && (layer_state & 2U) && layered_other_keycode != KC_NO ? 1 : 0;
}
static uint16_t fixture_keycode(keypos_t key) { return fixture_layer_keycode(fixture_layer(key), key); }
#define MAX_LAYER_BITS 3
#define MAX_LAYER 4
static bool disable_action_cache;
uint16_t keymap_key_to_keycode(uint8_t layer, keypos_t key) { return fixture_layer_keycode(layer, key); }
uint8_t read_source_layers_cache(keypos_t key);
uint16_t read_source_keycode_cache(keypos_t key);
void begin_source_keycode_record(keyrecord_t *record);
bool read_source_keycode_record(keyrecord_t *record, uint16_t *keycode);
void update_source_keycode_record(keyrecord_t *record, uint16_t keycode);
void update_source_layers_cache(keypos_t key, uint8_t layer);
uint8_t layer_switch_get_layer(keypos_t key);
action_t layer_switch_get_action(keypos_t key);
action_t store_or_get_action(bool pressed, keypos_t key);
uint16_t get_event_keycode(keyevent_t event, bool cache);
bool is_tap_action(action_t action);
/* Product retro tapping is a VIA option; the fixture toggles it per case. */
bool retro_tap_primed; uint16_t retro_tap_curr_key; uint8_t retro_tap_curr_mods, retro_tap_next_mods;
static bool fixture_retro;
bool get_retro_tapping(uint16_t keycode, keyrecord_t *record) { (void)keycode; (void)record; return fixture_retro; }
#define IS_KB_KEYCODE(code) ((code) >= QK_KB_0 && (code) <= QK_KB_31)
uint16_t get_record_keycode(keyrecord_t *record, bool cache);
uint16_t tap_dance_owned_keycode(const keyrecord_t *record);
static action_t action_for_keycode(uint16_t code)
{
  if (code >= QK_LAYER_TAP && code <= QK_LAYER_TAP_MAX) return (action_t){.code = ACTION_LAYER_TAP_KEY((code >> 8) & 15, code & 255)};
  if (code >= QK_MOMENTARY && code <= QK_MOMENTARY_MAX) return (action_t){.code = ACTION_LAYER_MOMENTARY(code & 31)};
  if (code >= QK_MOD_TAP && code <= QK_MOD_TAP_MAX) return (action_t){.code = ACTION_MODS_TAP_KEY((code >> 8) & 31, code & 255)};
  if (code >= QK_ONE_SHOT_MOD && code <= QK_ONE_SHOT_MOD_MAX) return (action_t){.code = ACTION_MODS_ONESHOT(code & 31)};
  if (code >= QK_ONE_SHOT_LAYER && code <= QK_ONE_SHOT_LAYER_MAX) return (action_t){.code = ACTION_LAYER_ONESHOT(code & 31)};
  if (code >= QK_TOGGLE_LAYER && code <= QK_TOGGLE_LAYER_MAX) return (action_t){.code = ACTION_LAYER_TOGGLE(code & 31)};
  if (code >= QK_TAP_DANCE && code <= QK_TAP_DANCE_MAX) return (action_t){0};
  if (IS_SYSTEM_KEYCODE(code)) return (action_t){.code = ACTION_USAGE_SYSTEM(KEYCODE2SYSTEM(code))};
  if (IS_CONSUMER_KEYCODE(code)) return (action_t){.code = ACTION_USAGE_CONSUMER(KEYCODE2CONSUMER(code))};
  if (IS_MOUSE_KEYCODE(code)) return (action_t){.code = ACTION_MOUSEKEY(code)};
  if (code >= QK_LAYER_TAP_TOGGLE && code <= QK_LAYER_TAP_TOGGLE_MAX) return (action_t){.code = ACTION_LAYER_TAP_TOGGLE(code & 31)};
  /* As in the product, a keycode past the basic/modded range without an
   * action of its own (lighting, macro, custom) is ACTION_NO. */
  return (action_t){.code = code <= QK_MODS_MAX ? code : ACTION_NO};
}
action_t action_for_key(uint8_t layer, keypos_t key) { return action_for_keycode(keymap_key_to_keycode(layer, key)); }
static bool pre_process_record_kb(uint16_t code, keyrecord_t *record) {
  return accept_record && (!fixture_pre_process_record || fixture_pre_process_record(code, record));
}
/* Quantum handlers are adapters: the log shows which dance-output edges
 * reach them, and QK_USER_0 stands for a keycode whose handler clears the
 * keyboard (a magic keycode in the product's QMK). */
static uint16_t quantum_log[8]; static bool quantum_log_down[8]; static unsigned quantum_log_len;
static bool process_record_kb(uint16_t code, keyrecord_t *record)
{
  if (fixture_process_record && !fixture_process_record(code, record)) return false;
  if (record->tap_dance_injected && quantum_log_len < 8) { quantum_log[quantum_log_len] = code; quantum_log_down[quantum_log_len++] = record->event.pressed; }
  if (code == QK_USER_0 && record->event.pressed) clear_keyboard();
  return true;
}
static void post_process_record_kb(uint16_t code, keyrecord_t *record) {
  if (fixture_post_process_record) fixture_post_process_record(code, record);
}
// The fixture injects the dynamic term directly; DT_* adjustment keycodes are outside this harness.
static bool process_dynamic_tapping_term(uint16_t code, keyrecord_t *record) { (void)code; (void)record; return true; }
static bool process_rgb(uint16_t code, keyrecord_t *record) { (void)code; (void)record; return true; }
static bool process_action_kb(keyrecord_t *record) { (void)record; return true; }
static void eeconfig_init(void) {}
static void velocikey_toggle(void) { rgblight_config.velocikey = !rgblight_config.velocikey; }
static bool matrix_can_read(void) { return true; }
static bool matrix_scan(void) { bool changed = scan_changed; scan_changed = false; return changed; }
static matrix_row_t matrix_get_row(uint8_t row) { return matrix_rows[row]; }
static bool should_process_keypress(void) { return accept_keypress; }
static bool has_ghost_in_row(uint8_t row, matrix_row_t value) { (void)row; (void)value; return false; }
static void matrix_scan_perf_task(void) {}
static void matrix_print(void) {}
static bool host_suspended; static unsigned wake_requests;
static bool usbHidHostSleeping(void) { return host_suspended; }
static bool usbHidRequestRemoteWakeFromInput(void) { ++wake_requests; return host_suspended; }

static void rgblight_setrgb(uint8_t r, uint8_t g, uint8_t b)
{
  background_on = r || g || b;
  written_val = r;
  color_writes++;
  physical_color_writes += physical_dispatch;
  rgblight_request_render();
}
static void rgblight_sethsv_noeeprom_old(uint8_t h, uint8_t s, uint8_t v)
{
  (void)h; (void)s;
  rgblight_setrgb(v, v, v);
}
static void rgblight_render_frame(void)
{
  assert(!physical_dispatch);
  visible_frame = output_suspended ? 0 : (indicator_on ? 2 : (background_on ? 1 : 0));
  visible_val = written_val;
  frame_count++;
}
static void eeconfig_flush_rgblight_current(bool force) { (void)force; }
static void rgblight_timer_task(void) {} // V260913R1: 만료는 rgblight_task의 1 ms 게이트가 판정한다. 애니메이션 타이머는 관여하지 않는다.

#include "test_input_remap_support.h"
