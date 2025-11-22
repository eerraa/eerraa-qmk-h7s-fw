#include "indicator_port.h"
#include "ver_port.h"
#include "sys_port.h"
#include "bootmode.h"
#include "usb_monitor.h"
#include "debounce_profile.h"

static void via_handle_usb_polling_channel(uint8_t *data, uint8_t length);  // V251108R8: BootMode/USB 모니터 분기 공통화


void via_custom_value_command_kb(uint8_t *data, uint8_t length)
{
  // data = [ command_id, channel_id, value_id, value_data ]
  uint8_t *command_id = &(data[0]);
  uint8_t *channel_id = &(data[1]);

  if (*channel_id == id_qmk_usb_polling)
  {
    via_handle_usb_polling_channel(data, length);                   // V251108R8: USB POLLING 채널 분기를 단일 함수로 처리
    return;
  }

  if (*channel_id == id_custom_channel)
  {
    indicator_port_via_command(data, length);  // V251012R2: 커스텀 인디케이터 채널 처리
    return;
  }

  if (*channel_id == id_qmk_key_response)
  {
    if (debounce_profile_handle_via_command(data, length))
    {
      return;
    }
  }

  if (*channel_id == id_qmk_version)
  {
    via_qmk_version(data, length);
    return;
  }

  if (*channel_id == id_qmk_system)
  {
    via_qmk_system(data, length);
    return;
  }

#ifdef KILL_SWITCH_ENABLE
  if (*channel_id == id_qmk_kill_switch_lr)
  {
    via_qmk_kill_swtich_command(0, data, length);
    return;
  }
  if (*channel_id == id_qmk_kill_switch_ud)
  {
    via_qmk_kill_swtich_command(1, data, length);
    return;
  }
#endif
#ifdef KKUK_ENABLE
  if (*channel_id == id_qmk_kkuk)
  {
    via_qmk_kkuk_command(data, length);
    return;
  }
#endif

  // Return the unhandled state
  *command_id = id_unhandled;
}

static void via_handle_usb_polling_channel(uint8_t *data, uint8_t length)
{
  uint8_t *command_id = &(data[0]);

  if (length < 4U)
  {
    *command_id = id_unhandled;
    return;
  }

  uint8_t value_id = data[2];

#ifdef BOOTMODE_ENABLE
  if (*command_id == id_custom_save ||
      value_id == id_qmk_usb_bootmode_select ||
      value_id == id_qmk_usb_bootmode_apply)
  {
    via_qmk_usb_bootmode_command(data, length);
    return;
  }
#endif

#ifdef USB_MONITOR_ENABLE
  if (value_id == id_qmk_usb_monitor_toggle)
  {
    via_qmk_usb_monitor_command(data, length);
    return;
  }
#endif

  *command_id = id_unhandled;
}

