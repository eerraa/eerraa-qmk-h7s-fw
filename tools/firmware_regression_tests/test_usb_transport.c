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
static PCD_HandleTypeDef pcd;
static bool opened_in[16], opened_out[16];
static const uint8_t *active[16];
static uint8_t active_image[16][32];
static uint32_t active_length[16], rx_length[16];
static uint8_t *rx_buffer, *control_buffer;
static uint32_t control_length, control_arms, control_errors, led_updates;
static uint8_t led_value, open_fail, hs_interval = 1U;
static uint32_t arm_fail[16], clock_ms, wake_start_count, wake_end_count;
static bool wake_asserted;
static struct { uint8_t ep, length, data[32]; } delivered[20000];
static unsigned delivered_count;

uint32_t millis(void) { return clock_ms; }
uint32_t micros(void) { return clock_ms * 1000U; }
uint8_t usbBootModeGetHsInterval(void) { return hs_interval; }
HAL_StatusTypeDef HAL_PCD_ActivateRemoteWakeup(PCD_HandleTypeDef *h)
{ assert(h == &pcd && test_irqmask); wake_asserted = true; wake_start_count++; return HAL_OK; }
HAL_StatusTypeDef HAL_PCD_DeActivateRemoteWakeup(PCD_HandleTypeDef *h)
{ assert(h == &pcd); wake_asserted = false; wake_end_count++; return HAL_OK; }
void usbHidSetStatusLed(uint8_t value) { led_value = value; led_updates++; }
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *d, uint8_t ep, uint8_t type, uint16_t size)
{
  assert(d == &USBD_Device && type == USBD_EP_TYPE_INTR && size <= 64U);
  if (ep == open_fail) return USBD_FAIL;
  if (ep & 0x80U) { assert(!opened_in[ep & 15U]); opened_in[ep & 15U] = true; }
  else { assert(!opened_out[ep]); opened_out[ep] = true; }
  return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *d, uint8_t ep)
{
  assert(d == &USBD_Device);
  if (ep & 0x80U) { assert(opened_in[ep & 15U]); opened_in[ep & 15U] = false; active[ep & 15U] = NULL; }
  else { assert(opened_out[ep]); opened_out[ep] = false; rx_buffer = NULL; }
  return USBD_OK;
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
  rx_buffer = data; return USBD_OK;
}
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *d, uint8_t ep) { (void)d; return rx_length[ep & 15U]; }
USBD_StatusTypeDef USBD_CtlPrepareRx(USBD_HandleTypeDef *d, uint8_t *data, uint32_t length)
{ (void)d; control_buffer = data; control_length = length; control_arms++; return USBD_OK; }
USBD_StatusTypeDef USBD_CtlSendData(USBD_HandleTypeDef *d, uint8_t *data, uint32_t length)
{ (void)d; (void)data; (void)length; return USBD_OK; }
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
static void test_suspend_tap(void)
{
  drain(); delivered_count = 0U;
  uint8_t down[HID_KEYBOARD_REPORT_SIZE] = {0}, up[HID_KEYBOARD_REPORT_SIZE] = {0}; down[2] = 6U;
  clock_ms = UINT32_MAX - 2U;
  USBD_Device.dev_old_state = USBD_STATE_CONFIGURED;
  USBD_Device.dev_state = USBD_STATE_SUSPENDED;
  USBD_Device.dev_remote_wakeup = 1U;
  usbHidOnSuspend();
  usbHidSendReport(down, sizeof(down)); usbHidSendReport(up, sizeof(up));
  clock_ms += 4U; usbHidWakeTick(); assert(!wake_asserted);
  clock_ms += 1U; usbHidWakeTick(); assert(wake_asserted && wake_start_count == 1U);
  clock_ms += 10U; usbHidWakeTick(); assert(!wake_asserted && wake_end_count >= 1U);
  USBD_Device.dev_state = USBD_STATE_CONFIGURED;
  drain();
  bool saw_press = false, saw_release_after = false;
  for (unsigned i = 0; i < delivered_count; i++) if (delivered[i].ep == 1U) {
    if (delivered[i].data[2] == 6U) saw_press = true;
    if (saw_press && delivered[i].data[2] == 0U) saw_release_after = true;
  }
  assert(saw_press && saw_release_after);  // a tap entirely during resume latency must not disappear
  USBD_Device.dev_state = USBD_STATE_SUSPENDED; USBD_Device.dev_remote_wakeup = 0U;
  usbHidOnSuspend(); usbHidSendReport(down, sizeof(down)); clock_ms += 20U; usbHidWakeTick();
  assert(!wake_asserted && wake_start_count == 1U);
  USBD_Device.dev_state = USBD_STATE_CONFIGURED; usbHidSendReport(up, sizeof(up)); drain();
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
int main(void)
{
  uint8_t data[32] = {0};
  assert(!usbHidEnqueueViaResponse(data, 32U, 0U));
  test_first_mouse_and_order();
  test_overflow();
  test_via_and_epoch();
  test_control();
  test_pool_lifecycle();
  test_suspend_tap();
  test_descriptor_intervals();
  stop();
  puts("PASS: actual HID class/pool: retained HAL pointers, FIFO ordering/retry/overflow, VIA NAK/credit/epochs, EP0 bounds, 2048 configurations, suspend tap/wake, FS/HS intervals");
  return 0;
}
