#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define __IO volatile
#define UNUSED(x) ((void)(x))
#include "usb_irq_defs.inc"

/* The register layout, constants, IRQ branches, LL readers and endpoint
 * helpers come from production sources. Hardware is deliberately limited to
 * W1C flags, interrupt summaries, one RX status read/pop and FIFO word counts.
 * Injected simultaneous flags do not establish that a particular silicon/bus
 * timing produced that combination in the reported BRICK60 incidents.
 */
static union {
  uint64_t alignment;
  uint8_t bytes[4096];
} registers;

static PCD_HandleTypeDef pcd;
static USB_OTG_GlobalTypeDef *controller;
static uint8_t tx_data[16][128], out_data[16][128], rx_packet[64];
static uint32_t fifo_free[16];
static bool rx_pending, callback_rearm;
static unsigned generation, resets, writes, receives, transmits, rx_reads;
#ifdef TEST_USB_IRQ_BRIDGE
static unsigned inject_reset_on_read, interrupt_reads;
static bool inject_reset_on_rx;
#endif
#ifdef TEST_USB_IRQ_OBSERVE
static unsigned observed_paths;
#endif

typedef struct {
  char kind;
  uint8_t endpoint;
  unsigned generation;
} event_t;
static event_t events[256];
static unsigned event_count;

static USB_OTG_DeviceTypeDef *device(void)
{
  return (USB_OTG_DeviceTypeDef *)(registers.bytes + USB_OTG_DEVICE_BASE);
}

static USB_OTG_INEndpointTypeDef *in_ep(unsigned ep)
{
  return (USB_OTG_INEndpointTypeDef *)(registers.bytes + USB_OTG_IN_ENDPOINT_BASE +
                                     ep * USB_OTG_EP_REG_SIZE);
}

static USB_OTG_OUTEndpointTypeDef *out_ep(unsigned ep)
{
  return (USB_OTG_OUTEndpointTypeDef *)(registers.bytes + USB_OTG_OUT_ENDPOINT_BASE +
                                      ep * USB_OTG_EP_REG_SIZE);
}

static void record(char kind, uint8_t endpoint)
{
  assert(event_count < sizeof(events) / sizeof(events[0]));
  events[event_count++] = (event_t){kind, endpoint, generation};
}

static void model_sync(void)
{
  uint32_t daint = 0U;
  for (unsigned ep = 0U; ep < pcd.Init.dev_endpoints; ep++) {
    /* With the empty threshold selected, TXFE is a level flag when the FIFO
     * is empty. It cannot be acknowledged by writing DIEPINT.TXFE.
     */
    in_ep(ep)->DTXFSTS = fifo_free[ep];
    if (fifo_free[ep] == 64U) in_ep(ep)->DIEPINT |= USB_OTG_DIEPINT_TXFE;
    else in_ep(ep)->DIEPINT &= ~USB_OTG_DIEPINT_TXFE;
    uint32_t in_mask = device()->DIEPMSK |
        (((device()->DIEPEMPMSK >> ep) & 1U) << 7);
    if ((in_ep(ep)->DIEPINT & in_mask) != 0U) daint |= 1U << ep;
    if ((out_ep(ep)->DOEPINT & device()->DOEPMSK) != 0U) daint |= 1U << (ep + 16U);
  }
  device()->DAINT = daint;
  controller->GINTSTS &= ~(USB_OTG_GINTSTS_IEPINT | USB_OTG_GINTSTS_OEPINT |
                           USB_OTG_GINTSTS_RXFLVL);
  if ((daint & device()->DAINTMSK & 0xffffU) != 0U)
    controller->GINTSTS |= USB_OTG_GINTSTS_IEPINT;
  if ((daint & device()->DAINTMSK & 0xffff0000U) != 0U)
    controller->GINTSTS |= USB_OTG_GINTSTS_OEPINT;
  if (rx_pending) controller->GINTSTS |= USB_OTG_GINTSTS_RXFLVL;
}

static uint32_t USB_ReadInterrupts(const USB_OTG_GlobalTypeDef *instance)
{
  assert(instance == controller);
#ifdef TEST_USB_IRQ_BRIDGE
  if (++interrupt_reads == inject_reset_on_read)
    controller->GINTSTS |= USB_OTG_GINTSTS_USBRST;
#endif
  model_sync();
  return instance->GINTSTS & instance->GINTMSK;
}

static uint32_t USB_GetMode(const USB_OTG_GlobalTypeDef *instance)
{
  assert(instance == controller);
  return instance->GINTSTS & 1U;
}

static void model_clear_gint(USB_OTG_GlobalTypeDef *instance, uint32_t bits)
{
  assert(instance == controller);
  instance->GINTSTS &= ~bits;
  model_sync();
}

static void model_clear_otg(USB_OTG_GlobalTypeDef *instance, uint32_t bits)
{
  assert(instance == controller);
  instance->GOTGINT &= ~bits;
}

static void model_clear_in(const USB_OTG_GlobalTypeDef *instance, unsigned ep, uint32_t bits)
{
  assert(instance == controller && ep < pcd.Init.dev_endpoints);
  in_ep(ep)->DIEPINT &= ~(bits & ~USB_OTG_DIEPINT_TXFE);
  model_sync();
}

static void model_clear_out(const USB_OTG_GlobalTypeDef *instance, unsigned ep, uint32_t bits)
{
  assert(instance == controller && ep < pcd.Init.dev_endpoints);
  out_ep(ep)->DOEPINT &= ~bits;
  model_sync();
}

static uint32_t model_rx_pop(USB_OTG_GlobalTypeDef *instance)
{
  assert(instance == controller && rx_pending);
  rx_pending = false;
#ifdef TEST_USB_IRQ_BRIDGE
  if (inject_reset_on_rx) {
    instance->GINTSTS |= USB_OTG_GINTSTS_USBRST;
    inject_reset_on_rx = false;
  }
#endif
  model_sync();
  return instance->GRXSTSP;
}

static void *USB_ReadPacket(const USB_OTG_GlobalTypeDef *instance, uint8_t *dest, uint16_t len)
{
  assert(instance == controller && dest != NULL && len <= sizeof(rx_packet));
  memcpy(dest, rx_packet, len);
  rx_reads++;
  return dest;
}

static HAL_StatusTypeDef USB_WritePacket(const USB_OTG_GlobalTypeDef *instance, uint8_t *src,
                                         uint8_t ep, uint16_t len, uint8_t dma)
{
  assert(instance == controller && dma == 0U && src != NULL && ep < pcd.Init.dev_endpoints);
  assert(len <= pcd.IN_ep[ep].maxpacket && fifo_free[ep] >= (len + 3U) / 4U);
  fifo_free[ep] -= (len + 3U) / 4U;
  writes++;
  record('T', ep);
  model_sync();
  return HAL_OK;
}

static HAL_StatusTypeDef USB_FlushTxFifo(USB_OTG_GlobalTypeDef *instance, uint32_t fifo)
{
  assert(instance == controller && (fifo < pcd.Init.dev_endpoints || fifo == 0x10U));
  for (unsigned ep = 0U; ep < pcd.Init.dev_endpoints; ep++)
    if (fifo == 0x10U || fifo == ep) fifo_free[ep] = 64U;
  record('Q', (uint8_t)fifo);
  model_sync();
  return HAL_OK;
}

static uint32_t HAL_RCC_GetHCLKFreq(void) { return 200000000U; }

static HAL_StatusTypeDef USB_SetTurnaroundTime(USB_OTG_GlobalTypeDef *instance,
                                               uint32_t hclk, uint8_t speed)
{
  assert(instance == controller && hclk == 200000000U);
  assert(speed == USBD_FS_SPEED || speed == USBD_HS_SPEED);
  return HAL_OK;
}

static HAL_StatusTypeDef HAL_PCD_EP_Abort(PCD_HandleTypeDef *hpcd, uint8_t ep)
{
  assert(hpcd == &pcd);
  record('A', ep);
  return HAL_OK;
}

static HAL_StatusTypeDef HAL_PCD_EP_Transmit(PCD_HandleTypeDef *, uint8_t, uint8_t *, uint32_t);
static HAL_StatusTypeDef HAL_PCD_EP_Receive(PCD_HandleTypeDef *, uint8_t, uint8_t *, uint32_t);

#ifndef TEST_USB_IRQ_BRIDGE
static void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t ep)
{
  assert(hpcd == &pcd);
  record('I', ep);
  if (callback_rearm && ep != 0U) {
    assert(HAL_PCD_EP_Transmit(hpcd, (uint8_t)(ep | 0x80U), tx_data[ep], 22U) == HAL_OK);
    transmits++;
  }
}

static void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t ep)
{
  assert(hpcd == &pcd);
  record('O', ep);
  if (callback_rearm && ep != 0U) {
    assert(HAL_PCD_EP_Receive(hpcd, ep, out_data[ep], 32U) == HAL_OK);
    receives++;
  }
}

static void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
{
  assert(hpcd == &pcd);
  record('U', 0U);
}

static void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
{
  assert(hpcd == &pcd);
  /* Generation is a callback observation marker, not a model of USBD/HID
   * teardown. The production bridge invokes USBD_LL_Reset at this boundary.
   */
  generation++;
  resets++;
  record('R', 0U);
}

static void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd) { assert(hpcd == &pcd); record('F', 0U); }
static void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd) { assert(hpcd == &pcd); record('W', 0U); }
static void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd) { assert(hpcd == &pcd); record('S', 0U); }
static void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd) { assert(hpcd == &pcd); record('C', 0U); }
static void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd) { assert(hpcd == &pcd); record('D', 0U); }
static void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t ep) { assert(hpcd == &pcd); record('i', ep); }
static void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t ep) { assert(hpcd == &pcd); record('o', ep); }
#else
/* This mode joins the production wrapper/HAL/PCD bridge. The USBD/HID boundary
 * below records forwarding and admission; the separate PCD fixture executes
 * real USBD/HID teardown and payload ownership with the same bridge source.
 */
typedef enum { USBD_SPEED_HIGH, USBD_SPEED_FULL } USBD_SpeedTypeDef;
typedef enum { USBD_OK, USBD_FAIL } USBD_StatusTypeDef;
enum { USBD_STATE_DEFAULT = 1, USBD_STATE_ADDRESSED, USBD_STATE_CONFIGURED, USBD_STATE_SUSPENDED };
typedef struct {
  uint8_t dev_state, dev_old_state, dev_config, dev_remote_wakeup;
  void *pClassData, *pData;
} USBD_HandleTypeDef;
static USBD_HandleTypeDef USBD_Device;
#include "usb_irq_wake_defs.inc"
static volatile usb_hid_wake_state_t wake_state;
static volatile bool wake_skip_stale_sof;
static volatile uint32_t wake_suspend_epoch, suspend_ms;
static uint32_t transport_generation, clock_ms, irq_mask;
static void (*delay_event)(uint32_t);
static volatile bool pcd_reset_pending;
static bool is_connected, bus_suspended, pcd_resume_pending, pcd_resume_skip_stale_sof;
static bool hid_admission;
static uint32_t sof_count, bridge_pcgcctl;
static unsigned reset_begin_count, resume_count, ungate_count;
#define PCD_SPEED_HIGH USBD_HS_SPEED
#define PCD_SPEED_HIGH_IN_FULL USB_OTG_SPEED_HIGH_IN_FULL
#define PCD_SPEED_FULL USBD_FS_SPEED
static struct { uint32_t SCR; } mock_scb;
#define SCB (&mock_scb)
#define SCB_SCR_SLEEPDEEP_Msk 4U
#define SCB_SCR_SLEEPONEXIT_Msk 2U
#define __HAL_PCD_GATE_PHYCLOCK(h) ((void)(h), bridge_pcgcctl |= USB_OTG_PCGCCTL_STOPCLK)
#define __HAL_PCD_UNGATE_PHYCLOCK(h) ((void)(h), bridge_pcgcctl &= ~USB_OTG_PCGCCTL_STOPCLK, ungate_count++)
#define logPrintf(...) assert(false && "USB callbacks must not enter the shared logger")
static void Error_Handler(void) { assert(false); }
static void usbHidOnBusResetBegin(void)
{
  assert(pcd_reset_pending);
  if (hid_admission) transport_generation++;
  hid_admission = false;
  wake_state = USB_HID_WAKE_IDLE;
  wake_skip_stale_sof = false;
  reset_begin_count++;
  record('B', 0U);
}
bool usbIsResetPending(void);
static bool usbHidAdmissionOpen(void) { return hid_admission && !usbIsResetPending(); }
static uint32_t usbHidLock(void) { uint32_t old = irq_mask; irq_mask = 1U; return old; }
static void usbHidUnlock(uint32_t old) { irq_mask = old; }
static uint32_t millis(void) { return clock_ms; }
static void delay(uint32_t ms);
static USB_OTG_DeviceTypeDef *usbHidDeviceRegisters(PCD_HandleTypeDef *hpcd)
{ assert(hpcd == &pcd); return device(); }
#define __DSB() ((void)0)
HAL_StatusTypeDef HAL_PCD_ActivateRemoteWakeup(PCD_HandleTypeDef *);
HAL_StatusTypeDef HAL_PCD_DeActivateRemoteWakeup(PCD_HandleTypeDef *);
#include "usb_irq_wake.inc"
static USBD_StatusTypeDef USBD_LL_Resume(USBD_HandleTypeDef *d)
{
  assert(d == &USBD_Device && d->dev_state == USBD_STATE_SUSPENDED);
  d->dev_state = d->dev_old_state;
  resume_count++;
  record('W', 0U);
  return USBD_OK;
}
static USBD_StatusTypeDef USBD_LL_Suspend(USBD_HandleTypeDef *d)
{
  assert(d == &USBD_Device);
  if (d->dev_state != USBD_STATE_SUSPENDED) d->dev_old_state = d->dev_state;
  d->dev_state = USBD_STATE_SUSPENDED;
  record('S', 0U);
  return USBD_OK;
}
static USBD_StatusTypeDef USBD_LL_DataInStage(USBD_HandleTypeDef *d, uint8_t ep, uint8_t *data)
{
  assert(d == &USBD_Device);
  (void)data;
  if (d->dev_state != USBD_STATE_CONFIGURED || !hid_admission) return USBD_OK;
  record('I', ep);
  if (callback_rearm && ep != 0U) {
    assert(HAL_PCD_EP_Transmit(&pcd, (uint8_t)(ep | 0x80U), tx_data[ep], 22U) == HAL_OK);
    transmits++;
  }
  return USBD_OK;
}
static USBD_StatusTypeDef USBD_LL_DataOutStage(USBD_HandleTypeDef *d, uint8_t ep, uint8_t *data)
{
  assert(d == &USBD_Device);
  (void)data;
  if (d->dev_state != USBD_STATE_CONFIGURED || !hid_admission) return USBD_OK;
  record('O', ep);
  if (callback_rearm && ep != 0U) {
    assert(HAL_PCD_EP_Receive(&pcd, ep, out_data[ep], 32U) == HAL_OK);
    receives++;
  }
  return USBD_OK;
}
static USBD_StatusTypeDef USBD_LL_SetupStage(USBD_HandleTypeDef *d, uint8_t *setup)
{ assert(d == &USBD_Device); (void)setup; record('U', 0U); return USBD_OK; }
static USBD_StatusTypeDef USBD_LL_SetSpeed(USBD_HandleTypeDef *d, USBD_SpeedTypeDef speed)
{ assert(d == &USBD_Device); (void)speed; return USBD_OK; }
static USBD_StatusTypeDef USBD_LL_Reset(USBD_HandleTypeDef *d)
{
  assert(d == &USBD_Device && pcd_reset_pending && !hid_admission);
  d->dev_state = USBD_STATE_DEFAULT;
  d->dev_config = 0U;
  d->pClassData = NULL;
  generation++;
  resets++;
  record('R', 0U);
  return USBD_OK;
}
static USBD_StatusTypeDef USBD_LL_SOF(USBD_HandleTypeDef *d)
{ assert(d == &USBD_Device); record('F', 0U); return USBD_OK; }
static USBD_StatusTypeDef USBD_LL_DevConnected(USBD_HandleTypeDef *d)
{ assert(d == &USBD_Device); record('C', 0U); return USBD_OK; }
static USBD_StatusTypeDef USBD_LL_DevDisconnected(USBD_HandleTypeDef *d)
{ assert(d == &USBD_Device); record('D', 0U); return USBD_OK; }
static USBD_StatusTypeDef USBD_LL_IsoINIncomplete(USBD_HandleTypeDef *d, uint8_t ep)
{ assert(d == &USBD_Device); record('i', ep); return USBD_OK; }
static USBD_StatusTypeDef USBD_LL_IsoOUTIncomplete(USBD_HandleTypeDef *d, uint8_t ep)
{ assert(d == &USBD_Device); record('o', ep); return USBD_OK; }
#include "usb_irq_bridge.inc"
#if USE_HAL_PCD_REGISTER_CALLBACKS == 1U
#define HAL_PCD_DataInStageCallback PCD_DataInStageCallback
#define HAL_PCD_DataOutStageCallback PCD_DataOutStageCallback
#define HAL_PCD_SetupStageCallback PCD_SetupStageCallback
#define HAL_PCD_ResetCallback PCD_ResetCallback
#define HAL_PCD_SOFCallback PCD_SOFCallback
#define HAL_PCD_ResumeCallback PCD_ResumeCallback
#define HAL_PCD_SuspendCallback PCD_SuspendCallback
#define HAL_PCD_ConnectCallback PCD_ConnectCallback
#define HAL_PCD_DisconnectCallback PCD_DisconnectCallback
#define HAL_PCD_ISOINIncompleteCallback PCD_ISOINIncompleteCallback
#define HAL_PCD_ISOOUTIncompleteCallback PCD_ISOOUTIncompleteCallback
#endif
#endif
static void HAL_PCDEx_LPM_Callback(PCD_HandleTypeDef *hpcd, PCD_LPM_MsgTypeDef msg)
{
  assert(hpcd == &pcd);
  record('L', (uint8_t)msg);
}

#include "usb_irq_functions.inc"
#ifdef TEST_USB_IRQ_BRIDGE
#define hpcd_USB_OTG_HS pcd
#include "usb_irq_wrapper.inc"
#undef hpcd_USB_OTG_HS
#endif

static void reset_fixture(uint8_t speed)
{
  memset(&registers, 0, sizeof(registers));
  memset(&pcd, 0, sizeof(pcd));
  controller = (USB_OTG_GlobalTypeDef *)registers.bytes;
  pcd.Instance = controller;
  pcd.Init.dev_endpoints = 6U;
  pcd.Init.speed = speed;
  pcd.LPM_State = LPM_L0;
  controller->GSNPSID = USB_OTG_CORE_ID_310A;
  controller->GINTMSK = USB_OTG_GINTSTS_USBRST | USB_OTG_GINTSTS_ENUMDNE |
      USB_OTG_GINTSTS_IEPINT | USB_OTG_GINTSTS_OEPINT | USB_OTG_GINTSTS_RXFLVL |
      USB_OTG_GINTSTS_WKUINT | USB_OTG_GINTSTS_USBSUSP | USB_OTG_GINTSTS_SOF;
  device()->DAINTMSK = 0x003f003fU;
  device()->DIEPMSK = USB_OTG_DIEPMSK_XFRCM | USB_OTG_DIEPMSK_EPDM;
  device()->DOEPMSK = USB_OTG_DOEPMSK_XFRCM | USB_OTG_DOEPMSK_STUPM | USB_OTG_DOEPMSK_EPDM;
  device()->DSTS = speed == USBD_FS_SPEED ? DSTS_ENUMSPD_FS_PHY_30MHZ_OR_60MHZ :
                                           DSTS_ENUMSPD_HS_PHY_30MHZ_OR_60MHZ;
  for (unsigned ep = 0U; ep < pcd.Init.dev_endpoints; ep++) {
    fifo_free[ep] = 64U;
    pcd.IN_ep[ep].num = pcd.OUT_ep[ep].num = (uint8_t)ep;
    pcd.IN_ep[ep].is_in = 1U;
    pcd.IN_ep[ep].type = pcd.OUT_ep[ep].type = EP_TYPE_INTR;
    pcd.IN_ep[ep].maxpacket = pcd.OUT_ep[ep].maxpacket = 64U;
    pcd.IN_ep[ep].xfer_buff = tx_data[ep];
    pcd.OUT_ep[ep].xfer_buff = out_data[ep];
    in_ep(ep)->DIEPCTL = USB_OTG_DIEPCTL_USBAEP;
    out_ep(ep)->DOEPCTL = USB_OTG_DOEPCTL_USBAEP;
  }
#if USE_HAL_PCD_REGISTER_CALLBACKS == 1U
  pcd.DataInStageCallback = HAL_PCD_DataInStageCallback;
  pcd.DataOutStageCallback = HAL_PCD_DataOutStageCallback;
  pcd.SetupStageCallback = HAL_PCD_SetupStageCallback;
  pcd.ResetCallback = HAL_PCD_ResetCallback;
  pcd.SOFCallback = HAL_PCD_SOFCallback;
  pcd.ResumeCallback = HAL_PCD_ResumeCallback;
  pcd.SuspendCallback = HAL_PCD_SuspendCallback;
  pcd.ConnectCallback = HAL_PCD_ConnectCallback;
  pcd.DisconnectCallback = HAL_PCD_DisconnectCallback;
  pcd.ISOINIncompleteCallback = HAL_PCD_ISOINIncompleteCallback;
  pcd.ISOOUTIncompleteCallback = HAL_PCD_ISOOUTIncompleteCallback;
  pcd.LPMCallback = HAL_PCDEx_LPM_Callback;
#endif
  generation = 7U;
  resets = writes = receives = transmits = rx_reads = event_count = 0U;
  rx_pending = callback_rearm = false;
#ifdef TEST_USB_IRQ_BRIDGE
  memset(&USBD_Device, 0, sizeof(USBD_Device));
  USBD_Device.dev_state = USBD_STATE_CONFIGURED;
  USBD_Device.dev_config = 1U;
  USBD_Device.pClassData = tx_data;
  USBD_Device.pData = &pcd;
  USBD_Device.dev_remote_wakeup = 1U;
  pcd.pData = &USBD_Device;
  pcd_reset_pending = false;
  pcd_resume_pending = pcd_resume_skip_stale_sof = bus_suspended = false;
  is_connected = hid_admission = true;
  sof_count = mock_scb.SCR = 0U;
  reset_begin_count = resume_count = ungate_count = 0U;
  inject_reset_on_read = interrupt_reads = 0U;
  inject_reset_on_rx = false;
  bridge_pcgcctl = USB_OTG_PCGCCTL_GATECLK;
  transport_generation = generation;
  wake_state = USB_HID_WAKE_IDLE;
  wake_skip_stale_sof = false;
  wake_suspend_epoch = suspend_ms = irq_mask = 0U;
  clock_ms = 100U;
  delay_event = NULL;
#endif
  model_sync();
}

static unsigned find_event(char kind)
{
  for (unsigned i = 0U; i < event_count; i++) if (events[i].kind == kind) return i;
  return event_count;
}

static void show_events(const char *case_name)
{
  printf("%s:", case_name);
  for (unsigned i = 0U; i < event_count; i++)
    printf(" %c%u@g%u", events[i].kind, events[i].endpoint, events[i].generation);
  putchar('\n');
}

static void ownership_check(bool observed, bool valid, const char *failure)
{
#ifdef TEST_USB_IRQ_OBSERVE
  (void)valid;
  if (!observed) {
    printf("NOT OBSERVED under this fixture/source: %s\n", failure);
    return;
  }
  observed_paths++;
  printf("CONDITIONAL SOURCE PATH: %s\n", failure);
#else
  (void)observed;
  if (valid) return;
  fprintf(stderr, "FAIL: %s\n", failure);
  assert(valid);
#endif
}

static void test_normal_irq(uint8_t speed)
{
  reset_fixture(speed);
  HAL_PCD_IRQHandler(&pcd);
  assert(event_count == 0U);
  in_ep(1U)->DIEPINT |= USB_OTG_DIEPINT_XFRC;
  out_ep(4U)->DOEPINT |= USB_OTG_DOEPINT_XFRC;
  out_ep(0U)->DOEPINT |= USB_OTG_DOEPINT_STUP | USB_OTG_DOEPINT_STPKTRX;
  controller->GINTSTS |= USB_OTG_GINTSTS_SOF | USB_OTG_GINTSTS_WKUINT;
  HAL_PCD_IRQHandler(&pcd);
  assert(find_event('U') < find_event('O') && find_event('O') < find_event('I'));
  assert(find_event('I') < find_event('W') && find_event('W') < find_event('F'));
  assert(event_count == 5U && generation == 7U);
  assert(USB_ReadInterrupts(controller) == 0U);
  HAL_PCD_IRQHandler(&pcd);
  assert(event_count == 5U);
}

static void test_masked_and_w1c(uint8_t speed)
{
  reset_fixture(speed);
  device()->DAINTMSK = 0x10001U;
  in_ep(1U)->DIEPINT |= USB_OTG_DIEPINT_XFRC | USB_OTG_DIEPINT_EPDISD;
  out_ep(4U)->DOEPINT |= USB_OTG_DOEPINT_XFRC;
  HAL_PCD_IRQHandler(&pcd);
  assert(event_count == 0U);
  assert((in_ep(1U)->DIEPINT & USB_OTG_DIEPINT_XFRC) != 0U);
  assert((out_ep(4U)->DOEPINT & USB_OTG_DOEPINT_XFRC) != 0U);
  model_clear_in(controller, 1U, USB_OTG_DIEPINT_XFRC | USB_OTG_DIEPINT_TXFE);
  assert((in_ep(1U)->DIEPINT & USB_OTG_DIEPINT_XFRC) == 0U);
  assert((in_ep(1U)->DIEPINT & USB_OTG_DIEPINT_EPDISD) != 0U);
  assert((in_ep(1U)->DIEPINT & USB_OTG_DIEPINT_TXFE) != 0U);
  model_clear_out(controller, 4U, USB_OTG_DOEPINT_XFRC);
  assert((out_ep(4U)->DOEPINT & USB_OTG_DOEPINT_XFRC) == 0U);
  HAL_PCD_IRQHandler(&pcd);
  assert(event_count == 0U);
}

static void test_raw_reset_empty_mask(uint8_t speed)
{
  reset_fixture(speed);
  assert(HAL_PCD_EP_Transmit(&pcd, 0x81U, tx_data[1U], 22U) == HAL_OK);
  /* Inject the reset snapshot with EPENA already inactive; whether TXFE
   * remains a level in this state on H7RS is an explicit model assumption.
   */
  in_ep(1U)->DIEPCTL &= ~USB_OTG_DIEPCTL_EPENA;
  fifo_free[1U] = 0U;
  controller->GINTSTS |= USB_OTG_GINTSTS_USBRST;
  device()->DCFG |= 11U << 4;
  device()->DCTL |= USB_OTG_DCTL_RWUSIG;
  in_ep(1U)->DIEPCTL |= USB_OTG_DIEPCTL_STALL;
  out_ep(4U)->DOEPCTL |= USB_OTG_DOEPCTL_STALL;
  HAL_PCD_IRQHandler(&pcd);
  assert(resets == 0U && generation == 7U && writes == 0U);
  assert((device()->DCFG & USB_OTG_DCFG_DAD) == 0U);
  assert((device()->DCTL & USB_OTG_DCTL_RWUSIG) == 0U);
  assert((in_ep(1U)->DIEPCTL & USB_OTG_DIEPCTL_STALL) == 0U);
  assert((out_ep(4U)->DOEPCTL & USB_OTG_DOEPCTL_STALL) == 0U);
  assert((out_ep(4U)->DOEPCTL & USB_OTG_DOEPCTL_SNAK) != 0U);
  uint32_t remaining_empty_mask = device()->DIEPEMPMSK;
  HAL_PCD_IRQHandler(&pcd);
  show_events("raw USBRST then separate IRQ before ENUMDNE");
  printf("raw reset snapshot: DIEPEMPMSK=0x%08lx, EP1 EPENA=%u, old FIFO writes=%u\n",
         (unsigned long)remaining_empty_mask,
         (unsigned)((in_ep(1U)->DIEPCTL & USB_OTG_DIEPCTL_EPENA) != 0U), writes);
  unsigned tx = find_event('T');
  ownership_check(tx < event_count && events[tx].generation == 7U,
                  remaining_empty_mask == 0U && writes == 0U,
                  "raw USBRST retains the old empty-FIFO mask and the next IRQ reloads old IN bytes");
  controller->GINTSTS |= USB_OTG_GINTSTS_ENUMDNE;
  HAL_PCD_IRQHandler(&pcd);
  assert(resets == 1U && generation == 8U && pcd.Init.speed == speed);
}

static void test_reset_simultaneous_completion(uint8_t speed)
{
  reset_fixture(speed);
  callback_rearm = true;
  in_ep(1U)->DIEPINT |= USB_OTG_DIEPINT_XFRC;
  out_ep(4U)->DOEPINT |= USB_OTG_DOEPINT_XFRC;
  controller->GINTSTS |= USB_OTG_GINTSTS_USBRST | USB_OTG_GINTSTS_ENUMDNE;
  HAL_PCD_IRQHandler(&pcd);
  show_events("USBRST+ENUMDNE+IN/OUT XFRC");
  assert(resets == 1U && generation == 8U);
  unsigned reset = find_event('R'), in = find_event('I'), out = find_event('O');
  bool old_completion = (in < reset && events[in].generation == 7U) ||
                        (out < reset && events[out].generation == 7U);
  printf("completion callback rearms: IN=%u OUT=%u\n", transmits, receives);
  ownership_check(old_completion && (transmits != 0U || receives != 0U),
                  !old_completion,
                  "old IN/OUT completions are dispatched and rearm endpoints before the reset callback");
}

static void test_enumeration_simultaneous_setup(uint8_t speed)
{
  reset_fixture(speed);
  controller->GINTSTS |= USB_OTG_GINTSTS_USBRST;
  HAL_PCD_IRQHandler(&pcd);
  event_count = 0U;
  memset(rx_packet, 0x5a, 8U);
  controller->GRXSTSP = (STS_SETUP_UPDT << 17) | (8U << 4);
  rx_pending = true;
  out_ep(0U)->DOEPINT |= USB_OTG_DOEPINT_STUP | USB_OTG_DOEPINT_STPKTRX;
  controller->GINTSTS |= USB_OTG_GINTSTS_ENUMDNE;
  HAL_PCD_IRQHandler(&pcd);
  show_events("ENUMDNE+SETUP RX/STUP");
  assert(rx_reads == 1U && memcmp(pcd.Setup, rx_packet, 8U) == 0);
  assert(resets == 1U && generation == 8U);
  unsigned setup = find_event('U'), reset = find_event('R');
  ownership_check(setup < reset && events[setup].generation == 7U,
                  setup < event_count && reset < setup && events[setup].generation == 8U,
                  "the new SETUP is forwarded under the old generation before ENUMDNE resets the stack");
}

#ifdef TEST_USB_IRQ_BRIDGE
static void delay(uint32_t ms)
{
  assert(irq_mask == 0U);
  if (ms == 10U) {
    assert((device()->DCTL & USB_OTG_DCTL_RWUSIG) != 0U);
    assert((controller->GINTMSK & USB_OTG_GINTMSK_WUIM) == 0U);
    /* Model the device-generated early wake flag without changing the HAL's
     * production mask test or the HID's production 10ms delay. */
    controller->GINTSTS |= USB_OTG_GINTSTS_WKUINT;
    OTG_HS_IRQHandler();
    assert((device()->DCTL & USB_OTG_DCTL_RWUSIG) != 0U);
  }
  if (delay_event) {
    void (*event)(uint32_t) = delay_event;
    delay_event = NULL;
    event(ms);
  }
  clock_ms += ms;
}

static void bridge_suspend(void)
{
  device()->DSTS |= USB_OTG_DSTS_SUSPSTS;
  controller->GINTSTS |= USB_OTG_GINTSTS_USBSUSP;
  OTG_HS_IRQHandler();
  assert(USBD_Device.dev_state == USBD_STATE_SUSPENDED && bus_suspended);
  assert((bridge_pcgcctl & USB_OTG_PCGCCTL_STOPCLK) != 0U);
}

static void bridge_active(void)
{
  device()->DSTS &= ~USB_OTG_DSTS_SUSPSTS;
}

static void bridge_new_configuration(void)
{
  assert(USBD_Device.dev_state == USBD_STATE_DEFAULT && !pcd_reset_pending);
  USBD_Device.dev_state = USBD_STATE_CONFIGURED;
  USBD_Device.dev_config = 1U;
  USBD_Device.dev_remote_wakeup = 1U;
  USBD_Device.pClassData = tx_data;
  hid_admission = is_connected = true;
  transport_generation++;
}

static void test_bridge_normal(uint8_t speed)
{
  reset_fixture(speed);
  bridge_suspend();
  bridge_active();
  callback_rearm = true;
  in_ep(1U)->DIEPINT |= USB_OTG_DIEPINT_XFRC;
  out_ep(4U)->DOEPINT |= USB_OTG_DOEPINT_XFRC;
  controller->GINTSTS |= USB_OTG_GINTSTS_WKUINT;
  model_sync();
  OTG_HS_IRQHandler();
  assert(resume_count == 1U && transmits == 1U && receives == 1U && reset_begin_count == 0U);
  assert(find_event('W') < find_event('O') && find_event('O') < find_event('I'));
  assert(USBD_Device.dev_state == USBD_STATE_CONFIGURED && !bus_suspended);
  assert((bridge_pcgcctl & USB_OTG_PCGCCTL_STOPCLK) == 0U);
  assert((bridge_pcgcctl & USB_OTG_PCGCCTL_GATECLK) != 0U);
  reset_fixture(speed);
  bridge_suspend();
  controller->GINTSTS |= USB_OTG_GINTSTS_WKUINT | USB_OTG_GINTSTS_SOF;
  OTG_HS_IRQHandler();
  assert(pcd_resume_pending && USBD_Device.dev_state == USBD_STATE_SUSPENDED && resume_count == 0U);
  bridge_active();
  controller->GINTSTS |= USB_OTG_GINTSTS_SOF;
  OTG_HS_IRQHandler();
  assert(resume_count == 1U && USBD_Device.dev_state == USBD_STATE_CONFIGURED && !pcd_resume_pending);
  controller->GINTSTS |= USB_OTG_GINTSTS_WKUINT;
  OTG_HS_IRQHandler();
  assert(resume_count == 1U);
  puts("PASS: actual wrapper/HAL/bridge preserves endpoint-before-WKUINT, early-WKUINT/fresh-SOF and late duplicate Resume");
}

static void test_bridge_reset_entry(uint8_t speed)
{
  for (unsigned suspended = 0U; suspended < 2U; suspended++) {
    reset_fixture(speed);
    if (suspended) bridge_suspend();
    uint8_t state = USBD_Device.dev_state;
    PCD_EPTypeDef in_image[16], out_image[16];
    memcpy(in_image, pcd.IN_ep, sizeof(in_image));
    memcpy(out_image, pcd.OUT_ep, sizeof(out_image));
    bridge_active();
    controller->GINTSTS |= USB_OTG_GINTSTS_USBRST;
    usbPcdOnIrqEntry(&pcd);
    assert(pcd_reset_pending && reset_begin_count == 1U && !hid_admission && !usbIsConnect());
    assert(!memcmp(in_image, pcd.IN_ep, sizeof(in_image)) && !memcmp(out_image, pcd.OUT_ep, sizeof(out_image)));
    assert(USBD_Device.dev_state == state && (bridge_pcgcctl & USB_OTG_PCGCCTL_STOPCLK) == 0U);
    callback_rearm = true;
    in_ep(1U)->DIEPINT |= USB_OTG_DIEPINT_XFRC;
    out_ep(4U)->DOEPINT |= USB_OTG_DOEPINT_XFRC;
    out_ep(0U)->DOEPINT |= USB_OTG_DOEPINT_STUP | USB_OTG_DOEPINT_STPKTRX;
    controller->GINTSTS |= USB_OTG_GINTSTS_WKUINT | USB_OTG_GINTSTS_SOF;
    model_sync();
    OTG_HS_IRQHandler();
    assert(pcd_reset_pending && reset_begin_count == 1U && resets == 0U && resume_count == 0U);
    assert(find_event('B') < find_event('Q'));
    assert(find_event('I') == event_count && find_event('O') == event_count && find_event('U') == event_count && find_event('F') == event_count);
    assert(transmits == 0U && receives == 0U && USBD_Device.dev_state == state);
    assert((controller->GINTSTS & (USB_OTG_GINTSTS_USBRST | USB_OTG_GINTSTS_ENUMDNE)) == 0U);
    /* A separate endpoint IRQ in the consumed-bit gap is still blocked. */
    in_ep(1U)->DIEPINT |= USB_OTG_DIEPINT_XFRC;
    out_ep(4U)->DOEPINT |= USB_OTG_DOEPINT_XFRC;
    controller->GINTSTS |= USB_OTG_GINTSTS_SOF | USB_OTG_GINTSTS_WKUINT;
    model_sync();
    OTG_HS_IRQHandler();
    assert(pcd_reset_pending && reset_begin_count == 1U && transmits == 0U && receives == 0U && resume_count == 0U);
    /* HAL still handles STUP before ENUMDNE. The barrier intentionally drops
     * this coincident SETUP; only a later fresh SETUP reaches the new stack. */
    out_ep(0U)->DOEPINT |= USB_OTG_DOEPINT_STUP | USB_OTG_DOEPINT_STPKTRX;
    controller->GINTSTS |= USB_OTG_GINTSTS_ENUMDNE;
    model_sync();
    OTG_HS_IRQHandler();
    assert(!pcd_reset_pending && resets == 1U && generation == 8U && USBD_Device.dev_state == USBD_STATE_DEFAULT);
    assert(find_event('U') == event_count);
    out_ep(0U)->DOEPINT |= USB_OTG_DOEPINT_STUP | USB_OTG_DOEPINT_STPKTRX;
    model_sync();
    OTG_HS_IRQHandler();
    assert(find_event('R') < find_event('U') && events[find_event('U')].generation == 8U);
    bridge_new_configuration();
    in_ep(1U)->DIEPINT |= USB_OTG_DIEPINT_XFRC;
    out_ep(4U)->DOEPINT |= USB_OTG_DOEPINT_XFRC;
    model_sync();
    OTG_HS_IRQHandler();
    assert(transmits == 1U && receives == 1U && usbIsConnect());
  }
  puts("PASS: actual wrapper/HAL latches raw USBRST before old callbacks, persists across zero-bit gap, drops coincident ENUMDNE/STUP and admits fresh successor callbacks");
}

static void test_bridge_late_reset(uint8_t speed)
{
  reset_fixture(speed);
  bridge_suspend();
  controller->GINTSTS |= USB_OTG_GINTSTS_WKUINT;
  OTG_HS_IRQHandler();
  assert(pcd_resume_pending);
  bridge_active();
  /* Reset arrives only when HAL checks USBRST, after entry and endpoint
   * handling. An entry-only observer must fail this counterexample. */
  interrupt_reads = 0U;
  inject_reset_on_read = 9U;
  controller->GINTSTS |= USB_OTG_GINTSTS_SOF;
  OTG_HS_IRQHandler();
  assert(pcd_reset_pending && reset_begin_count == 1U && !pcd_resume_pending && resume_count == 0U);
  assert((controller->GINTSTS & USB_OTG_GINTSTS_USBRST) == 0U);
  reset_fixture(speed);
  bridge_suspend();
  bridge_active();
  controller->GRXSTSP = (STS_SETUP_UPDT << 17) | (8U << 4);
  memset(rx_packet, 0x5A, 8U);
  rx_pending = inject_reset_on_rx = true;
  out_ep(0U)->DOEPINT |= USB_OTG_DOEPINT_STUP | USB_OTG_DOEPINT_STPKTRX;
  model_sync();
  OTG_HS_IRQHandler();
  assert(rx_reads == 1U && pcd_reset_pending && reset_begin_count == 1U && find_event('U') == event_count);
  puts("PASS: USBRST arriving after wrapper entry is captured by live callback observation or the actual HAL raw-reset notification");
}

static void test_bridge_masked_host(uint8_t speed)
{
  reset_fixture(speed);
  bridge_suspend();
  controller->GINTMSK &= ~USB_OTG_GINTSTS_USBRST;
  controller->GINTSTS |= USB_OTG_GINTSTS_USBRST;
  OTG_HS_IRQHandler();
  assert(!pcd_reset_pending && reset_begin_count == 0U && hid_admission);
  controller->GINTMSK |= USB_OTG_GINTSTS_USBRST;
  controller->GINTSTS |= USB_OTG_GINTSTS_CMOD;
  OTG_HS_IRQHandler();
  assert(!pcd_reset_pending && reset_begin_count == 0U && hid_admission);
  puts("PASS: masked USBRST and host mode cannot retire device admission");
}

static void normal_pulse_completion(uint32_t ms)
{
  assert(ms == 10U);
  bridge_active();
  in_ep(1U)->DIEPINT |= USB_OTG_DIEPINT_XFRC;
  model_sync();
  OTG_HS_IRQHandler();
  assert(resume_count == 1U && transmits == 1U && (device()->DCTL & USB_OTG_DCTL_RWUSIG) != 0U);
}

static void reset_pulse_successor(uint32_t ms)
{
  assert(ms == 10U);
  bridge_active();
  controller->GINTSTS |= USB_OTG_GINTSTS_USBRST | USB_OTG_GINTSTS_ENUMDNE;
  OTG_HS_IRQHandler();
  assert(resets == 1U && !pcd_reset_pending && !hid_admission);
  assert((device()->DCTL & USB_OTG_DCTL_RWUSIG) == 0U && (controller->GINTMSK & USB_OTG_GINTMSK_WUIM) == 0U);
  bridge_new_configuration();
  bridge_suspend();
  /* This marker belongs to a successor epoch. An old HID pulse must leave it
   * alone, while restoring only its saved WUIM on the same PCD instance. */
  device()->DCTL |= USB_OTG_DCTL_RWUSIG;
}

static void test_bridge_remote_wake(uint8_t speed)
{
  reset_fixture(speed);
  bridge_suspend();
  clock_ms += 5U;
  callback_rearm = true;
  delay_event = normal_pulse_completion;
  uint32_t before = clock_ms;
  assert(usbHidRequestRemoteWakeFromInput());
  assert(clock_ms == before + 10U && (device()->DCTL & USB_OTG_DCTL_RWUSIG) == 0U);
  assert((controller->GINTMSK & USB_OTG_GINTMSK_WUIM) != 0U);
  OTG_HS_IRQHandler();
  assert(resume_count == 1U);
  reset_fixture(speed);
  bridge_suspend();
  clock_ms += 5U;
  delay_event = reset_pulse_successor;
  assert(usbHidRequestRemoteWakeFromInput());
  assert(USBD_Device.dev_state == USBD_STATE_SUSPENDED && (device()->DCTL & USB_OTG_DCTL_RWUSIG) != 0U);
  assert((controller->GINTMSK & USB_OTG_GINTMSK_WUIM) != 0U);
  bridge_active();
  controller->GINTSTS |= USB_OTG_GINTSTS_WKUINT;
  OTG_HS_IRQHandler();
  assert(USBD_Device.dev_state == USBD_STATE_CONFIGURED && resume_count == 1U && (device()->DCTL & USB_OTG_DCTL_RWUSIG) == 0U);
  puts("PASS: production HID pulse plus actual wrapper/HAL keeps normal 10ms timing, preserves a reset successor signal, restores WUIM and accepts successor host Resume");
}
#endif

int main(void)
{
  const uint8_t speeds[] = {USBD_FS_SPEED, USBD_HS_SPEED};
  for (unsigned i = 0U; i < sizeof(speeds) / sizeof(speeds[0]); i++) {
    printf("IRQ fixture speed: %s\n", speeds[i] == USBD_FS_SPEED ? "FS" : "HS");
#ifdef TEST_USB_IRQ_BRIDGE
    test_bridge_normal(speeds[i]);
    test_bridge_reset_entry(speeds[i]);
    test_bridge_late_reset(speeds[i]);
    test_bridge_masked_host(speeds[i]);
    test_bridge_remote_wake(speeds[i]);
#else
    test_normal_irq(speeds[i]);
    test_masked_and_w1c(speeds[i]);
    test_raw_reset_empty_mask(speeds[i]);
    test_reset_simultaneous_completion(speeds[i]);
    test_enumeration_simultaneous_setup(speeds[i]);
#endif
  }
#ifdef TEST_USB_IRQ_BRIDGE
  printf("PASS: actual IRQ wrapper/HAL/PCD reset admission and production HID wake, FS/HS, callback mode %u; register model only\n", USE_HAL_PCD_REGISTER_CALLBACKS);
#elif defined(TEST_USB_IRQ_OBSERVE)
  printf("SOURCE CHECKS PASS: normal IRQ dispatch, masked summaries, W1C acknowledgements, RX pop and reset address/stall handling; callback mode %u\n",
         USE_HAL_PCD_REGISTER_CALLBACKS);
  printf("OBSERVATIONS ONLY: %u conditional ownership paths across FS/HS; not hardware/stability acceptance\n",
         observed_paths);
#else
  puts("PASS: HAL reset IRQ blocks old endpoint work and ENUMDNE precedes new SETUP delivery (FS/HS)");
#endif
  return 0;
}
