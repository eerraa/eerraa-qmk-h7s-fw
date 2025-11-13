#include "usb_monitor_via.h"

#ifdef USB_MONITOR_ENABLE

#include <string.h>

#include "eeconfig.h"
#include "usb.h"
#include "via.h"

usb_monitor_config_t usb_monitor_config = {
  .enable   = 0U,                                               // V251112R5: 기본값을 OFF로 변경
  .reserved = {0},
};

EECONFIG_DEBOUNCE_HELPER(usb_monitor, EECONFIG_USER_USB_INSTABILITY, usb_monitor_config);

static void usb_monitor_apply_defaults(void)
{
  usb_monitor_config.enable = 0U;                               // V251112R5: 기본값 OFF
  memset(usb_monitor_config.reserved, 0, sizeof(usb_monitor_config.reserved));
}

void usb_monitor_storage_init(void)
{
  eeconfig_init_usb_monitor();

  if (usb_monitor_config.enable > 1U)
  {
    usb_monitor_apply_defaults();
    eeconfig_flush_usb_monitor(true);
  }
}

void usb_monitor_storage_set_enable(bool enable)
{
  usb_monitor_config.enable = enable ? 1U : 0U;
  eeconfig_flag_usb_monitor(true);
}

void usb_monitor_storage_flush(bool force)
{
  eeconfig_flush_usb_monitor(force);
}

bool usb_monitor_storage_is_enabled(void)
{
  return usb_monitor_config.enable != 0U;
}

void usb_monitor_storage_apply_defaults(void)
{
  usb_monitor_apply_defaults();                                        // V251112R5: EEPROM 초기화 시 기본값 재적용
  eeconfig_flag_usb_monitor(true);
  eeconfig_flush_usb_monitor(true);
}

void via_qmk_usb_monitor_command(uint8_t *data, uint8_t length)
{
  if (length < 4 || data == NULL)
  {
    return;
  }

  uint8_t *command_id = &(data[0]);
  uint8_t *value_id   = &(data[2]);
  uint8_t *value_data = &(data[3]);

  if (*value_id != id_qmk_usb_monitor_toggle)
  {
    return;
  }

  switch (*command_id)
  {
    case id_custom_set_value:
    {
      bool enable = value_data[0] != 0U;
      usbInstabilityStore(enable);
      value_data[0] = (uint8_t)usbInstabilityIsEnabled();
      break;
    }

    case id_custom_get_value:
    {
      value_data[0] = (uint8_t)usbInstabilityIsEnabled();
      break;
    }

    default:
      break;
  }
}

#endif
