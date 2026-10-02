#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* The production header explicitly supports this override. It is confined to
 * this host fixture; instruction/latency analysis uses the target ELF's actual
 * 0x0F000000 default and no target elapsed-time claim follows from these tests. */
#define HAL_USB_TIMEOUT 64U
#include "usb_ll_defs.inc"
#include "register_model.h"
#include "usb_ll_functions.inc"

static void check_fifo(bool is_tx, unsigned fifo, unsigned idle_after, unsigned clear_after, HAL_StatusTypeDef expected)
{
  usb_ll_reset();
  usb_ll_fifo.idle_after = idle_after;
  usb_ll_fifo.clear_after = clear_after;
  HAL_StatusTypeDef result = is_tx ? USB_FlushTxFifo(&usb_ll_controller, fifo) : USB_FlushRxFifo(&usb_ll_controller);
  assert(result == expected);
  assert(!usb_ll_fifo.command_before_idle);
  if (idle_after == 0U || idle_after > HAL_USB_TIMEOUT) {
    assert(usb_ll_fifo.commands == 0U && usb_ll_fifo.accesses == HAL_USB_TIMEOUT);
    assert(usb_ll_controller.registers[0] == 0U);
  } else {
    assert(usb_ll_fifo.commands == 1U);
    uint32_t command = is_tx ? USB_OTG_GRSTCTL_TXFFLSH | (fifo << 6) : USB_OTG_GRSTCTL_RXFFLSH;
    assert(usb_ll_fifo.command == command);
    if (expected == HAL_TIMEOUT) {
      assert(usb_ll_fifo.flush_polls == HAL_USB_TIMEOUT);
      assert((usb_ll_controller.registers[0] & command) == command);
    } else {
      assert(usb_ll_fifo.flush_polls == clear_after);
      assert((usb_ll_controller.registers[0] & (USB_OTG_GRSTCTL_TXFFLSH | USB_OTG_GRSTCTL_RXFFLSH)) == 0U);
    }
  }
}

static void check_endpoint(unsigned is_in, unsigned epnum, unsigned clear_after)
{
  usb_ll_reset();
  usb_ll_seed_endpoint(is_in, epnum, true, clear_after);
  usb_ll_controller.status[0] = USB_OTG_GINTSTS_BOUTNAKEFF;
  USB_OTG_EPTypeDef ep = {0};
  uint8_t buffer[64] = {0};
  ep.num = (uint8_t)epnum;
  ep.is_in = (uint8_t)is_in;
  ep.xfer_buff = buffer;
  ep.xfer_len = sizeof(buffer);
  ep.xfer_count = 9U;
  USB_OTG_EPTypeDef before = ep;
  HAL_StatusTypeDef result = USB_EPStopXfer(&usb_ll_controller, &ep);
  usb_ll_endpoint_script_t *script = &usb_ll_ep_script[is_in][epnum];
  assert(memcmp(&before, &ep, sizeof(ep)) == 0);
  assert(script->snak_order != 0U && script->snak_order < script->disable_order);
  assert(usb_ll_fifo.accesses == 0U);
  if (clear_after == 0U) {
    assert(result == HAL_ERROR);
    assert(script->disable_polls >= USB_EP_STOP_MAX_POLLS);
    assert((usb_ll_endpoint_control(is_in, epnum) & USB_OTG_DIEPCTL_EPENA) != 0U);
  } else {
    assert(result == HAL_OK);
    assert(script->disable_polls >= script->done_after);
    assert((usb_ll_endpoint_control(is_in, epnum) & USB_OTG_DIEPCTL_EPENA) == 0U);
  }

  HAL_StatusTypeDef deactivated = USB_DeactivateEndpoint(&usb_ll_controller, &ep);
  uint32_t endpoint_mask = 1U << (epnum + (is_in ? 0U : 16U));
  if (clear_after == 0U) {
    assert(deactivated == HAL_ERROR);
    assert(usb_ll_device.DAINTMSK == UINT32_MAX && usb_ll_device.DEACHMSK == UINT32_MAX);
    assert((usb_ll_endpoint_control(is_in, epnum) & USB_OTG_DIEPCTL_USBAEP) != 0U);
  } else {
    assert(deactivated == HAL_OK);
    assert(usb_ll_device.DAINTMSK == (UINT32_MAX & ~endpoint_mask));
    assert(usb_ll_device.DEACHMSK == (UINT32_MAX & ~endpoint_mask));
    assert((usb_ll_endpoint_control(is_in, epnum) & USB_OTG_DIEPCTL_USBAEP) == 0U);
  }
  assert(usb_ll_device.DIEPEMPMSK == UINT32_MAX && usb_ll_fifo.accesses == 0U);
}

static void check_direct_deactivate(unsigned is_in, unsigned epnum)
{
  usb_ll_reset();
  usb_ll_seed_endpoint(is_in, epnum, true, 50U);
  usb_ll_controller.status[0] = USB_OTG_GINTSTS_BOUTNAKEFF;
  USB_OTG_EPTypeDef ep = {0};
  ep.num = (uint8_t)epnum;
  ep.is_in = (uint8_t)is_in;
  assert(USB_DeactivateEndpoint(&usb_ll_controller, &ep) == HAL_ERROR);
  assert((usb_ll_endpoint_control(is_in, epnum) & USB_OTG_DIEPCTL_EPENA) != 0U);
  assert(usb_ll_ep_script[is_in][epnum].disable_polls < 50U);
  assert(usb_ll_fifo.accesses == 0U);
}

int main(void)
{
  for (unsigned fifo = 0U; fifo <= 16U; fifo++) {
    check_fifo(true, fifo, 1U, 1U, HAL_OK);
    check_fifo(true, fifo, 7U, 9U, HAL_OK);
    check_fifo(true, fifo, HAL_USB_TIMEOUT, HAL_USB_TIMEOUT, HAL_OK);
    check_fifo(true, fifo, 0U, 1U, HAL_TIMEOUT);
    check_fifo(true, fifo, HAL_USB_TIMEOUT + 1U, 1U, HAL_TIMEOUT);
    check_fifo(true, fifo, 1U, 0U, HAL_TIMEOUT);
    check_fifo(true, fifo, 1U, HAL_USB_TIMEOUT + 1U, HAL_TIMEOUT);
  }
  check_fifo(false, 0U, 1U, 1U, HAL_OK);
  check_fifo(false, 0U, 6U, 8U, HAL_OK);
  check_fifo(false, 0U, HAL_USB_TIMEOUT, HAL_USB_TIMEOUT, HAL_OK);
  check_fifo(false, 0U, 0U, 1U, HAL_TIMEOUT);
  check_fifo(false, 0U, 1U, 0U, HAL_TIMEOUT);
  for (unsigned is_in = 0U; is_in < 2U; is_in++) {
    for (unsigned ep = 1U; ep < 9U; ep++) {
      check_endpoint(is_in, ep, 3U);
      check_endpoint(is_in, ep, 0U);
      check_direct_deactivate(is_in, ep);
    }
  }
  puts("PASS: H7RS LL requires NAK and disable completion; direct deactivation rejects an active endpoint; FIFO timeout/selection retained");
  return 0;
}
