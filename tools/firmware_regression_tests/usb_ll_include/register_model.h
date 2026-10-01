#ifndef USB_LL_REGISTER_MODEL_H
#define USB_LL_REGISTER_MODEL_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* This is a register-access script, not a USB peripheral emulator. Commands
 * become visible at the following aliased access; hardware progress is advanced
 * at access boundaries. EP0, NAK-effective latency, bus traffic and silicon
 * timing are not modeled. Interrupt fields record accesses but use plain memory:
 * a C assignment is not a W1C emulation and cannot prove hardware clearing. */
#define __IO volatile

typedef struct { __IO uint32_t registers[1]; } USB_OTG_GlobalTypeDef;
typedef struct { __IO uint32_t DIEPCTL, interrupts[1]; } USB_OTG_INEndpointTypeDef;
typedef struct { __IO uint32_t DOEPCTL, interrupts[1]; } USB_OTG_OUTEndpointTypeDef;
typedef struct { __IO uint32_t DAINTMSK, DEACHMSK, DIEPEMPMSK; } USB_OTG_DeviceTypeDef;

typedef struct {
  unsigned accesses, disable_polls, clear_after;
  unsigned snak_order, disable_order;
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
static unsigned usb_ll_irq_accesses[2][16];
static uint32_t usb_ll_primask;
static bool usb_ll_require_mask;

static inline void usb_ll_reset(void)
{
  memset(&usb_ll_controller, 0, sizeof(usb_ll_controller));
  memset(&usb_ll_device, 0, sizeof(usb_ll_device));
  memset(usb_ll_in, 0, sizeof(usb_ll_in));
  memset(usb_ll_out, 0, sizeof(usb_ll_out));
  memset(usb_ll_ep_script, 0, sizeof(usb_ll_ep_script));
  memset(&usb_ll_fifo, 0, sizeof(usb_ll_fifo));
  memset(usb_ll_irq_accesses, 0, sizeof(usb_ll_irq_accesses));
  usb_ll_sequence = usb_ll_selected_ep = 0U;
  usb_ll_primask = 0U;
  usb_ll_require_mask = false;
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
  if ((*ctl & USB_OTG_DIEPCTL_EPDIS) != 0U && script->disable_order == 0U) {
    script->disable_order = ++usb_ll_sequence;
  }
  if (script->disable_order != 0U && (*ctl & USB_OTG_DIEPCTL_EPENA) != 0U) {
    script->disable_polls++;
    if (script->clear_after != 0U && script->disable_polls >= script->clear_after) {
      *ctl &= ~(USB_OTG_DIEPCTL_EPENA | USB_OTG_DIEPCTL_EPDIS);
      if (is_in) usb_ll_in[ep].interrupts[0] |= USB_OTG_DIEPINT_EPDISD;
      else usb_ll_out[ep].interrupts[0] |= USB_OTG_DOEPINT_EPDISD;
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
  usb_ll_device.DAINTMSK = usb_ll_device.DEACHMSK = usb_ll_device.DIEPEMPMSK = UINT32_MAX;
}

#define GRSTCTL registers[usb_ll_grstctl_access()]
#define DIEPINT interrupts[usb_ll_interrupt_access(1U)]
#define DOEPINT interrupts[usb_ll_interrupt_access(0U)]
#define USBx_DEVICE ((void)USBx_BASE, &usb_ll_device)
#define USBx_INEP(ep) ((void)USBx_BASE, usb_ll_in_endpoint(&usb_ll_controller, (ep)))
#define USBx_OUTEP(ep) ((void)USBx_BASE, usb_ll_out_endpoint(&usb_ll_controller, (ep)))

#endif
