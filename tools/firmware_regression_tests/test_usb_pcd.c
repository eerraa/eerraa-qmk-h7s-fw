/* Reuse the actual HID transport and hardware buffers; exercise the production
 * PCD bridge plus USBD core rather than the transport fixture's bridge model. */
#include "usbd_core.h"
#include "usbd_ctlreq.h"
#include "usbd_ioreq.h"
#define USBD_LL_OpenEP transport_OpenEP
#define USBD_LL_Transmit transport_Transmit
#define USBD_LL_PrepareReceive transport_PrepareReceive
#define USBD_CtlPrepareRx transport_CtlPrepareRx
#define USBD_CtlSendData transport_CtlSendData
#define USBD_CtlError transport_CtlError
#define usbIsResetPending transport_ResetPending
#define main usb_transport_fixture_main
#include "test_usb_transport.c"
#undef main
#undef usbIsResetPending
#undef USBD_LL_OpenEP
#undef USBD_LL_Transmit
#undef USBD_LL_PrepareReceive
#undef USBD_CtlPrepareRx
#undef USBD_CtlSendData
#undef USBD_CtlError
#include "usb_diagnostics.h"

static bool is_connected, bus_suspended;
static volatile bool pcd_reset_pending;
static bool pcd_resume_pending, pcd_resume_skip_stale_sof;
static uint32_t sof_count;
static uint32_t disconnect_callbacks;
static bool ep0_in_open, ep0_out_open;
static uint8_t ep0_address, ep0_stall_mask;
static uint32_t ep0_tx_count, endpoint_ll_calls, endpoint_class_calls;
static bool endpoint_stalled[256];
static uint8_t ep0_received[512], ep0_packet_lengths[8];
static uint32_t ep0_received_length, ep0_packet_count;
static const uint8_t device_descriptor[USB_LEN_DEV_DESC] = {
  USB_LEN_DEV_DESC, USB_DESC_TYPE_DEVICE, 0x00U, 0x02U, 0U, 0U, 0U,
  USB_MAX_EP0_SIZE, 0xFEU, 0xCAU, 0x60U, 0x00U, 0U, 1U, 0U, 0U, 0U, 1U,
};
static uint8_t *test_device_descriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  assert(speed == USBD_SPEED_FULL);
  *length = sizeof(device_descriptor);
  return (uint8_t *)device_descriptor;
}
static USBD_DescriptorsTypeDef test_descriptors = { .GetDeviceDescriptor = test_device_descriptor };
static struct { uint32_t SCR; } mock_scb;
#define SCB (&mock_scb)
#define SCB_SCR_SLEEPDEEP_Msk 4U
#define SCB_SCR_SLEEPONEXIT_Msk 2U
#define PCD_SPEED_HIGH 0U
#define PCD_SPEED_HIGH_IN_FULL 1U
#define PCD_SPEED_FULL 3U
#define __HAL_PCD_GATE_PHYCLOCK(h) ((void)(h), test_pcgcctl |= USB_OTG_PCGCCTL_STOPCLK)

void Error_Handler(void) { assert(false); }
USBD_StatusTypeDef USBD_LL_SetSpeed(USBD_HandleTypeDef *d, USBD_SpeedTypeDef speed)
{ assert(d == &USBD_Device); d->dev_speed = speed; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_DevDisconnected(USBD_HandleTypeDef *d)
{
  assert(d == &USBD_Device && !pcd_resume_pending && !pcd_resume_skip_stale_sof);
  disconnect_callbacks++;
  return USBD_OK;
}

/* Only the controller boundary is modeled. The generated code below retains
 * production reset, SETUP dispatch, standard requests and EP0 state handling. */
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *d, uint8_t ep, uint8_t type, uint16_t size)
{
  if ((ep & 0x7FU) != 0U) return transport_OpenEP(d, ep, type, size);
  assert(d == &USBD_Device && type == USBD_EP_TYPE_CTRL && size == USB_MAX_EP0_SIZE);
  if (ep & 0x80U) ep0_in_open = true;
  else ep0_out_open = true;
  return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *d, uint8_t ep, uint8_t *data, uint32_t length)
{
  if ((ep & 0x7FU) != 0U) return transport_Transmit(d, ep, data, length);
  assert(d == &USBD_Device && ep0_in_open);
  assert((test_pcgcctl & USB_OTG_PCGCCTL_STOPCLK) == 0U);
  sent_data = data;
  /* USB_EPStartXfer limits each EP0 IN arm to one maxpacket. */
  sent_length = MIN(length, USB_MAX_EP0_SIZE);
  pcd.IN_ep[0].xfer_buff = data;
  ep0_tx_count++;
  return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *d, uint8_t ep, uint8_t *data, uint32_t length)
{
  if (ep != 0U) return transport_PrepareReceive(d, ep, data, length);
  assert(d == &USBD_Device && ep0_out_open && length <= USB_MAX_EP0_SIZE);
  control_buffer = data;
  control_length = length;
  control_arms++;
  return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *d, uint8_t address)
{
  assert(d == &USBD_Device && address < 128U);
  ep0_address = address;
  return USBD_OK;
}
static void check_endpoint_access(USBD_HandleTypeDef *d, uint8_t ep)
{
  assert(d == &USBD_Device && (ep & 0x70U) == 0U);
  assert(ep & 0x80U ? opened_in[ep & 0xFU] : opened_out[ep]);
  endpoint_ll_calls++;
}
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *d, uint8_t ep)
{
  if ((ep & 0x7FU) != 0U) {
    check_endpoint_access(d, ep);
    endpoint_stalled[ep] = true;
  } else {
    assert(d == &USBD_Device);
    ep0_stall_mask |= ep & 0x80U ? 2U : 1U;
  }
  return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *d, uint8_t ep)
{
  check_endpoint_access(d, ep);
  endpoint_stalled[ep] = false;
  return USBD_OK;
}
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *d, uint8_t ep)
{
  check_endpoint_access(d, ep);
  return endpoint_stalled[ep];
}
USBD_StatusTypeDef USBD_RunTestMode(USBD_HandleTypeDef *d)
{ (void)d; assert(false); return USBD_FAIL; }

#undef logPrintf
#define logPrintf(...) assert(false && "PCD callbacks must not enter the shared logger")

#include "usb_pcd_callbacks.inc"

static void pcd_configure(void)
{
  assert(!pcd_reset_pending);
  configure(true);
  pcd.pData = &USBD_Device;
  pcd.Init.low_power_enable = 0U;
  pcd.Init.speed = PCD_SPEED_HIGH_IN_FULL;
  USBD_Device.pClass[0] = &USBD_HID;
  USBD_Device.dev_speed = USBD_SPEED_FULL;
  USBD_Device.pDesc = &test_descriptors;
  ep0_in_open = ep0_out_open = true;
  USBD_Device.ep_in[0].is_used = USBD_Device.ep_out[0].is_used = 1U;
  USBD_Device.ep_in[0].maxpacket = USBD_Device.ep_out[0].maxpacket = USB_MAX_EP0_SIZE;
  memset(endpoint_stalled, 0, sizeof(endpoint_stalled));
  ep0_stall_mask = 0U;
  mock_scb.SCR = 0U;
  test_pcgcctl = 0U;
  test_otg.device.DSTS = 0U;
  test_otg.global.GINTSTS = 0U;
  test_otg.global.GINTMSK = USB_OTG_GINTMSK_WUIM | USB_OTG_GINTSTS_USBRST | USB_OTG_GINTSTS_ENUMDNE;
}

static void pcd_suspend(void)
{
  test_otg.device.DSTS = USB_OTG_DSTS_SUSPSTS;
  HAL_PCD_SuspendCallback(&pcd);
  assert(USBD_Device.dev_state == USBD_STATE_SUSPENDED && bus_suspended);
  assert((test_pcgcctl & USB_OTG_PCGCCTL_STOPCLK) != 0U);
  assert(((mock_scb.SCR & SCB_SCR_SLEEPDEEP_Msk) != 0U) == (pcd.Init.low_power_enable != 0U));
}

static void pcd_resume_irq(void)
{
  /* Vendor handler clears RWUSIG before invoking the callback. */
  test_otg.device.DCTL &= ~USB_OTG_DCTL_RWUSIG;
  test_irqmask = 1U;
  HAL_PCD_ResumeCallback(&pcd);
  test_irqmask = 0U;
}

static void pcd_sof(void)
{
  /* A live PHY clock is required for ongoing bus progress. */
  assert((test_pcgcctl & USB_OTG_PCGCCTL_STOPCLK) == 0U);
  test_otg.global.GINTSTS |= USB_OTG_GINTSTS_SOF;
  test_irqmask = 1U;
  HAL_PCD_SOFCallback(&pcd);
  test_irqmask = 0U;
  test_otg.global.GINTSTS &= ~USB_OTG_GINTSTS_SOF;
}

static void keyboard_packet(uint8_t key)
{
  uint8_t data[HID_KEYBOARD_REPORT_SIZE] = {0};
  data[2] = key;
  assert(usbHidSendReport(data, sizeof(data)));
}

static void pcd_complete(unsigned ep)
{
  assert(active[ep] != NULL);
  assert(memcmp(active[ep], active_image[ep], active_length[ep]) == 0);
  active[ep] = NULL;
  test_irqmask = 1U;
  HAL_PCD_DataInStageCallback(&pcd, ep);
  test_irqmask = 0U;
}

static void pcd_receive(uint8_t marker)
{
  assert(rx_buffer != NULL);
  memset(rx_buffer, marker, HID_VIA_EP_SIZE);
  rx_buffer = NULL;
  rx_length[4] = HID_VIA_EP_SIZE;
  test_irqmask = 1U;
  HAL_PCD_DataOutStageCallback(&pcd, 4U);
  test_irqmask = 0U;
}

static void assert_via_request(uint8_t marker)
{
  uint8_t request[HID_VIA_EP_SIZE];
  uint32_t generation;
  assert(usbHidReadViaRequest(request, &generation));
  assert(request[0] == marker);
  assert(usbHidEnqueueViaResponse(request, sizeof(request), generation));
  assert(active[4] != NULL);
  pcd_complete(4U);
}

static void test_host_resume_clock(void)
{
  pcd_configure();
  pcd.Init.low_power_enable = 1U;
  pcd_suspend();
  /* Host resume is independent of device-driven Remote Wake. */
  test_otg.device.DSTS = 0U;
  pcd_resume_irq();
  assert((test_pcgcctl & USB_OTG_PCGCCTL_STOPCLK) == 0U);
  assert(USBD_Device.dev_state == USBD_STATE_CONFIGURED && !bus_suspended);
  pcd_sof();
  keyboard_packet(4U);
  pcd_complete(1U);
  pcd_receive(0x31U);
  assert_via_request(0x31U);
  stop();
  puts("PASS: host Resume ungates STOPCLK, restores keyboard and VIA");
}

static void test_host_early_resume(void)
{
  pcd_configure();
  pcd_suspend();
  /* Resume detection precedes the end of resume signaling. It must not yet
   * claim a configured bus, but fresh active SOF must finish that transition. */
  pcd_resume_irq();
  assert(USBD_Device.dev_state == USBD_STATE_SUSPENDED);
  assert((test_pcgcctl & USB_OTG_PCGCCTL_STOPCLK) == 0U);
  pcd_sof();
  assert(USBD_Device.dev_state == USBD_STATE_SUSPENDED && bus_suspended);
  test_otg.device.DSTS = 0U;
  pcd_sof();
  assert(USBD_Device.dev_state == USBD_STATE_CONFIGURED && !bus_suspended);
  keyboard_packet(4U);
  pcd_complete(1U);
  pcd_receive(0x32U);
  assert_via_request(0x32U);
  stop();
  puts("PASS: early host WKUINT waits for hardware-active SOF");
}

static void test_completions_before_resume(void)
{
  pcd_configure();
  keyboard_packet(4U);
  keyboard_packet(0U);
  pcd_receive(0x33U);
  uint8_t request[HID_VIA_EP_SIZE];
  uint32_t generation;
  assert(usbHidReadViaRequest(request, &generation));
  assert(usbHidEnqueueViaResponse(request, sizeof(request), generation));
  pcd_suspend();
  /* Hardware has resumed and clock is live; HAL handles OUT/IN XFRC before
   * WKUINT in the same IRQ. Completion ownership must survive that ordering. */
  test_pcgcctl &= ~USB_OTG_PCGCCTL_STOPCLK;
  test_otg.device.DSTS = 0U;
  pcd_receive(0x34U);
  pcd_complete(1U);
  pcd_complete(4U);
  pcd_resume_irq();
  pcd_sof();
  assert(active[1] != NULL && active[1][2] == 0U);
  pcd_complete(1U);
  assert_via_request(0x34U);
  assert(rx_buffer != NULL);
  stop();
  puts("PASS: OUT/IN XFRC before WKUINT preserves keyboard and VIA ownership");
}

static void test_in_before_resume(void)
{
  pcd_configure();
  keyboard_packet(4U);
  keyboard_packet(0U);
  pcd_suspend();
  test_otg.device.DSTS = 0U;
  pcd_complete(1U);
  assert(USBD_Device.dev_state == USBD_STATE_CONFIGURED);
  assert(active[1] != NULL && active[1][2] == 0U);
  pcd_complete(1U);
  pcd_resume_irq();
  assert(!pcd_resume_pending);
  keyboard_packet(5U);
  pcd_complete(1U);
  stop();
  puts("PASS: lone IN XFRC before WKUINT does not lose keyboard completion");
}

static void test_setup_before_resume(void)
{
  pcd_configure();
  pcd_suspend();
  test_otg.device.DSTS = 0U;
  const uint8_t setup[] = {0x80U, USB_REQ_GET_DESCRIPTOR, 0U, USB_DESC_TYPE_DEVICE, 0U, 0U, USB_LEN_DEV_DESC, 0U};
  memcpy(pcd.Setup, setup, sizeof(setup));
  uint32_t before = ep0_tx_count;
  test_irqmask = 1U;
  HAL_PCD_SetupStageCallback(&pcd);
  test_irqmask = 0U;
  assert(ep0_tx_count == before + 1U && !bus_suspended);
  assert(USBD_Device.dev_state == USBD_STATE_CONFIGURED && sent_length == USB_LEN_DEV_DESC);
  assert(memcmp(sent_data, device_descriptor, sent_length) == 0);
  assert((test_pcgcctl & USB_OTG_PCGCCTL_STOPCLK) == 0U);
  stop();
  puts("PASS: SETUP before WKUINT enters the core with restored configuration");
}

static void pcd_setup(uint8_t request_type, uint8_t request, uint16_t value, uint16_t length)
{
  const uint8_t setup[] = {request_type, request, value & 0xFFU, value >> 8,
                           0U, 0U, length & 0xFFU, length >> 8};
  memcpy(pcd.Setup, setup, sizeof(setup));
  /* The controller retires the previous control transfer on a fresh SETUP. */
  ep0_stall_mask = 0U;
  ep0_received_length = ep0_packet_count = 0U;
  test_irqmask = 1U;
  HAL_PCD_SetupStageCallback(&pcd);
  test_irqmask = 0U;
  assert(ep0_stall_mask == 0U);
}

static void pcd_ep0_complete_in(void)
{
  assert(ep0_packet_count < sizeof(ep0_packet_lengths));
  assert(ep0_received_length + sent_length <= sizeof(ep0_received));
  ep0_packet_lengths[ep0_packet_count++] = (uint8_t)sent_length;
  if (sent_length != 0U) {
    memcpy(ep0_received + ep0_received_length, sent_data, sent_length);
    ep0_received_length += sent_length;
    /* PCD_WriteEmptyTxFifo advances xfer_buff before the completion callback. */
    pcd.IN_ep[0].xfer_buff += sent_length;
  }
  test_irqmask = 1U;
  HAL_PCD_DataInStageCallback(&pcd, 0U);
  test_irqmask = 0U;
}

static void pcd_ep0_read(const uint8_t *expected, uint32_t length)
{
  while (USBD_Device.ep0_state == USBD_EP0_DATA_IN) pcd_ep0_complete_in();
  assert(ep0_received_length == length && memcmp(ep0_received, expected, length) == 0);
  assert(USBD_Device.ep0_state == USBD_EP0_STATUS_OUT && control_length == 0U);
  test_irqmask = 1U;
  HAL_PCD_DataOutStageCallback(&pcd, 0U);
  test_irqmask = 0U;
}

static USBD_StatusTypeDef pcd_endpoint_request(uint8_t request, uint16_t index)
{
  bool get_status = request == USB_REQ_GET_STATUS;
  const uint8_t setup[] = {get_status ? 0x82U : 0x02U, request, 0U, 0U,
                          index & 0xFFU, index >> 8, get_status ? 2U : 0U, 0U};
  ep0_stall_mask = 0U;
  ep0_received_length = ep0_packet_count = 0U;
  memcpy(pcd.Setup, setup, sizeof(setup));
  test_irqmask = 1U;
  USBD_StatusTypeDef result = USBD_LL_SetupStage(&USBD_Device, (uint8_t *)pcd.Setup);
  test_irqmask = 0U;
  return result;
}

static uint8_t endpoint_class_setup(USBD_HandleTypeDef *d, USBD_SetupReqTypedef *req)
{
  assert(d == &USBD_Device && req->wIndex == 0xFF91U);
  endpoint_class_calls++;
  return USBD_OK;
}

static void test_endpoint_request_bounds(void)
{
  pcd_configure();
  /* The reserved bits must be rejected before their low nibble aliases EP1. */
  uint32_t accesses = endpoint_ll_calls;
  assert(pcd_endpoint_request(USB_REQ_SET_FEATURE, 0x0091U) == USBD_FAIL);
  assert(ep0_stall_mask == 3U && endpoint_ll_calls == accesses);

  const uint8_t requests[] = {USB_REQ_GET_STATUS, USB_REQ_SET_FEATURE, USB_REQ_CLEAR_FEATURE};
  for (uint32_t index = 0U; index <= UINT16_MAX; index++) {
    /* Enumerated endpoint addresses, independent of the guard's bit masks. */
    bool present = index == 0x00U || index == 0x80U || index == 0x81U ||
                   index == 0x84U || index == 0x85U || index == 0x04U;
    for (unsigned i = 0U; i < sizeof(requests); i++) {
      uint32_t before_access = endpoint_ll_calls, before_tx = ep0_tx_count;
      USBD_StatusTypeDef result = pcd_endpoint_request(requests[i], (uint16_t)index);
      if (!present) {
        assert(result == USBD_FAIL && ep0_stall_mask == 3U);
        assert(endpoint_ll_calls == before_access && ep0_tx_count == before_tx);
      } else {
        assert(result == USBD_OK && ep0_stall_mask == 0U);
        if (requests[i] == USB_REQ_GET_STATUS) {
          const uint8_t expected[] = {0U, 0U};
          assert(USBD_Device.ep0_state == USBD_EP0_DATA_IN && sent_length == sizeof(expected));
          pcd_ep0_read(expected, sizeof(expected));
        } else {
          assert(USBD_Device.ep0_state == USBD_EP0_STATUS_IN && sent_length == 0U);
          pcd_ep0_complete_in();
          if ((index & 0x7FU) != 0U) {
            bool stalled = requests[i] == USB_REQ_SET_FEATURE;
            assert(endpoint_stalled[index] == stalled);
            assert(pcd_endpoint_request(USB_REQ_GET_STATUS, (uint16_t)index) == USBD_OK);
            const uint8_t expected[] = {stalled ? 1U : 0U, 0U};
            pcd_ep0_read(expected, sizeof(expected));
          }
        }
      }
    }
  }

  /* Class/vendor endpoint routing keeps its existing class-owned validation. */
  USBD_ClassTypeDef proxy_class = USBD_HID;
  proxy_class.Setup = endpoint_class_setup;
  USBD_Device.pClass[0] = &proxy_class;
  const uint8_t types[] = {USB_REQ_TYPE_CLASS, USB_REQ_TYPE_VENDOR};
  for (unsigned i = 0U; i < sizeof(types); i++) {
    USBD_SetupReqTypedef req = {.bmRequest = types[i] | USB_REQ_RECIPIENT_ENDPOINT,
                              .bRequest = 0x7FU, .wIndex = 0xFF91U};
    uint32_t before_class = endpoint_class_calls;
    ep0_stall_mask = 0U;
    assert(USBD_StdEPReq(&USBD_Device, &req) == USBD_OK);
    assert(endpoint_class_calls == before_class + 1U && ep0_stall_mask == 0U);
  }
  USBD_Device.pClass[0] = &USBD_HID;
  stop();
  puts("PASS: all 65536 standard endpoint indexes reject invalid addresses; valid status/halt/clear and class/vendor routing remain intact");
}

static void test_suspend_reset_enumeration(void)
{
  pcd_configure();
  for (unsigned cycle = 0U; cycle < 64U; cycle++) {
    keyboard_packet(4U);
    pcd_receive(0x41U);
    pcd_suspend();
    /* Host reboot can begin a new enumeration instead of resuming the old
     * configuration. Do not supply a WKUINT or manually restore STOPCLK. */
    assert((test_pcgcctl & USB_OTG_PCGCCTL_STOPCLK) != 0U);
    test_otg.device.DSTS = 0U;
    ep0_address = 0U;
    ep0_in_open = ep0_out_open = false;
    test_irqmask = 1U;
    HAL_PCD_ResetCallback(&pcd);
    test_irqmask = 0U;
    assert(!bus_suspended && !pcd_resume_pending && !pcd_resume_skip_stale_sof);
    assert(USBD_Device.dev_state == USBD_STATE_DEFAULT && USBD_Device.dev_config == 0U);
    assert(USBD_Device.pClassData == NULL && active[1] == NULL && rx_buffer == NULL);
    assert(ep0_in_open && ep0_out_open);
    assert(USBD_Device.ep_in[0].maxpacket == USB_MAX_EP0_SIZE);
    assert(USBD_Device.ep_out[0].maxpacket == USB_MAX_EP0_SIZE);

    pcd_setup(0x80U, USB_REQ_GET_DESCRIPTOR, USB_DESC_TYPE_DEVICE << 8, USB_LEN_DEV_DESC);
    assert(USBD_Device.ep0_state == USBD_EP0_DATA_IN && sent_length == USB_LEN_DEV_DESC);
    pcd_ep0_read(device_descriptor, sizeof(device_descriptor));
    assert(ep0_packet_count == 1U && ep0_packet_lengths[0] == USB_LEN_DEV_DESC);

    pcd_setup(0x00U, USB_REQ_SET_ADDRESS, 7U, 0U);
    assert(USBD_Device.dev_state == USBD_STATE_ADDRESSED && ep0_address == 7U);
    assert(USBD_Device.ep0_state == USBD_EP0_STATUS_IN && sent_length == 0U);
    pcd_ep0_complete_in();

    /* Use the real HID configuration: three interfaces and four endpoints.
     * A host first reads the header, then wTotalLength bytes (64 + 27 here). */
    uint16_t config_length;
    uint8_t expected_config[USB_HID_CONFIG_DESC_SIZ];
    const uint8_t *config = USBD_HID.GetFSConfigDescriptor(&config_length);
    assert(config_length == sizeof(expected_config) && config_length > USB_MAX_EP0_SIZE);
    memcpy(expected_config, config, config_length);
    assert(expected_config[0] == USB_LEN_CFG_DESC && expected_config[1] == USB_DESC_TYPE_CONFIGURATION);
    assert((expected_config[2] | ((uint16_t)expected_config[3] << 8)) == config_length);
    assert(expected_config[4] == 3U);
    pcd_setup(0x80U, USB_REQ_GET_DESCRIPTOR, USB_DESC_TYPE_CONFIGURATION << 8, USB_LEN_CFG_DESC);
    pcd_ep0_read(expected_config, USB_LEN_CFG_DESC);
    assert(ep0_packet_count == 1U && ep0_packet_lengths[0] == USB_LEN_CFG_DESC);
    pcd_setup(0x80U, USB_REQ_GET_DESCRIPTOR, USB_DESC_TYPE_CONFIGURATION << 8, config_length);
    pcd_ep0_read(expected_config, config_length);
    assert(ep0_packet_count == 2U && ep0_packet_lengths[0] == USB_MAX_EP0_SIZE);
    assert(ep0_packet_lengths[1] == config_length - USB_MAX_EP0_SIZE);

    pcd_setup(0x00U, USB_REQ_SET_CONFIGURATION, 1U, 0U);
    assert(USBD_Device.dev_state == USBD_STATE_CONFIGURED && USBD_Device.dev_config == 1U);
    assert(USBD_Device.pClassData != NULL && rx_buffer != NULL);
    assert(USBD_Device.ep0_state == USBD_EP0_STATUS_IN && sent_length == 0U);
    pcd_ep0_complete_in();
    drain();
    keyboard_packet(5U);
    pcd_complete(1U);
    pcd_receive(0x42U);
    assert_via_request(0x42U);
  }
  stop();
  puts("PASS: 64 Suspend/reset/EP0 cycles: device/configuration descriptors (9B and 64+27B), address/configure, keyboard and VIA without WKUINT");
}

static void test_pending_wake_boundaries(void)
{
  pcd_configure();
  pcd_suspend();
  test_otg.global.GINTSTS = USB_OTG_GINTSTS_SOF;
  pcd_resume_irq();
  assert(pcd_resume_pending && pcd_resume_skip_stale_sof);
  test_otg.device.DSTS = 0U;
  pcd_sof();
  assert(USBD_Device.dev_state == USBD_STATE_SUSPENDED);
  pcd_sof();
  assert(USBD_Device.dev_state == USBD_STATE_CONFIGURED && !pcd_resume_pending);
  for (unsigned boundary = 0U; boundary < 3U; boundary++) {
    pcd_suspend();
    test_otg.global.GINTSTS = USB_OTG_GINTSTS_SOF;
    pcd_resume_irq();
    assert(pcd_resume_pending && pcd_resume_skip_stale_sof);
    if (boundary == 0U) {
      HAL_PCD_SuspendCallback(&pcd);
    } else if (boundary == 1U) {
      test_irqmask = 1U;
      HAL_PCD_ResetCallback(&pcd);
      test_irqmask = 0U;
      assert(USBD_Device.dev_state == USBD_STATE_DEFAULT && USBD_Device.pClassData == NULL);
    } else {
      uint32_t before = disconnect_callbacks;
      HAL_PCD_DisconnectCallback(&pcd);
      assert(disconnect_callbacks == before + 1U);
    }
    assert(!pcd_resume_pending && !pcd_resume_skip_stale_sof);
    test_pcgcctl &= ~USB_OTG_PCGCCTL_STOPCLK;
    test_otg.device.DSTS = 0U;
    pcd_sof();
    assert(USBD_Device.dev_state == (boundary == 1U ? USBD_STATE_DEFAULT : USBD_STATE_SUSPENDED));
    if (boundary == 1U) configure(true);
  }
  stop();
  puts("PASS: pending host wake rejects old SOF and expires at Suspend/Reset/Disconnect");
}

static USBD_StatusTypeDef pcd_configuration_request(uint8_t configuration)
{
  const uint8_t setup[] = {0x00U, USB_REQ_SET_CONFIGURATION, configuration, 0U, 0U, 0U, 0U, 0U};
  memcpy(pcd.Setup, setup, sizeof(setup));
  ep0_stall_mask = 0U;
  test_irqmask = 1U;
  USBD_StatusTypeDef result = USBD_LL_SetupStage(&USBD_Device, (uint8_t *)pcd.Setup);
  test_irqmask = 0U;
  return result;
}

static void test_configuration_failures(void)
{
  const uint8_t endpoints[] = {HID_EPIN_ADDR, HID_EXK_EP_IN, HID_VIA_EP_IN, HID_VIA_EP_OUT};
  for (unsigned i = 0U; i < sizeof(endpoints); i++) {
    pcd_configure();
    USBD_Device.dev_config = 1U;
    keyboard_packet(4U);
    void *retained = USBD_Device.pClassDataCmsit[0];
    uint8_t ep = endpoints[i];
    close_status[ep] = USBD_FAIL;
    uint32_t before_tx = ep0_tx_count;
    assert(pcd_configuration_request(0U) == USBD_FAIL);
    assert(ep0_stall_mask == 3U && ep0_tx_count == before_tx);
    assert(USBD_Device.dev_state == USBD_STATE_ADDRESSED && USBD_Device.dev_config == 0U);
    assert(USBD_Device.pClassDataCmsit[0] == retained);
    uint32_t before_close = close_calls[ep], before_open = open_calls[ep];
    /* A new host configuration must not silently recycle failed ownership. */
    assert(pcd_configuration_request(1U) == USBD_FAIL);
    assert(ep0_stall_mask == 3U && ep0_tx_count == before_tx);
    assert(close_calls[ep] == before_close && open_calls[ep] == before_open);
    assert(USBD_Device.dev_config == 0U && USBD_Device.pClassDataCmsit[0] == retained);

    /* Model a subsequent explicit bus Reset after the stop condition clears. */
    close_status[ep] = USBD_OK;
    test_irqmask = 1U;
    HAL_PCD_ResetCallback(&pcd);
    test_irqmask = 0U;
    assert(USBD_Device.pClassDataCmsit[0] == NULL);
    pcd_setup(0x00U, USB_REQ_SET_ADDRESS, 7U, 0U);
    assert(pcd_configuration_request(1U) == USBD_OK && ep0_stall_mask == 0U);
    assert(USBD_Device.dev_state == USBD_STATE_CONFIGURED && USBD_Device.dev_config == 1U);
    drain();
    keyboard_packet(5U);
    pcd_complete(1U);
    pcd_receive(0x51U);
    assert_via_request(0x51U);
    stop();
  }

  pcd_configure();
  assert(pcd_configuration_request(0U) == USBD_OK && ep0_stall_mask == 0U);
  open_fail = HID_VIA_EP_IN;
  assert(pcd_configuration_request(1U) == USBD_FAIL && ep0_stall_mask == 3U);
  assert(USBD_Device.dev_state == USBD_STATE_ADDRESSED && USBD_Device.dev_config == 0U);
  open_fail = 0U;
  assert(pcd_configuration_request(1U) == USBD_OK && ep0_stall_mask == 0U);
  stop();
  puts("PASS: SET_CONFIGURATION stalls failed teardown without ACK/reuse; failed initialization leaves configuration zero; explicit Reset permits a clean session");
}

static void test_false_resume(void)
{
  pcd_configure();
  pcd_suspend();
  /* SOF alone cannot invent a host resume when no wake attempt exists. */
  test_pcgcctl &= ~USB_OTG_PCGCCTL_STOPCLK;
  pcd_sof();
  assert(USBD_Device.dev_state == USBD_STATE_SUSPENDED && bus_suspended);
  test_otg.device.DSTS = 0U;
  pcd_sof();
  assert(USBD_Device.dev_state == USBD_STATE_SUSPENDED && !bus_suspended);
  stop();
  puts("PASS: stale/suspended SOF does not invent a logical Resume");
}


static void pcd_observe_raw_reset(void)
{
  test_otg.device.DSTS = 0U;
  test_otg.global.GINTSTS |= USB_OTG_GINTSTS_USBRST;
  test_irqmask = 1U;
  usbPcdOnIrqEntry(&pcd);
  test_irqmask = 0U;
  assert(pcd_reset_pending && usbIsResetPending() && !usbIsConnect());
  assert((test_pcgcctl & USB_OTG_PCGCCTL_STOPCLK) == 0U);
}

static void pcd_finish_raw_reset(void)
{
  /* The separate IRQ fixture executes these acknowledgements in the real
   * HAL. Here the controller adapter supplies the ENUMDNE callback boundary. */
  test_otg.global.GINTSTS &= ~USB_OTG_GINTSTS_USBRST;
  test_otg.device.DSTS = 0U;
  test_irqmask = 1U;
  HAL_PCD_ResetCallback(&pcd);
  test_irqmask = 0U;
  test_otg.global.GINTSTS &= ~USB_OTG_GINTSTS_ENUMDNE;
  assert(!pcd_reset_pending && !usbIsResetPending());
  assert(!pcd_resume_pending && !pcd_resume_skip_stale_sof);
  assert(USBD_Device.dev_state == USBD_STATE_DEFAULT && USBD_Device.dev_config == 0U);
}

static void pcd_configure_successor(void)
{
  pcd_setup(0x80U, USB_REQ_GET_DESCRIPTOR, USB_DESC_TYPE_DEVICE << 8, USB_LEN_DEV_DESC);
  pcd_ep0_read(device_descriptor, sizeof(device_descriptor));
  pcd_setup(0x00U, USB_REQ_SET_ADDRESS, 7U, 0U);
  pcd_ep0_complete_in();
  assert(pcd_configuration_request(1U) == USBD_OK && ep0_stall_mask == 0U);
  pcd_ep0_complete_in();
  assert(USBD_Device.dev_state == USBD_STATE_CONFIGURED && USBD_Device.dev_config == 1U);
  drain();
}

static void test_raw_reset_boundary(void)
{
  for (unsigned suspended = 0U; suspended < 2U; suspended++) {
    for (unsigned phase = 0U; phase < 3U; phase++) {
      for (unsigned event = 0U; event < 6U; event++) {
        pcd_configure();
        USBD_Device.dev_config = 1U;
        is_connected = true;
        assert(usbIsConnect());
        keyboard_packet(4U);
        keyboard_packet(0U);
        pcd_receive(0xA1U);
        uint8_t via[HID_VIA_EP_SIZE];
        uint32_t old_generation;
        assert(usbHidReadViaRequest(via, &old_generation));
        assert(usbHidEnqueueViaResponse(via, sizeof(via), old_generation));
        pcd_receive(0xA2U);
        if (suspended) {
          pcd_suspend();
          pcd_resume_irq();
          assert(pcd_resume_pending);
        }
        uint8_t old_state = USBD_Device.dev_state;
        USBD_HID_HandleTypeDef *retained = USBD_Device.pClassData;
        USBD_HID_HandleTypeDef retained_image = *retained;
        PCD_HandleTypeDef descriptors = pcd;
        const uint8_t *keyboard = active[1], *response = active[4];
        uint8_t keyboard_image[HID_KEYBOARD_REPORT_SIZE], response_image[HID_VIA_EP_SIZE];
        memcpy(keyboard_image, keyboard, sizeof(keyboard_image));
        memcpy(response_image, response, sizeof(response_image));
        uint32_t before_receive = receive_arms, before_ep0 = ep0_tx_count;
        pcd_observe_raw_reset();
        assert(USBD_Device.dev_state == old_state && USBD_Device.pClassData == retained);
        assert(!memcmp(retained, &retained_image, sizeof(retained_image)));
        assert(!memcmp(&pcd, &descriptors, sizeof(pcd)));
        assert(active[1] == keyboard && active[4] == response);
        assert(!memcmp(keyboard, keyboard_image, sizeof(keyboard_image)));
        assert(!memcmp(response, response_image, sizeof(response_image)));
        assert(!pcd_resume_pending && !pcd_resume_skip_stale_sof);
        /* USBRST pending, consumed-bit gap, and ENUMDNE pending all retain the
         * same software barrier. These snapshots are conditional source input. */
        test_otg.global.GINTSTS = phase == 0U ? USB_OTG_GINTSTS_USBRST :
                                  phase == 2U ? USB_OTG_GINTSTS_ENUMDNE : 0U;
        uint8_t key[HID_KEYBOARD_REPORT_SIZE] = {0}; key[2] = 6U;
        assert(!usbHidSendReport(key, sizeof(key)));
        assert(!usbHidReadViaRequest(via, &old_generation));
        assert(!usbHidEnqueueViaResponse(via, sizeof(via), old_generation));
        assert(!usbHidViaResponsesPending());
        uint32_t before_wake = wake_start_count;
        assert(!usbHidRequestRemoteWakeFromInput() && wake_start_count == before_wake);
        assert(USBD_HID.SOF(&USBD_Device) == USBD_OK);
        if (event == 0U) {
          pcd_complete(1U);
          assert(active[1] == NULL);
        } else if (event == 1U) {
          pcd_receive(0xA3U);
          assert(rx_buffer == NULL);
        } else if (event == 2U) {
          const uint8_t setup[] = {0x21U, USBD_HID_REQ_SET_PROTOCOL, 0U, 0U, 0U, 0U, 0U, 0U};
          memcpy(pcd.Setup, setup, sizeof(setup));
          test_irqmask = 1U;
          HAL_PCD_SetupStageCallback(&pcd);
          test_irqmask = 0U;
        } else if (event == 3U) {
          pcd_sof();
        } else if (event == 4U) {
          pcd_resume_irq();
        } else {
          test_otg.device.DSTS = USB_OTG_DSTS_SUSPSTS;
          HAL_PCD_SuspendCallback(&pcd);
          assert((test_pcgcctl & USB_OTG_PCGCCTL_STOPCLK) == 0U);
        }
        assert(pcd_reset_pending && !usbIsConnect());
        assert(USBD_Device.dev_state == old_state && retained->Protocol == retained_image.Protocol);
        assert(receive_arms == before_receive && ep0_tx_count == before_ep0);
        assert(!pcd_resume_pending && !pcd_resume_skip_stale_sof);
        assert(!usbHidReadViaRequest(via, &old_generation));
        pcd_finish_raw_reset();
        assert(USBD_Device.pClassData == NULL && active[1] == NULL && active[4] == NULL && rx_buffer == NULL);
        pcd_configure_successor();
        assert(!usbHidReadViaRequest(via, &old_generation));
        keyboard_packet(5U);
        pcd_complete(1U);
        pcd_receive(0xA4U);
        assert_via_request(0xA4U);
        stop();
      }
    }
  }
  puts("PASS: 36 configured/suspended reset snapshots reject old IN/OUT/SETUP/SOF/WKUINT/Suspend and main admission; payload/descriptors remain owned until ENUMDNE, then fresh EP0/configuration/keyboard/VIA recover");
}

static void test_raw_reset_failed_teardown(void)
{
  pcd_configure();
  keyboard_packet(4U);
  keyboard_packet(0U);
  const uint8_t *retained_payload = active[1];
  uint8_t image[HID_KEYBOARD_REPORT_SIZE];
  memcpy(image, retained_payload, sizeof(image));
  void *retained_class = USBD_Device.pClassDataCmsit[0];
  pcd_suspend();
  pcd_observe_raw_reset();
  close_status[HID_EPIN_ADDR] = USBD_FAIL;
  pcd_finish_raw_reset();
  assert(USBD_Device.pClassDataCmsit[0] == retained_class && active[1] == retained_payload);
  assert(!memcmp(retained_payload, image, sizeof(image)));
  pcd_setup(0x00U, USB_REQ_SET_ADDRESS, 7U, 0U);
  uint32_t before_open = open_calls[HID_EPIN_ADDR];
  assert(pcd_configuration_request(1U) == USBD_FAIL && ep0_stall_mask == 3U);
  assert(open_calls[HID_EPIN_ADDR] == before_open && active[1] == retained_payload);
  close_status[HID_EPIN_ADDR] = USBD_OK;
  pcd_finish_raw_reset();
  assert(USBD_Device.pClassDataCmsit[0] == NULL && active[1] == NULL);
  pcd_configure_successor();
  keyboard_packet(5U);
  pcd_complete(1U);
  pcd_receive(0xA5U);
  assert_via_request(0xA5U);
  stop();
  puts("PASS: raw reset retires admission without reclaiming failed-close payload; explicit successful Reset releases ownership and successor configuration recovers");
}

static void test_remote_wake_completion_event(uint32_t ms)
{
  assert(ms == 10U && wake_asserted);
  assert((test_otg.global.GINTMSK & USB_OTG_GINTMSK_WUIM) == 0U);
  test_otg.device.DSTS = 0U;
  pcd_complete(1U);
  assert(wake_asserted && (test_otg.device.DCTL & USB_OTG_DCTL_RWUSIG) != 0U);
  assert(USBD_Device.dev_state == USBD_STATE_CONFIGURED);
}

static void test_pcd_remote_wake_pulse(void)
{
  pcd_configure();
  USBD_Device.dev_remote_wakeup = 1U;
  keyboard_packet(4U);
  keyboard_packet(0U);
  pcd_suspend();
  clock_ms += 5U;
  delay_event = test_remote_wake_completion_event;
  uint32_t before_end = wake_end_count, before_time = clock_ms;
  assert(usbHidRequestRemoteWakeFromInput());
  assert(clock_ms == before_time + 10U && wake_end_count == before_end + 1U && !wake_asserted);
  assert((test_otg.global.GINTMSK & USB_OTG_GINTMSK_WUIM) != 0U);
  assert(active[1] != NULL && active[1][2] == 0U);
  pcd_complete(1U);
  pcd_resume_irq();
  pcd_sof();
  assert(active[1] == NULL && !pcd_resume_pending);
  stop();
  puts("PASS: completion before WKUINT during real HID Remote Wake preserves 10ms signal/deactivation/WUIM and a late duplicate does not replay the queue");
}

int main(int argc, char **argv)
{
  const char *only = argc > 1 ? argv[1] : NULL;
  if (only == NULL || !strcmp(only, "clock")) test_host_resume_clock();
  if (only == NULL || !strcmp(only, "early")) test_host_early_resume();
  if (only == NULL || !strcmp(only, "completion")) test_completions_before_resume();
  if (only == NULL || !strcmp(only, "in")) test_in_before_resume();
  if (only == NULL || !strcmp(only, "setup")) test_setup_before_resume();
  if (only == NULL || !strcmp(only, "endpoints")) test_endpoint_request_bounds();
  if (only == NULL || !strcmp(only, "reset")) test_suspend_reset_enumeration();
  if (only == NULL || !strcmp(only, "pending")) test_pending_wake_boundaries();
  if (only == NULL || !strcmp(only, "configuration")) test_configuration_failures();
  if (only == NULL || !strcmp(only, "false")) test_false_resume();
  if (only == NULL || !strcmp(only, "raw")) test_raw_reset_boundary();
  if (only == NULL || !strcmp(only, "raw-failed")) test_raw_reset_failed_teardown();
  if (only == NULL || !strcmp(only, "wake")) test_pcd_remote_wake_pulse();
  return 0;
}
