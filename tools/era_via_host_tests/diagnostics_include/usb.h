#pragma once

#include <stdint.h>

typedef enum UsbBootMode
{
  USB_BOOT_MODE_FS_1K = 0,
  USB_BOOT_MODE_HS_2K,
  USB_BOOT_MODE_HS_4K,
  USB_BOOT_MODE_HS_8K,
  USB_BOOT_MODE_MAX,
} UsbBootMode_t;

UsbBootMode_t usbBootModeGet(void);
