/* Actual matrix producers, tapping queue/action execution and TD runtime;
 * no timing equation is reimplemented by this fixture. */
static void check_full_term_timing(void)
{
  _Static_assert(sizeof(((keyevent_t *)0)->time) == sizeof(uint32_t), "events must retain a wide clock");
  const uint16_t terms[] = {1, 99, 137, 500, 501, 1000, 32767, 32768, 65534, 65535};
  const uint32_t starts[] = {100000U, UINT32_MAX - 30000U};
  for (unsigned j = 0; j < sizeof(starts) / sizeof(starts[0]); j++)
  {
    const uint32_t start = starts[j];
    for (unsigned i = 0; i < sizeof(terms) / sizeof(terms[0]); i++)
    {
      const uint16_t term = terms[i];
      for (unsigned modtap = 0; modtap < 2; modtap++)
      {
        uint16_t code = modtap ? MT(MOD_LCTL, KC_CAPS) : LT(1, KC_CAPS);
        reset_fixture(code, RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, start - 10U);
        g_tapping_term = term;
        scan(start, 0, true);
        assert(tapping_key.event.time == start);
        tick(start + term - 1U);
        assert(layer_state == 0 && get_mods() == 0 && caps_press_count == 0);
        tick(start + term);
        assert(modtap ? get_mods() == MOD_LCTL : layer_state == 2);
        scan(start + term + 1U, 0, false);
        assert(layer_state == 0 && get_mods() == 0 && caps_press_count == 0);
        if (term > 1)
        {
          reset_fixture(code, RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, start - 10U);
          g_tapping_term = term;
          scan(start, 0, true);
          scan(start + term - 1U, 0, false);
          assert(caps_press_count == 1 && caps_release_count == 1 && layer_state == 0 && get_mods() == 0);
        }
      }
      for (uint8_t slot = 0; slot < TAPDANCE_SLOT_COUNT; slot++)
      {
        reset_fixture(TD(slot), RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, start - 10U);
        tapdance_user_data_t user = {.slot_index = slot};
        tap_dance_actions[slot] = (tap_dance_action_t){
          .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, NULL},
          .user_data = &user,
        };
        tapdance_state[slot] = (tapdance_slot_state_t){{KC_CAPS, MO(1), KC_B, KC_NO}, term};
        dance_event(slot, true, start);
        tick(start + term);
        assert(layer_state == 0 && caps_press_count == 0);
        tick(start + term + 1U);
        assert(layer_state == 2 && caps_press_count == 0);
        dance_event(slot, false, start + term + 2U);
        assert(layer_state == 0);
        tap_dance_actions[slot] = (tap_dance_action_t){0};
      }
    }
    // Skip the entire old 16-bit expiry window; never re-enter the tap window.
    for (uint16_t term = 65534; ; term++)
    {
      reset_fixture(LT(1, KC_CAPS), RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, start - 10U);
      g_tapping_term = term;
      scan(start, 0, true);
      tick(start + 65533U);
      assert(layer_state == 0);
      tick(start + 65536U);
      assert(layer_state == 2 && caps_press_count == 0);
      scan(start + 131072U, 0, false);
      assert(layer_state == 0 && caps_press_count == 0);
      if (term == UINT16_MAX) break;
    }
    // Both events of the second LT wait behind the first LT. The short second
    // tap stays a tap when replay happens long after its physical release.
    reset_fixture(LT(1, KC_CAPS), RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, start - 10U);
    g_tapping_term = UINT16_MAX;
    other_keycode = LT(1, KC_CAPS);
    scan(start, 0, true);
    scan(start + 1U, 1, true);
    scan(start + 2U, 1, false);
    assert(waiting_buffer_head != waiting_buffer_tail);
    assert(waiting_buffer[waiting_buffer_tail].event.time == start + 1U);
    tick(start + 3U); // Separate this fixture's tick epoch from the previous case.
    tick(start + 65536U);
    assert(layer_state == 2);
    assert(caps_press_count == 1 && caps_release_count == 1);
    assert(waiting_buffer_head == waiting_buffer_tail);
    scan(start + 65537U, 0, false);
    assert(layer_state == 0);
  }
  puts("PASS: full uint16 terms, LT/MT and all TD slots, exact expiry boundaries, skipped 16-bit window, queued timestamps and 32-bit wrap");
}
