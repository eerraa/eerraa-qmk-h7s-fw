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
} PCD_HandleTypeDef;
typedef struct { void *pData; } USBD_HandleTypeDef;

static inline uint32_t __get_PRIMASK(void) { return usb_ll_primask; }
static inline void __disable_irq(void) { usb_ll_primask = 1U; }
static inline void __set_PRIMASK(uint32_t mask) { usb_ll_primask = mask; }

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
    assert(usb_ll_irq_accesses[is_in][epnum] == 0U);
  }

  if (scenario == ABORT_STUCK || scenario == CLOSE_BUSY) {
    assert(status == (scenario == CLOSE_BUSY ? USBD_BUSY : USBD_FAIL));
    assert(usb_ll_fifo.accesses == 0U);
    assert(usb_ll_device.DAINTMSK == UINT32_MAX && usb_ll_device.DEACHMSK == UINT32_MAX);
    assert((usb_ll_endpoint_control(is_in, epnum) & USB_OTG_DIEPCTL_USBAEP) != 0U);
    if (scenario == ABORT_STUCK) assert((usb_ll_endpoint_control(is_in, epnum) & USB_OTG_DIEPCTL_EPENA) != 0U);
  } else {
    assert(usb_ll_device.DAINTMSK == (UINT32_MAX & ~endpoint_mask));
    assert(usb_ll_device.DEACHMSK == (UINT32_MAX & ~endpoint_mask));
    assert((usb_ll_endpoint_control(is_in, epnum) & (USB_OTG_DIEPCTL_EPENA | USB_OTG_DIEPCTL_USBAEP)) == 0U);
    assert(status == (scenario == CLOSE_OK ? USBD_OK : USBD_FAIL));
  }

  if (scenario == CLOSE_OK) {
    assert(usb_ll_irq_accesses[is_in][epnum] == 2U);
    if (is_in) {
      assert(descriptor->xfer_buff == NULL && descriptor->xfer_len == 0U && descriptor->xfer_count == 0U);
      assert(usb_ll_fifo.commands == 1U && usb_ll_fifo.command == (USB_OTG_GRSTCTL_TXFFLSH | (epnum << 6)));
    } else {
      check_descriptor(descriptor, previous);
      assert(usb_ll_fifo.accesses == 0U); /* shared RX FIFO and EP0 are untouched */
    }
  }
  assert(usb_ll_device.DIEPEMPMSK == (is_in ? UINT32_MAX & ~(1U << epnum) : UINT32_MAX));
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

int main(void)
{
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
