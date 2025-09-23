#pragma once

#include "hw_def.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _USE_HW_USB

#include <stdbool.h>
#include <stdint.h>

// [V250628R1] Enumerates supported USB boot frequencies.
typedef enum
{
  USB_BOOTMODE_HS_8K = 0,
  USB_BOOTMODE_HS_4K,
  USB_BOOTMODE_HS_2K,
  USB_BOOTMODE_FS_1K,
  USB_BOOTMODE_MAX
} UsbBootMode_t;

bool          usbModeInit(void);
UsbBootMode_t usbModeGet(void);
bool          usbModeIsFs(void);
uint8_t       usbModeGetHsInterval(void);
uint8_t       usbModeGetFsInterval(void);
const char   *usbModeGetName(UsbBootMode_t mode);
bool          usbModeSaveAndReset(UsbBootMode_t mode);

#endif

#ifdef __cplusplus
}
#endif

