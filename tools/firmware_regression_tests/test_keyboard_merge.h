/* Actual HID class tests: a logical candidate must never postpone first service. */
#include "usb_diagnostics.h"

static void merge_fixture(void)
{
  interval_fixture(1000U);
}

static void merge_seed(uint8_t *report)
{
  assert(usbHidSendReport(report, HID_KEYBOARD_REPORT_SIZE));
  assert(active[1] != NULL);
}

static void test_keyboard_merge_interleavings(void)
{
  merge_fixture();
  uint8_t report[HID_KEYBOARD_REPORT_SIZE] = {0};
  unsigned expected_down[16] = {0}, expected_up[16] = {0};
  uint32_t random = 0xA71953U;
  for (uint32_t scan = 1U; scan <= 160U; scan++) {
    for (unsigned event = 0U; event < 20U; event++) {
      random = random * 1664525U + 1013904223U;
      unsigned slot = (random >> 24) & 15U;
      uint8_t usage = (uint8_t)(slot + 4U);
      bool pressed = report[slot + 2U] == 0U;
      report[slot + 2U] = pressed ? usage : 0U;
      if (pressed) expected_down[slot]++;
      else expected_up[slot]++;
      if ((random & 7U) == 0U) {
        // Interleave an ordinary report/action barrier with the physical path.
        assert(usbHidSendReport(report, sizeof(report)));
      } else {
        assert(usbHidSubmitKeyUpdate(report, sizeof(report), scan, usage, pressed));
      }
      if ((random & 3U) == 1U && active[1] != NULL) complete(1U);
    }
    usbHidEndKeyScan(scan);
    drain();
  }
  unsigned actual_down[16] = {0}, actual_up[16] = {0};
  bool held[16] = {false};
  for (unsigned packet = 0U; packet < delivered_count; packet++) {
    assert(delivered[packet].ep == 1U);
    for (unsigned slot = 0U; slot < 16U; slot++) {
      bool next = delivered[packet].data[slot + 2U] != 0U;
      if (next != held[slot]) {
        if (next) actual_down[slot]++;
        else actual_up[slot]++;
      }
      held[slot] = next;
    }
  }
  // Compare externally delivered per-usage edges with 3200 input transitions,
  // independently of how many intermediate snapshots were combined.
  assert(!memcmp(expected_down, actual_down, sizeof(expected_down)));
  assert(!memcmp(expected_up, actual_up, sizeof(expected_up)));
  assert(active[1] == NULL && !memcmp(delivered[delivered_count - 1U].data, report, sizeof(report)));
}

static void test_keyboard_merge(void)
{
  for (unsigned mode = 0U; mode < 4U; mode++) {
    merge_fixture();
    USBD_Device.dev_speed = mode == 0U ? USBD_SPEED_FULL : USBD_SPEED_HIGH;
    hs_interval = mode == 0U ? 1U : (uint8_t)mode;
    uint8_t report[HID_KEYBOARD_REPORT_SIZE] = {0};

    // One update on an idle EP arms in the submission call, without SOF/end scan.
    report[2] = 4U;
    assert(usbHidSubmitKeyUpdate(report, sizeof(report), 1U, 4U, true));
    assert(active[1] && active[1][2] == 4U && delivered_count == 0U);
    complete(1U);
    assert(active[1] == NULL && delivered_count == 1U);
    usbHidEndKeyScan(1U);
    assert(active[1] == NULL);

    merge_fixture();
    memset(report, 0, sizeof(report));
    merge_seed(report);
    const uint8_t *immutable = active[1];
    report[2] = 4U;
    assert(usbHidSubmitKeyUpdate(report, sizeof(report), 2U, 4U, true));
    assert(active[1] == immutable && active[1][2] == 0U);
    // Completion in the middle of the scan must arm even this single candidate.
    complete(1U);
    assert(active[1] && active[1][2] == 4U);
    report[3] = 5U;
    assert(usbHidSubmitKeyUpdate(report, sizeof(report), 2U, 5U, true));
    assert(active[1][3] == 0U);
    complete(1U);
    assert(active[1] && !memcmp(active[1], report, sizeof(report)));
    complete(1U);
    usbHidEndKeyScan(2U);
    assert(delivered_count == 3U && active[1] == NULL);

    // Twenty same-scan presses: first arm is immediate; the busy suffix shares
    // one candidate, preserving every held usage and never editing the active one.
    merge_fixture();
    memset(report, 0, sizeof(report));
    for (unsigned i = 0U; i < HID_KEYBOARD_REPORT_SIZE - 2U; i++) {
      report[i + 2U] = (uint8_t)(i + 4U);
      assert(usbHidSubmitKeyUpdate(report, sizeof(report), 3U, (uint8_t)(i + 4U), true));
      assert(active[1] && active[1][2] == 4U && active[1][3] == 0U);
    }
    usbHidEndKeyScan(3U);
    complete(1U);
    assert(active[1] && !memcmp(active[1], report, sizeof(report)));
    complete(1U);
    assert(delivered_count == 2U && active[1] == NULL);
  }

  uint8_t report[HID_KEYBOARD_REPORT_SIZE] = {0};
  // A short down/up is never merged, even under the same scan token.
  merge_fixture();
  merge_seed(report);
  report[2] = 4U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 4U, 4U, true));
  report[2] = 0U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 4U, 4U, false));
  usbHidEndKeyScan(4U);
  drain();
  assert(delivered_count == 3U && delivered[1].data[2] == 4U && delivered[2].data[2] == 0U);

  // New scan, explicit barrier, and ordinary/synthetic submission freeze the
  // candidate. None may rewrite the immutable pending tail.
  for (unsigned barrier = 0U; barrier < 4U; barrier++) {
    merge_fixture();
    memset(report, 0, sizeof(report));
    merge_seed(report);
    report[2] = 4U;
    assert(usbHidSubmitKeyUpdate(report, sizeof(report), 5U, 4U, true));
    if (barrier == 0U) usbHidEndKeyScan(5U);
    if (barrier == 1U) usbHidEndKeyScan(0U);
    report[3] = 5U;
    if (barrier == 2U) assert(usbHidSendReport(report, sizeof(report)));
    else assert(usbHidSubmitKeyUpdate(report, sizeof(report), barrier == 3U ? 6U : 5U, 5U, true));
    drain();
    assert(delivered_count == 3U && delivered[1].data[3] == 0U && delivered[2].data[3] == 5U);
  }

  // Filtered/malformed delta, modifiers, duplicate usages and reordered slots
  // must use the ordinary path rather than trust the producer's annotation.
  for (unsigned invalid = 0U; invalid < 4U; invalid++) {
    merge_fixture();
    memset(report, 0, sizeof(report));
    merge_seed(report);
    report[2] = 4U;
    assert(usbHidSubmitKeyUpdate(report, sizeof(report), 7U, 4U, true));
    report[3] = 5U;
    if (invalid == 0U) report[0] = 2U;
    if (invalid == 1U) report[4] = 6U;
    if (invalid == 2U) report[3] = 4U;
    if (invalid == 3U) { report[2] = 5U; report[3] = 4U; }
    assert(usbHidSubmitKeyUpdate(report, sizeof(report), 7U, 5U, true));
    drain();
    assert(delivered_count == 3U && delivered[1].data[3] == 0U);
    assert(!memcmp(delivered[2].data, report, sizeof(report)));
  }

  // Failed arm retains the frozen candidate. A later update cannot overtake it.
  merge_fixture();
  memset(report, 0, sizeof(report));
  merge_seed(report);
  report[2] = 4U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 8U, 4U, true));
  arm_fail[1] = 2U;
  complete(1U);
  assert(active[1] == NULL);
  report[3] = 5U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 8U, 5U, true));
  assert(active[1] && active[1][2] == 4U && active[1][3] == 0U);
  drain();
  assert(delivered_count == 3U && delivered[1].data[3] == 0U && delivered[2].data[3] == 5U);

  // Delay belongs to the latest logical candidate after it becomes immutable.
  merge_fixture();
  memset(report, 0, sizeof(report));
  merge_seed(report);
  report[2] = 4U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 9U, 4U, true));
  usbHidDelayKeyboardReport(80U);
  report[2] = 0U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 9U, 4U, false));
  complete(1U); complete(1U);
  assert(active[1] == NULL);
  clock_ms += 79U; sof(); assert(active[1] == NULL);
  clock_ms++; sof(); assert(active[1] && active[1][2] == 0U);
  complete(1U);

  // Boot's six-slot projection makes merging releases unsafe: key G must first
  // become visible after A is released before G itself is released.
  merge_fixture();
  for (unsigned i = 0U; i < 7U; i++) report[i + 2U] = (uint8_t)(4U + i);
  ((USBD_HID_HandleTypeDef *)USBD_Device.pClassData)->Protocol = 0U;
  merge_seed(report);
  report[2] = 0U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 10U, 4U, false));
  report[8] = 0U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 10U, 10U, false));
  drain();
  assert(delivered_count == 3U && delivered[0].length == 8U);
  assert(delivered[1].data[7] == 10U && delivered[2].data[7] == 0U);

  merge_fixture();
  memset(report, 0, sizeof(report));
  merge_seed(report);
  report[2] = 4U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 10U, 4U, true));
  USBD_SetupReqTypedef protocol = {
    .bmRequest = 0x21U, .bRequest = USBD_HID_REQ_SET_PROTOCOL,
    .wValue = 0U, .wIndex = 0U, .wLength = 0U,
  };
  assert(USBD_HID.Setup(&USBD_Device, &protocol) == USBD_OK);
  assert(active_length[1] == HID_KEYBOARD_REPORT_SIZE && active[1][2] == 0U);
  report[3] = 5U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 10U, 5U, true));
  drain();
  assert(delivered_count == 4U && delivered[0].length == HID_KEYBOARD_REPORT_SIZE);
  assert(delivered[1].length == 8U && delivered[1].data[2] == 4U && delivered[1].data[3] == 0U);
  assert(delivered[2].data[3] == 0U && delivered[3].data[3] == 5U);

  // Diagnostics account for the report, with the earliest merged request time;
  // planned combination is not an overflow/drop and cannot hide queue residency.
  merge_fixture();
  memset(report, 0, sizeof(report));
  merge_seed(report);
  usbDiagnosticsInit();
  assert(usbDiagnosticsStart(10U, 0U, 125U, micros()));
  report[2] = 4U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 10U, 4U, true));
  clock_us_fraction = 20U;
  report[3] = 5U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 10U, 5U, true));
  clock_us_fraction = 40U; complete(1U);
  clock_us_fraction = 100U; complete(1U);
  usb_diagnostics_snapshot_t observation;
  usbDiagnosticsCapture(&observation, micros());
  assert(observation.report_samples == 1U && observation.latency_max_us == 100U);
  assert(observation.queue_depth_peak == 1U && observation.session_counters.report_drops == 0U);
  assert(usbDiagnosticsStop(micros()));

  merge_fixture();
  assert(usbDiagnosticsStart(10U, 0U, 125U, micros()));
  memset(report, 0, sizeof(report));
  merge_seed(report);
  report[2] = 4U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 10U, 4U, true));
  assert(usbDiagnosticsStop(micros()));
  assert(usbDiagnosticsStart(10U, 0U, 125U, micros()));
  report[3] = 5U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 10U, 5U, true));
  drain();
  assert(delivered_count == 3U && delivered[1].data[3] == 0U && delivered[2].data[3] == 5U);
  usbDiagnosticsCapture(&observation, micros());
  assert(observation.report_samples == 1U);
  assert(usbDiagnosticsStop(micros()));

  merge_fixture();
  memset(report, 0, sizeof(report));
  merge_seed(report);
  report[2] = 4U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 10U, 4U, true));
  uint8_t mouse[6] = {2U, 1U, 0U, 0U, 0U, 0U};
  assert(usbHidSendReportEXK(mouse, sizeof(mouse)));
  report[3] = 5U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 10U, 5U, true));
  complete(1U);
  assert(active[1] && active[1][2] == 4U && active[1][3] == 0U);
  complete(1U);
  assert(active[1] && active[1][3] == 5U);
  drain();

  // Suspend keeps the candidate/prefix; new sessions discard it and reconcile
  // only the latest state. Raw-reset admission retirement must not emit it.
  merge_fixture();
  memset(report, 0, sizeof(report));
  merge_seed(report);
  report[2] = 4U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 11U, 4U, true));
  USBD_Device.dev_old_state = USBD_STATE_CONFIGURED;
  USBD_Device.dev_state = USBD_STATE_SUSPENDED;
  usbHidOnSuspend();
  complete(1U); assert(active[1] == NULL);
  report[2] = 0U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 11U, 4U, false));
  USBD_Device.dev_state = USBD_STATE_CONFIGURED;
  drain();
  assert(delivered_count == 3U && delivered[1].data[2] == 4U && delivered[2].data[2] == 0U);

  merge_fixture();
  merge_seed(report);
  report[2] = 4U;
  assert(usbHidSubmitKeyUpdate(report, sizeof(report), 12U, 4U, true));
  usbHidOnBusResetBegin();
  complete(1U); assert(active[1] == NULL);
  report[2] = 0U;
  assert(!usbHidSubmitKeyUpdate(report, sizeof(report), 12U, 4U, false));
  stop(); configure(true);
  for (unsigned i = 1U; i < delivered_count; i++)
    if (delivered[i].ep == 1U) assert(delivered[i].data[2] == 0U);

  test_keyboard_merge_interleavings();
  merge_fixture();
  puts("PASS: actual HID nonwaiting merge: single/20-key, mid-scan completion, immutable active/head, tap/barriers/filter/Boot, failure/delay/suspend/reset");
}
