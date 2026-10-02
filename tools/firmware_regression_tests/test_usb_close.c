#include <stdint.h>
#include <stdio.h>

#define HAL_USB_TIMEOUT 64U
#include "usb_ll_defs.inc"
#include "usb_close_defs.inc"
#include "register_model.h"

typedef struct {
  USB_OTG_GlobalTypeDef *Instance;
  HAL_LockTypeDef Lock;
  struct { uint8_t dev_endpoints; } Init;
  PCD_EPTypeDef IN_ep[16], OUT_ep[16];
  uint32_t Setup[12];
} PCD_HandleTypeDef;
typedef struct { void *pData; } USBD_HandleTypeDef;

static inline uint32_t __get_PRIMASK(void) { return usb_ll_primask; }
static inline void __disable_irq(void) { usb_ll_primask = 1U; }
static inline void __set_PRIMASK(uint32_t mask) { usb_ll_primask = mask; }

static void *USB_ReadPacket(const USB_OTG_GlobalTypeDef *instance, uint8_t *dest, uint16_t size)
{
  assert(instance == &usb_ll_controller && size <= 64U);
  memcpy(dest, usb_ll_rx_last.data, size);
  return dest;
}

#include "usb_ll_functions.inc"
#include "usb_close_functions.inc"

static PCD_HandleTypeDef usb_close_handle;
static USBD_HandleTypeDef usb_close_device;
static uint8_t usb_close_buffers[2][16][64];

typedef enum { CLOSE_OK, ABORT_STUCK, CLOSE_BUSY, FIFO_AHB_STUCK, FIFO_FLUSH_STUCK } close_case_t;

static void seed(unsigned is_in, unsigned epnum, close_case_t scenario, uint32_t initial_mask)
{
  usb_ll_reset();
  memset(&usb_close_handle, 0, sizeof(usb_close_handle));
  usb_close_handle.Instance = &usb_ll_controller;
  usb_close_handle.Lock = scenario == CLOSE_BUSY ? HAL_LOCKED : HAL_UNLOCKED;
  usb_close_handle.Init.dev_endpoints = 9U;
  usb_close_device.pData = &usb_close_handle;
  for (unsigned dir = 0U; dir < 2U; dir++) {
    for (unsigned ep = 0U; ep < 16U; ep++) {
      PCD_EPTypeDef *descriptor = dir ? &usb_close_handle.IN_ep[ep] : &usb_close_handle.OUT_ep[ep];
      descriptor->num = (uint8_t)ep;
      descriptor->is_in = (uint8_t)dir;
      descriptor->xfer_buff = usb_close_buffers[dir][ep];
      descriptor->xfer_len = 22U;
      descriptor->xfer_count = 11U;
    }
  }
  usb_ll_seed_endpoint(is_in, epnum, true, scenario == ABORT_STUCK ? 0U : 3U);
  usb_ll_fifo.idle_after = scenario == FIFO_AHB_STUCK ? 0U : 1U;
  usb_ll_fifo.clear_after = scenario == FIFO_FLUSH_STUCK ? 0U : 4U;
  if (is_in) usb_ll_in[epnum].interrupts[0] = USB_OTG_DIEPINT_XFRC;
  else usb_ll_out[epnum].interrupts[0] = USB_OTG_DOEPINT_XFRC;
  usb_ll_primask = initial_mask;
  usb_ll_require_mask = true;
}

static void check_descriptor(const PCD_EPTypeDef *after, const PCD_EPTypeDef *before)
{
  assert(after->xfer_buff == before->xfer_buff);
  assert(after->xfer_len == before->xfer_len);
  assert(after->xfer_count == before->xfer_count);
}

static void check_case(unsigned is_in, unsigned epnum, close_case_t scenario, uint32_t initial_mask)
{
  seed(is_in, epnum, scenario, initial_mask);
  PCD_HandleTypeDef before = usb_close_handle;
  uint8_t address = (uint8_t)(epnum | (is_in ? 0x80U : 0U));
  USBD_StatusTypeDef status = USBD_LL_CloseEP(&usb_close_device, address);
  assert(usb_ll_primask == initial_mask);
  usb_ll_require_mask = false;
  PCD_EPTypeDef *descriptor = is_in ? &usb_close_handle.IN_ep[epnum] : &usb_close_handle.OUT_ep[epnum];
  const PCD_EPTypeDef *previous = is_in ? &before.IN_ep[epnum] : &before.OUT_ep[epnum];
  uint32_t endpoint_mask = 1U << (epnum + (is_in ? 0U : 16U));
  if (scenario != CLOSE_OK) {
    check_descriptor(descriptor, previous);
    uint32_t pending = is_in ? usb_ll_in[epnum].interrupts[0] : usb_ll_out[epnum].interrupts[0];
    assert((pending & USB_OTG_DIEPINT_XFRC) != 0U);
  }

  if (scenario != CLOSE_OK) {
    assert(status == (scenario == CLOSE_BUSY ? USBD_BUSY : USBD_FAIL));
    assert(usb_ll_device.DAINTMSK == UINT32_MAX && usb_ll_device.DEACHMSK == UINT32_MAX);
    assert((usb_ll_endpoint_control(is_in, epnum) & USB_OTG_DIEPCTL_USBAEP) != 0U);
    if (scenario == ABORT_STUCK || scenario == CLOSE_BUSY) assert(usb_ll_fifo.accesses == 0U);
  } else {
    assert(usb_ll_device.DAINTMSK == (UINT32_MAX & ~endpoint_mask));
    assert(usb_ll_device.DEACHMSK == (UINT32_MAX & ~endpoint_mask));
    assert((usb_ll_endpoint_control(is_in, epnum) & (USB_OTG_DIEPCTL_EPENA | USB_OTG_DIEPCTL_USBAEP)) == 0U);
    assert(status == USBD_OK);
  }

  if (scenario == CLOSE_OK) {
    assert(usb_ll_irq_clears[is_in][epnum] == 2U);
    if (is_in) {
      assert(descriptor->xfer_buff == NULL && descriptor->xfer_len == 0U && descriptor->xfer_count == 0U);
      assert(usb_ll_fifo.commands == 1U && usb_ll_fifo.command == (USB_OTG_GRSTCTL_TXFFLSH | (epnum << 6)));
    } else {
      check_descriptor(descriptor, previous);
      assert(usb_ll_fifo.accesses == 0U); /* shared RX FIFO and EP0 are untouched */
    }
  }
  assert(usb_ll_device.DIEPEMPMSK == ((is_in && scenario != CLOSE_BUSY) ? UINT32_MAX & ~(1U << epnum) : UINT32_MAX));
  assert(usb_close_handle.Lock == (scenario == CLOSE_BUSY ? HAL_LOCKED : HAL_UNLOCKED));
  for (unsigned ep = 0U; ep < 16U; ep++) {
    if (ep != epnum || !is_in) check_descriptor(&usb_close_handle.IN_ep[ep], &before.IN_ep[ep]);
    if (ep != epnum || is_in) check_descriptor(&usb_close_handle.OUT_ep[ep], &before.OUT_ep[ep]);
  }
}

static void check_invalid(uint32_t initial_mask)
{
  seed(1U, 1U, CLOSE_OK, initial_mask);
  PCD_HandleTypeDef before = usb_close_handle;
  assert(HAL_PCD_EP_Close(&usb_close_handle, 0U) == HAL_ERROR);
  assert(HAL_PCD_EP_Close(&usb_close_handle, 0x80U) == HAL_ERROR);
  assert(HAL_PCD_EP_Abort(&usb_close_handle, 0U) == HAL_ERROR);
  assert(HAL_PCD_EP_Abort(&usb_close_handle, 0x91U) == HAL_ERROR);
  assert(USBD_LL_CloseEP(&usb_close_device, 0x89U) == USBD_FAIL);
  assert(USBD_LL_CloseEP(&usb_close_device, 0x09U) == USBD_FAIL);
  assert(usb_ll_primask == initial_mask && usb_ll_fifo.accesses == 0U);
  assert(usb_ll_device.DIEPEMPMSK == UINT32_MAX && usb_ll_device.DAINTMSK == UINT32_MAX);
  for (unsigned ep = 0U; ep < 16U; ep++) {
    check_descriptor(&usb_close_handle.IN_ep[ep], &before.IN_ep[ep]);
    check_descriptor(&usb_close_handle.OUT_ep[ep], &before.OUT_ep[ep]);
  }
  usb_close_device.pData = NULL;
  assert(USBD_LL_CloseEP(&usb_close_device, 0x81U) == USBD_FAIL);
  assert(usb_ll_primask == initial_mask && usb_ll_fifo.accesses == 0U);
}

static void check_nak(unsigned dma, unsigned delay, bool already_effective, uint32_t initial_mask)
{
  seed(0U, 4U, CLOSE_OK, initial_mask);
  usb_ll_controller.GINTMSK = 0xA5A5FFFFU;
  uint32_t mask = usb_ll_controller.GINTMSK;
  usb_ll_controller.GAHBCFG = dma ? USB_OTG_GAHBCFG_DMAEN : 0U;
  usb_ll_nak_after = delay;
  if (already_effective) usb_ll_controller.status[0] |= USB_OTG_GINTSTS_BOUTNAKEFF;
  const uint8_t setup[8] = {0x00, 0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
  const uint8_t payload[5] = {0x12, 0x34, 0x56, 0x78, 0x90};
  bool queued = !dma && !already_effective;
  if (queued) {
    usb_ll_push_rx(0U, STS_SETUP_UPDT, sizeof(setup), setup);
    usb_ll_push_rx(2U, STS_DATA_UPDT, 3U, payload);
    usb_ll_push_rx(4U, STS_DATA_UPDT, sizeof(payload), payload);
  }
  USBD_StatusTypeDef result = USBD_LL_CloseEP(&usb_close_device, 4U);
  assert(usb_ll_primask == initial_mask);
  usb_ll_require_mask = false;
  assert(usb_ll_controller.GINTMSK == mask);
  bool success = delay != 0U || already_effective;
  assert(result == (success ? USBD_OK : USBD_FAIL));
  assert((usb_ll_ep_script[0][4].disable_order != 0U) == success);
  assert(((usb_ll_controller.GINTSTS & USB_OTG_GINTSTS_BOUTNAKEFF) != 0U) == already_effective);
  assert(usb_ll_fifo.accesses == 0U);
  if (queued) {
    assert(memcmp(usb_close_handle.Setup, setup, sizeof(setup)) == 0);
    assert(memcmp(usb_close_buffers[0][2], payload, 3U) == 0);
    assert(memcmp(usb_close_buffers[0][4], payload, sizeof(payload)) == 0);
    assert(usb_close_handle.OUT_ep[0].xfer_count == 19U);
    assert(usb_close_handle.OUT_ep[2].xfer_count == 14U);
    assert(usb_close_handle.OUT_ep[4].xfer_count == 16U);
    assert(usb_close_handle.OUT_ep[4].xfer_buff == usb_close_buffers[0][4] + 5U);
    assert(usb_ll_rx_pops == (success ? 4U : 3U));
  } else {
    assert(usb_ll_rx_pops == 0U);
  }
}

static void check_disabled_out(void)
{
  seed(0U, 4U, CLOSE_OK, 0U);
  usb_ll_out[4].DOEPCTL &= ~USB_OTG_DOEPCTL_EPENA;
  const uint8_t payload[] = {1U, 2U, 3U};
  usb_ll_push_rx(4U, STS_DATA_UPDT, sizeof(payload), payload);
  assert(USBD_LL_CloseEP(&usb_close_device, 4U) == USBD_OK);
  assert(usb_ll_nak_polls != 0U && usb_ll_rx_pops == 2U);
  assert(memcmp(usb_close_buffers[0][4], payload, sizeof(payload)) == 0);
  assert(usb_close_handle.OUT_ep[4].xfer_count == 14U);
  assert(usb_ll_primask == 0U);
  usb_ll_require_mask = false;
}

static void check_completion_boundary(unsigned is_in, bool nak_timeout)
{
  seed(is_in, 4U, CLOSE_OK, 0U);
  usb_ll_endpoint_script_t *script = &usb_ll_ep_script[is_in][4];
  if (nak_timeout) script->nak_after = 0U;
  else script->done_after = 0U;
  /* A previous disable's W1C flag must not satisfy the new operation. */
  if (is_in) usb_ll_in[4].interrupts[0] |= USB_OTG_DIEPINT_EPDISD;
  else usb_ll_out[4].interrupts[0] |= USB_OTG_DOEPINT_EPDISD;
  uint8_t address = (uint8_t)(4U | (is_in ? 0x80U : 0U));
  PCD_EPTypeDef before = is_in ? usb_close_handle.IN_ep[4] : usb_close_handle.OUT_ep[4];
  assert(HAL_PCD_EP_Close(&usb_close_handle, address) == HAL_ERROR);
  assert(HAL_PCD_EP_Abort(&usb_close_handle, address) == HAL_ERROR);
  assert(usb_ll_primask == 0U && usb_close_handle.Lock == HAL_UNLOCKED);
  PCD_EPTypeDef *ep = is_in ? &usb_close_handle.IN_ep[4] : &usb_close_handle.OUT_ep[4];
  check_descriptor(ep, &before);
  assert(usb_ll_fifo.commands == 0U);
  assert((usb_ll_endpoint_control(is_in, 4U) & USB_OTG_DIEPCTL_USBAEP) != 0U);
  if (nak_timeout) {
    assert(script->disable_order == 0U);
    script->nak_after = script->nak_polls + 2U;
  } else {
    assert((usb_ll_endpoint_control(is_in, 4U) & USB_OTG_DIEPCTL_EPENA) == 0U);
    assert((usb_ll_endpoint_control(is_in, 4U) & USB_OTG_DIEPCTL_EPDIS) != 0U);
    /* Retry cannot turn early EPENA clear into successful completion. */
    assert(HAL_PCD_EP_Close(&usb_close_handle, address) == HAL_ERROR);
    check_descriptor(ep, &before);
    script->done_after = script->disable_polls + 3U;
  }
  assert(HAL_PCD_EP_Close(&usb_close_handle, address) == HAL_OK);
  assert(usb_ll_primask == 0U && usb_close_handle.Lock == HAL_UNLOCKED);
  usb_ll_require_mask = false;
}

static void check_in_natural_completion(void)
{
  seed(1U, 1U, CLOSE_OK, 1U);
  usb_ll_ep_script[1][1].nak_after = 0U;
  usb_ll_ep_script[1][1].natural_after = 3U;
  assert(HAL_PCD_EP_Close(&usb_close_handle, 0x81U) == HAL_OK);
  assert(usb_ll_ep_script[1][1].disable_order == 0U);
  assert(usb_ll_fifo.commands == 1U && usb_ll_primask == 1U);
  assert(usb_close_handle.IN_ep[1].xfer_buff == NULL);
  usb_ll_require_mask = false;
}

static void check_abort_and_close_ownership(void)
{
  for (unsigned is_in = 0U; is_in < 2U; is_in++) {
    seed(is_in, 4U, CLOSE_OK, 0U);
    uint8_t address = (uint8_t)(4U | (is_in ? 0x80U : 0U));
    PCD_EPTypeDef before = is_in ? usb_close_handle.IN_ep[4] : usb_close_handle.OUT_ep[4];
    assert(HAL_PCD_EP_Abort(&usb_close_handle, address) == HAL_OK);
    check_descriptor(is_in ? &usb_close_handle.IN_ep[4] : &usb_close_handle.OUT_ep[4], &before);
    assert((usb_ll_endpoint_control(is_in, 4U) & USB_OTG_DIEPCTL_USBAEP) != 0U);
    assert(usb_ll_fifo.commands == (is_in ? 1U : 0U));
    assert(usb_ll_primask == 0U && usb_close_handle.Lock == HAL_UNLOCKED);
    assert(HAL_PCD_EP_Close(&usb_close_handle, address) == HAL_OK);
    assert((usb_ll_endpoint_control(is_in, 4U) & USB_OTG_DIEPCTL_USBAEP) == 0U);
    usb_ll_require_mask = false;
  }
  seed(1U, 1U, CLOSE_BUSY, 0U);
  assert(HAL_PCD_EP_Abort(&usb_close_handle, 0x81U) == HAL_BUSY);
  assert(usb_ll_device.DIEPEMPMSK == UINT32_MAX && usb_ll_primask == 0U);
  usb_ll_require_mask = false;
}

int main(void)
{
  check_completion_boundary(1U, true);
  check_completion_boundary(1U, false);
  check_completion_boundary(0U, false);
  check_in_natural_completion();
  seed(1U, 1U, CLOSE_OK, 0U);
  usb_close_handle.IN_ep[1].type = EP_TYPE_ISOC;
  usb_ll_ep_script[1][1].nak_after = 0U;
  assert(HAL_PCD_EP_Abort(&usb_close_handle, 0x81U) == HAL_OK);
  assert(usb_ll_fifo.commands == 1U && usb_ll_primask == 0U);
  usb_ll_require_mask = false;
  check_abort_and_close_ownership();
  puts("PASS: direct HAL Close/Abort owns NAK, fresh EPDISD and FIFO completion; stale flags, early EPENA clear and failed retries cannot retire buffers");
  check_disabled_out();
  for (unsigned mask = 0U; mask < 2U; mask++) {
    for (unsigned dma = 0U; dma < 2U; dma++) {
      check_nak(dma, 1U, false, mask);
      check_nak(dma, 100U, false, mask);
      check_nak(dma, 0U, false, mask);
      check_nak(dma, 0U, true, mask);
    }
  }
  puts("PASS: OUT abort waits for NAK, preserves queued SETUP/data, restores masks, and rejects NAK timeout without disabling");
  for (uint32_t mask = 0U; mask < 2U; mask++) {
    for (unsigned direction = 0U; direction < 2U; direction++) {
      unsigned is_in = 1U - direction;
      for (unsigned ep = 1U; ep < 9U; ep++) {
        check_case(is_in, ep, CLOSE_OK, mask);
        check_case(is_in, ep, ABORT_STUCK, mask);
        check_case(is_in, ep, CLOSE_BUSY, mask);
        if (is_in) {
          check_case(is_in, ep, FIFO_AHB_STUCK, mask);
          check_case(is_in, ep, FIFO_FLUSH_STUCK, mask);
        }
      }
    }
    check_invalid(mask);
  }
  puts("PASS: production bridge/HAL/LL close preserves descriptors and skips later stages on abort, lock or FIFO failure; success retires IN and preserves shared RX ownership with PRIMASK restored");
  return 0;
}
