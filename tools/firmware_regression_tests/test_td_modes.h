/* Gesture matrices execute the production matrix/action/TD path. */
static void mode_fixture(uint8_t mask, uint8_t mode)
{
  direct_fixture(KC_CAPS, KC_NO, 200);
  tapdance_store_mode(0, mode);
  tapdance_state[0].actions[1] = (mask & 1U) ? MO(1) : KC_TRANSPARENT;
  tapdance_state[0].actions[2] = (mask & 2U) ? KC_CAPS : KC_TRANSPARENT;
  tapdance_state[0].actions[3] = (mask & 4U) ? MO(2) : KC_TRANSPARENT;
}

static void check_td_modes(void)
{
  for (uint8_t mask = 0; mask < 8; ++mask)
  {
    mode_fixture(mask, 1);
    scan(1000, 0, true);
    assert(caps_press_count == (mask == 0 ? 1U : 0U));
    scan(1040, 0, false); tick(1300);
    assert(caps_press_count == 1 && caps_release_count == 1 && layer_state == 0);

    mode_fixture(mask, 1);
    scan(1000, 0, true); tick(1201);
    assert(layer_state == ((mask & 1U) ? 2U : 0U));
    assert(caps_press_count == ((mask & 1U) ? 0U : 1U));
    scan(1300, 0, false);
    assert(layer_state == 0 && caps_release_count == caps_press_count);

    mode_fixture(mask, 1);
    scan(1000, 0, true); scan(1040, 0, false);
    scan(1080, 0, true); scan(1120, 0, false); tick(1400);
    assert(caps_press_count == ((mask & 2U) ? 1U : 2U));
    assert(caps_release_count == caps_press_count && layer_state == 0);

    mode_fixture(mask, 1);
    scan(1000, 0, true); scan(1040, 0, false);
    scan(1080, 0, true); tick(1281);
    assert(layer_state == ((mask & 4U) ? 4U : (mask & 1U) ? 2U : 0U));
    assert(caps_press_count == ((mask & 4U) ? 0U : (mask & 1U) ? 1U : 2U));
    scan(1400, 0, false);
    assert(layer_state == 0 && caps_release_count == caps_press_count);
  }

  /* KC_NO selects silence; KC_TRNS removes the override. */
  mode_fixture(7, 1);
  tapdance_state[0].actions[1] = KC_NO;
  scan(1000, 0, true); tick(1201); scan(1300, 0, false);
  assert(caps_press_count == 0 && layer_state == 0);
  mode_fixture(7, 1);
  tapdance_state[0].actions[2] = KC_NO;
  scan(1000, 0, true); scan(1040, 0, false); scan(1080, 0, true); scan(1120, 0, false);
  assert(caps_press_count == 0 && layer_state == 0);
  mode_fixture(7, 1);
  tapdance_state[0].actions[3] = KC_NO;
  scan(1000, 0, true); scan(1040, 0, false); scan(1080, 0, true); tick(1281); scan(1400, 0, false);
  assert(caps_press_count == 0 && layer_state == 0);

  for (uint8_t mask = 0; mask < 8; mask += 2)
  {
    mode_fixture(mask, 2);
    if (mask & 2U) tapdance_state[0].actions[2] = KC_NO;
    scan(1000, 0, true); assert(caps_press_count == 1);
    scan(1040, 0, false); scan(1080, 0, true);
    if (mask) assert(caps_press_count == 1);
    scan(1120, 0, false); tick(1400);
    assert(caps_press_count == ((mask & 2U) ? 1U : 2U));
    assert(caps_release_count == caps_press_count);
  }

  mode_fixture(4, 2);
  tapdance_state[0].actions[3] = KC_NO;
  scan(1000, 0, true); scan(1040, 0, false); scan(1080, 0, true); tick(1281); scan(1400, 0, false);
  assert(caps_press_count == 1 && caps_release_count == 1 && layer_state == 0);
  puts("PASS: all 8 action combinations, four gestures, immediate double/tap-hold, explicit silence and fallback");
}
