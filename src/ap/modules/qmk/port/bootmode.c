#include "bootmode.h"

#ifdef BOOTMODE_ENABLE

#include "usb.h"
#include "via.h"

// V251108R3: VIA 선택값을 리셋 전까지 보류하고 Apply 요청은 메인 루프에서 처리
static UsbBootMode_t pending_boot_mode = USB_BOOT_MODE_FS_1K;
static bool          pending_boot_mode_init = false;
static uint8_t       bootmode_encode_via_value(UsbBootMode_t mode);          // V251113R1: VIA JSON과 열거형 간 값 변환
static UsbBootMode_t bootmode_decode_via_value(uint8_t via_value);

static void bootmode_sync_pending(void)
{
  if (pending_boot_mode_init == false)
  {
    pending_boot_mode      = usbBootModeGet();
    pending_boot_mode_init = true;
  }
}

// V251108R1: channel 13 value ID 1/2 BootMode 처리기
void via_qmk_usb_bootmode_command(uint8_t *data, uint8_t length)
{
  if (data == NULL)
  {
    return;
  }

  bootmode_sync_pending();

  uint8_t *command_id = &(data[0]);

  if ((*command_id != id_custom_save) && length < 4)
  {
    return;
  }

  uint8_t *value_id   = &(data[2]);
  uint8_t *value_data = &(data[3]);

  switch (*command_id)
  {
    case id_custom_set_value:
    {
      if (*value_id == id_qmk_usb_bootmode_select)
      {
        UsbBootMode_t req_mode = bootmode_decode_via_value(value_data[0]);      // V251113R1: VIA 옵션 순서와 열거형 순서를 분리
        if (req_mode < USB_BOOT_MODE_MAX)
        {
          pending_boot_mode = req_mode;  // V251108R1: 값만 보류, 실제 적용은 Apply 토글 시점
        }
        value_data[0] = bootmode_encode_via_value(pending_boot_mode);
      }
      else if (*value_id == id_qmk_usb_bootmode_apply)
      {
        uint8_t request = value_data[0];                                  // V251109R5: VIA echo 유지
        if (request != 0U)
        {
          usbBootModeScheduleApply(pending_boot_mode);  // V251108R6: 동일 값이라도 Apply 요청 시 재부팅
        }
        value_data[0] = request;
      }
      break;
    }

    case id_custom_get_value:
    {
      if (*value_id == id_qmk_usb_bootmode_select)
      {
        value_data[0] = bootmode_encode_via_value(pending_boot_mode);  // V251113R1
      }
      else if (*value_id == id_qmk_usb_bootmode_apply)
      {
        value_data[0] = 0U;
      }
      break;
    }

    case id_custom_save:
    {
      break;  // V251108R4: 저장 명령은 Indicator와 동일하게 no-op
    }

    default:
      *command_id = id_unhandled;
      break;
  }
}

static uint8_t bootmode_encode_via_value(UsbBootMode_t mode)
{
  switch (mode)
  {
    case USB_BOOT_MODE_HS_8K:
      return 0U;
    case USB_BOOT_MODE_HS_4K:
      return 1U;
    case USB_BOOT_MODE_HS_2K:
      return 2U;
    case USB_BOOT_MODE_FS_1K:
    default:
      return 3U;
  }
}

static UsbBootMode_t bootmode_decode_via_value(uint8_t via_value)
{
  switch (via_value)
  {
    case 0U:
      return USB_BOOT_MODE_HS_8K;
    case 1U:
      return USB_BOOT_MODE_HS_4K;
    case 2U:
      return USB_BOOT_MODE_HS_2K;
    case 3U:
      return USB_BOOT_MODE_FS_1K;
    default:
      return USB_BOOT_MODE_MAX;
  }
}

#endif
