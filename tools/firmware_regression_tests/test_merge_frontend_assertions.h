/* Production report construction/provenance and filter run above this fixture.
 * This adapter records USB submission calls, not USB hardware completions. */
typedef struct {
  bool is_report;
  uint32_t scan_token;
  uint8_t usage;
  bool pressed;
  report_keyboard_t report;
} merge_frontend_observation_t;
static merge_frontend_observation_t merge_trace[4096];
static unsigned merge_trace_count;

static void merge_record_report(const report_keyboard_t *report, uint32_t scan_token, uint8_t usage, bool pressed)
{
  assert(merge_trace_count < sizeof(merge_trace) / sizeof(merge_trace[0]));
  merge_trace[merge_trace_count++] = (merge_frontend_observation_t){true, scan_token, usage, pressed, *report};
}
static void merge_record_barrier(uint32_t scan_token)
{
  assert(merge_trace_count < sizeof(merge_trace) / sizeof(merge_trace[0]));
  merge_trace[merge_trace_count++] = (merge_frontend_observation_t){.scan_token = scan_token};
}
static unsigned merge_reports(void)
{
  unsigned count = 0;
  for (unsigned i = 0; i < merge_trace_count; ++i) count += merge_trace[i].is_report;
  return count;
}
static unsigned merge_report_index(unsigned n)
{
  for (unsigned i = 0; i < merge_trace_count; ++i)
    if (merge_trace[i].is_report && n-- == 0U) return i;
  assert(!"missing frontend report");
  return 0U;
}
static bool merge_barrier_between(unsigned first, unsigned last)
{
  for (unsigned i = first + 1U; i < last; ++i)
    if (!merge_trace[i].is_report) return true;
  return false;
}
static void merge_reset(uint16_t keycode)
{
  fixture_keyboard_observer = NULL;
  fixture_scan_end_observer = NULL;
  fixture_pre_process_record = fixture_process_record = NULL;
  fixture_post_process_record = NULL;
  driver = NULL;
  memset(matrix_rows, 0, sizeof(matrix_rows));
  scan_changed = true;
  matrix_task();
  memset(kill_switch_config, 0, sizeof(kill_switch_config));
  memset(kill_switch_seen, 0, sizeof(kill_switch_seen));
  for (unsigned i = 0; i < KILL_SWITCH_MAX_CH; ++i) kill_switch_last[i] = -1;
  reset_fixture(keycode, RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 60000U);
  merge_trace_count = 0U;
  fixture_keyboard_observer = merge_record_report;
  fixture_scan_end_observer = merge_record_barrier;
}
static void merge_same_scan(uint16_t keys)
{
  matrix_rows[0] = keys;
  scan_changed = true;
  physical_dispatch = true;
  matrix_task();
  physical_dispatch = false;
}

static void check_merge_physical_scope(void)
{
  merge_reset(KC_A);
  other_keycode = KC_B;
  merge_same_scan(3U);
  assert(merge_reports() == 2U);
  unsigned a = merge_report_index(0), b = merge_report_index(1);
  assert(merge_trace[a].scan_token != 0U && merge_trace[a].scan_token == merge_trace[b].scan_token);
  assert(merge_trace[a].usage == KC_A && merge_trace[b].usage == KC_B && merge_trace[a].pressed && merge_trace[b].pressed);
  assert(!merge_barrier_between(a, b));
  assert(merge_trace_count > b + 1U && !merge_trace[b + 1U].is_report);
  /* The first report has reached the native host call before end of scan. */
  assert(merge_trace[0].is_report && report_has_key(&merge_trace[a].report, KC_A));
  merge_trace_count = 0U;
  merge_same_scan(0U);
  assert(merge_reports() == 2U);
  a = merge_report_index(0); b = merge_report_index(1);
  assert(merge_trace[a].scan_token != 0U && merge_trace[a].scan_token == merge_trace[b].scan_token);
  assert(!merge_trace[a].pressed && !merge_trace[b].pressed);
  assert(!report_has_key(&merge_trace[b].report, KC_A) && !report_has_key(&merge_trace[b].report, KC_B));
}
static void check_merge_scan_identity(void)
{
  merge_reset(KC_A);
  other_keycode = KC_B;
  merge_same_scan(1U);
  const uint32_t first_token = merge_trace[merge_report_index(0)].scan_token;
  const uint32_t first_time = now_ms;
  merge_trace_count = 0U;
  merge_same_scan(3U);
  const uint32_t second_token = merge_trace[merge_report_index(0)].scan_token;
  assert(now_ms == first_time && first_token != 0U && second_token != 0U && first_token != second_token);
}
static void check_merge_duplicate_usage(void)
{
  merge_reset(KC_A);
  other_keycode = KC_A;
  merge_same_scan(3U);
  assert(merge_reports() == 3U);
  unsigned first = merge_report_index(0), up = merge_report_index(1), down = merge_report_index(2);
  assert(merge_trace[first].scan_token != 0U);
  assert(merge_trace[up].scan_token == 0U && merge_trace[down].scan_token == 0U);
  assert(!report_has_key(&merge_trace[up].report, KC_A) && report_has_key(&merge_trace[down].report, KC_A));
  assert(merge_barrier_between(first, up));
}
static void check_merge_layer_barrier(void)
{
  merge_reset(KC_A);
  other_keycode = MO(1);
  third_keycode = KC_C;
  merge_same_scan(7U);
  assert(merge_reports() == 2U && (layer_state & 2U));
  unsigned a = merge_report_index(0), c = merge_report_index(1);
  assert(merge_trace[a].scan_token != 0U && merge_trace[c].scan_token != 0U);
  assert(merge_barrier_between(a, c));
}
static bool merge_consume_b(uint16_t code, keyrecord_t *record)
{
  (void)record;
  return code != KC_B;
}
static void check_merge_consumed_barrier(bool preprocess)
{
  merge_reset(KC_A);
  other_keycode = KC_B;
  third_keycode = KC_C;
  if (preprocess) fixture_pre_process_record = merge_consume_b;
  else fixture_process_record = merge_consume_b;
  merge_same_scan(7U);
  assert(merge_reports() == 2U);
  unsigned a = merge_report_index(0), c = merge_report_index(1);
  assert(merge_trace[a].scan_token != 0U && merge_trace[c].scan_token != 0U);
  assert(!report_has_key(&merge_trace[c].report, KC_B));
  assert(merge_barrier_between(a, c));
}
static void merge_change_layer_after_a(uint16_t code, keyrecord_t *record)
{
  if (code == KC_A && record->event.pressed) layer_on(1U);
}
static void check_merge_post_callback_barrier(void)
{
  merge_reset(KC_A);
  other_keycode = KC_B;
  fixture_post_process_record = merge_change_layer_after_a;
  merge_same_scan(3U);
  assert(merge_reports() == 2U && (layer_state & 2U));
  assert(merge_barrier_between(merge_report_index(0), merge_report_index(1)));
}
static keyrecord_t merge_record(uint8_t code, bool pressed)
{
  (void)code;
  return (keyrecord_t){.event = {.key = {.row = 0U, .col = 0U}, .time = now_ms, .type = KEY_EVENT, .pressed = pressed}, .report_scan_token = 123U};
}
static void check_merge_modifier_guards(void)
{
  for (unsigned mode = 0; mode < 5U; ++mode) {
    merge_reset(KC_A);
    if (mode == 0U) set_mods(MOD_BIT(KC_LCTL));
    if (mode == 1U) set_weak_mods(MOD_BIT(KC_LSFT));
    if (mode == 2U) set_oneshot_mods(MOD_BIT(KC_LALT));
    if (mode == 3U) set_oneshot_locked_mods(MOD_BIT(KC_LGUI));
    if (mode == 4U) set_oneshot_layer(1U, ONESHOT_START);
    keyrecord_t record = merge_record(KC_A, true);
    process_action(&record, action_for_keycode(KC_A));
    assert(merge_reports() == 1U && merge_trace[merge_report_index(0)].scan_token == 0U);
  }
}
static void check_merge_lock_guards(void)
{
  const uint8_t keys[] = {KC_CAPS_LOCK, KC_NUM_LOCK, KC_SCROLL_LOCK, KC_LOCKING_CAPS_LOCK, KC_LOCKING_NUM_LOCK, KC_LOCKING_SCROLL_LOCK};
  for (unsigned i = 0; i < sizeof(keys); ++i) {
    merge_reset(keys[i]);
    merge_same_scan(1U);
    for (unsigned j = 0; j < merge_trace_count; ++j)
      if (merge_trace[j].is_report) assert(merge_trace[j].scan_token == 0U);
  }
}
static void check_merge_socd_pair(void)
{
  merge_reset(KC_A);
  other_keycode = KC_B;
  kill_switch_config[0].enable = 1U;
  kill_switch_config[0].mode = KILL_SWITCH_MODE_LAST_INPUT;
  kill_switch_config[0].keycode[0] = KC_A;
  kill_switch_config[0].keycode[1] = KC_B;
  merge_same_scan(3U);
  assert(merge_reports() == 2U);
  unsigned a = merge_report_index(0), b = merge_report_index(1);
  assert(merge_trace[a].scan_token == 0U && merge_trace[b].scan_token == 0U);
  assert(report_has_key(&merge_trace[a].report, KC_A) && !report_has_key(&merge_trace[a].report, KC_B));
  assert(!report_has_key(&merge_trace[b].report, KC_A) && report_has_key(&merge_trace[b].report, KC_B));
}
static void check_merge_legacy_and_td(void)
{
  merge_reset(KC_A);
  action_exec((keyevent_t){.key = {.row = 0U, .col = 0U}, .time = now_ms, .type = KEY_EVENT, .pressed = true});
  assert(merge_reports() == 1U && merge_trace[merge_report_index(0)].scan_token == 0U);
  merge_reset(KC_A);
  keyrecord_t record = merge_record(KC_A, true);
  record.tap_dance_injected = true;
  process_action(&record, action_for_keycode(KC_A));
  assert(merge_reports() == 1U && merge_trace[merge_report_index(0)].scan_token == 0U);
  merge_reset(KC_A);
  record = merge_record(KC_A, true);
  record.tap.count = 1U;
  process_action(&record, action_for_keycode(KC_A));
  assert(merge_reports() == 1U && merge_trace[merge_report_index(0)].scan_token == 0U);
  merge_reset(KC_A);
  record = merge_record(KC_A, true);
  action_owner_select(0U);
  process_action(&record, action_for_keycode(KC_A));
  action_owner_select(UINT8_MAX);
  assert(merge_reports() == 1U && merge_trace[merge_report_index(0)].scan_token == 0U);
}
static void check_merge_tapping_replay(void)
{
  merge_reset(LT(1, KC_ESC));
  other_keycode = KC_A;
  scan(now_ms, 0U, true);
  assert(merge_reports() == 0U && merge_trace_count != 0U);
  scan(now_ms + 1U, 1U, true);
  assert(merge_reports() == 0U);
  tick(now_ms + 250U);
  assert(merge_reports() > 0U);
  for (unsigned i = 0; i < merge_trace_count; ++i)
    if (merge_trace[i].is_report) assert(merge_trace[i].scan_token == 0U);
  assert(report_has_key(&merge_trace[merge_report_index(merge_reports() - 1U)].report, KC_A));
}
static unsigned merge_driver_calls;
static void merge_reentrant_driver(report_keyboard_t *report)
{
  if (++merge_driver_calls == 1U) host_keyboard_send(report);
}
static void check_merge_single_report_scope(void)
{
  merge_reset(KC_A);
  host_driver_t fixture_driver = {.send_keyboard = merge_reentrant_driver};
  merge_driver_calls = 0U;
  driver = &fixture_driver;
  merge_same_scan(1U);
  driver = NULL;
  assert(merge_reports() == 2U);
  assert(merge_trace[merge_report_index(0)].scan_token != 0U);
  assert(merge_trace[merge_report_index(1)].scan_token == 0U);
}
static void check_merge_no_report_barrier(void)
{
  merge_reset(KC_A);
  other_keycode = KC_B;
  merge_same_scan(1U);
  unsigned reports_before = merge_reports();
  unsigned trace_before = merge_trace_count;
  /* A direct release with no held usage produces no snapshot after the filter/cache. */
  keyrecord_t record = merge_record(KC_B, false);
  process_action(&record, action_for_keycode(KC_B));
  action_exec_physical((keyevent_t){.key = {.row = 0U, .col = 1U}, .time = now_ms, .type = KEY_EVENT, .pressed = false}, 999U);
  assert(merge_reports() == reports_before && merge_trace_count > trace_before);
  assert(!merge_trace[merge_trace_count - 1U].is_report && merge_trace[merge_trace_count - 1U].scan_token == 999U);
}

int main(int argc, char **argv)
{
  if (argc == 2) {
    if (strcmp(argv[1], "duplicate") == 0) check_merge_duplicate_usage();
    else if (strcmp(argv[1], "consumed") == 0) check_merge_consumed_barrier(true);
    else if (strcmp(argv[1], "legacy") == 0) check_merge_legacy_and_td();
    else if (strcmp(argv[1], "scope") == 0) check_merge_single_report_scope();
    else assert(!"unknown merge frontend case");
    return 0;
  }
  check_merge_physical_scope();
  check_merge_scan_identity();
  check_merge_duplicate_usage();
  check_merge_layer_barrier();
  check_merge_consumed_barrier(true);
  check_merge_consumed_barrier(false);
  check_merge_post_callback_barrier();
  check_merge_modifier_guards();
  check_merge_lock_guards();
  check_merge_socd_pair();
  check_merge_legacy_and_td();
  check_merge_tapping_replay();
  check_merge_single_report_scope();
  check_merge_no_report_barrier();
  puts("PASS: actual matrix/action/host/SOCD: immediate physical submissions, scan identity, no-report barriers, duplicate/lock/modifier/TD/replay guards and single-report provenance scope");
  return 0;
}
