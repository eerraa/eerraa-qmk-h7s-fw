#pragma once
#include "hw_def.h"
void usbBootModeApplyDefaults(void);
bool usbIsResetPending(void);
#ifdef TEST_USB_TRANSPORT
#include "usbd_core.h"
#define USB_OTG_DEVICE_BASE 0x800U
#define USB_OTG_PCGCCTL_STOPCLK 1U
#define USB_OTG_PCGCCTL_GATECLK 2U
#define USB_OTG_DSTS_SUSPSTS 1U
#define USB_OTG_DCTL_RWUSIG 1U
#define USB_OTG_GINTSTS_CMOD 1U
#define USB_OTG_GINTSTS_SOF 8U
#define USB_OTG_GINTSTS_USBRST 0x1000U
#define USB_OTG_GINTSTS_ENUMDNE 0x2000U
#define USB_OTG_GINTSTS_WKUINT 0x80000000U
#define USB_OTG_GINTMSK_WUIM 0x80000000U
typedef struct { volatile uint32_t GINTSTS, GINTMSK; } USB_OTG_GlobalTypeDef;
typedef struct { volatile uint32_t DCTL, DSTS; } USB_OTG_DeviceTypeDef;
typedef struct {
  USB_OTG_GlobalTypeDef global;
  uint8_t reserved[USB_OTG_DEVICE_BASE - sizeof(USB_OTG_GlobalTypeDef)];
  USB_OTG_DeviceTypeDef device;
} test_otg_t;
typedef struct {
  USB_OTG_GlobalTypeDef *Instance;
  void *pData;
  struct { uint32_t low_power_enable, speed; } Init;
  uint32_t Setup[12];
  struct { uint8_t *xfer_buff; } IN_ep[16], OUT_ep[16];
} PCD_HandleTypeDef;
extern test_otg_t test_otg;
extern uint32_t test_pcgcctl;
void test_usb_ungate(PCD_HandleTypeDef *h);
#define __HAL_PCD_UNGATE_PHYCLOCK(h) test_usb_ungate(h)
#define __HAL_PCD_CLEAR_FLAG(h, flag) ((h)->Instance->GINTSTS &= ~(flag))
#define __DSB() ((void)0)
HAL_StatusTypeDef HAL_PCD_ActivateRemoteWakeup(PCD_HandleTypeDef *);
HAL_StatusTypeDef HAL_PCD_DeActivateRemoteWakeup(PCD_HandleTypeDef *);
uint8_t usbBootModeGetHsInterval(void);
void delay(uint32_t ms);
#endif
