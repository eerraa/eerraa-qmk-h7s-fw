/* The controller boundary may retain an endpoint after CloseEP fails. Compile
 * the production HID and core teardown against that boundary; no USB hardware
 * or electrical/register/IRQ timing is modeled here. */
#define main usb_transport_fixture_main
#include "test_usb_transport.c"
#undef main

static USBD_StatusTypeDef ll_stop_result, ll_deinit_result;
static uint8_t class_deinit_result;
static uint8_t teardown_order[3];
static unsigned teardown_order_count;

USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *d)
{
  assert(d == &USBD_Device && teardown_order_count < 3U);
  teardown_order[teardown_order_count++] = 1U;
  return ll_stop_result;
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *d)
{
  assert(d == &USBD_Device && teardown_order_count < 3U);
  teardown_order[teardown_order_count++] = 3U;
  return ll_deinit_result;
}

#include "usb_teardown_core.inc"

static uint8_t status_class_deinit(USBD_HandleTypeDef *d, uint8_t config)
{
  (void)config;
  assert(d == &USBD_Device && teardown_order_count < 3U);
  teardown_order[teardown_order_count++] = 2U;
  return class_deinit_result;
}

static void check_blocked_admission(void *handle, uint32_t generation)
{
  assert(strcmp(usbHidGetPollingLabel(), "Unavailable") == 0);
  uint8_t keyboard[HID_KEYBOARD_REPORT_SIZE] = {0}, extra[6] = {2U, 1U, 1U};
  uint8_t via[HID_VIA_EP_SIZE] = {0};
  keyboard[2] = 5U;
  assert(!usbHidSendReport(keyboard, sizeof(keyboard)));
  assert(!usbHidSendReportEXK(extra, sizeof(extra)));
  assert(!usbHidReadViaRequest(via, &generation));
  assert(!usbHidEnqueueViaResponse(via, sizeof(via), generation));
  USBD_SetupReqTypedef req = {.bmRequest = 0xA1U, .bRequest = USBD_HID_REQ_GET_PROTOCOL,
                            .wIndex = 0U, .wLength = 1U};
  assert(USBD_HID.Setup(&USBD_Device, &req) == USBD_FAIL);
  uint32_t saved_opens[256], saved_closes[256], saved_receive_arms = receive_arms;
  memcpy(saved_opens, open_calls, sizeof(saved_opens));
  memcpy(saved_closes, close_calls, sizeof(saved_closes));
  sof();
  assert(USBD_SetClassConfig(&USBD_Device, 1U) == USBD_FAIL);
  assert(!memcmp(saved_opens, open_calls, sizeof(saved_opens)));
  assert(!memcmp(saved_closes, close_calls, sizeof(saved_closes)));
  assert(receive_arms == saved_receive_arms);
  assert(USBD_Device.pClassData == handle && USBD_Device.pClassDataCmsit[0] == handle);
  assert(USBD_static_malloc(1U) == NULL); /* The failed class keeps its bounded pool slot. */
}

static void test_failed_close(uint8_t failed_ep, bool boot, bool already_stopped)
{
  USBD_Device.pClass[0] = &USBD_HID;
  configure(true);
  if (boot) {
    USBD_SetupReqTypedef req = {.bmRequest = 0x21U, .bRequest = USBD_HID_REQ_SET_PROTOCOL,
                              .wValue = 0U, .wIndex = 0U};
    assert(USBD_HID.Setup(&USBD_Device, &req) == USBD_OK);
    drain();
  }
  uint8_t keyboard[HID_KEYBOARD_REPORT_SIZE] = {0}, extra[6] = {2U, 1U, 17U};
  uint8_t via[HID_VIA_EP_SIZE];
  uint32_t generation;
  keyboard[0] = 2U; keyboard[2] = 4U;
  assert(usbHidSendReport(keyboard, sizeof(keyboard)));
  assert(usbHidSendReportEXK(extra, sizeof(extra)));
  via_packet(0x6BU, HID_VIA_EP_SIZE);
  assert(usbHidReadViaRequest(via, &generation));
  assert(usbHidEnqueueViaResponse(via, sizeof(via), generation));
  via_packet(0x79U, HID_VIA_EP_SIZE); /* A queued command must not dispatch after failure. */
  void *handle = USBD_Device.pClassData;
  const uint8_t *payload = (failed_ep & 0x80U) ? active[failed_ep & 15U] : NULL;
  uint8_t payload_image[HID_VIA_EP_SIZE];
  uint32_t payload_length = payload ? active_length[failed_ep & 15U] : 0U;
  if (payload) memcpy(payload_image, payload, payload_length);
  uint8_t *old_receive_buffer = rx_buffer;
  usb_hid_transport_stats_t before, after;
  usbHidGetTransportStats(&before);
  close_status[failed_ep] = USBD_FAIL;
  close_stops_on_failure[failed_ep] = already_stopped;
  USBD_StatusTypeDef result = USBD_ClrClassConfig(&USBD_Device, 0U);
  /* Check contents before return status: the old HID reset overwrote active.data
   * while our controller retained its pointer. This is a source-proven failure. */
  if (payload && !already_stopped) {
    assert(active[failed_ep & 15U] == payload);
    assert(!memcmp(payload, payload_image, payload_length));
  }
  assert(result == USBD_FAIL);
  if (failed_ep & 0x80U) assert(USBD_Device.ep_in[failed_ep & 15U].is_used);
  else assert(USBD_Device.ep_out[failed_ep].is_used);
  check_blocked_admission(handle, generation);
  usbHidGetTransportStats(&after);
  assert(after.session_discards == before.session_discards);
  assert(!usbHidViaResponsesPending());
  if (payload && !already_stopped) {
    assert(!memcmp(payload, payload_image, payload_length));
    /* Even a completion from the retained endpoint cannot restart old queues. */
    complete(failed_ep & 15U);
    assert(active[failed_ep & 15U] == NULL);
    assert(USBD_HID.DataIn(&USBD_Device, failed_ep & 15U) == USBD_OK);
    assert(!usbHidViaResponsesPending());
  }
  if (failed_ep == HID_VIA_EP_OUT && !already_stopped) {
    assert(rx_buffer == old_receive_buffer && rx_buffer != NULL);
    memset(old_receive_buffer, 0xA5U, HID_VIA_EP_SIZE); /* A late hardware write stays in static storage. */
    rx_buffer = NULL;
    rx_length[HID_VIA_EP_OUT] = HID_VIA_EP_SIZE;
    assert(USBD_HID.DataOut(&USBD_Device, HID_VIA_EP_OUT) == USBD_FAIL);
    assert(!usbHidReadViaRequest(via, &generation));
    assert(rx_buffer == NULL);
  }
  close_status[failed_ep] = USBD_OK;
  close_stops_on_failure[failed_ep] = false;
  stop(); /* Only this later lifecycle teardown authorizes storage reuse. */
  assert(!usbHidEnqueueViaResponse(via, sizeof(via), generation));
  configure(true);
  assert(!usbHidEnqueueViaResponse(via, sizeof(via), generation));
  stop();
}

static void test_partial_init(bool failed_receive)
{
  USBD_Device.pClass[0] = &USBD_HID;
  pcd.Instance = &test_otg.global;
  USBD_Device.pData = &pcd;
  USBD_Device.dev_state = USBD_STATE_ADDRESSED;
  uint8_t failed_close = failed_receive ? HID_VIA_EP_OUT : HID_EPIN_ADDR;
  close_status[failed_close] = USBD_FAIL;
  open_fail = failed_receive ? 0U : HID_EXK_EP_IN;
  receive_fail = failed_receive;
  assert(USBD_SetClassConfig(&USBD_Device, 1U) == USBD_FAIL);
  void *handle = USBD_Device.pClassData;
  assert(handle != NULL);
  assert(failed_receive ? USBD_Device.ep_out[HID_VIA_EP_OUT].is_used : USBD_Device.ep_in[1].is_used);
  assert(rx_buffer == NULL);
  check_blocked_admission(handle, 0U);
  open_fail = 0U; receive_fail = false; close_status[failed_close] = USBD_OK;
  stop();
  configure(true);
  stop();
}

static void failed_reset_during_signal(uint32_t ms)
{
  assert(ms == 10U);
  USBD_Device.dev_state = USBD_STATE_DEFAULT;
  close_status[HID_EPIN_ADDR] = USBD_FAIL;
  assert(USBD_ClrClassConfig(&USBD_Device, 0U) == USBD_FAIL);
  /* Only model the observed lifecycle resetting the electrical signal. The
   * endpoint still owns its IN payload; no new configuration is admitted. */
  test_otg.device.DCTL = 0U;
  test_otg.device.DSTS = 0U;
  test_otg.global.GINTSTS = 0U;
  test_otg.global.GINTMSK = USB_OTG_GINTMSK_WUIM;
  wake_asserted = false;
  delay_signal_reset = true;
}

static void test_failed_reset_during_wake(void)
{
  USBD_Device.pClass[0] = &USBD_HID;
  configure(true);
  uint8_t keyboard[HID_KEYBOARD_REPORT_SIZE] = {0};
  keyboard[2] = 4U;
  assert(usbHidSendReport(keyboard, sizeof(keyboard)));
  const uint8_t *payload = active[1];
  uint8_t payload_image[HID_KEYBOARD_REPORT_SIZE];
  memcpy(payload_image, payload, sizeof(payload_image));
  begin_sleep();
  clock_ms += 5U;
  unsigned ends = wake_end_count;
  delay_event = failed_reset_during_signal;
  assert(usbHidRequestRemoteWakeFromInput());
  assert(wake_end_count == ends); /* The old attempt cannot deassert a later session's signal. */
  assert(!usbHidConsumeWakeSof());
  assert(active[1] == payload && !memcmp(payload, payload_image, sizeof(payload_image)));
  close_status[HID_EPIN_ADDR] = USBD_OK;
  stop();
}

static void test_core_status(bool deinit, USBD_StatusTypeDef stop_status,
                             uint8_t class_status, USBD_StatusTypeDef deinit_status)
{
  USBD_ClassTypeDef status_class = {.DeInit = status_class_deinit};
  USBD_Device.pClass[0] = &status_class;
  USBD_Device.dev_state = USBD_STATE_CONFIGURED;
  ll_stop_result = stop_status; class_deinit_result = class_status; ll_deinit_result = deinit_status;
  teardown_order_count = 0U;
  USBD_StatusTypeDef result = deinit ? USBD_DeInit(&USBD_Device) : USBD_Stop(&USBD_Device);
  bool expected_success = stop_status == USBD_OK && class_status == USBD_OK &&
                          (!deinit || deinit_status == USBD_OK);
  assert((result == USBD_OK) == expected_success);
  assert(teardown_order_count == (deinit ? 3U : 2U));
  assert(teardown_order[0] == 1U && teardown_order[1] == 2U);
  if (deinit) assert(teardown_order[2] == 3U); /* LL_DeInit still runs after either earlier error. */
  USBD_Device.pClass[0] = &USBD_HID;
}

int main(int argc, char **argv)
{
  assert(argc <= 2);
  const char *selected = argc > 1 ? argv[1] : "all";
  bool all = !strcmp(selected, "all");
  const char *cases[] = {"all", "in", "boot", "out", "quiesced", "partial-init", "wake",
                        "stop-ll", "stop-class", "deinit-stop", "deinit-class", "deinit-ll"};
  bool known = false;
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) known |= !strcmp(selected, cases[i]);
  assert(known);
  if (all || !strcmp(selected, "in")) {
    test_failed_close(HID_EPIN_ADDR, false, false);
    test_failed_close(HID_VIA_EP_IN, false, false);
    test_failed_close(HID_EXK_EP_IN, false, false);
  }
  if (all || !strcmp(selected, "boot")) test_failed_close(HID_EPIN_ADDR, true, false);
  if (all || !strcmp(selected, "out")) test_failed_close(HID_VIA_EP_OUT, false, false);
  if (all || !strcmp(selected, "quiesced")) test_failed_close(HID_EPIN_ADDR, false, true);
  if (all || !strcmp(selected, "partial-init")) {
    test_partial_init(false);
    test_partial_init(true);
  }
  if (all || !strcmp(selected, "wake")) test_failed_reset_during_wake();
  if (all || !strcmp(selected, "stop-ll")) test_core_status(false, USBD_FAIL, USBD_OK, USBD_OK);
  if (all || !strcmp(selected, "stop-class")) test_core_status(false, USBD_OK, USBD_FAIL, USBD_OK);
  if (all || !strcmp(selected, "deinit-stop")) test_core_status(true, USBD_BUSY, USBD_OK, USBD_OK);
  if (all || !strcmp(selected, "deinit-class")) test_core_status(true, USBD_OK, USBD_FAIL, USBD_OK);
  if (all || !strcmp(selected, "deinit-ll")) test_core_status(true, USBD_OK, USBD_OK, USBD_FAIL);
  if (all) {
    test_core_status(false, USBD_OK, USBD_OK, USBD_OK);
    test_core_status(true, USBD_OK, USBD_OK, USBD_OK);
  }
  puts("PASS: failed HID close retains IN/Boot/OUT ownership and bounded storage; Init/late completion admission stays closed; core Stop/DeInit preserve errors and call order");
  return 0;
}
