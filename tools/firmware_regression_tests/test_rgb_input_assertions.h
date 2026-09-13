static void task(void)
{
  tap_dance_task();
  rgblight_task();
}

static void scan(uint32_t time, uint8_t col, bool pressed)
{
  now_ms = time;
  matrix_rows[0] = pressed ? (matrix_rows[0] | (1U << col)) : (matrix_rows[0] & ~(1U << col));
  scan_changed = true;
  physical_dispatch = true;
  matrix_task();
  physical_dispatch = false;
}

static void tick(uint32_t time)
{
  now_ms = time;
  matrix_task();
  task();
}

static void reset_fixture(uint16_t keycode, uint8_t mode, uint32_t time)
{
  accept_keypress = accept_record = true;
  for (uint8_t col = 0; col < 2; col++) scan(time, col, false);
  memset(&tapping_key, 0, sizeof(tapping_key));
  memset(waiting_buffer, 0, sizeof(waiting_buffer));
  waiting_buffer_head = waiting_buffer_tail = 0;
  active_td = last_tap_time = 0;
  memset(tapdance_runtime, 0, sizeof(tapdance_runtime));
  memset(tapdance_state, 0, sizeof(tapdance_state));
  for (unsigned i = 0; i < TAPDANCE_SLOT_COUNT; i++) tap_dance_actions[i].state = (tap_dance_state_t){0};
  tapdance_state[0] = (tapdance_slot_state_t){{KC_CAPS, MO(1), KC_NO, KC_NO}, 200};
  mapped_keycode = keycode;
  layer_state = 0;
  output_suspended = indicator_on = false;
  rgblight_host_led_pending = false;
  host_led_raw = mods = weak_mods = 0;
  caps_press_count = caps_release_count = 0;
  caps_press_time = caps_release_time = 0;
  keyboard_delay_calls = last_keyboard_delay = 0;
  physical_color_writes = 0;
  rgblight_config.enable = true;
  rgblight_config.velocikey = false;
  typing_speed = 0;
  rgblight_config.speed = 45; // 5 + 45 = 50 ms pulse: tap 판정 term과 독립적이다.
  rgblight_config.val = 76;
  rgblight_config.hue = rgblight_config.sat = 0;
  rgblight_config.mode = mode; // V260913R1: 실제 커밋 경로가 base_mode를 여기서 다시 계산한다.
  rgblight_status.base_mode = mode;
  rgblight_status.timer_enabled = true;
  rgblight_effect_pulse_on_base_mode_update();
  rgblight_task();
}

static void check_tap(bool dance, bool trace_only)
{
  reset_fixture(dance ? TD(0) : LT(1, KC_CAPS), RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 1000);
  scan(1000, 0, true);
  task();
  int at_press = visible_frame;
  assert(caps_press_count == 0);
  scan(1040, 0, false);
  task();
  printf("TRACE %s: down=1000 frame=%d; Caps press=%u release=%u; final=%d; input color writes=%u\n",
         dance ? "TD0" : "LT", at_press, (unsigned)caps_press_time, (unsigned)caps_release_time,
         visible_frame, (unsigned)physical_color_writes);
  assert(caps_press_time == 1040 && caps_release_time == 1040 && now_ms == 1040);
  assert(caps_press_count == 1 && caps_release_count == 1 && layer_state == 0 && visible_frame == 2);
  // V260911R5: Caps 탭의 폭은 TD와 LT가 같다. 어느 경로가 합성했든 80 ms를 한 번 요청한다.
  assert(keyboard_delay_calls == 1U && last_keyboard_delay == TAP_HOLD_CAPS_DELAY);
  if (!trace_only)
  {
    assert(at_press == 0); // Caps 판정 전에도 두 키 모두 물리 press에 OFF pulse가 보여야 한다.
  }
}

static void check_hold(bool dance)
{
  reset_fixture(dance ? TD(0) : LT(1, KC_CAPS), RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 2000);
  scan(2000, 0, true); task();
  assert(visible_frame == 0);
  tick(2051); // pulse duration을 넘어도 물리 hold가 유지된다.
  assert(visible_frame == 0 && layer_state == 0);
  tick(2201);
  assert(layer_state == 2 && caps_press_count == 0 && visible_frame == 0);
  scan(2300, 0, false); task();
  assert(layer_state == 0 && caps_press_count == 0 && visible_frame == 1);
}

static void check_modes_and_last_key(void)
{
  for (uint8_t mode = 43; mode <= 46; mode++)
  {
    bool normal_on = mode == 44 || mode == 46;
    bool hold = mode >= 45;
    reset_fixture(KC_A, mode, 3000);
    assert(visible_frame == normal_on);
    scan(3000, 0, true); task();
    assert(visible_frame == !normal_on);
    tick(3051);
    assert(visible_frame == (hold ? !normal_on : normal_on));
    scan(3060, 1, true); task();
    scan(3070, 0, false); task(); // 이전 키의 release는 최근 키 hold를 해제하지 않는다.
    tick(3120);
    assert(visible_frame == (hold ? !normal_on : normal_on));
    scan(3130, 1, false); task();
    assert(visible_frame == normal_on);
  }
}

static void check_filter_and_replay(void)
{
  reset_fixture(LT(1, KC_CAPS), 46, 4000);
  scan(4000, 0, true); task();
  scan(4010, 1, true); task(); // LT 판정 버퍼가 다른 키를 잡아도 물리 추적은 즉시 바뀐다.
  assert(rgblight_pulse_effect_state.key_col == 1);
  scan(4020, 1, false); task();
  tick(4061);
  assert(visible_frame == 1); // last-pressed-key 의미를 보존한다.
  scan(4070, 0, false); task();
  assert(rgblight_pulse_effect_state.deadline_ms == 0); // 지연 재생이 pulse를 다시 시작하지 않는다.

  reset_fixture(KC_A, 46, 5000);
  accept_record = false;
  scan(5000, 0, true); task();
  assert(visible_frame == 0);
  accept_keypress = false;
  scan(5060, 0, false); task();
  assert(visible_frame == 1 && !rgblight_pulse_effect_state.key_tracking_valid);
}

static void check_indicators_sleep_wrap(void)
{
  reset_fixture(KC_A, 46, 6000);
  rgblight_indicator_post_host_event((led_t){2}); task();
  scan(6001, 0, true); task();
  assert(visible_frame == 2); // Caps ON은 pulse보다 우선한다.
  tick(6060);
  rgblight_indicator_post_host_event((led_t){0}); task();
  assert(visible_frame == 0); // 인디케이터 해제 후 아직 눌린 키의 pulse 복구
  scan(6061, 0, false); task();
  assert(visible_frame == 1);

  output_suspended = true;
  rgblight_request_render(); task();
  scan(6100, 0, true); task();
  assert(visible_frame == 0);
  scan(6160, 0, false); task();
  output_suspended = false;
  rgblight_request_render(); task();
  assert(visible_frame == 1);

  reset_fixture(KC_A, 46, UINT32_MAX - 20);
  scan(UINT32_MAX - 20, 0, true); task();
  scan(UINT32_MAX - 10, 0, false); task();
  tick(28); assert(visible_frame == 0);
  tick(29); assert(visible_frame == 1); // physical press + 50ms, 32-bit wrap

  rgblight_config.enable = false;
  unsigned before = color_writes;
  scan(100, 0, true); task();
  scan(101, 0, false); task();
  assert(!rgblight_config.enable && color_writes == before);
}

static void check_distinct_tapping_semantics(void)
{
  for (unsigned dance = 0; dance < 2; dance++)
  {
    reset_fixture(dance ? TD(0) : LT(1, KC_CAPS), 46, 7000);
    scan(7000, 0, true); task();
    scan(7030, 1, true); task();
    assert(caps_press_count == dance); // TD interruption은 tap, LT 기본 정책은 판정을 보류한다.
    scan(7040, 1, false); task();
    scan(7050, 0, false); task();
    assert(caps_press_count == 1 && caps_release_count == 1 && layer_state == 0);

    reset_fixture(dance ? TD(0) : LT(1, KC_CAPS), 46, 8000);
    scan(8000, 0, true); task();
    scan(8040, 0, false); task();
    scan(8150, 0, true); task();
    tick(8351);
    assert(caps_press_count == (dance ? 1U : 2U)); // LT quick-tap 반복과 TD의 새 dance를 유지한다.
    assert(layer_state == (dance ? 2U : 0U));
    scan(8400, 0, false); task();
    assert(layer_state == 0 && caps_release_count == caps_press_count);
  }

  reset_fixture(TD(0), 46, 9000);
  tapdance_state[0].actions[2] = KC_B;
  scan(9000, 0, true); task();
  assert(visible_frame == 0);
  scan(9040, 0, false); task();
  assert(caps_press_count == 0); // double 동작이 있으면 단일 tap도 TD term을 기다린다.
  tick(9201);
  assert(caps_press_count == 1 && caps_release_count == 1);
}

static void check_velocikey_physical_presses(void)
{
  reset_fixture(LT(1, KC_CAPS), 46, 10000);
  rgblight_config.velocikey = true;
  scan(10000, 0, true);
  assert(typing_speed == 4);
  scan(10010, 1, true);
  assert(typing_speed == 8);
  scan(10020, 1, false);
  scan(10040, 0, false);
  assert(typing_speed == 8); // release/논리 재생은 Velocikey를 가속하지 않는다.
  task();
}

static void check_mod_tap_and_following_caps(void)
{
  reset_fixture(MT(MOD_LCTL, KC_CAPS), 46, 11000);
  scan(11000, 0, true); task();
  scan(11040, 0, false); task();
  assert(now_ms == 11040 && caps_press_time == 11040 && caps_release_time == 11040);
  assert(caps_press_count == 1 && caps_release_count == 1 && mods == 0);

  reset_fixture(TD(0), 46, 12000);
  scan(12000, 0, true); task();
  scan(12040, 0, false); task();
  mapped_keycode = KC_CAPS;
  scan(12041, 0, true); task();
  tick(12150);
  assert(caps_press_count == 2 && caps_release_count == 1); // 이전 synthetic tap의 미래 key-up이 없어야 한다.
  scan(12200, 0, false); task();
  assert(caps_release_count == 2);
}

static void check_plain_tap_requests_no_interval(void)
{
  // V260911R5: Caps가 아닌 합성 탭은 TAP_CODE_DELAY(0)라 간격도 차단 대기도 요청하지 않는다.
  reset_fixture(TD(0), RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 3000);
  tapdance_state[0] = (tapdance_slot_state_t){{KC_A, MO(1), KC_NO, KC_NO}, 200};
  scan(3000, 0, true);
  task();
  scan(3040, 0, false);
  task();
  assert(keyboard_delay_calls == 0U && blocking_delay_calls == 0U && layer_state == 0);
}

static void check_config_commit_renders_committed_value(void)
{
  // V260913R1: VIA·키코드 설정은 커밋 뒤 RGB task가 그린다. 설정 함수가 직접 그리면 직전 값이 보인다.
  reset_fixture(KC_A, RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 13000);
  assert(visible_frame == 1 && visible_val == 76);
  rgblight_sethsv_eeprom_helper(0, 0, 50, false);
  assert(rgblight_config.val == 50 && visible_val == 76); // 커밋은 즉시, 프레임은 task가 만든다.
  task();
  assert(visible_val == 50);
  rgblight_sethsv_eeprom_helper(0, 0, 200, false); task();
  assert(visible_val == 200);

  scan(13100, 0, true); task();
  assert(visible_frame == 0);
  rgblight_sethsv_eeprom_helper(0, 0, 120, false); task();
  assert(visible_frame == 0 && rgblight_pulse_effect_state.latched && rgblight_pulse_effect_state.key_tracking_valid); // 색 커밋은 물리 hold를 끊지 않는다.
  scan(13200, 0, false); task();
  assert(visible_frame == 1 && visible_val == 120);

  scan(13300, 0, true); task();
  assert(visible_frame == 0);
  rgblight_config.mode = RGBLIGHT_MODE_PULSE_ON_PRESS;
  rgblight_sethsv_eeprom_helper(0, 0, 120, false); task();
  assert(rgblight_status.base_mode == RGBLIGHT_MODE_PULSE_ON_PRESS && !rgblight_pulse_effect_state.latched); // 베이스 모드 전환만 래치를 버린다.
  assert(visible_frame == 0); // Pulse On Press의 기본 출력은 OFF다.
  scan(13400, 0, false); task();
  assert(visible_frame == 0);
  rgblight_config.mode = RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD;
  rgblight_sethsv_eeprom_helper(0, 0, 90, false); task();
  assert(visible_frame == 1 && visible_val == 90);
}

#include "test_tapdance_timing.h"

int main(int argc, char **argv)
{
  bool trace_only = argc > 1 && strcmp(argv[1], "--trace") == 0;
  _Static_assert(TAP_HOLD_CAPS_DELAY == 80 && TAP_CODE_DELAY == 0, "QMK Caps compatibility default must remain 80 ms");
  check_tap(true, trace_only);
  check_tap(false, trace_only);
  if (trace_only) return 0;
  check_plain_tap_requests_no_interval();
  assert(physical_color_writes == 0);
  check_hold(true);
  check_hold(false);
  check_modes_and_last_key();
  check_filter_and_replay();
  check_indicators_sleep_wrap();
  check_config_commit_renders_committed_value();
  check_distinct_tapping_semantics();
  check_velocikey_physical_presses();
  check_mod_tap_and_following_caps();
  assert(physical_color_writes == 0);
  unsigned press_count = caps_press_count;
  uint32_t before = now_ms;
  unsigned intervals_before = keyboard_delay_calls;
  tap_code(KC_CAPS);
  tap_code16(KC_CAPS);
  assert(caps_press_count == press_count + 2U && now_ms == before);
  assert(keyboard_delay_calls == intervals_before + 2U && last_keyboard_delay == 80U);
  tap_code_delay(KC_A, 17);
  assert(now_ms == before && last_keyboard_delay == 17U);
  tap_code16_delay((MOD_LSFT << 8) | KC_A, 200);
  assert(now_ms == before && last_keyboard_delay == 200U);
  assert(blocking_delay_calls == 0U); // TD/LT와 기본 tap helper에 숨은 HAL tick 대기도 없어야 한다.
  wait_ms(0);
  assert(now_ms == before && blocking_delay_calls == 0U);
  wait_ms(3);
  assert(now_ms == before + 4U && blocking_delay_calls == 1U); // 명시적 비영 지연의 기존 의미는 유지

  check_slot_decision_boundaries();
  check_finished_release_preserves_other_dance();
  puts("PASS: actual matrix/QMK TD/LT/RGB + wait port: 80ms Caps interval requested without blocking; pulse, holds, replay, overlay, sleep and wrap; slot terms and overlapping dances");
  return 0;
}
