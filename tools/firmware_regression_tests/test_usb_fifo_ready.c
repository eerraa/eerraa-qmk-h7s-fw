#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define __IO volatile
#define UNUSED(x) ((void)(x))
#include "usb_irq_defs.inc"
#include "usb_fifo_ready_defs.inc"
#include "hid_tx_queue.h"

/* Actual LL arm/write, FIFO refill, HAL IRQ and bridge bodies execute below.
 * The fixture models register flags/word stores and an explicitly completed
 * transfer. It does not infer host ACK, reset quiescence or target elapsed time.
 */
static union { uint64_t alignment; uint8_t bytes[4096]; } registers;
static PCD_HandleTypeDef pcd;
typedef struct { void *pData; } USBD_HandleTypeDef;
static USBD_HandleTypeDef stack;
static USB_OTG_GlobalTypeDef *controller;
static uint32_t irq_mask, fifo_free[16], fifo_capacity[16], fifo_word[16];
static unsigned fifo_record[16], writes, callbacks, reset_begins, resets, flushes;
static unsigned aborts, closes, endpoint_flushes;
static bool rx_pending, queue_callback, callback_fifo_short, require_mask;
static HAL_StatusTypeDef abort_status, close_status, flush_status;
static uint8_t backing[16][128];
static hid_tx_queue_t queue;
static hid_tx_packet_t slots[4];

typedef struct {
  uint8_t endpoint;
  uint16_t length;
  const uint8_t *source;
  uint8_t data[64];
} packet_write_t;
static packet_write_t written[256];

static USB_OTG_DeviceTypeDef *device(void)
{ return (USB_OTG_DeviceTypeDef *)(registers.bytes + USB_OTG_DEVICE_BASE); }
static USB_OTG_INEndpointTypeDef *in_ep(unsigned ep)
{ return (USB_OTG_INEndpointTypeDef *)(registers.bytes + USB_OTG_IN_ENDPOINT_BASE + ep * USB_OTG_EP_REG_SIZE); }
static USB_OTG_OUTEndpointTypeDef *out_ep(unsigned ep)
{ return (USB_OTG_OUTEndpointTypeDef *)(registers.bytes + USB_OTG_OUT_ENDPOINT_BASE + ep * USB_OTG_EP_REG_SIZE); }

static uint32_t __get_PRIMASK(void) { return irq_mask; }
static void __disable_irq(void) { irq_mask = 1U; }
static void __set_PRIMASK(uint32_t value) { irq_mask = value; }
static uint32_t unaligned_read(const uint8_t *src)
{ uint32_t word; memcpy(&word, src, sizeof(word)); return word; }
#define __UNALIGNED_UINT32_READ(src) unaligned_read((src))

static void model_sync(void)
{
  uint32_t summary = 0U;
  for (unsigned ep = 0U; ep < pcd.Init.dev_endpoints; ep++) {
    in_ep(ep)->DTXFSTS = fifo_free[ep];
    if (fifo_free[ep] == fifo_capacity[ep]) in_ep(ep)->DIEPINT |= USB_OTG_DIEPINT_TXFE;
    else in_ep(ep)->DIEPINT &= ~USB_OTG_DIEPINT_TXFE;
    uint32_t mask = device()->DIEPMSK | (((device()->DIEPEMPMSK >> ep) & 1U) << 7);
    if ((in_ep(ep)->DIEPINT & mask) != 0U) summary |= 1U << ep;
    if ((out_ep(ep)->DOEPINT & device()->DOEPMSK) != 0U) summary |= 1U << (ep + 16U);
  }
  device()->DAINT = summary;
  controller->GINTSTS &= ~(USB_OTG_GINTSTS_IEPINT | USB_OTG_GINTSTS_OEPINT | USB_OTG_GINTSTS_RXFLVL);
  if ((summary & device()->DAINTMSK & 0xffffU) != 0U) controller->GINTSTS |= USB_OTG_GINTSTS_IEPINT;
  if ((summary & device()->DAINTMSK & 0xffff0000U) != 0U) controller->GINTSTS |= USB_OTG_GINTSTS_OEPINT;
  if (rx_pending) controller->GINTSTS |= USB_OTG_GINTSTS_RXFLVL;
}

static uint32_t USB_ReadInterrupts(const USB_OTG_GlobalTypeDef *instance)
{ assert(instance == controller); model_sync(); return instance->GINTSTS & instance->GINTMSK; }
static uint32_t USB_GetMode(const USB_OTG_GlobalTypeDef *instance)
{ assert(instance == controller); return instance->GINTSTS & 1U; }
static void model_clear_gint(USB_OTG_GlobalTypeDef *instance, uint32_t bits)
{ assert(instance == controller); instance->GINTSTS &= ~bits; model_sync(); }
static void model_clear_otg(USB_OTG_GlobalTypeDef *instance, uint32_t bits)
{ assert(instance == controller); instance->GOTGINT &= ~bits; }
static void model_clear_in(const USB_OTG_GlobalTypeDef *instance, unsigned ep, uint32_t bits)
{ assert(instance == controller); in_ep(ep)->DIEPINT &= ~(bits & ~USB_OTG_DIEPINT_TXFE); model_sync(); }
static void model_clear_out(const USB_OTG_GlobalTypeDef *instance, unsigned ep, uint32_t bits)
{ assert(instance == controller); out_ep(ep)->DOEPINT &= ~bits; model_sync(); }
static uint32_t model_rx_pop(USB_OTG_GlobalTypeDef *instance)
{ assert(instance == controller && rx_pending); rx_pending = false; model_sync(); return instance->GRXSTSP; }
static void *USB_ReadPacket(const USB_OTG_GlobalTypeDef *instance, uint8_t *dest, uint16_t len)
{ assert(instance == controller && dest != NULL); memset(dest, 0, len); return dest; }

static void model_fifo_write(uintptr_t base, unsigned ep, uint32_t value)
{
  assert(base == (uintptr_t)controller && ep < pcd.Init.dev_endpoints && fifo_free[ep] != 0U);
  assert((in_ep(ep)->DIEPCTL & USB_OTG_DIEPCTL_EPENA) != 0U);
  assert((in_ep(ep)->DIEPCTL & USB_OTG_DIEPCTL_CNAK) != 0U);
  if (require_mask) assert(irq_mask == 1U);
  PCD_EPTypeDef *descriptor = &pcd.IN_ep[ep];
  uint32_t length = descriptor->xfer_len - descriptor->xfer_count;
  if (length > descriptor->maxpacket) length = descriptor->maxpacket;
  assert(length > 0U && length <= sizeof(written[0].data));
  if (fifo_word[ep] == 0U) {
    assert(writes < sizeof(written) / sizeof(written[0]));
    fifo_record[ep] = writes++;
    written[fifo_record[ep]].endpoint = (uint8_t)ep;
    written[fifo_record[ep]].length = (uint16_t)length;
    written[fifo_record[ep]].source = descriptor->xfer_buff;
  }
  packet_write_t *packet = &written[fifo_record[ep]];
  memcpy(&packet->data[fifo_word[ep] * 4U], &value, sizeof(value));
  fifo_word[ep]++;
  if (fifo_word[ep] == (length + 3U) / 4U) fifo_word[ep] = 0U;
  fifo_free[ep]--;
  model_sync();
}

static HAL_StatusTypeDef USB_FlushTxFifo(USB_OTG_GlobalTypeDef *instance, uint32_t fifo)
{
  assert(instance == controller && (fifo < pcd.Init.dev_endpoints || fifo == 0x10U));
  for (unsigned ep = 0U; ep < pcd.Init.dev_endpoints; ep++)
    if (fifo == 0x10U || fifo == ep) fifo_free[ep] = fifo_capacity[ep];
  flushes++;
  model_sync();
  return HAL_OK;
}
static uint32_t HAL_RCC_GetHCLKFreq(void) { return 200000000U; }
static HAL_StatusTypeDef USB_SetTurnaroundTime(USB_OTG_GlobalTypeDef *instance, uint32_t hclk, uint8_t speed)
{ assert(instance == controller && hclk == 200000000U); assert(speed == USBD_FS_SPEED || speed == USBD_HS_SPEED); return HAL_OK; }
static HAL_StatusTypeDef HAL_PCD_EP_Flush(PCD_HandleTypeDef *handle, uint8_t ep);
static HAL_StatusTypeDef HAL_PCD_EP_Abort(PCD_HandleTypeDef *handle, uint8_t ep)
{
  assert(handle == &pcd); aborts++;
  if (abort_status == HAL_OK) in_ep(ep & EP_ADDR_MSK)->DIEPCTL &= ~USB_OTG_DIEPCTL_EPENA;
  return abort_status;
}
static HAL_StatusTypeDef HAL_PCD_EP_Close(PCD_HandleTypeDef *handle, uint8_t ep)
{
  assert(handle == &pcd);
  uint32_t mask = irq_mask;
  irq_mask = 1U;
  unsigned epnum = ep & EP_ADDR_MSK;
  device()->DIEPEMPMSK &= ~(1U << epnum);
  HAL_StatusTypeDef ret = HAL_PCD_EP_Abort(handle, ep);
  if (ret == HAL_OK) { closes++; ret = close_status; }
  if (ret == HAL_OK) ret = HAL_PCD_EP_Flush(handle, ep);
  if (ret == HAL_OK) {
    in_ep(epnum)->DIEPCTL &= ~USB_OTG_DIEPCTL_USBAEP;
    model_clear_in(controller, epnum, in_ep(epnum)->DIEPINT);
    pcd.IN_ep[epnum].xfer_buff = NULL;
    pcd.IN_ep[epnum].xfer_len = pcd.IN_ep[epnum].xfer_count = 0U;
  }
  irq_mask = mask;
  return ret;
}
static HAL_StatusTypeDef HAL_PCD_EP_Flush(PCD_HandleTypeDef *handle, uint8_t ep)
{
  assert(handle == &pcd); endpoint_flushes++;
  if (flush_status == HAL_OK) return USB_FlushTxFifo(controller, ep & EP_ADDR_MSK);
  return flush_status;
}

static bool arm_queue(void *, const hid_tx_packet_t *);
static void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *handle, uint8_t ep)
{
  assert(handle == &pcd); callbacks++;
  if (queue_callback && ep == (HID_EPIN_ADDR & EP_ADDR_MSK)) {
    assert((in_ep(ep)->DIEPINT & USB_OTG_DIEPINT_XFRC) == 0U);
    assert(hidTxComplete(&queue));
    if (callback_fifo_short) fifo_free[ep] = 0U;
    model_sync();
    (void)hidTxKick(&queue, arm_queue, NULL);
  }
}
static void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *handle, uint8_t ep) { assert(handle == &pcd); (void)ep; }
static void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *handle) { assert(handle == &pcd); }
static void HAL_PCD_ResetBeginCallback(PCD_HandleTypeDef *handle) { assert(handle == &pcd); reset_begins++; }
static void HAL_PCD_ResetCallback(PCD_HandleTypeDef *handle) { assert(handle == &pcd); resets++; }
static void HAL_PCD_SOFCallback(PCD_HandleTypeDef *handle) { assert(handle == &pcd); }
static void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *handle) { assert(handle == &pcd); }
static void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *handle) { assert(handle == &pcd); }
static void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *handle) { assert(handle == &pcd); }
static void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *handle) { assert(handle == &pcd); }
static void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *handle, uint8_t ep) { assert(handle == &pcd); (void)ep; }
static void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *handle, uint8_t ep) { assert(handle == &pcd); (void)ep; }
static void HAL_PCDEx_LPM_Callback(PCD_HandleTypeDef *handle, PCD_LPM_MsgTypeDef message) { assert(handle == &pcd); (void)message; }

#include "usb_fifo_ready_functions.inc"

static void invoke_irq(void)
{
  uint32_t before = irq_mask;
  irq_mask = 1U;
  model_sync();
  HAL_PCD_IRQHandler(&pcd);
  assert(irq_mask == 1U);
  irq_mask = before;
}

static void seed(uint8_t speed, uint32_t mask)
{
  memset(&registers, 0, sizeof(registers));
  memset(&pcd, 0, sizeof(pcd));
  memset(written, 0, sizeof(written));
  memset(fifo_word, 0, sizeof(fifo_word));
  controller = (USB_OTG_GlobalTypeDef *)registers.bytes;
  pcd.Instance = controller;
  pcd.Init.dev_endpoints = 9U;
  pcd.Init.speed = speed;
  pcd.LPM_State = LPM_L0;
  pcd.Lock = HAL_UNLOCKED;
  stack.pData = &pcd;
  irq_mask = mask;
  writes = callbacks = reset_begins = resets = flushes = aborts = closes = endpoint_flushes = 0U;
  queue_callback = callback_fifo_short = rx_pending = false;
  require_mask = true;
  abort_status = close_status = flush_status = HAL_OK;
  hidTxInit(&queue, slots, (uint16_t)(sizeof(slots) / sizeof(slots[0])));
  controller->GSNPSID = USB_OTG_CORE_ID_310A;
  controller->GINTMSK = USB_OTG_GINTSTS_USBRST | USB_OTG_GINTSTS_ENUMDNE |
                       USB_OTG_GINTSTS_IEPINT | USB_OTG_GINTSTS_OEPINT | USB_OTG_GINTSTS_RXFLVL;
  device()->DAINTMSK = 0x01ff01ffU;
  device()->DIEPMSK = USB_OTG_DIEPMSK_XFRCM | USB_OTG_DIEPMSK_EPDM;
  device()->DOEPMSK = USB_OTG_DOEPMSK_XFRCM | USB_OTG_DOEPMSK_STUPM | USB_OTG_DOEPMSK_EPDM;
  device()->DSTS = speed == USBD_FS_SPEED ? DSTS_ENUMSPD_FS_PHY_30MHZ_OR_60MHZ : DSTS_ENUMSPD_HS_PHY_30MHZ_OR_60MHZ;
  for (unsigned ep = 0U; ep < pcd.Init.dev_endpoints; ep++) {
    PCD_EPTypeDef *descriptor = &pcd.IN_ep[ep];
    descriptor->num = (uint8_t)ep;
    descriptor->is_in = 1U;
    descriptor->type = ep == 0U ? EP_TYPE_CTRL : ep == 2U ? EP_TYPE_BULK : EP_TYPE_INTR;
    descriptor->maxpacket = 64U;
    descriptor->tx_fifo_num = ep;
    descriptor->xfer_buff = backing[ep];
    pcd.OUT_ep[ep].num = (uint8_t)ep;
    pcd.OUT_ep[ep].maxpacket = 64U;
    pcd.OUT_ep[ep].xfer_buff = backing[ep];
    fifo_capacity[ep] = ep == 2U ? 128U : ep == 4U || ep == 5U ? 16U : 32U;
    fifo_free[ep] = fifo_capacity[ep];
    in_ep(ep)->DIEPCTL = USB_OTG_DIEPCTL_USBAEP | ((uint32_t)descriptor->type << 18) | 64U;
    out_ep(ep)->DOEPCTL = USB_OTG_DOEPCTL_USBAEP | 64U;
    for (unsigned byte = 0U; byte < sizeof(backing[ep]); byte++) backing[ep][byte] = (uint8_t)(ep * 17U + byte);
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
  model_sync();
}

static void signal_completion(unsigned ep, bool stale_txfe)
{
  fifo_free[ep] = fifo_capacity[ep];
  in_ep(ep)->DIEPCTL &= ~USB_OTG_DIEPCTL_EPENA;
  in_ep(ep)->DIEPINT |= USB_OTG_DIEPINT_XFRC;
  if (stale_txfe) device()->DIEPEMPMSK |= 1U << ep;
  invoke_irq();
}

static void check_packet(unsigned index, unsigned ep, const uint8_t *payload, uint32_t length)
{
  assert(index < writes && written[index].endpoint == ep && written[index].length == length);
  assert(memcmp(written[index].data, payload, length) == 0);
}

static void check_immediate(uint8_t speed, uint32_t mask, uint32_t length, uint32_t free_words)
{
  seed(speed, mask);
  fifo_free[1] = free_words;
  model_sync();
  uint8_t saved[128]; memcpy(saved, backing[1], sizeof(saved));
  PCD_EPTypeDef other = pcd.IN_ep[4];
  uint32_t other_mask = 1U << 4;
  device()->DIEPEMPMSK |= other_mask;
  assert(USBD_LL_Transmit(&stack, HID_EPIN_ADDR, backing[1], length) == USBD_OK);
  assert(irq_mask == mask && writes == 1U && callbacks == 0U);
  assert(pcd.IN_ep[1].xfer_buff == backing[1] + length && pcd.IN_ep[1].xfer_count == length);
  assert((in_ep(1)->DIEPTSIZ & USB_OTG_DIEPTSIZ_XFRSIZ) == length);
  assert((in_ep(1)->DIEPTSIZ & USB_OTG_DIEPTSIZ_PKTCNT) == (1U << 19));
  assert((device()->DIEPEMPMSK & (1U << 1)) == 0U && (device()->DIEPEMPMSK & other_mask) != 0U);
  assert(memcmp(&other, &pcd.IN_ep[4], sizeof(other)) == 0);
  check_packet(0U, 1U, saved, length);
  assert(memcmp(backing[1], saved, sizeof(saved)) == 0);
  device()->DIEPEMPMSK &= ~other_mask;
  for (unsigned i = 0U; i < 3U; i++) invoke_irq();
  assert(writes == 1U && callbacks == 0U);
  assert(USBD_LL_Transmit(&stack, HID_EPIN_ADDR, backing[1] + 64U, length) == USBD_BUSY);
  assert(writes == 1U && pcd.IN_ep[1].xfer_buff == backing[1] + length && irq_mask == mask);
  /* FIFO consumption without matching XFRC still cannot release the owner. */
  fifo_free[1] = fifo_capacity[1]; model_sync();
  invoke_irq();
  assert(writes == 1U && callbacks == 0U);
  assert(memcmp(backing[1], saved, sizeof(saved)) == 0);
  signal_completion(1U, true);
  assert(callbacks == 1U && writes == 1U);
  invoke_irq();
  assert(callbacks == 1U && writes == 1U);
}

static void check_fallback(uint8_t speed, uint32_t mask, uint32_t length, uint32_t free_words)
{
  seed(speed, mask);
  fifo_free[1] = free_words; model_sync();
  uint8_t saved[128]; memcpy(saved, backing[1], sizeof(saved));
  assert(USBD_LL_Transmit(&stack, HID_EPIN_ADDR, backing[1], length) == USBD_OK);
  assert(irq_mask == mask && writes == 0U && callbacks == 0U);
  assert(pcd.IN_ep[1].xfer_count == 0U && pcd.IN_ep[1].xfer_buff == backing[1]);
  assert((device()->DIEPEMPMSK & (1U << 1)) != 0U);
  for (unsigned i = 0U; i < 3U; i++) invoke_irq();
  assert(writes == 0U && callbacks == 0U && memcmp(backing[1], saved, sizeof(saved)) == 0);
  fifo_free[1] = fifo_capacity[1]; model_sync();
  invoke_irq();
  assert(writes == 1U && callbacks == 0U && pcd.IN_ep[1].xfer_count == length);
  assert((device()->DIEPEMPMSK & (1U << 1)) == 0U);
  check_packet(0U, 1U, saved, length);
  invoke_irq();
  assert(writes == 1U && callbacks == 0U);
  signal_completion(1U, false);
  assert(callbacks == 1U && writes == 1U && irq_mask == mask);
}

static bool arm_queue(void *context, const hid_tx_packet_t *packet)
{
  (void)context;
  return USBD_LL_Transmit(&stack, HID_EPIN_ADDR, (uint8_t *)packet->data, packet->length) == USBD_OK;
}

static void check_queue(uint8_t speed, uint32_t mask, uint32_t length, bool short_rearm)
{
  seed(speed, mask);
  queue_callback = true;
  callback_fifo_short = short_rearm;
  hid_tx_packet_t first = {.length = (uint8_t)length}, second = {.length = (uint8_t)length};
  for (unsigned i = 0U; i < length; i++) { first.data[i] = (uint8_t)(i + 11U); second.data[i] = (uint8_t)(i + 71U); }
  assert(hidTxPush(&queue, &first));
  assert(hidTxKick(&queue, arm_queue, NULL));
  assert(queue.busy && writes == 1U && callbacks == 0U);
  hid_tx_packet_t active = queue.active;
  assert(hidTxPush(&queue, &second));
  assert(!hidTxKick(&queue, arm_queue, NULL));
  assert(memcmp(&active, &queue.active, sizeof(active)) == 0 && queue.count == 1U);
  assert(written[0].source == queue.active.data);
  check_packet(0U, 1U, first.data, length);
  signal_completion(1U, true);
  assert(callbacks == 1U && queue.busy && queue.count == 0U);
  assert(memcmp(queue.active.data, second.data, length) == 0);
  if (short_rearm) {
    assert(writes == 1U && pcd.IN_ep[1].xfer_count == 0U);
    assert((device()->DIEPEMPMSK & (1U << 1)) != 0U);
    fifo_free[1] = fifo_capacity[1]; invoke_irq();
  }
  assert(writes == 2U && pcd.IN_ep[1].xfer_count == length);
  assert((device()->DIEPEMPMSK & (1U << 1)) == 0U);
  check_packet(1U, 1U, second.data, length);
  invoke_irq();
  assert(writes == 2U && callbacks == 1U);
  callback_fifo_short = false;
  signal_completion(1U, true);
  assert(callbacks == 2U && !queue.busy && writes == 2U && irq_mask == mask);
  invoke_irq();
  assert(callbacks == 2U && writes == 2U);
}

static void check_busy_and_invalid(uint8_t speed, uint32_t mask)
{
  for (unsigned scenario = 0U; scenario < 10U; scenario++) {
    seed(speed, mask);
    switch (scenario) {
      case 0: in_ep(1)->DIEPCTL |= USB_OTG_DIEPCTL_EPENA; break;
      case 1: in_ep(1)->DIEPINT |= USB_OTG_DIEPINT_XFRC; break;
      case 2: in_ep(1)->DIEPINT |= USB_OTG_DIEPINT_EPDISD; break;
      case 3: pcd.Lock = HAL_LOCKED; break;
      case 4: in_ep(1)->DIEPCTL &= ~USB_OTG_DIEPCTL_USBAEP; break;
      case 5: in_ep(1)->DIEPCTL &= ~USB_OTG_DIEPCTL_EPTYP; break;
      case 6: pcd.IN_ep[1].type = EP_TYPE_BULK; break;
      case 7: pcd.IN_ep[1].maxpacket = 7U; break;
      case 8: pcd.IN_ep[1].is_in = 0U; break;
      case 9: pcd.IN_ep[1].num = 2U; break;
    }
    PCD_HandleTypeDef before = pcd;
    uint32_t ctl = in_ep(1)->DIEPCTL, emp = device()->DIEPEMPMSK;
    assert(HAL_PCD_EP_TransmitReady(&pcd, HID_EPIN_ADDR, backing[1], HID_KEYBOARD_REPORT_SIZE) ==
           (scenario < 4U ? HAL_BUSY : HAL_ERROR));
    assert(irq_mask == mask && writes == 0U && callbacks == 0U);
    assert(memcmp(&pcd, &before, sizeof(pcd)) == 0);
    assert(in_ep(1)->DIEPCTL == ctl && device()->DIEPEMPMSK == emp);
  }
  seed(speed, mask);
  PCD_HandleTypeDef before = pcd;
  for (unsigned ep = 0U; ep < 256U; ep++) {
    if (ep == HID_EPIN_ADDR) continue;
    HAL_StatusTypeDef expected = ep >= 0x82U && ep < 0x89U ? HAL_OK : HAL_ERROR;
    if (expected == HAL_OK) continue; /* opt-in API supports other explicit IN owners */
    assert(HAL_PCD_EP_TransmitReady(&pcd, (uint8_t)ep, backing[1], 22U) == HAL_ERROR);
  }
  assert(HAL_PCD_EP_TransmitReady(&pcd, HID_EPIN_ADDR, NULL, 22U) == HAL_ERROR);
  assert(HAL_PCD_EP_TransmitReady(&pcd, HID_EPIN_ADDR, backing[1], 0U) == HAL_ERROR);
  assert(HAL_PCD_EP_TransmitReady(&pcd, HID_EPIN_ADDR, backing[1], 65U) == HAL_ERROR);
  assert(HAL_PCD_EP_TransmitReady(NULL, HID_EPIN_ADDR, backing[1], 22U) == HAL_ERROR);
  pcd.Instance = NULL;
  assert(HAL_PCD_EP_TransmitReady(&pcd, HID_EPIN_ADDR, backing[1], 22U) == HAL_ERROR);
  pcd.Instance = controller;
  assert(memcmp(&pcd, &before, sizeof(pcd)) == 0 && irq_mask == mask && writes == 0U);
  pcd.Init.dma_enable = 1U;
  before = pcd;
  assert(HAL_PCD_EP_TransmitReady(&pcd, HID_EPIN_ADDR, backing[1], 22U) == HAL_ERROR);
  assert(memcmp(&pcd, &before, sizeof(pcd)) == 0 && irq_mask == mask && writes == 0U);
}

static void check_exceptions(uint8_t speed, uint32_t mask)
{
  seed(speed, mask);
  assert(HAL_PCD_EP_Transmit(&pcd, HID_EPIN_ADDR, backing[1], HID_KEYBOARD_REPORT_SIZE) == HAL_OK);
  assert(writes == 0U && callbacks == 0U && pcd.IN_ep[1].xfer_count == 0U);
  assert((device()->DIEPEMPMSK & (1U << 1)) != 0U && irq_mask == mask);
  const struct { uint8_t ep; uint32_t length; } cases[] = {
    {0x80U, 8U}, {0x80U, 0U}, {HID_EPIN_ADDR, 0U}, {2U | 0x80U, 22U},
    {HID_VIA_EP_IN, 32U}, {HID_VIA_EP_IN, 22U}, {HID_EXK_EP_IN, 8U},
    {HID_EXK_EP_IN, 3U}, {HID_EXK_EP_IN, 6U}, {HID_EPIN_ADDR, 7U},
    {HID_EPIN_ADDR, 64U}, {HID_EPIN_ADDR, 65U},
  };
  for (unsigned i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
    seed(speed, mask);
    unsigned ep = cases[i].ep & EP_ADDR_MSK;
    assert(USBD_LL_Transmit(&stack, cases[i].ep, backing[ep], cases[i].length) == USBD_OK);
    assert(writes == 0U && callbacks == 0U && irq_mask == mask);
    assert(pcd.IN_ep[ep].xfer_buff == backing[ep] && pcd.IN_ep[ep].xfer_count == 0U);
    assert((in_ep(ep)->DIEPCTL & USB_OTG_DIEPCTL_EPENA) != 0U);
    assert(((device()->DIEPEMPMSK & (1U << ep)) != 0U) == (cases[i].length != 0U));
  }
  for (unsigned dma = 0U; dma < 2U; dma++) {
    seed(speed, mask);
    pcd.Init.dma_enable = 1U;
    uint32_t length = dma ? 0U : HID_KEYBOARD_REPORT_SIZE;
    assert(USBD_LL_Transmit(&stack, HID_EPIN_ADDR, backing[1], length) == USBD_OK);
    assert(writes == 0U && pcd.IN_ep[1].xfer_count == 0U && irq_mask == mask);
    assert((device()->DIEPEMPMSK & (1U << 1)) == 0U);
    assert(pcd.IN_ep[1].dma_addr == (uint32_t)(uintptr_t)backing[1]);
  }
  seed(speed, mask);
  pcd.IN_ep[1].type = EP_TYPE_BULK;
  in_ep(1)->DIEPCTL = (in_ep(1)->DIEPCTL & ~USB_OTG_DIEPCTL_EPTYP) | (EP_TYPE_BULK << 18);
  assert(USBD_LL_Transmit(&stack, HID_EPIN_ADDR, backing[1], 22U) == USBD_OK);
  assert(writes == 0U && (device()->DIEPEMPMSK & (1U << 1)) != 0U);
  seed(speed, mask);
  pcd.IN_ep[1].maxpacket = 16U;
  assert(USBD_LL_Transmit(&stack, HID_EPIN_ADDR, backing[1], 22U) == USBD_OK);
  assert(writes == 0U && pcd.IN_ep[1].xfer_count == 0U);
  assert((in_ep(1)->DIEPTSIZ & USB_OTG_DIEPTSIZ_PKTCNT) == (2U << 19));
}

static void check_completion_chain(uint8_t speed, uint32_t mask, uint32_t length)
{
  seed(speed, mask);
  queue_callback = true;
  hid_tx_packet_t packet = {.length = (uint8_t)length};
  packet.data[2] = 1U;
  assert(hidTxPush(&queue, &packet));
  assert(hidTxKick(&queue, arm_queue, NULL));
  for (unsigned n = 0U; n < 64U; n++) {
    hid_tx_packet_t active = queue.active;
    check_packet(n, 1U, active.data, length);
    if (n < 63U) {
      packet.data[2]++;
      assert(hidTxPush(&queue, &packet));
      assert(!hidTxKick(&queue, arm_queue, NULL));
    }
    invoke_irq();
    assert(memcmp(&active, &queue.active, sizeof(active)) == 0);
    assert(writes == n + 1U && callbacks == n && queue.busy);
    callback_fifo_short = n < 63U && (n & 1U) != 0U;
    signal_completion(1U, true);
    assert(callbacks == n + 1U);
    if (n < 63U) {
      if (callback_fifo_short) {
        assert(writes == n + 1U && pcd.IN_ep[1].xfer_count == 0U);
        fifo_free[1] = fifo_capacity[1]; invoke_irq();
      }
      assert(queue.busy && queue.count == 0U && writes == n + 2U);
      assert(pcd.IN_ep[1].xfer_count == length);
    } else {
      assert(!queue.busy && writes == 64U && irq_mask == mask);
    }
  }
}

static void check_failed_arm_head(uint8_t speed, uint32_t mask)
{
  seed(speed, mask);
  hid_tx_packet_t packet = {.length = HID_KEYBOARD_REPORT_SIZE, .data = {2U, 0U, 4U}};
  assert(hidTxPush(&queue, &packet));
  in_ep(1)->DIEPINT |= USB_OTG_DIEPINT_XFRC;
  uint16_t head = queue.head;
  assert(!hidTxKick(&queue, arm_queue, NULL));
  assert(queue.count == 1U && queue.head == head && !queue.busy && writes == 0U);
  in_ep(1)->DIEPINT &= ~USB_OTG_DIEPINT_XFRC;
  assert(hidTxKick(&queue, arm_queue, NULL));
  assert(queue.count == 0U && queue.busy && writes == 1U && irq_mask == mask);
  check_packet(0U, 1U, packet.data, packet.length);
}

static void check_reset_and_close(uint8_t speed, uint32_t mask)
{
  for (unsigned scenario = 0U; scenario < 4U; scenario++) {
    seed(speed, mask);
    assert(USBD_LL_Transmit(&stack, HID_EPIN_ADDR, backing[1], 22U) == USBD_OK);
    PCD_EPTypeDef descriptor = pcd.IN_ep[1];
    uint8_t payload[128]; memcpy(payload, backing[1], sizeof(payload));
    if (scenario == 0U) abort_status = HAL_ERROR;
    if (scenario == 1U) close_status = HAL_BUSY;
    if (scenario == 2U) flush_status = HAL_ERROR;
    USBD_StatusTypeDef result = USBD_LL_CloseEP(&stack, HID_EPIN_ADDR);
    assert(irq_mask == mask && aborts == 1U && callbacks == 0U && writes == 1U);
    assert(closes == (scenario == 0U ? 0U : 1U));
    assert(endpoint_flushes == (scenario < 2U ? 0U : 1U));
    assert(memcmp(payload, backing[1], sizeof(payload)) == 0);
    if (scenario < 3U) {
      assert(result == (scenario == 1U ? USBD_BUSY : USBD_FAIL));
      assert(memcmp(&descriptor, &pcd.IN_ep[1], sizeof(descriptor)) == 0);
    } else {
      assert(result == USBD_OK && pcd.IN_ep[1].xfer_buff == NULL);
      assert(pcd.IN_ep[1].xfer_count == 0U && pcd.IN_ep[1].xfer_len == 0U);
      assert(USBD_LL_Transmit(&stack, HID_EPIN_ADDR, backing[1], 22U) == USBD_FAIL);
      assert(pcd.IN_ep[1].xfer_buff == NULL && writes == 1U);
    }
  }
  seed(speed, mask);
  assert(USBD_LL_Transmit(&stack, HID_EPIN_ADDR, backing[1], 22U) == USBD_OK);
  PCD_EPTypeDef descriptor = pcd.IN_ep[1];
  uint8_t payload[128]; memcpy(payload, backing[1], sizeof(payload));
  controller->GINTSTS |= USB_OTG_GINTSTS_USBRST;
  invoke_irq();
  assert(reset_begins == 1U && resets == 0U && callbacks == 0U && flushes == 1U);
  assert(writes == 1U && memcmp(&descriptor, &pcd.IN_ep[1], sizeof(descriptor)) == 0);
  assert(memcmp(payload, backing[1], sizeof(payload)) == 0);
  controller->GINTSTS |= USB_OTG_GINTSTS_ENUMDNE;
  invoke_irq();
  assert(resets == 1U && callbacks == 0U && writes == 1U && irq_mask == mask);
}

int main(void)
{
  const uint8_t speeds[] = {USBD_FS_SPEED, USBD_HS_SPEED};
  const uint32_t lengths[] = {HID_BOOT_KEYBOARD_REPORT_SIZE, HID_KEYBOARD_REPORT_SIZE};
  for (unsigned s = 0U; s < sizeof(speeds) / sizeof(speeds[0]); s++) {
    for (uint32_t mask = 0U; mask < 2U; mask++) {
      for (unsigned l = 0U; l < sizeof(lengths) / sizeof(lengths[0]); l++) {
        uint32_t words = (lengths[l] + 3U) / 4U;
        check_immediate(speeds[s], mask, lengths[l], 32U);
        check_immediate(speeds[s], mask, lengths[l], words);
        for (uint32_t free = 0U; free < words; free++) check_fallback(speeds[s], mask, lengths[l], free);
        check_queue(speeds[s], mask, lengths[l], false);
        check_queue(speeds[s], mask, lengths[l], true);
        check_completion_chain(speeds[s], mask, lengths[l]);
      }
      check_busy_and_invalid(speeds[s], mask);
      check_exceptions(speeds[s], mask);
      check_failed_arm_head(speeds[s], mask);
      check_reset_and_close(speeds[s], mask);
    }
  }
  puts("PASS: production keyboard LL/HAL/IRQ/queue arms and fills 8/22B once, falls back without waiting, preserves completion ownership, handles stale TXFE/rearm/busy and leaves EP0/ZLP/CDC/EXK/VIA/DMA paths intact (FS/HS, PRIMASK 0/1)");
  return 0;
}
