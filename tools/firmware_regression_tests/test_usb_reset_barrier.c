/* Production reset scheduling/service, USBD Stop/DeInit and the actual HID
 * queues/pool run together. EEPROM durability, endpoint ownership and MCU reset
 * use host adapters; this is not a controller/electrical/timing model. */
#define main usb_transport_fixture_main
#include "test_usb_transport.c"
#undef main

#define USB_NON_MODE 0U
static struct { bool pending; uint32_t ready_ms; } usb_reset_request;
static bool is_init, eeprom_pending;
static uint32_t is_usb_mode;
static unsigned reset_stops, reset_deinits, reset_count;
static void *retained_handle;
static const uint8_t *retained_payload;
static uint8_t retained_image[HID_VIA_EP_SIZE];
static uint32_t retained_length;
static bool check_retained_on_reset;

static bool eeprom_is_pending(void) { return eeprom_pending; }
static void resetToReset(void)
{
  assert(!eeprom_pending && !usbHidViaResponsesPending());
  if (check_retained_on_reset) {
    assert(USBD_Device.pClassData == retained_handle);
    assert(USBD_Device.pClassDataCmsit[0] == retained_handle);
    assert(USBD_static_malloc(1U) == NULL);
    if (retained_payload) assert(!memcmp(retained_payload, retained_image, retained_length));
  }
  reset_count++;
}

USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *d)
{
  assert(d == &USBD_Device);
  reset_stops++;
  return USBD_FAIL; /* An explicit MCU reset may still finish after teardown errors. */
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *d)
{
  assert(d == &USBD_Device);
  reset_deinits++;
  return USBD_FAIL;
}

#include "usb_teardown_core.inc"
#include "usb_reset_barrier_service.inc"

static void begin_case(void)
{
  usb_reset_request.pending = false;
  is_init = true;
  is_usb_mode = 1U;
  eeprom_pending = false;
  reset_stops = reset_deinits = reset_count = 0U;
  check_retained_on_reset = false;
  retained_handle = NULL;
  retained_payload = NULL;
  retained_length = 0U;
  test_usb_reset_pending = false;
  USBD_Device.pClass[0] = &USBD_HID;
  configure(true);
}

static void expect_no_reset(void)
{
  assert(reset_stops == 0U && reset_deinits == 0U && reset_count == 0U);
  assert(is_init && is_usb_mode == 1U);
}

static void expect_reset_once(void)
{
  assert(!usb_reset_request.pending && !is_init && is_usb_mode == USB_NON_MODE);
  assert(reset_stops == 2U && reset_deinits == 1U && reset_count == 1U);
  usbProcessDeferredReset();
  assert(reset_stops == 2U && reset_deinits == 1U && reset_count == 1U);
}

static uint32_t enqueue_pair(bool queued_only)
{
  uint8_t reply[HID_VIA_EP_SIZE];
  uint32_t generation = 0U;
  if (queued_only) arm_fail[HID_VIA_EP_IN & 15U] = 2U;
  for (uint8_t value = 0x6BU; value <= 0x6CU; value++) {
    via_packet(value, HID_VIA_EP_SIZE);
    assert(usbHidReadViaRequest(reply, &generation));
    assert(usbHidEnqueueViaResponse(reply, sizeof(reply), generation));
  }
  assert((active[HID_VIA_EP_IN & 15U] == NULL) == queued_only);
  assert(usbHidViaResponsesPending());
  return generation;
}

static void test_live_barrier(bool suspended)
{
  begin_case();
  enqueue_pair(false);
  clock_ms = UINT32_MAX - 3U;
  assert(usbScheduleGraceReset(8U));
  assert(usb_reset_request.ready_ms == 4U);
  usbProcessDeferredReset();
  expect_no_reset();
  clock_ms = 4U;
  eeprom_pending = true;
  if (suspended) {
    begin_sleep();
    assert(usbHidHostSleeping() && usbHidViaResponsesPending());
    usbProcessDeferredReset();
    expect_no_reset();
    test_otg.device.DSTS = 0U;
    bridge_resume_callback();
  }
  complete(HID_VIA_EP_IN & 15U);
  assert(active[HID_VIA_EP_IN & 15U] && active[HID_VIA_EP_IN & 15U][0] == 0x6CU);
  assert(usbHidViaResponsesPending());
  eeprom_pending = false;
  usbProcessDeferredReset();
  expect_no_reset();
  complete(HID_VIA_EP_IN & 15U);
  assert(!usbHidViaResponsesPending());
  eeprom_pending = true;
  usbProcessDeferredReset();
  expect_no_reset();
  eeprom_pending = false;
  usbProcessDeferredReset();
  expect_reset_once();
  stop();
  puts("PASS: live same-generation replies preserve FIFO drain, wrap-safe grace and EEPROM durability before explicit reset");
}

static void test_retired_barrier(uint8_t failed_ep, bool queued_only, bool requested)
{
  begin_case();
  uint8_t keyboard[HID_KEYBOARD_REPORT_SIZE] = {0}, extra[6] = {2U, 1U, 17U};
  keyboard[2] = 4U;
  assert(usbHidSendReport(keyboard, sizeof(keyboard)));
  assert(usbHidSendReportEXK(extra, sizeof(extra)));
  uint32_t generation = enqueue_pair(queued_only);
  via_packet(0x79U, HID_VIA_EP_SIZE); /* Retired queued commands cannot dispatch. */
  retained_handle = USBD_Device.pClassData;
  retained_payload = failed_ep & 0x80U ? active[failed_ep & 15U] : NULL;
  retained_length = retained_payload ? active_length[failed_ep & 15U] : 0U;
  if (retained_payload) memcpy(retained_image, retained_payload, retained_length);
  uint8_t *old_receive = rx_buffer;
  if (requested) assert(usbScheduleGraceReset(0U));
  eeprom_pending = requested;
  close_status[failed_ep] = USBD_FAIL;
  assert(USBD_HID.DeInit(&USBD_Device, 0U) == USBD_FAIL);
  assert(USBD_Device.pClassData == retained_handle && USBD_static_malloc(1U) == NULL);
  if (retained_payload) {
    assert(active[failed_ep & 15U] == retained_payload);
    assert(!memcmp(retained_payload, retained_image, retained_length));
  }
  if (failed_ep == HID_VIA_EP_OUT) assert(rx_buffer == old_receive && rx_buffer != NULL);
  assert(!usbHidViaResponsesPending());
  assert(!usbHidSendReport(keyboard, sizeof(keyboard)));
  assert(!usbHidSendReportEXK(extra, sizeof(extra)));
  uint8_t reply[HID_VIA_EP_SIZE] = {0};
  assert(!usbHidReadViaRequest(reply, &generation));
  assert(!usbHidEnqueueViaResponse(reply, sizeof(reply), generation));
  assert(USBD_SetClassConfig(&USBD_Device, 1U) == USBD_FAIL);
  begin_sleep();
  assert(!usbHidHostSleeping() && !usbHidRequestRemoteWakeFromInput());
  for (unsigned i = 0U; i < 1000U; i++) {
    sof();
    assert(USBD_HID.DataIn(&USBD_Device, HID_VIA_EP_IN & 15U) == USBD_OK);
    usbProcessDeferredReset();
    clock_ms++;
  }
  expect_no_reset();
  assert(usb_reset_request.pending == requested);
  if (retained_payload) {
    complete(failed_ep & 15U); /* A late completion cannot restart a retired queue. */
    assert(active[failed_ep & 15U] == NULL);
    assert(!memcmp(retained_payload, retained_image, retained_length));
  }
  if (failed_ep == HID_VIA_EP_OUT) {
    memset(old_receive, 0xA5U, HID_VIA_EP_SIZE);
    rx_buffer = NULL;
    rx_length[HID_VIA_EP_OUT] = HID_VIA_EP_SIZE;
    assert(USBD_HID.DataOut(&USBD_Device, HID_VIA_EP_OUT) == USBD_FAIL);
    assert(!usbHidReadViaRequest(reply, &generation) && rx_buffer == NULL);
  }
  eeprom_pending = false;
  check_retained_on_reset = requested;
  usbProcessDeferredReset();
  printf("OBSERVE retired ep=0x%02X queued=%u requested=%u via_pending=%u reset_pending=%u stops=%u deinits=%u resets=%u\n",
         failed_ep, (unsigned)queued_only, (unsigned)requested, (unsigned)usbHidViaResponsesPending(),
         (unsigned)usb_reset_request.pending, reset_stops, reset_deinits, reset_count);
  if (requested) expect_reset_once();
  else expect_no_reset();
  assert(!usbHidViaResponsesPending());
  close_status[failed_ep] = USBD_OK;
  arm_fail[HID_VIA_EP_IN & 15U] = 0U;
  stop(); /* This later successful teardown is the only payload/pool reuse. */
  check_retained_on_reset = false;
  puts("PASS: failed teardown retains payload/handle, retires old VIA drain, rejects late admission and preserves EEPROM/user-reset conditions");
}

static void test_reset_admission(void)
{
  begin_case();
  uint32_t generation = enqueue_pair(false);
  uint8_t reply[HID_VIA_EP_SIZE] = {0}, keyboard[HID_KEYBOARD_REPORT_SIZE] = {0};
  uint8_t extra[6] = {2U, 1U, 0U};
  const uint8_t *payload = active[HID_VIA_EP_IN & 15U];
  uint8_t image[HID_VIA_EP_SIZE];
  memcpy(image, payload, sizeof(image));
  void *handle = USBD_Device.pClassData;
  uint8_t *old_receive = rx_buffer;
  uint32_t saved_opens[256], saved_closes[256], saved_receive_arms = receive_arms;
  memcpy(saved_opens, open_calls, sizeof(saved_opens));
  memcpy(saved_closes, close_calls, sizeof(saved_closes));
  USBD_SetupReqTypedef led = {.bmRequest = 0x21U, .bRequest = USBD_HID_REQ_SET_REPORT,
                            .wValue = 0x0200U, .wIndex = 0U, .wLength = 1U};
  assert(USBD_HID.Setup(&USBD_Device, &led) == USBD_OK);
  unsigned saved_led_updates = led_updates;
  control_buffer[0] = 7U;
  rx_length[0] = 1U;
  /* Isolate the bridge pending-query contract; this stub does not observe raw IRQ flags or prove IRQ timing. */
  test_usb_reset_pending = true;
  assert(!usbHidSendReport(keyboard, sizeof(keyboard)));
  assert(!usbHidSendReportEXK(extra, sizeof(extra)));
  assert(!usbHidEnqueueViaResponse(reply, sizeof(reply), generation));
  assert(!usbHidReadViaRequest(reply, &generation));
  assert(!usbHidViaResponsesPending());
  sof();
  assert(USBD_HID.DataIn(&USBD_Device, HID_VIA_EP_IN & 15U) == USBD_OK);
  assert(USBD_HID.Setup(&USBD_Device, &led) == USBD_FAIL);
  assert(USBD_HID.EP0_RxReady(&USBD_Device) == USBD_OK && led_updates == saved_led_updates);
  usbHidOnBusResetBegin();
  usbHidOnBusResetBegin(); /* Duplicate observation cannot reopen or release old ownership. */
  assert(USBD_Device.pClassData == handle && USBD_Device.pClassDataCmsit[0] == handle);
  assert(active[HID_VIA_EP_IN & 15U] == payload && !memcmp(payload, image, sizeof(image)));
  assert(rx_buffer == old_receive && !memcmp(saved_opens, open_calls, sizeof(saved_opens)));
  assert(!memcmp(saved_closes, close_calls, sizeof(saved_closes)) && receive_arms == saved_receive_arms);
  begin_sleep();
  assert(!usbHidHostSleeping() && !usbHidRequestRemoteWakeFromInput());
  test_usb_reset_pending = false;
  assert(USBD_SetClassConfig(&USBD_Device, 1U) == USBD_FAIL);
  assert(!usbHidEnqueueViaResponse(reply, sizeof(reply), generation));
  stop();
  configure(true);
  assert(!usbHidEnqueueViaResponse(reply, sizeof(reply), generation));
  via_packet(0x83U, HID_VIA_EP_SIZE);
  uint32_t fresh_generation;
  assert(usbHidReadViaRequest(reply, &fresh_generation) && fresh_generation != generation);
  assert(usbHidEnqueueViaResponse(reply, sizeof(reply), fresh_generation));
  drain();
  stop();
  expect_no_reset();
  puts("PASS: raw-reset retirement keeps controller storage, closes main/control/completion admission and accepts only the later configuration generation");
}

static void reset_during_wake(uint32_t ms)
{
  assert(ms == 5U || ms == 10U);
  test_usb_reset_pending = true;
  usbHidOnBusResetBegin();
  if (ms == 10U) {
    /* Model only reset's electrical signal termination; do not replace GINTMSK
     * with an expected result. The production wake owner must restore WUIM. */
    test_otg.device.DCTL = 0U;
    test_otg.device.DSTS = 0U;
    test_otg.global.GINTSTS = 0U;
    wake_asserted = false;
    delay_signal_reset = true;
  }
}

static void test_reset_during_wake(bool signaled)
{
  begin_case();
  enqueue_pair(false);
  const uint8_t *payload = active[HID_VIA_EP_IN & 15U];
  uint8_t image[HID_VIA_EP_SIZE];
  memcpy(image, payload, sizeof(image));
  begin_sleep();
  if (signaled) clock_ms += 5U;
  unsigned starts = wake_start_count, ends = wake_end_count;
  uint32_t previous_mask = test_otg.global.GINTMSK;
  delay_event = reset_during_wake;
  assert(usbHidRequestRemoteWakeFromInput() == signaled);
  assert(wake_start_count == starts + (signaled ? 1U : 0U) && wake_end_count == ends);
  assert(test_otg.global.GINTMSK == previous_mask);
  assert(!usbHidConsumeWakeSof() && !usbHidHostSleeping());
  assert(active[HID_VIA_EP_IN & 15U] == payload && !memcmp(payload, image, sizeof(image)));
  assert(!usbHidRequestRemoteWakeFromInput());
  test_usb_reset_pending = false;
  stop();
  expect_no_reset();
  puts("PASS: raw reset cancels pre-signal/old-epoch wake authority without clearing payloads or inventing a user reset; WUIM restoration stays intact");
}

int main(int argc, char **argv)
{
  assert(argc <= 2);
  const char *selected = argc > 1 ? argv[1] : "all";
  bool all = !strcmp(selected, "all");
  const char *cases[] = {"all", "live", "suspend", "retired-keyboard", "retired-via",
                        "retired-out", "retired-queued", "no-request", "admission", "wake"};
  bool known = false;
  for (unsigned i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) known |= !strcmp(selected, cases[i]);
  assert(known);
  if (all || !strcmp(selected, "live")) test_live_barrier(false);
  if (all || !strcmp(selected, "suspend")) test_live_barrier(true);
  if (all || !strcmp(selected, "retired-keyboard")) test_retired_barrier(HID_EPIN_ADDR, false, true);
  if (all || !strcmp(selected, "retired-via")) test_retired_barrier(HID_VIA_EP_IN, false, true);
  if (all || !strcmp(selected, "retired-out")) test_retired_barrier(HID_VIA_EP_OUT, false, true);
  if (all || !strcmp(selected, "retired-queued")) test_retired_barrier(HID_EPIN_ADDR, true, true);
  if (all || !strcmp(selected, "no-request")) test_retired_barrier(HID_VIA_EP_IN, false, false);
  if (all || !strcmp(selected, "admission")) test_reset_admission();
  if (all || !strcmp(selected, "wake")) {
    test_reset_during_wake(false);
    test_reset_during_wake(true);
  }
  puts("PASS: production HID and user-reset service agree on active versus retired responses");
  return 0;
}
