// V260911R3: 실제 HID class의 유지 시간, FIFO 순서, 완료/포화/세션 상태를 검증한다.
static void interval_fixture(uint32_t time)
{
  stop();
  clock_ms = time;
  clock_us_fraction = 0U;
  configure(true);
  uint8_t neutral[HID_KEYBOARD_REPORT_SIZE] = {0};
  assert(usbHidSendReport(neutral, sizeof(neutral)));
  drain();
  delivered_count = 0U;
}

static bool interval_key(uint8_t key)
{
  uint8_t report[HID_KEYBOARD_REPORT_SIZE] = {0};
  report[2] = key;
  return usbHidSendReport(report, sizeof(report));
}

static void interval_expire(uint32_t completed_at, uint16_t ms)
{
  clock_ms = completed_at + ms - 1U;
  sof();
  assert(active[1] == NULL);
  clock_ms++;
  sof();
  assert(active[1] != NULL);
}

static void test_keyboard_intervals(void)
{
  // 80/200ms, FS/HS, report/boot: 전송 전에 소비한 시간은 유지 시간으로 세지 않는다.
  for (unsigned mode = 0; mode < 8U; mode++)
  {
    interval_fixture(1000U);
    uint16_t hold = (mode & 4U) ? 200U : 80U;
    USBD_Device.dev_speed = (mode & 1U) ? USBD_SPEED_HIGH : USBD_SPEED_FULL;
    ((USBD_HID_HandleTypeDef *)USBD_Device.pClassData)->Protocol = (mode & 2U) ? 0U : 1U;
    assert(interval_key(0x39U));
    const uint8_t *press = active[1];
    usbHidDelayKeyboardReport(hold);
    assert(interval_key(0U));
    assert(interval_key(4U));
    assert(interval_key(0U));
    assert(clock_ms == 1000U && active[1] == press && press[2] == 0x39U);
    clock_ms = 1500U;
    sof();
    complete(1U);
    assert(active[1] == NULL);

    uint8_t mouse[6] = {2U, 1U, 3U, 0U, 0U, 0U};
    assert(usbHidSendReportEXK(mouse, sizeof(mouse)));
    complete(5U);
    via_packet(0xA5U, 32U);
    uint8_t request[32]; uint32_t generation;
    assert(usbHidReadViaRequest(request, &generation));
    assert(usbHidEnqueueViaResponse(request, sizeof(request), generation));
    complete(4U);
    assert(active[1] == NULL && clock_ms == 1500U); // 다른 IN은 Caps 간격에 종속되지 않는다.

    interval_expire(1500U, hold);
    assert(active[1][2] == 0U);
    complete(1U);
    assert(active[1] && active[1][2] == 4U);
    complete(1U); complete(1U);
    assert(delivered_count == 6U && delivered[3].time_ms - delivered[0].time_ms >= hold);
  }

  // 빠른 연속 tap: 각 완료 시점에 독립된 간격을 적용하고 키 순서를 바꾸지 않는다.
  interval_fixture(3000U);
  for (unsigned i = 0; i < 16U; i++)
  {
    assert(interval_key(0x39U)); usbHidDelayKeyboardReport(80U);
    assert(interval_key(0U)); assert(interval_key(4U)); assert(interval_key(0U));
  }
  assert(clock_ms == 3000U);
  for (unsigned i = 0; i < 16U; i++)
  {
    assert(active[1] && active[1][2] == 0x39U);
    complete(1U);
    interval_expire(clock_ms, 80U);
    complete(1U); complete(1U); complete(1U);
  }
  assert(delivered_count == 64U && active[1] == NULL);
  for (unsigned i = 0; i < 64U; i++)
  {
    const uint8_t sequence[] = {0x39U, 0U, 4U, 0U};
    assert(delivered[i].data[2] == sequence[i % 4U]);
    if (i % 4U == 1U) assert(delivered[i].time_ms - delivered[i - 1U].time_ms >= 80U);
  }

  // 이미 완료된 press의 경과 시간은 공제한다. 불필요하게 80ms를 더 기다리지 않는다.
  interval_fixture(5000U);
  assert(interval_key(0x39U)); complete(1U);
  clock_ms += 60U;
  usbHidDelayKeyboardReport(80U);
  assert(interval_key(0U));
  interval_expire(5000U, 80U); complete(1U);
  assert(interval_key(0x39U)); complete(1U);
  clock_ms += 100U;
  usbHidDelayKeyboardReport(80U);
  assert(interval_key(0U));
  assert(active[1]); complete(1U);

  // 논리 tap은 이미 해제됐다. 이후 동일 usage 물리 press의 수명은 타이머가 건드리지 않는다.
  interval_fixture(6000U);
  assert(interval_key(0x39U)); usbHidDelayKeyboardReport(80U); assert(interval_key(0U));
  assert(interval_key(0x39U));
  uint8_t caps_and_a[HID_KEYBOARD_REPORT_SIZE] = {0};
  caps_and_a[2] = 0x39U; caps_and_a[3] = 4U;
  assert(usbHidSendReport(caps_and_a, sizeof(caps_and_a)));
  complete(1U); interval_expire(6000U, 80U);
  complete(1U); complete(1U); complete(1U);
  clock_ms += 300U; sof();
  assert(delivered_count == 4U && active[1] == NULL);
  assert(delivered[3].data[2] == 0x39U && delivered[3].data[3] == 4U);
  assert(interval_key(4U)); complete(1U);
  assert(interval_key(0U)); complete(1U);

  // press/release arm 실패는 스냅샷이나 간격을 잃거나, 만료된 간격을 재시작하지 않는다.
  interval_fixture(7000U);
  arm_fail[1] = 3U;
  assert(interval_key(0x39U)); usbHidDelayKeyboardReport(80U); assert(interval_key(0U));
  clock_ms += 300U;
  sof(); assert(active[1]); complete(1U);
  clock_ms += 79U; sof(); assert(active[1] == NULL);
  clock_ms++; arm_fail[1] = 1U; sof(); assert(active[1] == NULL);
  sof(); assert(active[1] && active[1][2] == 0U); complete(1U);

  // 포화 시 수락한 prefix의 마지막 유지 시간을 보존하고 최신 release로 수렴한다.
  interval_fixture(8000U);
  for (unsigned i = 0; i < 128U; i++) assert(interval_key(4U));
  assert(interval_key(0x39U)); usbHidDelayKeyboardReport(80U);
  assert(!interval_key(0U)); assert(!interval_key(0U));
  for (unsigned i = 0; i < 129U; i++) complete(1U);
  assert(delivered[128].data[2] == 0x39U && active[1] == NULL);
  interval_expire(8000U, 80U); complete(1U);
  assert(delivered_count == 130U && delivered[129].data[2] == 0U);

  // Suspend 중 생산한 tap은 Resume 후 실제 press 완료부터 시간을 센다.
  interval_fixture(9000U);
  begin_sleep();
  assert(interval_key(0x39U)); usbHidDelayKeyboardReport(80U); assert(interval_key(0U));
  clock_ms += 1000U; sof(); assert(active[1] == NULL);
  USBD_Device.dev_state = USBD_STATE_CONFIGURED;
  usbHidOnResume(); sof(); complete(1U);
  interval_expire(10000U, 80U); complete(1U);
  assert(delivered_count == 2U && delivered[1].time_ms - delivered[0].time_ms == 80U);

  // 새 세대는 이전 시간 예약과 backlog를 폐기하고 최신 상태만 복구한다.
  interval_fixture(11000U);
  assert(interval_key(0x39U)); usbHidDelayKeyboardReport(80U); assert(interval_key(0U));
  complete(1U); assert(active[1] == NULL);
  stop(); configure(true);
  assert(interval_key(4U)); assert(active[1]); complete(1U);
  assert(interval_key(0U)); complete(1U);

  // ms 경계 직전의 완료를 정수 ms로 잘라 유지 시간이 짧아지지 않아야 한다.
  interval_fixture(12000U);
  clock_us_fraction = 999U;
  assert(interval_key(0x39U)); usbHidDelayKeyboardReport(80U); assert(interval_key(0U));
  complete(1U);
  clock_ms += 80U; clock_us_fraction = 998U;
  sof(); assert(active[1] == NULL);
  clock_us_fraction = 999U;
  sof(); assert(active[1]); complete(1U);

  interval_fixture(UINT32_MAX - 39U);
  assert(interval_key(0x39U)); usbHidDelayKeyboardReport(80U); assert(interval_key(0U));
  complete(1U); interval_expire(UINT32_MAX - 39U, 80U); complete(1U);
  assert(delivered[1].time_ms - delivered[0].time_ms == 80U);
  puts("PASS: keyboard report intervals: 80/200ms completion origin, FIFO, independent EXK/VIA, repeats, overlap, failures, overflow, suspend, reset, wrap");
}
