#include "usb_mode.h"

#ifdef _USE_HW_USB

#include <string.h>

#include "cli.h"
#include "eeprom.h"
#include "log.h"
#include "reset.h"

#include "ap/modules/qmk/port/port.h"

#define USB_MODE_DEFAULT USB_BOOTMODE_HS_8K

static UsbBootMode_t boot_mode = USB_MODE_DEFAULT;
static uint8_t       hs_interval = 1;
static uint8_t       fs_interval = 1;

static const char *usb_mode_names[USB_BOOTMODE_MAX] =
  {
    "HS-8K",
    "HS-4K",
    "HS-2K",
    "FS-1K",
  };

// [V250628R1] Map boot mode to HID polling intervals.
static void usbModeUpdateInterval(void)
{
  switch (boot_mode)
  {
    case USB_BOOTMODE_HS_8K:
      hs_interval = 1;
      fs_interval = 1;
      break;

    case USB_BOOTMODE_HS_4K:
      hs_interval = 2;
      fs_interval = 1;
      break;

    case USB_BOOTMODE_HS_2K:
      hs_interval = 3;
      fs_interval = 1;
      break;

    case USB_BOOTMODE_FS_1K:
    default:
      hs_interval = 1;
      fs_interval = 1;
      break;
  }
}

static bool usbModeReadRaw(uint8_t *raw_mode)
{
  return eepromRead((uint32_t)EECONFIG_USER_BOOTMODE, raw_mode, 1);
}

static bool usbModeWriteRaw(uint8_t raw_mode)
{
  return eepromWrite((uint32_t)EECONFIG_USER_BOOTMODE, &raw_mode, 1);
}

static UsbBootMode_t usbModeSanitize(uint8_t raw_mode)
{
  if (raw_mode < USB_BOOTMODE_MAX)
  {
    return (UsbBootMode_t)raw_mode;
  }
  return USB_MODE_DEFAULT;
}

static void usbModeLogCurrent(void)
{
  logPrintf("[  ] USB BootMode : %s\n", usbModeGetName(boot_mode));
}

#ifdef _USE_HW_CLI
static bool usbModeParseArg(const char *arg, UsbBootMode_t *mode)
{
  if (strcmp(arg, "8k") == 0)
  {
    *mode = USB_BOOTMODE_HS_8K;
    return true;
  }
  if (strcmp(arg, "4k") == 0)
  {
    *mode = USB_BOOTMODE_HS_4K;
    return true;
  }
  if (strcmp(arg, "2k") == 0)
  {
    *mode = USB_BOOTMODE_HS_2K;
    return true;
  }
  if (strcmp(arg, "1k") == 0)
  {
    *mode = USB_BOOTMODE_FS_1K;
    return true;
  }
  return false;
}

static void cliBoot(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("Boot mode : %s\n", usbModeGetName(boot_mode));
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "set"))
  {
    UsbBootMode_t mode;

    if (usbModeParseArg(args->getStr(1), &mode))
    {
      cliPrintf("Apply boot mode : %s\n", usbModeGetName(mode));
      if (!usbModeSaveAndReset(mode))
      {
        cliPrintf("Fail to update boot mode\n");
      }
    }
    else
    {
      cliPrintf("Invalid mode. use 8k|4k|2k|1k\n");
    }
    ret = true;
  }

  if (!ret)
  {
    cliPrintf("boot info\n");
    cliPrintf("boot set 8k\n");
    cliPrintf("boot set 4k\n");
    cliPrintf("boot set 2k\n");
    cliPrintf("boot set 1k\n");
  }
}
#endif

bool usbModeInit(void)
{
  uint8_t raw_mode = (uint8_t)USB_MODE_DEFAULT;

  if (!usbModeReadRaw(&raw_mode) || raw_mode >= USB_BOOTMODE_MAX)
  {
    raw_mode = (uint8_t)USB_MODE_DEFAULT;
    // [V250628R1] Initialise EEPROM slot when empty or corrupted.
    (void)usbModeWriteRaw(raw_mode);
  }

  boot_mode = usbModeSanitize(raw_mode);
  usbModeUpdateInterval();
  usbModeLogCurrent();

#ifdef _USE_HW_CLI
  cliAdd("boot", cliBoot);
#endif

  return true;
}

UsbBootMode_t usbModeGet(void)
{
  return boot_mode;
}

bool usbModeIsFs(void)
{
  return boot_mode == USB_BOOTMODE_FS_1K;
}

uint8_t usbModeGetHsInterval(void)
{
  return hs_interval;
}

uint8_t usbModeGetFsInterval(void)
{
  return fs_interval;
}

const char *usbModeGetName(UsbBootMode_t mode)
{
  if (mode < USB_BOOTMODE_MAX)
  {
    return usb_mode_names[mode];
  }
  return "UNKNOWN";
}

bool usbModeSaveAndReset(UsbBootMode_t mode)
{
  uint8_t raw_mode = (uint8_t)mode;

  if (mode >= USB_BOOTMODE_MAX)
  {
    return false;
  }

  if (!usbModeWriteRaw(raw_mode))
  {
    return false;
  }

  boot_mode = usbModeSanitize(raw_mode);
  usbModeUpdateInterval();

  // [V250628R1] Reboot to apply the newly stored USB boot mode.
  resetToReset();
  return true;
}

#endif

