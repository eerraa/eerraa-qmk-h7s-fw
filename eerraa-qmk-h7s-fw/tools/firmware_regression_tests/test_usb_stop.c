#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "usb_stop_defs.inc"

typedef struct { volatile uint32_t GAHBCFG, GUSBCFG, GCCFG; } USB_OTG_GlobalTypeDef;
typedef struct { volatile uint32_t DCTL; } USB_OTG_DeviceTypeDef;
typedef struct {
  USB_OTG_GlobalTypeDef *Instance;
  HAL_LockTypeDef Lock;
  struct { uint8_t battery_charging_enable; } Init;
} PCD_HandleTypeDef;
typedef struct { void *pData; } USBD_HandleTypeDef;

static USB_OTG_GlobalTypeDef controller;
static USB_OTG_DeviceTypeDef device;
static volatile uint32_t pcgcctl;
static PCD_HandleTypeDef handle;
static USBD_HandleTypeDef stack;
static HAL_StatusTypeDef flush_status;
static unsigned flush_calls;

/* The address aliases are plain memory. This fixture uses the actual LL
 * disconnect/global-interrupt routines and injects only the FIFO return status;
 * it does not emulate PHY clocks, W1C, electrical disconnect or FIFO progress. */
#define USBx_DEVICE ((void)USBx_BASE, &device)
#define USBx_PCGCCTL (*( (void)USBx_BASE, &pcgcctl))

static HAL_StatusTypeDef USB_FlushTxFifo(USB_OTG_GlobalTypeDef *instance, uint32_t fifo)
{
  assert(instance == &controller && fifo == 0x10U);
  assert(handle.Lock == HAL_LOCKED);
  assert((controller.GAHBCFG & USB_OTG_GAHBCFG_GINT) == 0U);
  assert((device.DCTL & USB_OTG_DCTL_SDIS) != 0U);
  assert((pcgcctl & (USB_OTG_PCGCCTL_STOPCLK | USB_OTG_PCGCCTL_GATECLK)) == 0U);
  /* PHY cleanup is still after the flush, including its failure path. */
  assert((controller.GCCFG & USB_OTG_GCCFG_PWRDWN) != 0U);
  flush_calls++;
  return flush_status;
}

#include "usb_stop_functions.inc"

static void seed(unsigned fs_phy, unsigned battery, HAL_StatusTypeDef status)
{
  controller.GAHBCFG = USB_OTG_GAHBCFG_GINT | 0x200U;
  controller.GUSBCFG = fs_phy ? USB_OTG_GUSBCFG_PHYSEL : 0U;
  controller.GCCFG = USB_OTG_GCCFG_PWRDWN | 0x400U;
  device.DCTL = 0x200U;
  pcgcctl = USB_OTG_PCGCCTL_STOPCLK | USB_OTG_PCGCCTL_GATECLK | 0x100U;
  handle.Instance = &controller;
  handle.Lock = HAL_UNLOCKED;
  handle.Init.battery_charging_enable = (uint8_t)battery;
  stack.pData = &handle;
  flush_status = status;
  flush_calls = 0U;
}

static void check_cleanup(unsigned fs_phy, unsigned battery)
{
  assert(flush_calls == 1U && handle.Lock == HAL_UNLOCKED);
  assert(handle.Instance == &controller && handle.Init.battery_charging_enable == battery);
  assert(controller.GAHBCFG == 0x200U);
  assert(controller.GUSBCFG == (fs_phy ? USB_OTG_GUSBCFG_PHYSEL : 0U));
  assert(device.DCTL == (USB_OTG_DCTL_SDIS | 0x200U) && pcgcctl == 0x100U);
  assert(controller.GCCFG == (fs_phy && battery ? 0x400U : USB_OTG_GCCFG_PWRDWN | 0x400U));
}

static USBD_StatusTypeDef mapped(HAL_StatusTypeDef status)
{
  if (status == HAL_OK) return USBD_OK;
  if (status == HAL_BUSY) return USBD_BUSY;
  return USBD_FAIL;
}

static void check_status(unsigned fs_phy, unsigned battery, HAL_StatusTypeDef status)
{
  seed(fs_phy, battery, status);
#ifndef USB_STOP_BRIDGE_ONLY
  assert(HAL_PCD_Stop(&handle) == status);
  check_cleanup(fs_phy, battery);
  seed(fs_phy, battery, status);
#endif
  assert(USBD_LL_Stop(&stack) == mapped(status));
  check_cleanup(fs_phy, battery);
}

static void check_busy(unsigned fs_phy, unsigned battery, bool bridge)
{
  seed(fs_phy, battery, HAL_ERROR);
  handle.Lock = HAL_LOCKED;
  USB_OTG_GlobalTypeDef before = controller;
  uint32_t before_dctl = device.DCTL, before_pcgcctl = pcgcctl;
  if (bridge) assert(USBD_LL_Stop(&stack) == USBD_BUSY);
  else assert(HAL_PCD_Stop(&handle) == HAL_BUSY);
  assert(handle.Lock == HAL_LOCKED && flush_calls == 0U);
  assert(controller.GAHBCFG == before.GAHBCFG && controller.GUSBCFG == before.GUSBCFG && controller.GCCFG == before.GCCFG);
  assert(device.DCTL == before_dctl && pcgcctl == before_pcgcctl);
}

int main(void)
{
  const HAL_StatusTypeDef statuses[] = {HAL_OK, HAL_ERROR, HAL_TIMEOUT, HAL_BUSY};
  for (unsigned fs_phy = 0U; fs_phy < 2U; fs_phy++) {
    for (unsigned battery = 0U; battery < 2U; battery++) {
      for (unsigned i = 0U; i < sizeof(statuses) / sizeof(statuses[0]); i++) check_status(fs_phy, battery, statuses[i]);
      check_busy(fs_phy, battery, false);
      check_busy(fs_phy, battery, true);
    }
  }
  puts("PASS: production HAL Stop/bridge preserves every FIFO status, unlocks and completes PHY cleanup after failure; busy lock makes no controller/FIFO changes");
  return 0;
}
