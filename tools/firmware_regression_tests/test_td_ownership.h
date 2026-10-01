/* Production TD/port/action path. Clock, matrix and output sinks are adapters. */
static void check_td_ownership(const char *name)
{
  reset_fixture(TD(0), 46, 999);
  tick(999); /* Re-establish the production tick epoch after the test clock reset. */
  tapdance_state[0] = (tapdance_slot_state_t){{KC_CAPS, MO(1), KC_NO, KC_NO}, 200};
  if (strcmp(name, "same_slot") == 0) {
    other_keycode = TD(0);
    scan(1000, 0, true); tick(1201);
    tapdance_state[0].actions[1] = MO(2);
    tapdance_state[0].term_ms = 37;
    scan(1210, 1, true); tick(1248);
    assert(layer_state == 6);
    scan(1250, 1, false); task(); assert(layer_state == 2);
    scan(1251, 0, false); task(); assert(layer_state == 0);
  } else if (strcmp(name, "remap") == 0) {
    scan(1000, 0, true); tick(1201); assert(layer_state == 2);
    mapped_keycode = KC_X;
    scan(1210, 0, false); task(); assert(layer_state == 0);
  } else if (strcmp(name, "remap_shared") == 0) {
    /* The new entry at a held TD position must not release another input's output. */
    const uint16_t outputs[] = {KC_X, KC_LSFT, MO(2)};
    for (unsigned i = 0; i < 3; ++i) {
      const uint32_t t = 1000U + 1000U * i;
      reset_fixture(TD(0), 46, t - 1U); tick(t - 1U);
      tapdance_state[0] = (tapdance_slot_state_t){{KC_CAPS, MO(1), KC_NO, KC_NO}, 200};
      other_keycode = outputs[i];
      scan(t, 0, true); tick(t + 201U); scan(t + 210U, 1, true);
      mapped_keycode = outputs[i];
      scan(t + 220U, 0, false); task();
      assert(is_key_pressed(KC_X) == (i == 0) && get_mods() == (i == 1 ? MOD_BIT(KC_LSFT) : 0) &&
             keyboard_report->mods == get_mods() && layer_state == (i == 2 ? 4U : 0U));
      scan(t + 230U, 1, false); task();
      assert(!is_key_pressed(KC_X) && get_mods() == 0 && layer_state == 0);
    }
  } else if (strcmp(name, "queued") == 0) {
    mapped_keycode = LT(1, KC_CAPS); other_keycode = TD(0);
    tapdance_state[0].actions[1] = MO(2); tapdance_state[0].term_ms = 50;
    scan(1000, 0, true); scan(1010, 1, true); tick(1201);
    assert(layer_state == 6);
    scan(1220, 1, false); scan(1221, 0, false); task(); assert(layer_state == 0);
  } else if (strcmp(name, "scan_gap") == 0) {
    scan(1000, 0, true); scan(1201, 0, false); task();
    assert(caps_press_count == 0 && layer_state == 0);
  } else if (strcmp(name, "shared") == 0) {
    other_keycode = TD(1);
    tapdance_state[1] = (tapdance_slot_state_t){{KC_X, MO(1), KC_NO, KC_NO}, 37};
    scan(1000, 0, true); tick(1201);
    scan(1210, 1, true); tick(1248); assert(layer_state == 2);
    scan(1250, 0, false); task(); assert(layer_state == 2);
    scan(1251, 1, false); task(); assert(layer_state == 0);
  } else { assert(!"unknown ownership case"); }
  printf("PASS ownership %s\n", name);
}
