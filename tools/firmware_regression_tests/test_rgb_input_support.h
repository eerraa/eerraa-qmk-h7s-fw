// V260911R1: 판정/입력/RGB task는 실제 소스, matrix·시계·HID·LED 출력만 호스트 대역을 사용한다.
#define RGBLIGHT_ENABLE
#define VELOCIKEY_ENABLE
#define TYPING_SPEED_MAX_VALUE 200
#define RGBLIGHT_USE_TIMER
#define RGBLIGHT_EFFECT_PULSE_ON_PRESS
#define RGBLIGHT_EFFECT_PULSE_OFF_PRESS
#define RGBLIGHT_EFFECT_PULSE_ON_PRESS_HOLD
#define RGBLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD
#define RGBLIGHT_MODE_PULSE_ON_PRESS 43
#define RGBLIGHT_MODE_PULSE_OFF_PRESS 44
#define RGBLIGHT_MODE_PULSE_ON_PRESS_HOLD 45
#define RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD 46
#define RGBLIGHT_EFFECT_PULSE_DURATION_MIN_MS 20
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
#define ac_dprintf(...) ((void)0)
#define debug_event(...) ((void)0)
#define debug_record(...) ((void)0)
#define debug_action(...) ((void)0)
#define layer_debug(...) ((void)0)
#define default_layer_debug(...) ((void)0)
#define dprintln(...) ((void)0)

_Static_assert(sizeof(action_t) == 2, "QMK action requires GCC packed bitfield layout");

typedef uint16_t matrix_row_t;
typedef uint32_t layer_state_t;
typedef int animation_status_t;
typedef struct { uint8_t raw; } led_t;
static struct { bool enable, velocikey; uint8_t speed, hue, sat, val; } rgblight_config;
static struct { uint8_t base_mode; bool timer_enabled; } rgblight_status;
static struct { bool matrix; } debug_config;
static bool rgblight_indicator_supported = true;
static bool is_rgblight_initialized = true;
static bool output_suspended, indicator_on, background_on, physical_dispatch;
static bool accept_keypress = true, accept_record = true;
static uint8_t host_led_raw, mods, weak_mods;
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
static layer_state_t layer_state, default_layer_state;

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
static bool timer_expired32(uint32_t now, uint32_t due) { return (int32_t)(now - due) >= 0; }
void wait_ms(uint32_t ms);
static void delay(uint32_t ms)
{
  blocking_delay_calls++;
  now_ms += ms + 1U; // V260911R2: HAL은 0ms에도 tick을 더한다. 실제 wait 포트의 차단 여부를 검증한다.
}
static void clear_keyboard(void) { assert(!"unexpected tapping-buffer overflow"); }
static uint8_t get_mods(void) { return mods; }
static uint8_t get_weak_mods(void) { return weak_mods; }
static void clear_weak_mods(void) { weak_mods = 0; }
static void add_weak_mods(uint8_t value) { weak_mods |= value; }
static void del_weak_mods(uint8_t value) { weak_mods &= ~value; }
static void add_mods(uint8_t value) { mods |= value; }
static void del_mods(uint8_t value) { mods &= ~value; }
static void register_mods(uint8_t value) { add_mods(value); }
static void unregister_mods(uint8_t value) { del_mods(value); }
static void send_keyboard_report(void) {}
static void register_code(uint8_t code)
{
  if (code == KC_CAPS_LOCK)
  {
    caps_press_time = now_ms;
    caps_press_count++;
    host_led_raw ^= 2;
    rgblight_indicator_post_host_event((led_t){host_led_raw});
  }
}
static void unregister_code(uint8_t code)
{
  if (code == KC_CAPS_LOCK)
  {
    caps_release_time = now_ms;
    caps_release_count++;
  }
}
static void register_code16(uint16_t code) { register_code((uint8_t)code); }
static void unregister_code16(uint16_t code) { unregister_code((uint8_t)code); }
static void layer_on(uint8_t layer) { layer_state |= 1U << layer; }
static void layer_off(uint8_t layer) { layer_state &= ~(1U << layer); }
static void layer_invert(uint8_t layer) { layer_state ^= 1U << layer; }
static void layer_move(uint8_t layer) { layer_state = 1U << layer; }
static void layer_clear(void) { layer_state = 0; }
static void layer_and(layer_state_t value) { layer_state &= value; }
static void layer_or(layer_state_t value) { layer_state |= value; }
static void layer_xor(layer_state_t value) { layer_state ^= value; }
static void layer_state_set(layer_state_t value) { layer_state = value; }
static void default_layer_and(layer_state_t value) { default_layer_state &= value; }
static void default_layer_or(layer_state_t value) { default_layer_state |= value; }
static void default_layer_xor(layer_state_t value) { default_layer_state ^= value; }
static void default_layer_set(layer_state_t value) { default_layer_state = value; }
static void register_mouse(uint8_t code, bool pressed) { (void)code; (void)pressed; }
static uint8_t host_keyboard_leds(void) { return host_led_raw; }
static void led_set(uint8_t value) { rgblight_indicator_post_host_event((led_t){value}); }

uint16_t get_record_keycode(keyrecord_t *record, bool cache)
{
  (void)cache;
  return record->event.key.col == 0 ? mapped_keycode : KC_A;
}
static action_t action_for_keycode(uint16_t code)
{
  if (code == LT(1, KC_CAPS)) return (action_t){.code = ACTION_LAYER_TAP_KEY(1, KC_CAPS)};
  if (code == MO(1)) return (action_t){.code = ACTION_LAYER_MOMENTARY(1)};
  if (code == MT(MOD_LCTL, KC_CAPS)) return (action_t){.code = ACTION_MODS_TAP_KEY(MOD_LCTL, KC_CAPS)};
  if (code >= QK_TAP_DANCE && code <= QK_TAP_DANCE_MAX) return (action_t){0};
  return (action_t){.code = code};
}
static action_t layer_switch_get_action(keypos_t key)
{
  return action_for_keycode(key.col == 0 ? mapped_keycode : KC_A);
}
static action_t store_or_get_action(bool pressed, keypos_t key)
{
  (void)pressed;
  return layer_switch_get_action(key);
}
static bool pre_process_record_kb(uint16_t code, keyrecord_t *record) { (void)code; (void)record; return accept_record; }
static bool process_record_kb(uint16_t code, keyrecord_t *record) { (void)code; (void)record; return true; }
static void post_process_record_kb(uint16_t code, keyrecord_t *record) { (void)code; (void)record; }
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
static void suspend_wakeup_key_event(uint8_t row, uint8_t col, bool pressed) { (void)row; (void)col; (void)pressed; }

static void rgblight_setrgb(uint8_t r, uint8_t g, uint8_t b)
{
  background_on = r || g || b;
  color_writes++;
  physical_color_writes += physical_dispatch;
  rgblight_request_render();
}
static void rgblight_sethsv_noeeprom_old(uint8_t h, uint8_t s, uint8_t v)
{
  (void)h; (void)s;
  rgblight_setrgb(v, v, v);
}
static void rgblight_indicator_apply_host_led(led_t state)
{
  bool active = (state.raw & 2) != 0;
  if (active == indicator_on) return;
  indicator_on = active;
  if (!active) rgblight_indicator_restore_pulse_effect();
  rgblight_request_render();
}
static void rgblight_render_frame(void)
{
  assert(!physical_dispatch);
  visible_frame = output_suspended ? 0 : (indicator_on ? 2 : (background_on ? 1 : 0));
  frame_count++;
}
static void eeconfig_flush_rgblight_current(bool force) { (void)force; }
static void rgblight_timer_task(void) { rgblight_effect_pulse_evaluate_output(); }
