// 슬롯별 실제 QMK 판정 경계와 서로 겹치는 dance의 수명을 검증한다.
static void dance_event(uint8_t slot, bool pressed, uint32_t time)
{
  now_ms = time;
  keyrecord_t record = {0};
  record.event.pressed = pressed;
  record.event.time = time;
  preprocess_tap_dance(TD(slot), &record);
  process_tap_dance(TD(slot), &record);
}

static void check_slot_decision_boundaries(void)
{
  for (uint8_t slot = 0; slot < TAPDANCE_SLOT_COUNT; slot++)
  {
    reset_fixture(TD(slot), 46, 65000);
    tapdance_user_data_t user = {.slot_index = slot};
    tap_dance_actions[slot] = (tap_dance_action_t){
      .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, NULL},
      .user_data = &user,
    };
    const uint16_t terms[TAPDANCE_SLOT_COUNT] = {100, 101, 137, 199, 201, 333, 499, 500};
    uint16_t term = terms[slot];
    tapdance_state[slot] = (tapdance_slot_state_t){{KC_CAPS, MO(1), KC_B, KC_NO}, term};
    dance_event(slot, true, 65500);
    tick(65500 + term);
    assert(layer_state == 0 && caps_press_count == 0);
    tick(65501 + term);
    assert(layer_state == 2 && caps_press_count == 0);
    dance_event(slot, false, 65502 + term);
    assert(layer_state == 0);
    // Double가 있으면 짧은 단일 탭도 동일한 슬롯의 term 이후에 출력한다.
    dance_event(slot, true, 67000);
    dance_event(slot, false, 67010);
    tick(67000 + term);
    assert(caps_press_count == 0);
    tick(67001 + term);
    assert(caps_press_count == 1 && caps_release_count == 1);
    tap_dance_actions[slot] = (tap_dance_action_t){0};
  }
}

static void check_finished_release_preserves_other_dance(void)
{
  reset_fixture(TD(0), 46, 70000);
  tapdance_user_data_t users[2] = {{.slot_index = 0}, {.slot_index = 1}};
  for (uint8_t slot = 0; slot < 2; slot++)
  {
    tap_dance_actions[slot] = (tap_dance_action_t){
      .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, NULL},
      .user_data = &users[slot],
    };
    tapdance_state[slot] = (tapdance_slot_state_t){{KC_CAPS, MO(1), KC_NO, KC_NO}, 137};
  }
  dance_event(0, true, 70000);
  tick(70138);
  assert(layer_state == 2);
  dance_event(1, true, 70200);
  dance_event(0, false, 70210);
  assert(layer_state == 0);
  assert(active_td == TD(1));
  tick(70337);
  assert(layer_state == 0);
  tick(70338);
  assert(layer_state == 2);
  dance_event(1, false, 70400);
  assert(layer_state == 0 && caps_press_count == 0);
}
