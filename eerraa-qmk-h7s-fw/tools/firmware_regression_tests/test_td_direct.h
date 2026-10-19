/* Actual TD/action engine: the base press is physical, only the second waits. */
static void direct_fixture(uint16_t base, uint16_t hold, uint16_t term)
{
  reset_fixture(TD(0), RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 999);
  tick(999);
  tapdance_timing_defaults();
  tapdance_store_mode(0, 2);
  tapdance_state[0] = (tapdance_slot_state_t){{base, KC_TRANSPARENT, KC_TRANSPARENT, hold == KC_NO ? KC_TRANSPARENT : hold}, term};
}

#include "test_td_modes.h"

static void check_td_direct(void)
{
  direct_fixture(KC_CAPS, MO(1), 200);
  scan(1000, 0, true);
  assert(caps_press_count == 1 && caps_press_time == 1000);
  scan(1040, 0, false);
  assert(caps_release_count == 1 && caps_release_time == 1040 && keyboard_delay_calls == 0);
  tick(1300);
  assert(caps_press_count == 1 && layer_state == 0);

  direct_fixture(KC_CAPS, MO(1), 200);
  scan(1000, 0, true); tick(1400);
  assert(caps_press_count == 1 && caps_release_count == 0 && layer_state == 0);
  scan(1500, 0, false);
  assert(caps_release_time == 1500 && keyboard_delay_calls == 0);

  direct_fixture(KC_CAPS, MO(1), 200);
  scan(1000, 0, true); scan(1040, 0, false); scan(1080, 0, true);
  assert(caps_press_count == 1 && layer_state == 0);
  tick(1280); assert(layer_state == 0);
  tick(1281); assert(layer_state == 2 && caps_press_count == 1);
  scan(1400, 0, false); assert(layer_state == 0 && caps_release_count == 1);

  direct_fixture(KC_CAPS, MO(1), 200);
  scan(1000, 0, true); scan(1040, 0, false); scan(1080, 0, true); scan(1120, 0, false);
  assert(caps_press_count == 2 && caps_release_count == 2 && layer_state == 0);
  assert(caps_press_time == 1120 && keyboard_delay_calls == 1);
  scan(1160, 0, true); assert(caps_press_count == 3);
  scan(1190, 0, false); tick(1500); assert(caps_release_count == 3);

  /* Interruption resolves to the base key, with no duplicated first tap. */
  direct_fixture(KC_CAPS, MO(1), 200);
  scan(1000, 0, true); scan(1040, 0, false); scan(1080, 0, true); scan(1100, 1, true);
  assert(caps_press_count == 2 && layer_state == 0);
  scan(1110, 0, false); scan(1120, 1, false);
  assert(caps_release_count == 2);

  /* Settings edited mid-gesture do not change the captured base or hold. */
  direct_fixture(KC_CAPS, MO(1), 200);
  scan(1000, 0, true);
  tapdance_store_mode(0, 0);
  tapdance_state[0].actions[0] = KC_B;
  tapdance_state[0].actions[3] = MO(2);
  scan(1040, 0, false); scan(1080, 0, true); tick(1281);
  assert(layer_state == 2 && caps_press_count == 1 && caps_release_count == 1);
  scan(1300, 0, false); assert(layer_state == 0);

  /* Clear/reset retires both the immediate key and a resolved layer hold. */
  direct_fixture(KC_X, MO(1), 200);
  scan(1000, 0, true); assert(is_key_pressed(KC_X));
  clear_keyboard(); assert(!is_key_pressed(KC_X));
  scan(1010, 0, false); tick(1300); assert(!is_key_pressed(KC_X));
  direct_fixture(KC_X, MO(1), 200);
  scan(1000, 0, true); scan(1040, 0, false); scan(1080, 0, true); tick(1281);
  tapdance_storage_apply_defaults();
  assert(layer_state == 0 && tapdance_mode(0) == 0);
  scan(1400, 0, false); assert(layer_state == 0);

  /* Same output held by an ordinary key outlives the direct dance. */
  direct_fixture(KC_X, MO(1), 200); other_keycode = KC_X;
  scan(1000, 1, true); scan(1010, 0, true); scan(1020, 0, false);
  assert(is_key_pressed(KC_X));
  scan(1030, 1, false); assert(!is_key_pressed(KC_X));

  direct_fixture(KC_A, MO(1), 200);
  set_oneshot_mods(MOD_BIT(KC_LCTL)); last_a_mods = 0;
  scan(1000, 0, true);
  assert(last_a_mods == MOD_BIT(KC_LCTL));
  scan(1010, 0, false); tick(1201);
  assert(get_mods() == 0 && get_oneshot_mods() == 0 && !is_key_pressed(KC_A));

  for (uint8_t slot = 0; slot < TAPDANCE_SLOT_COUNT; ++slot) {
    const uint16_t term = slot == 0 ? 1 : slot == 7 ? 65535 : 200;
    direct_fixture(KC_CAPS, MO(1), term);
    tapdance_store_mode(slot, 2);
    tapdance_state[slot] = tapdance_state[0];
    const uint32_t start = 0xfffffff0U;
    dance_event(slot, true, start); dance_event(slot, false, start);
    dance_event(slot, true, start + 1U);
    now_ms = start + 1U + term; tap_dance_task(); assert(layer_state == 0);
    ++now_ms; tap_dance_task(); assert(layer_state == 2 && caps_press_count == 1);
    dance_event(slot, false, now_ms + 1); assert(layer_state == 0);
  }
  /* Long multi-press window, independent short hold threshold. */
  direct_fixture(KC_CAPS, MO(1), 2000);
  tapdance_timing.hold_ms[0] = 180;
  scan(1000, 0, true); scan(1040, 0, false);
  tick(1800); scan(1900, 0, true);
  tick(2080); assert(layer_state == 0);
  tick(2081); assert(layer_state == 2 && caps_press_count == 1);
  scan(2100, 0, false); assert(layer_state == 0 && caps_release_count == 1);

  /* A pending second hold changes the other key's source layer first. */
  direct_fixture(KC_CAPS, MO(1), 2000);
  tapdance_timing.hold_on_other = 1;
  layered_other_keycode = KC_B;
  scan(1000, 0, true); scan(1040, 0, false); scan(1080, 0, true);
  scan(1100, 1, true);
  assert(layer_state == 2 && is_key_pressed(KC_B) && !is_key_pressed(KC_A));
  assert(caps_press_count == 1 && caps_release_count == 1);
  scan(1110, 0, false); assert(layer_state == 0 && is_key_pressed(KC_B));
  scan(1120, 1, false); assert(!is_key_pressed(KC_B));

  /* First physical hold remains the base even with early hold enabled. */
  direct_fixture(KC_CAPS, MO(1), 2000);
  tapdance_timing.hold_on_other = 1;
  scan(1000, 0, true); scan(1020, 1, true);
  assert(layer_state == 0 && caps_press_count == 1 && caps_release_count == 0);
  scan(1030, 1, false); scan(1040, 0, false);
  assert(caps_release_count == 1);

  /* Timing and early-hold policy are captured at the first press. */
  direct_fixture(KC_CAPS, MO(1), 2000);
  tapdance_timing.hold_ms[0] = 180;
  scan(1000, 0, true); scan(1040, 0, false);
  tapdance_timing.hold_ms[0] = 2000;
  tapdance_state[0].term_ms = 50;
  scan(1900, 0, true); tick(2081); assert(layer_state == 2);
  scan(2100, 0, false); assert(layer_state == 0);

  /* Deferred first hold also resolves before the interrupting key. */
  direct_fixture(KC_CAPS, MO(1), 2000);
  tapdance_store_mode(0, 1);
  tapdance_state[0].actions[1] = MO(1);
  tapdance_timing.hold_on_other = 1;
  layered_other_keycode = KC_B;
  scan(1000, 0, true); scan(1020, 1, true);
  assert(layer_state == 2 && is_key_pressed(KC_B) && caps_press_count == 0);
  scan(1030, 1, false); scan(1040, 0, false); assert(layer_state == 0);
  check_td_modes();
  puts("PASS: direct TD physical edges, second-press decisions, interruption, snapshots, reset, ownership, all slots/terms/wrap");
}
