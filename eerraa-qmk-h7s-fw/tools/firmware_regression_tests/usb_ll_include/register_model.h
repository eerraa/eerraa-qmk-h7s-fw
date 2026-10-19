#ifndef USB_LL_REGISTER_MODEL_H
#define USB_LL_REGISTER_MODEL_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* This is a register-access script, not a USB peripheral emulator. Commands
 * become visible at the following aliased access; hardware progress is advanced
 * at access boundaries. OUT disable requires effective NAK; queued RX statuses
 * and injected NAK delay/failure are modeled. Bus traffic and silicon timing
 * are not modeled. Extracted endpoint interrupt stores use explicit W1C
 * adapters. Early EPENA clear and delayed/missing EPDISD are independent. */
#define __IO volatile

typedef struct { __IO uint32_t registers[1], status[1], rx_status[1], GINTMSK, GAHBCFG; } USB_OTG_GlobalTypeDef;
typedef struct { __IO uint32_t DIEPCTL, interrupts[1]; } USB_OTG_INEndpointTypeDef;
typedef struct { __IO uint32_t DOEPCTL, interrupts[1]; } USB_OTG_OUTEndpointTypeDef;
typedef struct { __IO uint32_t DAINTMSK, DEACHMSK, DIEPEMPMSK, DCTL; } USB_OTG_DeviceTypeDef;

typedef struct {
  unsigned accesses, disable_polls, clear_after;
  unsigned snak_order, disable_order;
  unsigned nak_after, nak_polls, done_after, natural_after;
} usb_ll_endpoint_script_t;

typedef struct {
  unsigned accesses, idle_polls, idle_after, flush_polls, clear_after;
  unsigned commands;
  bool idle_seen, flush_seen, command_before_idle;
  uint32_t expected, command;
} usb_ll_fifo_script_t;

static USB_OTG_GlobalTypeDef usb_ll_controller;
static USB_OTG_DeviceTypeDef usb_ll_device;
static USB_OTG_INEndpointTypeDef usb_ll_in[16];
static USB_OTG_OUTEndpointTypeDef usb_ll_out[16];
static usb_ll_endpoint_script_t usb_ll_ep_script[2][16];
static usb_ll_fifo_script_t usb_ll_fifo;
static unsigned usb_ll_sequence, usb_ll_selected_ep;
static unsigned usb_ll_irq_accesses[2][16], usb_ll_irq_clears[2][16];
static uint32_t usb_ll_primask;
static bool usb_ll_require_mask;
typedef struct { uint32_t status; uint8_t data[64]; } usb_ll_rx_t;
static usb_ll_rx_t usb_ll_rx[8], usb_ll_rx_last;
static unsigned usb_ll_rx_head, usb_ll_rx_count, usb_ll_rx_pops;
static unsigned usb_ll_nak_after, usb_ll_nak_polls;
static bool usb_ll_nak_queued;

static inline void usb_ll_push_rx(unsigned ep, unsigned type, unsigned size, const void *data)
{
  assert(usb_ll_rx_count < 8U && size <= 64U);
  usb_ll_rx_t *entry = &usb_ll_rx[(usb_ll_rx_head + usb_ll_rx_count++) % 8U];
  entry->status = ep | (size << 4) | (type << 17);
  if (size != 0U) memcpy(entry->data, data, size);
}

static inline unsigned usb_ll_status_access(void)
{
  if (usb_ll_require_mask) assert(usb_ll_primask == 1U);
  if ((usb_ll_device.DCTL & USB_OTG_DCTL_CGONAK) != 0U) {
    usb_ll_device.DCTL &= ~(USB_OTG_DCTL_CGONAK | USB_OTG_DCTL_SGONAK);
    usb_ll_controller.status[0] &= ~USB_OTG_GINTSTS_BOUTNAKEFF;
    usb_ll_nak_queued = false;
    usb_ll_nak_polls = 0U;
  } else if ((usb_ll_device.DCTL & USB_OTG_DCTL_SGONAK) != 0U && !usb_ll_nak_queued) {
    usb_ll_nak_polls++;
    if (usb_ll_nak_after != 0U && usb_ll_nak_polls >= usb_ll_nak_after) {
      usb_ll_nak_queued = true;
      if ((usb_ll_controller.GAHBCFG & USB_OTG_GAHBCFG_DMAEN) != 0U)
        usb_ll_controller.status[0] |= USB_OTG_GINTSTS_BOUTNAKEFF;
      else usb_ll_push_rx(0U, STS_GOUT_NAK, 0U, NULL);
    }
  }
  if (usb_ll_rx_count != 0U) usb_ll_controller.status[0] |= USB_OTG_GINTSTS_RXFLVL;
  else usb_ll_controller.status[0] &= ~USB_OTG_GINTSTS_RXFLVL;
  return 0U;
}

static inline unsigned usb_ll_rx_pop(void)
{
  if (usb_ll_require_mask) assert(usb_ll_primask == 1U);
  assert(usb_ll_rx_count != 0U);
  usb_ll_rx_last = usb_ll_rx[usb_ll_rx_head++ % 8U];
  usb_ll_rx_count--;
  usb_ll_rx_pops++;
  usb_ll_controller.rx_status[0] = usb_ll_rx_last.status;
  if ((usb_ll_rx_last.status >> 17) == STS_GOUT_NAK)
    usb_ll_controller.status[0] |= USB_OTG_GINTSTS_BOUTNAKEFF;
  return 0U;
}

static inline void usb_ll_reset(void)
{
  memset(&usb_ll_controller, 0, sizeof(usb_ll_controller));
  memset(&usb_ll_device, 0, sizeof(usb_ll_device));
  memset(usb_ll_in, 0, sizeof(usb_ll_in));
  memset(usb_ll_out, 0, sizeof(usb_ll_out));
  memset(usb_ll_ep_script, 0, sizeof(usb_ll_ep_script));
  memset(&usb_ll_fifo, 0, sizeof(usb_ll_fifo));
  memset(usb_ll_irq_accesses, 0, sizeof(usb_ll_irq_accesses));
  memset(usb_ll_irq_clears, 0, sizeof(usb_ll_irq_clears));
  usb_ll_sequence = usb_ll_selected_ep = 0U;
  usb_ll_primask = 0U;
  usb_ll_require_mask = false;
  usb_ll_rx_head = usb_ll_rx_count = usb_ll_rx_pops = 0U;
  usb_ll_nak_after = 1U;
  usb_ll_nak_polls = 0U;
  usb_ll_nak_queued = false;
}

static inline unsigned usb_ll_grstctl_access(void)
{
  if (usb_ll_require_mask) assert(usb_ll_primask == 1U);
  usb_ll_fifo.accesses++;
  uint32_t value = usb_ll_controller.registers[0];
  if (value != usb_ll_fifo.expected) {
    usb_ll_fifo.commands++;
    usb_ll_fifo.command = value;
    usb_ll_fifo.command_before_idle |= !usb_ll_fifo.idle_seen;
    usb_ll_fifo.flush_seen = (value & (USB_OTG_GRSTCTL_TXFFLSH | USB_OTG_GRSTCTL_RXFFLSH)) != 0U;
    /* AHBIDL is read-only hardware state, unaffected by a command write. */
    if (usb_ll_fifo.idle_seen) value |= USB_OTG_GRSTCTL_AHBIDL;
  }
  if (!usb_ll_fifo.flush_seen) {
    usb_ll_fifo.idle_polls++;
    if (usb_ll_fifo.idle_after != 0U && usb_ll_fifo.idle_polls >= usb_ll_fifo.idle_after) {
      usb_ll_fifo.idle_seen = true;
      value |= USB_OTG_GRSTCTL_AHBIDL;
    }
  } else {
    usb_ll_fifo.flush_polls++;
    if (usb_ll_fifo.clear_after != 0U && usb_ll_fifo.flush_polls >= usb_ll_fifo.clear_after) {
      value &= ~(USB_OTG_GRSTCTL_TXFFLSH | USB_OTG_GRSTCTL_RXFFLSH);
    }
  }
  usb_ll_fifo.expected = value;
  usb_ll_controller.registers[0] = value;
  return 0U;
}

static inline void usb_ll_endpoint_access(unsigned is_in, unsigned ep, __IO uint32_t *ctl)
{
  assert(ep < 16U);
  if (usb_ll_require_mask) assert(usb_ll_primask == 1U);
  usb_ll_selected_ep = ep;
  usb_ll_endpoint_script_t *script = &usb_ll_ep_script[is_in][ep];
  script->accesses++;
  if ((*ctl & USB_OTG_DIEPCTL_SNAK) != 0U && script->snak_order == 0U) {
    script->snak_order = ++usb_ll_sequence;
  }
  if (is_in && (*ctl & USB_OTG_DIEPCTL_SNAK) != 0U) {
    script->nak_polls++;
    if (script->nak_after && script->nak_polls >= script->nak_after)
      usb_ll_in[ep].interrupts[0] |= USB_OTG_DIEPINT_INEPNE;
    if (script->natural_after && script->nak_polls >= script->natural_after) {
      *ctl &= ~USB_OTG_DIEPCTL_EPENA;
      usb_ll_in[ep].interrupts[0] |= USB_OTG_DIEPINT_XFRC;
    }
  }
  if ((*ctl & USB_OTG_DIEPCTL_EPDIS) != 0U && script->disable_order == 0U) {
    script->disable_order = ++usb_ll_sequence;
  }
  if (script->disable_order != 0U && (*ctl & USB_OTG_DIEPCTL_EPDIS) != 0U) {
    script->disable_polls++;
    if (script->clear_after != 0U && script->disable_polls >= script->clear_after &&
        (is_in || (usb_ll_controller.status[0] & USB_OTG_GINTSTS_BOUTNAKEFF) != 0U)) {
      *ctl &= ~USB_OTG_DIEPCTL_EPENA;
      if (script->done_after != 0U && script->disable_polls >= script->done_after) {
        *ctl &= ~USB_OTG_DIEPCTL_EPDIS;
        if (is_in) usb_ll_in[ep].interrupts[0] |= USB_OTG_DIEPINT_EPDISD;
        else usb_ll_out[ep].interrupts[0] |= USB_OTG_DOEPINT_EPDISD;
      }
    }
  }
}

static inline USB_OTG_INEndpointTypeDef *usb_ll_in_endpoint(const USB_OTG_GlobalTypeDef *instance, unsigned ep)
{
  assert(instance == &usb_ll_controller);
  usb_ll_endpoint_access(1U, ep, &usb_ll_in[ep].DIEPCTL);
  return &usb_ll_in[ep];
}

static inline USB_OTG_OUTEndpointTypeDef *usb_ll_out_endpoint(const USB_OTG_GlobalTypeDef *instance, unsigned ep)
{
  assert(instance == &usb_ll_controller);
  usb_ll_endpoint_access(0U, ep, &usb_ll_out[ep].DOEPCTL);
  return &usb_ll_out[ep];
}

static inline unsigned usb_ll_interrupt_access(unsigned is_in)
{
  if (usb_ll_require_mask) assert(usb_ll_primask == 1U);
  usb_ll_irq_accesses[is_in][usb_ll_selected_ep]++;
  return 0U;
}

static inline uint32_t usb_ll_endpoint_control(unsigned is_in, unsigned ep)
{
  return is_in ? usb_ll_in[ep].DIEPCTL : usb_ll_out[ep].DOEPCTL;
}

static inline void usb_ll_seed_endpoint(unsigned is_in, unsigned ep, bool enabled, unsigned clear_after)
{
  uint32_t attrs = USB_OTG_DIEPCTL_USBAEP | USB_OTG_DIEPCTL_MPSIZ |
                   USB_OTG_DIEPCTL_EPTYP | USB_OTG_DIEPCTL_SD0PID_SEVNFRM;
  if (is_in) attrs |= USB_OTG_DIEPCTL_TXFNUM;
  if (enabled) attrs |= USB_OTG_DIEPCTL_EPENA;
  if (is_in) usb_ll_in[ep].DIEPCTL = attrs;
  else usb_ll_out[ep].DOEPCTL = attrs;
  usb_ll_ep_script[is_in][ep].clear_after = clear_after;
  usb_ll_ep_script[is_in][ep].done_after = clear_after ? clear_after + 2U : 0U;
  usb_ll_ep_script[is_in][ep].nak_after = 1U;
  usb_ll_device.DAINTMSK = usb_ll_device.DEACHMSK = usb_ll_device.DIEPEMPMSK = UINT32_MAX;
}

static inline void usb_ll_clear_in(unsigned ep, uint32_t bits)
{
  usb_ll_in[ep].interrupts[0] &= ~bits;
  usb_ll_irq_clears[1][ep]++;
}

static inline void usb_ll_clear_out(unsigned ep, uint32_t bits)
{
  usb_ll_out[ep].interrupts[0] &= ~bits;
  usb_ll_irq_clears[0][ep]++;
}

#define GRSTCTL registers[usb_ll_grstctl_access()]
#define GINTSTS status[usb_ll_status_access()]
#define GRXSTSP rx_status[usb_ll_rx_pop()]
#define DIEPINT interrupts[usb_ll_interrupt_access(1U)]
#define DOEPINT interrupts[usb_ll_interrupt_access(0U)]
#define USBx_DEVICE ((void)USBx_BASE, &usb_ll_device)
#define USBx_INEP(ep) ((void)USBx_BASE, usb_ll_in_endpoint(&usb_ll_controller, (ep)))
#define USBx_OUTEP(ep) ((void)USBx_BASE, usb_ll_out_endpoint(&usb_ll_controller, (ep)))

#endif
