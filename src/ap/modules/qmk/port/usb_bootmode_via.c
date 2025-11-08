#include "usb_bootmode_via.h"

#ifdef BOOTMODE_ENABLE

#include "usb.h"
#include "via.h"

// V251108R1: channel 13 value ID 1/2 BootMode 처리기
void via_qmk_usb_bootmode_command(uint8_t *data, uint8_t length)
{
  if (length < 4 || data == NULL)
  {
    return;
  }

  uint8_t *command_id = &(data[0]);
  uint8_t *value_id   = &(data[2]);
  uint8_t *value_data = &(data[3]);

  switch (*command_id)
  {
    case id_custom_set_value:
    {
      if (*value_id == id_qmk_usb_bootmode_select)
      {
        UsbBootMode_t req_mode = (UsbBootMode_t)value_data[0];
        if (req_mode < USB_BOOT_MODE_MAX)
        {
          usbBootModeStore(req_mode);
        }
        else
        {
          value_data[0] = (uint8_t)usbBootModeGet();
        }
      }
      else if (*value_id == id_qmk_usb_bootmode_apply)
      {
        if (value_data[0] == 1U)
        {
          usbBootModeSaveAndReset(usbBootModeGet());
        }
        value_data[0] = 0U;
      }
      break;
    }

    case id_custom_get_value:
    {
      if (*value_id == id_qmk_usb_bootmode_select)
      {
        value_data[0] = (uint8_t)usbBootModeGet();
      }
      else if (*value_id == id_qmk_usb_bootmode_apply)
      {
        value_data[0] = 0U;
      }
      break;
    }

    case id_custom_save:
    default:
      break;
  }
}

#endif
