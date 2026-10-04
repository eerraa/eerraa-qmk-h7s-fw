static void input_remap_via(uint16_t code, unsigned mutation)
{
  uint8_t data[32] = {id_dynamic_keymap_set_keycode, 0, 0, 0, code >> 8, code & 255};
  if (mutation == 1 || mutation == 2) {
    data[0] = id_dynamic_keymap_set_buffer; data[1] = 0; data[2] = 0;
    data[3] = mutation == 1 ? 2 : 1; data[4] = code >> 8; data[5] = code;
  } else if (mutation == 3) { data[0] = id_dynamic_keymap_reset; fixture_reset_keycode = code; }
  raw_hid_receive(data, sizeof(data));
  if (mutation == 2) { data[2] = 1; data[4] = code; raw_hid_receive(data, sizeof(data)); }
  assert(dynamic_keymap_get_keycode(0, 0, 0) == code);
}

static void input_remap_begin(uint16_t code)
{
  reset_fixture(code, RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 1000);
  fixture_dynamic_keymap = true;
  memset(fixture_keymap_image, 0, sizeof(fixture_keymap_image));
  for (unsigned layer = 0; layer < DYNAMIC_KEYMAP_LAYER_COUNT; ++layer)
    dynamic_keymap_set_keycode(layer, 0, 0, code);
  scan(1000, 0, true);
}

static void check_input_synthetic_remap(void)
{
  for (unsigned after = 0; after < 2; ++after) {
    input_remap_begin(LT(1, KC_A));
    for (unsigned layer = 0; layer < DYNAMIC_KEYMAP_LAYER_COUNT; ++layer) {
      dynamic_keymap_set_keycode(layer, 0, 1, LT(2, KC_X));
      dynamic_keymap_set_keycode(layer, 0, 2, KC_B);
    }
    scan(1050, 0, false); scan(1100, 0, true);
    assert(tapping_key.tap.count == 2 && is_key_pressed(KC_A));
    input_remap_via(KC_B, 0); scan(1150, 2, true);
    assert(is_key_pressed(KC_A) && is_key_pressed(KC_B));
    const uint32_t next = after ? 1400 : 1200;
    scan(next, 1, true);
    assert(!is_key_pressed(KC_A) && is_key_pressed(KC_B));
    scan(next + 25, 2, false); scan(next + 50, 0, false); scan(next + 75, 1, false);
    tick(next + 400);
    assert(!is_key_pressed(KC_A) && !is_key_pressed(KC_B) && layer_state == 0);
  }
  puts("PASS: within-term and after-term synthetic tapping up preserve old action and another held usage");
}

static void check_input_generation_overflow(void)
{
  input_remap_begin(KC_A); scan(1050, 0, false);
  assert(source_press_generation[0][0] == 1);
  assert(source_keycode_cache[0][0].generation == 1);
  input_remap_via(KC_B, 0);
  for (unsigned layer = 0; layer < DYNAMIC_KEYMAP_LAYER_COUNT; ++layer) {
    dynamic_keymap_set_keycode(layer, 0, 0, KC_B);
    dynamic_keymap_set_keycode(layer, 0, 1, LT(2, KC_X));
    dynamic_keymap_set_keycode(layer, 0, 2, KC_NO);
  }
  g_tapping_term = UINT16_MAX;
  uint32_t time = 1100;
  const uint32_t overflow_before = waiting_buffer_overflow_count;
  /* Every prefix is cancelled by the actual ring overflow; no col0 press
     reaches quantum, so its executed cache remains generation 1 / KC_A. */
  for (unsigned batch = 0; batch < 511; ++batch) {
    scan(time, 1, true); time += 25;
    scan(time, 2, true); time += 25;
    for (unsigned i = 0; i < 128; ++i) {
      scan(time, 0, true); time += 25;
      scan(time, 0, false); time += 25;
    }
    assert(waiting_buffer_overflow_count == overflow_before + batch + 1);
    scan(time, 2, false); time += 25;
    scan(time, 1, false); time += 25;
    assert(source_keycode_cache[0][0].generation == 1);
    assert(!is_key_pressed(KC_B));
  }
  assert(source_press_generation[0][0] == 65409U);
  scan(time, 1, true); time += 25;
  scan(time, 2, true); time += 25;
  for (unsigned i = 0; i < 126; ++i) {
    scan(time, 0, true); time += 25;
    scan(time, 0, false); time += 25;
  }
  assert(source_press_generation[0][0] == UINT16_MAX);
  scan(time, 0, true); time += 25;
  scan(time, 0, false); time += 25;
  assert(source_press_generation[0][0] == 1);
  tick(time + 65536U);
  scan(time + 65561U, 2, false);
  scan(time + 65586U, 1, false);
  printf("generation=%u executed_generation=%u executed_code=%04x overflows=%lu elapsed_ms=%lu A=%d B=%d all_up=%d\n",
         source_press_generation[0][0], source_keycode_cache[0][0].generation,
         source_keycode_cache[0][0].keycode,
         (unsigned long)(waiting_buffer_overflow_count - overflow_before),
         (unsigned long)(time + 65586U - 1000U), is_key_pressed(KC_A), is_key_pressed(KC_B), matrix_rows[0] == 0);
  fflush(stdout);
  assert(!is_key_pressed(KC_A) && !is_key_pressed(KC_B));
  assert(layer_state == 0);
  puts("PASS: generation wrap after discarded prefixes cannot reuse stale executed press");
}

static void check_input_remap(const char *name)
{
  if (!strcmp(name, "generation_overflow")) { check_input_generation_overflow(); return; }
  if (!strcmp(name, "synthetic")) { check_input_synthetic_remap(); return; }
  if (!strcmp(name, "overflow")) {
    reset_fixture(MO(1), RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 1000);
    other_keycode = LT(2, KC_X); third_keycode = KC_A;
    g_tapping_term = UINT16_MAX;
    layer_on(3); /* persistent layer must survive recovery */
    scan(1000, 0, true); assert(layer_state & 2U);
    scan(1025, 1, true); scan(1050, 0, false);
    const uint32_t overflows = waiting_buffer_overflow_count;
    for (unsigned i = 0; i < 128; ++i) {
      scan(1075 + i * 50, 2, true); scan(1100 + i * 50, 2, false);
    }
    assert(waiting_buffer_overflow_count == overflows + 1);
    assert(!(layer_state & 2U)); assert(layer_state & 8U);
    scan(8000, 1, false); assert(layer_state == 8U);
    other_keycode = KC_B;
    scan(8100, 1, true); assert(is_key_pressed(KC_B)); scan(8125, 1, false);
    assert(!is_key_pressed(KC_B));
    /* Same-bit persistent state and explicit TO replacement have their own
     * owner, so a stale physical up cannot clear the replacement. */
    reset_fixture(MO(1), RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 9000);
    scan(9000, 0, true); layer_on(1);
    layer_clear_physical_momentary(); assert(layer_state == 2U);
    scan(9025, 0, false); assert(layer_state == 2U);
    scan(9050, 0, true); layer_move(1); scan(9075, 0, false); assert(layer_state == 2U);
    layer_clear();
    puts("PASS: production tapping overflow releases MO and retains persistent layer");
    return;
  }
  for (unsigned mutation = 0; mutation < 4; ++mutation) {
    input_remap_begin(KC_A); assert(is_key_pressed(KC_A));
    input_remap_via(KC_B, mutation); scan(1050, 0, false);
    assert(!is_key_pressed(KC_A) && !is_key_pressed(KC_B));
    scan(1075, 0, true); assert(is_key_pressed(KC_B)); scan(1100, 0, false);
    input_remap_begin(KC_LCTL); assert(get_mods() == MOD_BIT(KC_LCTL));
    input_remap_via(KC_NO, mutation); scan(1050, 0, false); assert(get_mods() == 0);
    input_remap_begin(MO(1)); assert(layer_state == 2U);
    input_remap_via(MO(2), mutation); scan(1050, 0, false); assert(layer_state == 0);
    input_remap_begin(MT(MOD_LCTL, KC_A));
    input_remap_via(KC_B, mutation); tick(1250); assert(get_mods() == MOD_BIT(KC_LCTL));
    scan(1300, 0, false); assert(get_mods() == 0 && !is_key_pressed(KC_B));
    input_remap_begin(LT(1, KC_A));
    input_remap_via(KC_B, mutation); tick(1251); assert(layer_state == 2U);
    scan(1300, 0, false); assert(layer_state == 0 && !is_key_pressed(KC_B));
    input_remap_begin(LT(1, KC_A));
    const unsigned before_tap = a_reports;
    input_remap_via(KC_B, mutation); scan(1050, 0, false);
    assert(a_reports > before_tap && !is_key_pressed(KC_A) && !is_key_pressed(KC_B) && layer_state == 0);
    input_remap_begin(KC_MS_BTN1); assert(mouse_codes_down);
    input_remap_via(KC_B, mutation); scan(1050, 0, false); assert(mouse_codes_down == 0);
    input_remap_begin(KC_AUDIO_VOL_UP); assert(last_consumer);
    input_remap_via(KC_B, mutation); scan(1050, 0, false); assert(last_consumer == 0);
    input_remap_begin(KC_A);
    tapdance_state[0].actions[1] = KC_A;
    for (unsigned layer = 0; layer < 4; ++layer) dynamic_keymap_set_keycode(layer, 0, 1, TD(0));
    scan(1010, 1, true); tick(1253);
    input_remap_via(KC_B, mutation); scan(1300, 0, false); assert(is_key_pressed(KC_A));
    scan(1350, 1, false); assert(!is_key_pressed(KC_A) && !is_key_pressed(KC_B));
  }
  check_input_synthetic_remap();

  /* A duplicate ingress uses the current lifetime and cannot register the
   * changed mapping; the next physical press owns a fresh generation. */
  input_remap_begin(KC_A); input_remap_via(KC_B, 0);
  action_exec_physical((keyevent_t){.key = {.row=0,.col=0},.time=1025,.type=KEY_EVENT,.pressed=true}, 100);
  assert(is_key_pressed(KC_A) && !is_key_pressed(KC_B));
  scan(1050, 0, false); assert(!is_key_pressed(KC_A));
  /* A held modifier up queues before a later generation at the same switch.
   * VIA changes the entry while the LT queue still owns both lifetimes. */
  input_remap_begin(KC_LCTL);
  for (unsigned layer = 0; layer < 4; ++layer) dynamic_keymap_set_keycode(layer, 0, 1, LT(2, KC_X));
  scan(1010, 1, true); scan(1020, 0, false);
  input_remap_via(KC_B, 0);
  for (unsigned layer = 1; layer < 4; ++layer) dynamic_keymap_set_keycode(layer, 0, 0, KC_B);
  scan(1030, 0, true); scan(1040, 0, false); tick(1252);
  assert(get_mods() == 0 && !is_key_pressed(KC_B));
  scan(1300, 1, false); assert(layer_state == 0);

  /* A generation wraps only between lifetimes and zero remains unassigned. */
  input_remap_begin(KC_A); scan(1050, 0, false);
  source_press_generation[0][0] = UINT16_MAX - 1;
  scan(1100, 0, true); input_remap_via(KC_B, 0); scan(1125, 0, false);
  assert(!is_key_pressed(KC_A));
  scan(1150, 0, true); assert(source_press_generation[0][0] == 1 && is_key_pressed(KC_B));
  scan(1175, 0, false); assert(!is_key_pressed(KC_B));

  /* Retiring outputs while held never redirects the later up to a new map. */
  input_remap_begin(KC_A); clear_keyboard(); input_remap_via(KC_B, 0); scan(1050, 0, false);
  assert(!is_key_pressed(KC_A) && !is_key_pressed(KC_B));
  scan(1100, 0, true); assert(is_key_pressed(KC_B)); scan(1125, 0, false);
  check_input_generation_overflow();
  puts("PASS: actual VIA keycode/buffer/split/reset mutations preserve held press release and tap-hold decision");
}
