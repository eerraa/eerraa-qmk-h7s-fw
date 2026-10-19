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
  clear_keyboard();
  clear_oneshot_mods(); clear_oneshot_locked_mods(); reset_oneshot_layer();
  active_td = NULL;
  memset(tap_dance_states, 0, sizeof(tap_dance_states));
  memset(tapdance_runtime, 0, sizeof(tapdance_runtime));
  memset(tapdance_state, 0, sizeof(tapdance_state));
  memset(tapdance_storage.reserved, 0, sizeof(tapdance_storage.reserved));
  for (unsigned i = 0; i < TAPDANCE_SLOT_COUNT; i++) {
    tapdance_user_data[i].slot_index = i;
    tap_dance_actions[i] = (tap_dance_action_t){.fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, tapdance_on_each_release}, .user_data = &tapdance_user_data[i]};
  }
  tapdance_state[0] = (tapdance_slot_state_t){{KC_CAPS, MO(1), KC_NO, KC_NO}, 200};
  mapped_keycode = keycode;
  other_keycode = KC_A;
  layered_other_keycode = third_keycode = KC_NO;
  quantum_log_len = 0;
  fixture_retro = false; retro_tap_primed = false; retro_tap_curr_key = 0; retro_tap_curr_mods = retro_tap_next_mods = 0;
  memset(source_layers_cache, 0, sizeof(source_layers_cache));
  memset(source_press_generation, 0, sizeof(source_press_generation));
  memset(source_press_down, 0, sizeof(source_press_down));
  memset(source_keycode_cache, 0, sizeof(source_keycode_cache));
  fixture_dynamic_keymap = false;
  g_tapping_term = 200;
  layer_clear();
  output_suspended = false;
  host_suspended = false;
  memset(rgblight_indicator_state, 0, sizeof(rgblight_indicator_state));
  rgblight_indicator_state[0].config.target = RGBLIGHT_INDICATOR_TARGET_CAPS;
  rgblight_indicator_state[0].config.val = 200;
  rgblight_host_led_pending = false;
  host_led_raw = 0; clear_mods(); clear_weak_mods();
  automatic_caps_feedback = true; require_shared_a = forbid_a = false; shared_a_gaps = forbidden_a_reports = 0;
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

static void check_modes_and_held_keys(void)
{
  for (uint8_t mode = RGBLIGHT_MODE_PULSE_OFF_PRESS; mode <= RGBLIGHT_MODE_PULSE_ON_PRESS_HOLD; mode++)
  {
    bool normal_on = mode == RGBLIGHT_MODE_PULSE_OFF_PRESS || mode == RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD;
    bool hold = mode == RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD || mode == RGBLIGHT_MODE_PULSE_ON_PRESS_HOLD;
    reset_fixture(KC_A, mode, 3000);
    assert(visible_frame == normal_on);
    scan(3000, 0, true); task();
    assert(visible_frame == !normal_on);
    tick(3051);
    assert(visible_frame == (hold ? !normal_on : normal_on));
    scan(3060, 1, true); task();
    scan(3070, 0, false); task(); // 다른 키가 눌려 있으면 먼저 누른 키를 떼도 hold가 이어진다.
    tick(3120);
    assert(visible_frame == (hold ? !normal_on : normal_on));
    scan(3130, 1, false); task();
    assert(visible_frame == normal_on);
  }
}

static void check_filter_and_replay(void)
{
  reset_fixture(LT(1, KC_CAPS), RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 4000);
  scan(4000, 0, true); task();
  scan(4010, 1, true); task(); // LT 판정 버퍼가 다른 키를 잡아도 물리 추적은 즉시 바뀐다.
  assert(rgblight_pulse_effect_state.pressed_count == 2);
  scan(4020, 1, false); task();
  tick(4061);
  assert(visible_frame == 0); // 먼저 누른 키가 아직 눌려 있어 Hold가 이어진다.
  scan(4070, 0, false); task();
  // 마지막 키를 떼면 Hold가 끝나고, 탭으로 확정된 Caps 표시가 pulse 대신 보인다.
  assert(rgblight_pulse_effect_state.pressed_count == 0 && !rgblight_pulse_effect_state.latched && visible_frame == 2);
  assert(rgblight_pulse_effect_state.deadline_ms == 0); // 지연 재생이 pulse를 다시 시작하지 않는다.

  reset_fixture(KC_A, RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 5000);
  accept_record = false;
  scan(5000, 0, true); task();
  assert(visible_frame == 0);
  accept_keypress = false;
  scan(5060, 0, false); task();
  assert(visible_frame == 1 && rgblight_pulse_effect_state.pressed_count == 0);
}

static void check_indicators_sleep_wrap(void)
{
  reset_fixture(KC_A, RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 6000);
  rgblight_indicator_post_host_event((led_t){2}); task();
  scan(6001, 0, true); task();
  assert(visible_frame == 2); // Caps ON은 pulse보다 우선한다.
  tick(6060);
  rgblight_indicator_post_host_event((led_t){0}); task();
  assert(visible_frame == 0); // 인디케이터 해제 후 아직 눌린 키의 pulse 복구
  scan(6061, 0, false); task();
  assert(visible_frame == 1);

  // 절전 경계(EERRAA와 같음): 들어가면 래치·추적 키를 버리고, 절전 중 press는 받지 않으며,
  // 풀리면 기본 출력이다. 절전 전에 누른 키의 release는 절전 뒤의 새 hold를 끝내지 못한다.
  scan(6100, 0, true); task();
  assert(visible_frame == 0 && rgblight_pulse_effect_state.pressed_count == 1);
  rgblight_set_output_suspend_state(true); task();
  assert(visible_frame == 0 && !rgblight_pulse_effect_state.latched && rgblight_pulse_effect_state.pressed_count == 0);
  scan(6110, 1, true); task();
  assert(visible_frame == 0 && !rgblight_pulse_effect_state.latched && rgblight_pulse_effect_state.pressed_count == 0);
  scan(6120, 1, false); task();
  rgblight_set_output_suspend_state(false); task();
  assert(visible_frame == 1 && !rgblight_pulse_effect_state.latched && rgblight_pulse_effect_state.pressed_count == 0);
  scan(6130, 1, true); task();
  tick(6200);
  assert(visible_frame == 0 && rgblight_pulse_effect_state.pressed_count == 1);
  scan(6210, 0, false); task();
  assert(visible_frame == 0 && rgblight_pulse_effect_state.pressed_count == 1);
  scan(6220, 1, false); task();
  assert(visible_frame == 1 && rgblight_pulse_effect_state.pressed_count == 0);

  reset_fixture(KC_A, RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, UINT32_MAX - 20);
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
    reset_fixture(dance ? TD(0) : LT(1, KC_CAPS), RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 7000);
    scan(7000, 0, true); task();
    scan(7030, 1, true); task();
    assert(caps_press_count == dance); // TD interruption은 tap, LT 기본 정책은 판정을 보류한다.
    scan(7040, 1, false); task();
    scan(7050, 0, false); task();
    assert(caps_press_count == 1 && caps_release_count == 1 && layer_state == 0);

    reset_fixture(dance ? TD(0) : LT(1, KC_CAPS), RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 8000);
    scan(8000, 0, true); task();
    scan(8040, 0, false); task();
    scan(8150, 0, true); task();
    tick(8351);
    assert(caps_press_count == (dance ? 1U : 2U)); // LT quick-tap 반복과 TD의 새 dance를 유지한다.
    assert(layer_state == (dance ? 2U : 0U));
    scan(8400, 0, false); task();
    assert(layer_state == 0 && caps_release_count == caps_press_count);
  }

  reset_fixture(TD(0), RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 9000);
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
  reset_fixture(LT(1, KC_CAPS), RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 10000);
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
  reset_fixture(MT(MOD_LCTL, KC_CAPS), RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 11000);
  scan(11000, 0, true); task();
  scan(11040, 0, false); task();
  assert(now_ms == 11040 && caps_press_time == 11040 && caps_release_time == 11040);
  assert(caps_press_count == 1 && caps_release_count == 1 && get_mods() == 0);

  reset_fixture(TD(0), RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 12000);
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

// QMK's wakeup-key rule: a key pressed while the host sleeps wakes it but is not typed, not
// even on release after the host is back; the next press types normally.
static void check_wakeup_key_is_not_typed(void)
{
  reset_fixture(KC_B, RGBLIGHT_MODE_STATIC_LIGHT, 21000);
  forbid_a = true; forbidden_a_reports = 0; wake_requests = 0;
  host_suspended = true;
  scan(21010, 1, true); task();
  assert(wake_requests == 1U);
  host_suspended = false;
  tick(21020);
  scan(21030, 1, false); task();
  assert(forbidden_a_reports == 0U);
  host_suspended = true;
  scan(21040, 1, true); scan(21050, 1, false);
  host_suspended = false;
  tick(21060);
  assert(forbidden_a_reports == 0U);
  forbid_a = false; a_reports = 0;
  scan(21070, 1, true); task();
  scan(21080, 1, false); task();
  assert(a_reports == 1U && wake_requests == 3U);
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
  assert(visible_frame == 0 && rgblight_pulse_effect_state.latched && rgblight_pulse_effect_state.pressed_count == 1); // 색 커밋은 물리 hold를 끊지 않는다.
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

  // 모드 전환 전에 눌린 키의 release는 새 모드에서 누른 키의 Hold를 끝내지 못한다.
  scan(13500, 0, true); task();
  rgblight_config.mode = RGBLIGHT_MODE_PULSE_ON_PRESS_HOLD;
  rgblight_sethsv_eeprom_helper(0, 0, 90, false); task();
  assert(visible_frame == 0 && rgblight_pulse_effect_state.pressed_count == 0);
  scan(13510, 1, true); task();
  scan(13520, 0, false); task();
  assert(rgblight_pulse_effect_state.pressed_count == 1);
  tick(13600);
  assert(visible_frame == 1);
  scan(13610, 1, false); task();
  assert(visible_frame == 0 && rgblight_pulse_effect_state.pressed_count == 0);
}

#include "test_tapdance_timing.h"
#include "test_full_term_timing.h"
#include "test_td_ownership.h"
#include "test_td_lifetime.h"
#include "test_td_direct.h"
#include "test_input_remap_assertions.h"

int main(int argc, char **argv)
{
  if (argc > 2 && strcmp(argv[1], "--ownership") == 0) { check_td_ownership(argv[2]); return 0; }
  if (argc > 2 && strcmp(argv[1], "--lifetime") == 0) { check_td_lifetime(argv[2]); return 0; }
  if (argc > 2 && strcmp(argv[1], "--input-remap") == 0) { check_input_remap(argv[2]); return 0; }
  bool trace_only = argc > 1 && strcmp(argv[1], "--trace") == 0;
  _Static_assert(TAP_HOLD_CAPS_DELAY == 80 && TAP_CODE_DELAY == 0, "QMK Caps compatibility default must remain 80 ms");
  check_td_direct();
  check_tap(true, trace_only);
  check_tap(false, trace_only);
  if (trace_only) return 0;
  check_plain_tap_requests_no_interval();
  check_wakeup_key_is_not_typed();
  assert(physical_color_writes == 0);
  check_hold(true);
  check_hold(false);
  check_modes_and_held_keys();
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
  check_full_term_timing();
  const char *ownership_cases[] = {"same_slot", "remap", "remap_shared", "queued", "scan_gap", "shared"};
  for (unsigned i = 0; i < sizeof(ownership_cases) / sizeof(ownership_cases[0]); ++i) check_td_ownership(ownership_cases[i]);
  const char *lifetime_cases[] = {"capture", "partial", "ordinary_layer", "modtap",
      "usage_00", "usage_01", "usage_10", "usage_11", "clear_pending", "clear_queue",
      "clear_queue_pair", "init", "capacity", "toggle", "report_only", "oneshot",
      "oneshot_layer", "old_epoch", "caps_feedback", "oneshot_mod_consumed", "oneshot_layer_consumed",
      "partial_reverse", "ordinary_layer_reverse", "modtap_reverse", "interrupt_relookup",
      "cancel_keeps_waiting", "lighting_boundary", "term_reconfigure", "layer_override", "layer_clear", "tri_no_leak", "tri_shared", "tri_active",
      "hid_shared", "extra_current", "osl_hold", "tap_edge", "osl_double_hold", "rolling_edge", "tap_code_edge",
      "queued_layer", "queued_remap", "retro_release", "retro_hold_lt", "double_cancel",
      "mod_replay", "mod_rolloff", "retro_swallow", "stale_tombstone",
      "retro_td_position", "retro_td_col0", "retro_td_lt", "retro_td_other", "retro_td_off",
      "retro_held_mods", "retro_key_roll", "osl_layer_key", "fallback_mods", "fallback_mods_second",
      "quantum_tap", "quantum_hold", "quantum_nested", "quantum_clear", "quantum_clear_hold",
      "tap_only_release", "storage_reset", "own_clear_hold", "dispatch_epoch", "own_clear_chord"};
  for (unsigned i = 0; i < sizeof(lifetime_cases) / sizeof(lifetime_cases[0]); ++i) check_td_lifetime(lifetime_cases[i]);
  check_input_remap("remap");
  check_input_remap("overflow");
  puts("PASS: actual matrix/QMK TD/LT/RGB + wait port: 80ms Caps interval requested without blocking; pulse, holds, replay, overlay, sleep and wrap; slot terms and overlapping dances");
  return 0;
}
