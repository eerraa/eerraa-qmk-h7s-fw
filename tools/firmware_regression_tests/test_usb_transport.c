#include <assert.h>
#include <stdio.h>
#include "hw_def.h"
#include "usb.h"
#include "usbd_hid.h"
#include "usbd_hid_internal.h"
#include "usbd_cdc.h"
#include "usbd_ctlreq.h"

uint32_t test_irqmask, test_ipsr;
USBD_HandleTypeDef USBD_Device;
test_otg_t test_otg;
uint32_t test_pcgcctl;
static PCD_HandleTypeDef pcd;
static bool test_usb_reset_pending;
static bool opened_in[16], opened_out[16];
static const uint8_t *active[16];
static uint8_t active_image[16][32];
static uint32_t active_length[16], rx_length[16];
static uint8_t *rx_buffer, *control_buffer;
static const uint8_t *sent_data;
static uint32_t sent_length;
static uint32_t control_length, control_arms, control_errors, led_updates;
static uint8_t led_value, open_fail, hs_interval = 1U;
static uint16_t clock_us_fraction;
static USBD_StatusTypeDef close_status[256];
static bool close_stops_on_failure[256], receive_fail;
static uint32_t close_calls[256], open_calls[256], receive_arms;
static uint32_t arm_fail[16], clock_ms, wake_start_count, wake_end_count;
static uint32_t wake_irq_services, logical_resume_count, ungate_count;
static bool wake_asserted;
static bool delay_signal_reset;
static void (*delay_event)(uint32_t ms);
static struct { uint32_t time_ms; uint8_t ep, length, data[32]; } delivered[20000];
static unsigned delivered_count;

uint32_t millis(void) { return clock_ms; }
uint32_t micros(void) { return clock_ms * 1000U + clock_us_fraction; }
uint8_t usbBootModeGetHsInterval(void) { return hs_interval; }
bool usbIsResetPending(void) { return test_usb_reset_pending; }
static void complete_logical_resume(void)
{
  assert(USBD_Device.dev_state == USBD_STATE_SUSPENDED);
  USBD_Device.dev_state = USBD_Device.dev_old_state;
  logical_resume_count++;
}
static void bridge_resume_callback(void)
{
  if (USBD_Device.dev_state != USBD_STATE_SUSPENDED) return;
  if ((test_otg.device.DSTS & USB_OTG_DSTS_SUSPSTS) != 0U) return;
  usbHidOnResume();
  complete_logical_resume();
}
static void bridge_sof_callback(void)
{
  if (USBD_Device.dev_state != USBD_STATE_SUSPENDED) return;
  if ((test_otg.device.DSTS & USB_OTG_DSTS_SUSPSTS) != 0U) return;
  if (!usbHidConsumeWakeSof()) return;
  complete_logical_resume();
}
static void service_mock_wkuint(void)
{
  if ((test_otg.global.GINTMSK & USB_OTG_GINTMSK_WUIM) == 0U ||
      (test_otg.global.GINTSTS & USB_OTG_GINTSTS_WKUINT) == 0U) return;
  // Match the vendor IRQ order: RWUSIG is cleared before the Resume callback.
  test_otg.device.DCTL &= ~USB_OTG_DCTL_RWUSIG;
  wake_asserted = false;
  wake_irq_services++;
  bridge_resume_callback();
  test_otg.global.GINTSTS &= ~USB_OTG_GINTSTS_WKUINT;
}
void delay(uint32_t ms)
{
  assert(test_irqmask == 0U);
  service_mock_wkuint();
  if (ms == 10U) assert(wake_asserted);  // R1 regression: early WKUINT must not shorten RWUSIG.
  if (delay_event) {
    void (*event)(uint32_t) = delay_event;
    delay_event = NULL;
    event(ms);
  }
  service_mock_wkuint();
  if (ms == 10U && !delay_signal_reset) assert(wake_asserted);
  delay_signal_reset = false;
  clock_ms += ms;
}
void test_usb_ungate(PCD_HandleTypeDef *h)
{
  assert(h == &pcd && test_irqmask);
  ungate_count++;
  test_pcgcctl &= ~USB_OTG_PCGCCTL_STOPCLK;
}
HAL_StatusTypeDef HAL_PCD_ActivateRemoteWakeup(PCD_HandleTypeDef *h)
{
  assert(h == &pcd && test_irqmask);
  wake_start_count++;
  if (test_otg.device.DSTS & USB_OTG_DSTS_SUSPSTS) {
    test_otg.device.DCTL |= USB_OTG_DCTL_RWUSIG;
    test_otg.global.GINTSTS |= USB_OTG_GINTSTS_WKUINT;  // H7RS early device-driven WKUINT.
  }
  wake_asserted = (test_otg.device.DCTL & USB_OTG_DCTL_RWUSIG) != 0U;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_PCD_DeActivateRemoteWakeup(PCD_HandleTypeDef *h)
{
  assert(h == &pcd && test_irqmask);
  test_otg.device.DCTL &= ~USB_OTG_DCTL_RWUSIG;
  wake_asserted = false;
  wake_end_count++;
  return HAL_OK;
}
void usbHidSetStatusLed(uint8_t value) { led_value = value; led_updates++; }
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *d, uint8_t ep, uint8_t type, uint16_t size)
{
  assert(d == &USBD_Device && type == USBD_EP_TYPE_INTR && size <= 64U);
  open_calls[ep]++;
  if (ep == open_fail) return USBD_FAIL;
  if (ep & 0x80U) { assert(!opened_in[ep & 15U]); opened_in[ep & 15U] = true; }
  else { assert(!opened_out[ep]); opened_out[ep] = true; }
  return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *d, uint8_t ep)
{
  assert(d == &USBD_Device);
  close_calls[ep]++;
  // A failed abort/close may leave the controller owning its payload and receive buffer.
  if (close_status[ep] != USBD_OK && !close_stops_on_failure[ep]) return close_status[ep];
  if (ep & 0x80U) { assert(opened_in[ep & 15U] || d->ep_in[ep & 15U].is_used); opened_in[ep & 15U] = false; active[ep & 15U] = NULL; }
  else { assert(opened_out[ep] || d->ep_out[ep].is_used); opened_out[ep] = false; rx_buffer = NULL; }
  return close_status[ep];
}
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *d, uint8_t ep, uint8_t *data, uint32_t length)
{
  unsigned index = ep & 15U;
  assert(test_irqmask && d == &USBD_Device && opened_in[index] && (ep & 0x80U));
  assert(length > 0U && length <= 32U && active[index] == NULL && (uintptr_t)data % 4U == 0U);
  if (arm_fail[index]) { arm_fail[index]--; return USBD_FAIL; }
  active[index] = data; active_length[index] = length;
  memcpy(active_image[index], data, length);
  return USBD_OK;  // Deliberately retain the pointer; no "hardware" payload read until complete().
}
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *d, uint8_t ep, uint8_t *data, uint32_t length)
{
  assert(d == &USBD_Device && ep == HID_VIA_EP_OUT && opened_out[ep] && rx_buffer == NULL && length == 32U);
  receive_arms++;
  if (receive_fail) return USBD_FAIL;
  rx_buffer = data; return USBD_OK;
}
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *d, uint8_t ep) { (void)d; return rx_length[ep & 15U]; }
USBD_StatusTypeDef USBD_CtlPrepareRx(USBD_HandleTypeDef *d, uint8_t *data, uint32_t length)
{ (void)d; control_buffer = data; control_length = length; control_arms++; return USBD_OK; }
USBD_StatusTypeDef USBD_CtlSendData(USBD_HandleTypeDef *d, uint8_t *data, uint32_t length)
{ (void)d; sent_data = data; sent_length = length; return USBD_OK; }
void USBD_CtlError(USBD_HandleTypeDef *d, USBD_SetupReqTypedef *req) { (void)d; (void)req; control_errors++; }
void *USBD_GetEpDesc(uint8_t *configuration, uint8_t address)
{
  uint16_t total = configuration[2] | ((uint16_t)configuration[3] << 8);
  for (uint16_t off = 0; off + 2U <= total && configuration[off] >= 2U; off += configuration[off])
    if (configuration[off + 1U] == USB_DESC_TYPE_ENDPOINT && configuration[off + 2U] == address)
      return (USBD_EpDescTypeDef *)&configuration[off];
  return NULL;
}

static void sof(void) { assert(USBD_HID.SOF(&USBD_Device) == USBD_OK); assert(test_irqmask == 0U); }
static void complete(unsigned ep)
{
  assert(ep < 16U && active[ep] != NULL && delivered_count < 20000U);
  assert(memcmp(active[ep], active_image[ep], active_length[ep]) == 0);
  delivered[delivered_count].time_ms = clock_ms;
  delivered[delivered_count].ep = ep;
  delivered[delivered_count].length = active_length[ep];
  memcpy(delivered[delivered_count].data, active[ep], active_length[ep]);
  delivered_count++;
  active[ep] = NULL;
  assert(USBD_HID.DataIn(&USBD_Device, ep) == USBD_OK);
  assert(test_irqmask == 0U);
}
static void drain(void)
{
  for (unsigned limit = 0; limit < 2000U; limit++) {
    sof();
    bool any = false;
    for (unsigned ep = 1U; ep <= 5U; ep++) if (active[ep]) { complete(ep); any = true; }
    if (!any) return;
  }
  assert(!"transport did not drain");
}
static void stop(void)
{
  assert(USBD_HID.DeInit(&USBD_Device, 0U) == USBD_OK);
  for (unsigned ep = 0; ep < 16U; ep++) assert(!opened_in[ep] && !opened_out[ep] && !active[ep]);
  assert(USBD_Device.pClassData == NULL && USBD_Device.pClassDataCmsit[0] == NULL);
  USBD_Device.dev_state = USBD_STATE_DEFAULT;
}
static void configure(bool service)
{
  pcd.Instance = &test_otg.global;
  USBD_Device.pData = &pcd;
  USBD_Device.dev_speed = USBD_SPEED_HIGH;
  USBD_Device.dev_state = USBD_STATE_ADDRESSED;
  assert(USBD_HID.Init(&USBD_Device, 0U) == USBD_OK);
  USBD_HID_HandleTypeDef *h = USBD_Device.pClassData;
  assert(h && h->Protocol == 1U && h->IdleState == 0U && h->AltSetting == 0U);
  USBD_Device.dev_state = USBD_STATE_CONFIGURED;
  if (service) drain();
}
static void via_packet(uint8_t value, uint32_t size)
{
  assert(rx_buffer != NULL);
  memset(rx_buffer, value, 32U);
  rx_buffer = NULL;
  rx_length[4] = size;
  assert(USBD_HID.DataOut(&USBD_Device, 4U) == USBD_OK);
}
static void test_first_mouse_and_order(void)
{
  configure(false);
  uint8_t mouse[6] = {2, 1, 17, 3, 1, 0};
  assert(usbHidSendReportEXK(mouse, sizeof(mouse)));
  drain();
  bool found = false;
  for (unsigned i = 0; i < delivered_count; i++)
    if (delivered[i].ep == 5U && !memcmp(delivered[i].data, mouse, 6U)) found = true;
  assert(found);  // first relative movement must not be folded into initial neutral reconciliation
  delivered_count = 0U;
  uint8_t press[HID_KEYBOARD_REPORT_SIZE] = {0}, release[HID_KEYBOARD_REPORT_SIZE] = {0}; press[2] = 4U;
  assert(usbHidSendReport(press, sizeof(press)));
  const uint8_t *first = active[1];
  assert(usbHidSendReport(release, sizeof(release)));
  assert(active[1] == first && !memcmp(first, press, sizeof(press)));
  USBD_HID.DataIn(&USBD_Device, 5U);  // unrelated / duplicate completion cannot release keyboard ownership
  assert(active[1] == first);
  complete(1U); assert(active[1] && !memcmp(active[1], release, sizeof(release))); complete(1U);
  assert(delivered_count == 2U && delivered[0].data[2] == 4U && delivered[1].data[2] == 0U);
  delivered_count = 0U;
  arm_fail[1] = 2U;
  assert(usbHidSendReport(press, sizeof(press)));
  assert(active[1] == NULL);
  assert(usbHidSendReport(release, sizeof(release)));
  drain();
  assert(delivered_count == 2U && delivered[0].data[2] == 4U && delivered[1].data[2] == 0U);
}
// V260911R2: 지연 없이 같은 버퍼로 연속 제출해도 Caps와 다음 키의 전이는 FIFO 순서를 보존한다.
static void test_zero_delay_caps(void)
{
  USBD_HID_HandleTypeDef *hid = USBD_Device.pClassData;
  for (unsigned mode = 0; mode < 4U; mode++)
  {
    drain();
    delivered_count = 0U;
    USBD_Device.dev_speed = (mode & 1U) ? USBD_SPEED_HIGH : USBD_SPEED_FULL;
    hid->Protocol = (mode & 2U) ? 0U : 1U;
    uint32_t start_ms = clock_ms;
    uint8_t report[HID_KEYBOARD_REPORT_SIZE] = {0};
    for (unsigned i = 0; i < 16U; i++)
    {
      report[2] = 0x39U;
      assert(usbHidSendReport(report, sizeof(report)));
      report[2] = 0U;
      assert(usbHidSendReport(report, sizeof(report)));
      report[2] = 4U;
      assert(usbHidSendReport(report, sizeof(report)));
      report[2] = 0U;
      assert(usbHidSendReport(report, sizeof(report)));
    }
    assert(clock_ms == start_ms && delivered_count == 0U);
    assert(active[1] && active[1][2] == 0x39U); // release로 원본 버퍼를 바꿔도 active press는 보존
    for (unsigned i = 0; i < 64U; i++)
    {
      clock_ms += 5U; // 느린 호스트에서도 입력 생산은 기다리지 않았고 전송만 완료에 따라 진행한다.
      complete(1U);
      const uint8_t expected[] = {0x39U, 0U, 4U, 0U};
      assert(delivered[i].ep == 1U && delivered[i].data[2] == expected[i % 4U]);
    }
    assert(delivered_count == 64U && active[1] == NULL);
  }
  hid->Protocol = 1U;
  USBD_Device.dev_speed = USBD_SPEED_HIGH;
}

static void test_overflow(void)
{
  delivered_count = 0U;
  uint8_t data[HID_KEYBOARD_REPORT_SIZE] = {0};
  unsigned accepted = 0U;
  for (unsigned i = 0; i < 300U; i++) { data[2] = (uint8_t)(4U + i % 200U); if (usbHidSendReport(data, sizeof(data))) accepted++; }
  memset(data, 0, sizeof(data)); usbHidSendReport(data, sizeof(data));
  assert(accepted == 129U); drain();
  assert(delivered_count == 130U && !memcmp(delivered[129].data, data, sizeof(data)));
  usb_hid_transport_stats_t stats; usbHidGetTransportStats(&stats);
  assert(stats.keyboard_coalesced >= 172U && stats.arm_failures >= 2U);
  delivered_count = 0U;
  uint8_t mouse[6] = {2U, 1U, 5U, 0U, 0U, 0U};
  for (unsigned i = 0; i < 300U; i++) usbHidSendReportEXK(mouse, sizeof(mouse));
  uint8_t system[3] = {3U, 0U, 0U}, consumer[3] = {4U, 0U, 0U};
  mouse[1] = 0U; mouse[2] = 23U;
  usbHidSendReportEXK(system, 3U); usbHidSendReportEXK(consumer, 3U); usbHidSendReportEXK(mouse, 6U);
  drain();
  int last_mouse = -1;
  for (unsigned i = 0; i < delivered_count; i++) if (delivered[i].ep == 5U && delivered[i].data[0] == 2U) last_mouse = (int)i;
  assert(last_mouse >= 0 && delivered[last_mouse].data[1] == 0U && delivered[last_mouse].data[2] == 0U);
}
static void test_via_and_epoch(void)
{
  uint8_t data[32]; uint32_t generation;
  via_packet(0xE1U, 31U); assert(!usbHidReadViaRequest(data, &generation) && rx_buffer);
  for (unsigned i = 0; i < 16U; i++) via_packet((uint8_t)i, 32U);
  assert(rx_buffer == NULL);
  for (unsigned i = 0; i < 16U; i++) {
    assert(usbHidReadViaRequest(data, &generation) && data[0] == i && rx_buffer);
    assert(usbHidEnqueueViaResponse(data, 32U, generation));
  }
  drain();
  for (unsigned i = 0; i < 129U; i++) {
    via_packet((uint8_t)i, 32U);
    assert(usbHidReadViaRequest(data, &generation));
    assert(usbHidEnqueueViaResponse(data, 32U, generation));
  }
  via_packet(0xA3U, 32U);
  assert(!usbHidReadViaRequest(data, &generation));  // do not execute a new command without response credit
  complete(4U);
  assert(usbHidReadViaRequest(data, &generation) && data[0] == 0xA3U);
  assert(usbHidEnqueueViaResponse(data, 32U, generation));
  drain();
  via_packet(0x72U, 32U); assert(usbHidReadViaRequest(data, &generation));
  stop(); configure(true);
  assert(!usbHidEnqueueViaResponse(data, 32U, generation));
  assert(!usbHidReadViaRequest(data, &generation));
}
static void test_control(void)
{
  USBD_SetupReqTypedef req = {.bmRequest = 0x21U, .bRequest = USBD_HID_REQ_SET_REPORT, .wValue = 0x0200U, .wIndex = 0U, .wLength = 1U};
  unsigned before = control_arms;
  assert(USBD_HID.Setup(&USBD_Device, &req) == USBD_OK && control_length == 1U && control_arms == before + 1U);
  *control_buffer = 0x05U; rx_length[0] = 1U;
  USBD_HID.EP0_RxReady(&USBD_Device); assert(led_updates == 1U && led_value == 0x05U);
  USBD_HID.EP0_RxReady(&USBD_Device); assert(led_updates == 1U);
  const uint16_t lengths[] = {0U, 2U, 64U, 65U, 65535U};
  for (unsigned i = 0; i < sizeof(lengths)/sizeof(lengths[0]); i++) {
    req.wLength = lengths[i];
    assert(USBD_HID.Setup(&USBD_Device, &req) == USBD_FAIL && control_arms == before + 1U);
  }
  req.wLength = 1U; req.wIndex = 1U; assert(USBD_HID.Setup(&USBD_Device, &req) == USBD_FAIL);
  req.wIndex = 0U; req.wValue = 0x0201U; assert(USBD_HID.Setup(&USBD_Device, &req) == USBD_FAIL);
  req.wValue = 0x0100U; assert(USBD_HID.Setup(&USBD_Device, &req) == USBD_FAIL);
  req.wValue = 0x0200U; assert(USBD_HID.Setup(&USBD_Device, &req) == USBD_OK);
  rx_length[0] = 0U; USBD_HID.EP0_RxReady(&USBD_Device); assert(led_updates == 1U);
  // EP0 HAL rounds RX up to maxpacket even for an accepted one-byte LED request.
  assert(USBD_HID.Setup(&USBD_Device, &req) == USBD_OK);
  memset(control_buffer, 0xA5, USB_MAX_EP0_SIZE);
  rx_length[0] = USB_MAX_EP0_SIZE;
  USBD_HID.EP0_RxReady(&USBD_Device);
  assert(led_updates == 1U);
  assert(control_errors == 8U);
}
static void reuse_control_stack(void) __attribute__((noinline));
static void reuse_control_stack(void)
{
  volatile uint8_t scratch[1024];
  for (unsigned i = 0U; i < sizeof(scratch); i++) scratch[i] = 0xA5U;
}

static void test_status_buffer_lifetime(void)
{
  USBD_SetupReqTypedef req = {.bmRequest = 0x81U, .bRequest = USB_REQ_GET_STATUS,
                            .wValue = 0U, .wIndex = 0U, .wLength = 2U};
  assert(USBD_Device.dev_state == USBD_STATE_CONFIGURED);
  assert(USBD_HID.Setup(&USBD_Device, &req) == USBD_OK && sent_length == 2U);
  const uint8_t *pending_status = sent_data;
  /* The non-DMA HAL retains this pointer until a later TXFE interrupt. Other
   * callbacks can reuse the stack after Setup returns, before that FIFO read. */
  reuse_control_stack();
  assert(pending_status[0] == 0U && pending_status[1] == 0U);
  puts("PASS: interface GET_STATUS retains its two zero bytes after Setup stack reuse");
}

// Boot protocol keyboard 보고는 8바이트이고, 앞에서부터 비어 있지 않은 슬롯 6개를 싣는다.
static void test_boot_protocol(void)
{
  stop();
  configure(true);
  USBD_HID_HandleTypeDef *hid = USBD_Device.pClassData;
  delivered_count = 0U;
  uint8_t report[HID_KEYBOARD_REPORT_SIZE] = {0};
  report[0] = 0x02U; report[2] = 4U;
  for (unsigned i = 0; i < 7U; i++) report[4U + i] = (uint8_t)(5U + i);  // 슬롯 1은 빈 자리
  assert(usbHidSendReport(report, sizeof(report)));
  drain();
  assert(delivered_count == 1U && delivered[0].length == HID_KEYBOARD_REPORT_SIZE);

  USBD_SetupReqTypedef req = {.bmRequest = 0x21U, .bRequest = USBD_HID_REQ_SET_PROTOCOL, .wValue = 0U, .wIndex = 0U, .wLength = 0U};
  unsigned errors = control_errors;
  assert(USBD_HID.Setup(&USBD_Device, &req) == USBD_OK && hid->Protocol == 0U);
  assert(active[1] != NULL);  // 바뀐 형식으로 현재 상태를 다시 보낸다.
  drain();
  const uint8_t boot[HID_BOOT_KEYBOARD_REPORT_SIZE] = {0x02U, 0U, 4U, 5U, 6U, 7U, 8U, 9U};
  assert(delivered_count == 2U && delivered[1].length == 8U && !memcmp(delivered[1].data, boot, 8U));
  USBD_SetupReqTypedef get = {.bmRequest = 0xA1U, .bRequest = USBD_HID_REQ_GET_PROTOCOL, .wIndex = 0U, .wLength = 1U};
  assert(USBD_HID.Setup(&USBD_Device, &get) == USBD_OK && sent_length == 1U && sent_data[0] == 0U);
  get.wIndex = 2U;
  assert(USBD_HID.Setup(&USBD_Device, &get) == USBD_OK && sent_length == 1U && sent_data[0] == 1U);
  assert(USBD_HID.Setup(&USBD_Device, &req) == USBD_OK);
  drain();
  assert(delivered_count == 2U);  // 같은 protocol은 다시 보내지 않는다.

  report[2] = 0U;  // 첫 키를 떼면 일곱째 키가 여섯째 자리로 들어온다.
  assert(usbHidSendReport(report, sizeof(report)));
  drain();
  const uint8_t shifted[HID_BOOT_KEYBOARD_REPORT_SIZE] = {0x02U, 0U, 5U, 6U, 7U, 8U, 9U, 10U};
  assert(delivered_count == 3U && delivered[2].length == 8U && !memcmp(delivered[2].data, shifted, 8U));

  // 전송 중 전환: 무장한 Boot 보고는 완료까지 불변이고, 대기 보고와 재동기화는 Report 형식으로 나간다.
  report[2] = 4U;
  assert(usbHidSendReport(report, sizeof(report)));
  const uint8_t *armed = active[1];
  report[2] = 0U;
  assert(usbHidSendReport(report, sizeof(report)));
  req.wValue = 1U;
  assert(USBD_HID.Setup(&USBD_Device, &req) == USBD_OK && hid->Protocol == 1U && active[1] == armed);
  drain();
  assert(delivered_count == 6U && delivered[3].length == 8U && delivered[3].data[2] == 4U);
  for (unsigned i = 4U; i < 6U; i++)
    assert(delivered[i].length == HID_KEYBOARD_REPORT_SIZE && !memcmp(delivered[i].data, report, sizeof(report)));

  // 다른 인터페이스와 잘못된 요청은 keyboard 형식을 바꾸지 않는다.
  req.wValue = 0U; req.wIndex = 2U;
  assert(USBD_HID.Setup(&USBD_Device, &req) == USBD_OK && hid->Protocol == 1U);
  req.wIndex = 0U; req.wValue = 2U;
  assert(USBD_HID.Setup(&USBD_Device, &req) == USBD_FAIL);
  req.wValue = 0U; req.wLength = 1U;
  assert(USBD_HID.Setup(&USBD_Device, &req) == USBD_FAIL);
  req.wLength = 0U; req.bmRequest = 0x22U;
  assert(USBD_HID.Setup(&USBD_Device, &req) == USBD_FAIL);
  assert(hid->Protocol == 1U && control_errors == errors + 3U);
  drain();
  assert(delivered_count == 6U);

  req.bmRequest = 0x21U;
  assert(USBD_HID.Setup(&USBD_Device, &req) == USBD_OK && hid->Protocol == 0U);
  drain();
  stop();
  delivered_count = 0U;
  configure(true);  // 재구성은 Report protocol로 돌아간다(configure가 확인한다).
  unsigned keyboard_packets = 0U;
  for (unsigned i = 0; i < delivered_count; i++)
    if (delivered[i].ep == 1U) { assert(delivered[i].length == HID_KEYBOARD_REPORT_SIZE); keyboard_packets++; }
  assert(keyboard_packets == 1U);
}
static void test_pool_lifecycle(void)
{
  stop();
  void *slot = USBD_static_malloc(sizeof(USBD_CDC_HandleTypeDef));
  assert(slot && !USBD_static_malloc(1U));
  memset(slot, 0xAF, sizeof(USBD_CDC_HandleTypeDef));
  USBD_static_free((uint8_t *)slot + 1U); assert(!USBD_static_malloc(1U));
  USBD_static_free(slot); USBD_static_free(slot); USBD_static_free(NULL);
  assert(!USBD_static_malloc(0U) && !USBD_static_malloc(UINT32_MAX));
  test_irqmask = 1U; slot = USBD_static_malloc(sizeof(USBD_HID_HandleTypeDef)); assert(slot && test_irqmask == 1U);
  assert(((uint8_t *)slot)[0] == 0U); USBD_static_free(slot); assert(test_irqmask == 1U); test_irqmask = 0U;
  for (unsigned i = 0; i < 2048U; i++) { configure(false); stop(); }
  open_fail = HID_EXK_EP_IN;
  USBD_Device.dev_state = USBD_STATE_ADDRESSED;
  assert(USBD_HID.Init(&USBD_Device, 0U) == USBD_FAIL);
  assert(USBD_Device.pClassData == NULL && !opened_in[1] && !opened_in[4] && !opened_out[4]);
  open_fail = 0U;
  configure(true);
}

static void begin_sleep(void)
{
  USBD_Device.dev_old_state = USBD_STATE_CONFIGURED;
  USBD_Device.dev_state = USBD_STATE_SUSPENDED;
  USBD_Device.dev_remote_wakeup = 1U;
  test_otg.device.DSTS = USB_OTG_DSTS_SUSPSTS;
  test_otg.device.DCTL = 0U;
  test_otg.global.GINTSTS = 0U;
  test_otg.global.GINTMSK = USB_OTG_GINTMSK_WUIM;
  test_pcgcctl = USB_OTG_PCGCCTL_STOPCLK | USB_OTG_PCGCCTL_GATECLK;
  wake_asserted = false;
  delay_event = NULL;
  usbHidOnSuspend();
}

static void host_resume_no_sof(uint32_t ms)
{
  (void)ms;
  test_otg.device.DSTS = 0U;
}

static void host_resume_with_sof(uint32_t ms)
{
  (void)ms;
  test_otg.device.DSTS = 0U;
  test_otg.global.GINTSTS |= USB_OTG_GINTSTS_SOF;
  bridge_sof_callback();
  test_otg.global.GINTSTS &= ~USB_OTG_GINTSTS_SOF;
}

static void suspended_sof(uint32_t ms)
{
  (void)ms;
  test_otg.global.GINTSTS |= USB_OTG_GINTSTS_SOF;
  bridge_sof_callback();
  assert(USBD_Device.dev_state == USBD_STATE_SUSPENDED);
  test_otg.global.GINTSTS &= ~USB_OTG_GINTSTS_SOF;
}

static void stale_sof_then_hardware_resume(uint32_t ms)
{
  (void)ms;
  test_otg.device.DSTS = 0U;
  bridge_sof_callback();
  assert(USBD_Device.dev_state == USBD_STATE_SUSPENDED);
  test_otg.global.GINTSTS &= ~USB_OTG_GINTSTS_SOF;
}

static void deliver_fresh_resume_sof(void)
{
  test_otg.device.DSTS = 0U;
  test_otg.global.GINTSTS |= USB_OTG_GINTSTS_SOF;
  bridge_sof_callback();
  test_otg.global.GINTSTS &= ~USB_OTG_GINTSTS_SOF;
}

static void reset_and_reconfigure_during_signal(uint32_t ms)
{
  assert(ms == 10U);
  // Model the core reset clearing the old electrical wake signal before the new
  // class generation is configured on the same PCD instance.
  test_otg.device.DCTL = 0U;
  test_otg.device.DSTS = 0U;
  test_otg.global.GINTSTS = 0U;
  test_otg.global.GINTMSK = USB_OTG_GINTMSK_WUIM;
  wake_asserted = false;
  delay_signal_reset = true;
  stop();
  configure(true);
}

static void test_remote_wake(void)
{
  unsigned starts = wake_start_count, ends = wake_end_count;
  unsigned irq_base = wake_irq_services, resume_base = logical_resume_count, ungate_base = ungate_count;

  // H7RS raises a device-driven WKUINT when RWUSIG is asserted. WUIM isolation must
  // keep the signal alive for the full 10 ms, then discard only that still-suspended IRQ.
  clock_ms = 100U;
  begin_sleep();
  assert(usbHidRequestRemoteWakeFromInput());
  assert(clock_ms == 115U);  // 5 ms minimum suspend + 10 ms signal.
  assert(!wake_asserted && wake_start_count == starts + 1U && wake_end_count == ends + 1U);
  assert(wake_irq_services == irq_base && logical_resume_count == resume_base);
  assert((test_otg.global.GINTSTS & USB_OTG_GINTSTS_WKUINT) == 0U);
  assert((test_otg.global.GINTMSK & USB_OTG_GINTMSK_WUIM) != 0U);
  assert(ungate_count == ungate_base + 1U);
  assert((test_pcgcctl & USB_OTG_PCGCCTL_STOPCLK) == 0U);
  assert((test_pcgcctl & USB_OTG_PCGCCTL_GATECLK) != 0U);  // no speculative GATECLK write.

  // A failed pulse does not consume the whole suspend session; another physical press may retry.
  assert(usbHidRequestRemoteWakeFromInput());
  assert(wake_start_count == starts + 2U && wake_end_count == ends + 2U);

  // Host-disabled Remote Wake never touches the signal path.
  begin_sleep();
  USBD_Device.dev_remote_wakeup = 0U;
  assert(!usbHidRequestRemoteWakeFromInput());
  assert(wake_start_count == starts + 2U);

  // R2 failure regression: hardware Resume + fresh SOF must complete logical USBD Resume
  // even when no second usable WKUINT arrives. Raw-HID OUT must be consumable afterward.
  begin_sleep();
  clock_ms += 5U;
  delay_event = host_resume_with_sof;
  resume_base = logical_resume_count;
  irq_base = wake_irq_services;
  assert(usbHidRequestRemoteWakeFromInput());
  assert(USBD_Device.dev_state == USBD_STATE_CONFIGURED);
  assert(logical_resume_count == resume_base + 1U && wake_irq_services == irq_base);
  service_mock_wkuint();  // late duplicate WKUINT after SOF fallback.
  assert(logical_resume_count == resume_base + 1U && wake_irq_services == irq_base + 1U);
  delivered_count = 0U;
  uint8_t key[HID_KEYBOARD_REPORT_SIZE] = {0};
  key[2] = 4U;
  assert(usbHidSendReport(key, sizeof(key)));
  drain();
  bool saw_keyboard = false;
  for (unsigned i = 0; i < delivered_count; i++)
    if (delivered[i].ep == 1U && delivered[i].data[2] == 4U) saw_keyboard = true;
  assert(saw_keyboard);
  uint8_t via[HID_VIA_EP_SIZE]; uint32_t generation;
  via_packet(0x5AU, HID_VIA_EP_SIZE);
  assert(usbHidReadViaRequest(via, &generation) && via[0] == 0x5AU);
  assert(usbHidEnqueueViaResponse(via, HID_VIA_EP_SIZE, generation));
  drain();

  // A SOF that was already pending before RWUSIG is not fresh enough to recover USBD.
  begin_sleep();
  clock_ms += 5U;
  test_otg.global.GINTSTS |= USB_OTG_GINTSTS_SOF;
  delay_event = stale_sof_then_hardware_resume;
  resume_base = logical_resume_count;
  assert(usbHidRequestRemoteWakeFromInput());
  assert(USBD_Device.dev_state == USBD_STATE_SUSPENDED && logical_resume_count == resume_base);
  deliver_fresh_resume_sof();
  assert(USBD_Device.dev_state == USBD_STATE_CONFIGURED && logical_resume_count == resume_base + 1U);
  service_mock_wkuint();
  assert(logical_resume_count == resume_base + 1U);

  // A fresh-looking SOF while hardware still reports Suspend cannot recover USBD.
  begin_sleep();
  clock_ms += 5U;
  delay_event = suspended_sof;
  resume_base = logical_resume_count;
  assert(usbHidRequestRemoteWakeFromInput());
  assert(USBD_Device.dev_state == USBD_STATE_SUSPENDED && logical_resume_count == resume_base);
  deliver_fresh_resume_sof();
  assert(USBD_Device.dev_state == USBD_STATE_CONFIGURED && logical_resume_count == resume_base + 1U);

  // A genuine post-pulse WKUINT is accepted only after DSTS says hardware Resume occurred.
  begin_sleep();
  clock_ms += 5U;
  delay_event = host_resume_no_sof;
  resume_base = logical_resume_count;
  irq_base = wake_irq_services;
  assert(usbHidRequestRemoteWakeFromInput());
  assert(USBD_Device.dev_state == USBD_STATE_SUSPENDED);
  service_mock_wkuint();
  assert(wake_irq_services == irq_base + 1U && logical_resume_count == resume_base + 1U);
  assert(USBD_Device.dev_state == USBD_STATE_CONFIGURED);

  // A late/spurious WKUINT while SUSPSTS remains set is never a logical Resume.
  begin_sleep();
  clock_ms += 5U;
  resume_base = logical_resume_count;
  assert(usbHidRequestRemoteWakeFromInput());
  test_otg.global.GINTSTS |= USB_OTG_GINTSTS_WKUINT;
  service_mock_wkuint();
  assert(USBD_Device.dev_state == USBD_STATE_SUSPENDED && logical_resume_count == resume_base);
  deliver_fresh_resume_sof();
  assert(USBD_Device.dev_state == USBD_STATE_CONFIGURED && logical_resume_count == resume_base + 1U);

  // Reset/reconfiguration during signaling belongs to a new transport generation.
  // The old attempt must not run its final HAL deactivation against that session.
  begin_sleep();
  clock_ms += 5U;
  ends = wake_end_count;
  delay_event = reset_and_reconfigure_during_signal;
  assert(usbHidRequestRemoteWakeFromInput());
  assert(USBD_Device.dev_state == USBD_STATE_CONFIGURED);
  assert(wake_end_count == ends);
  assert((test_otg.global.GINTMSK & USB_OTG_GINTMSK_WUIM) != 0U);
}

static void test_suspend_tap(void)
{
  drain(); delivered_count = 0U;
  uint8_t down[HID_KEYBOARD_REPORT_SIZE] = {0}, up[HID_KEYBOARD_REPORT_SIZE] = {0}; down[2] = 0x39U;
  unsigned wake_base = wake_start_count;

  // Report production during Suspend preserves the tap but does not own wake signaling;
  // the physical matrix event is the sole Remote Wake trigger.
  clock_ms = UINT32_MAX - 2U;
  begin_sleep();
  usbHidSendReport(down, sizeof(down)); usbHidSendReport(up, sizeof(up));
  assert(!wake_asserted && wake_start_count == wake_base);
  USBD_Device.dev_state = USBD_STATE_CONFIGURED;
  drain();
  bool saw_press = false, saw_release_after = false;
  for (unsigned i = 0; i < delivered_count; i++) if (delivered[i].ep == 1U) {
    if (delivered[i].data[2] == 0x39U) saw_press = true;
    if (saw_press && delivered[i].data[2] == 0U) saw_release_after = true;
  }
  assert(saw_press && saw_release_after);  // a tap entirely during resume latency must not disappear
  begin_sleep(); USBD_Device.dev_remote_wakeup = 0U;
  assert(!usbHidRequestRemoteWakeFromInput()); usbHidSendReport(down, sizeof(down));
  assert(!wake_asserted && wake_start_count == wake_base);
  USBD_Device.dev_state = USBD_STATE_CONFIGURED; usbHidSendReport(up, sizeof(up)); drain();
}
static void test_host_sleeping(void)
{
  // The wakeup-key rule asks this, not the physical bus flag: Suspend before enumeration is not
  // a sleeping host, so a key held at power-on is not swallowed.
  USBD_Device.dev_old_state = USBD_STATE_DEFAULT; USBD_Device.dev_state = USBD_STATE_SUSPENDED;
  assert(!usbHidHostSleeping());
  USBD_Device.dev_old_state = USBD_STATE_ADDRESSED;
  assert(!usbHidHostSleeping());
  USBD_Device.dev_old_state = USBD_STATE_CONFIGURED;
  assert(usbHidHostSleeping());
  USBD_Device.dev_state = USBD_STATE_CONFIGURED;
  assert(!usbHidHostSleeping());
}

static void test_descriptor_intervals(void)
{
  uint16_t size;
  const uint8_t endpoints[] = {HID_EPIN_ADDR, HID_VIA_EP_IN, HID_VIA_EP_OUT, HID_EXK_EP_IN};
  for (uint8_t interval = 1U; interval <= 3U; interval++) {
    hs_interval = interval;
    uint8_t *desc = USBD_HID.GetHSConfigDescriptor(&size);
    for (unsigned i = 0; i < 4U; i++) assert(((USBD_EpDescTypeDef *)USBD_GetEpDesc(desc, endpoints[i]))->bInterval == interval);
    desc = USBD_HID.GetFSConfigDescriptor(&size);
    for (unsigned i = 0; i < 4U; i++) assert(((USBD_EpDescTypeDef *)USBD_GetEpDesc(desc, endpoints[i]))->bInterval == 1U);
  }
}
#include "test_usb_polling.h"
#include "test_keyboard_intervals.h"
#include "test_keyboard_merge.h"

int main(void)
{
  uint8_t data[32] = {0};
  assert(!usbHidEnqueueViaResponse(data, 32U, 0U));
  test_polling_label();
  test_first_mouse_and_order();
  test_zero_delay_caps();
  test_keyboard_intervals();
  test_keyboard_merge();
  test_overflow();
  test_via_and_epoch();
  test_control();
  test_status_buffer_lifetime();
  test_boot_protocol();
  test_pool_lifecycle();
  test_remote_wake();
  test_suspend_tap();
  test_descriptor_intervals();
  test_host_sleeping();
  stop();
  puts("PASS: actual HID class/pool: FIFO/VIA/EP0 lifecycle, 8-byte Boot protocol report plus isolated 10 ms Remote Wake, hardware-verified single Resume, stale/SUSPSTS SOF rejection, late-WKUINT idempotence, post-wake VIA");
  return 0;
}
