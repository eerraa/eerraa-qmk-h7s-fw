#include "sys_port.h"
#include "bootloader.h"
#include "timer.h"



enum via_qmk_ver_item {
  id_qmk_system_dfu            = 1,
  id_qmk_system_eep_reset_0    = 2,
  id_qmk_system_eep_reset_1    = 3,
  id_qmk_system_eep_reset_done = 4,
};

#define SYS_EEP_RESET_BIT_0             (1U << 0)
#define SYS_EEP_RESET_BIT_1             (1U << 1)
#define SYS_EEP_RESET_BIT_DONE          (1U << 2)
#define SYS_EEP_RESET_MASK              (SYS_EEP_RESET_BIT_0 | SYS_EEP_RESET_BIT_1 | SYS_EEP_RESET_BIT_DONE)
#define SYS_EEP_RESET_CONFIRM_WINDOW_MS (10000U)  // V260901R1: 첫 확인부터 10초. 이후 토글은 창을 늘리지 않는다


static void via_qmk_sys_get_value(uint8_t *data);
static void via_qmk_sys_set_value(uint8_t *data);
static uint8_t sys_eep_reset_bit(uint8_t value_id);
static void sys_eep_reset_expire(void);


static uint8_t  eep_reset_confirm = 0x00;
static uint32_t eep_reset_confirm_started_ms = 0;


static uint8_t sys_eep_reset_bit(uint8_t value_id)
{
  switch (value_id)
  {
    case id_qmk_system_eep_reset_0:
      return SYS_EEP_RESET_BIT_0;
    case id_qmk_system_eep_reset_1:
      return SYS_EEP_RESET_BIT_1;
    case id_qmk_system_eep_reset_done:
      return SYS_EEP_RESET_BIT_DONE;
    default:
      return 0U;
  }
}

static void sys_eep_reset_expire(void)
{
  if (eep_reset_confirm != 0U &&
      timer_elapsed32(eep_reset_confirm_started_ms) >= SYS_EEP_RESET_CONFIRM_WINDOW_MS)
  {
    eep_reset_confirm = 0U;  // V260901R1: 부분 확인은 10초 뒤 폐기한다. VIA GET이 꺼짐을 돌려준다
  }
}

void via_qmk_system_task(void)
{
  sys_eep_reset_expire();
}

void via_qmk_system(uint8_t *data, uint8_t length)
{
  // data = [ command_id, channel_id, value_id, value_data ]
  uint8_t *command_id        = &(data[0]);
  uint8_t *value_id_and_data = &(data[2]);

  switch (*command_id)
  {
    case id_custom_set_value:
      {
        if (length < 4U)
        {
          *command_id = id_unhandled;
          break;
        }
        via_qmk_sys_set_value(value_id_and_data);
        break;
      }
    case id_custom_get_value:
      {
        if (length < 4U)
        {
          *command_id = id_unhandled;
          break;
        }
        via_qmk_sys_get_value(value_id_and_data);
        break;
      }
    case id_custom_save:
      {
        break;
      }
    default:
      {
        *command_id = id_unhandled;
        break;
      }
  }
}

void via_qmk_sys_set_value(uint8_t *data)
{
  // data = [ value_id, value_data ]
  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);
  uint8_t  bit;

  switch (*value_id)
  {
    case id_qmk_system_dfu:
      {
        if (value_data[0] != 0U)
        {
          bootloader_jump_deferred();  // V250310R6: VIA 응답 송신 이후 부트모드로 전환
          value_data[0] = 1;           // V260311R1: Jump to Boot 토글은 VIA 요청값 echo를 유지해 응답 mismatch를 방지
        }
        break;  // V260901R1: SET 0 은 점프하지 않는다. GET 은 항상 0
      }
    default:
      {
        sys_eep_reset_expire();
        bit = sys_eep_reset_bit(*value_id);
        if (bit == 0U)
        {
          break;
        }
        if (value_data[0] != 0U)
        {
          if (eep_reset_confirm == 0U)
          {
            eep_reset_confirm_started_ms = timer_read32();
          }
          eep_reset_confirm |= bit;
          if ((eep_reset_confirm & SYS_EEP_RESET_MASK) == SYS_EEP_RESET_MASK)
          {
            eep_reset_confirm = 0U;
            eeprom_req_clean();
          }
        }
        else
        {
          eep_reset_confirm &= (uint8_t)~bit;  // V260901R1: 공식 JSON 토글 OFF 는 해당 비트만 내린다
        }
        break;
      }
  }
}

void via_qmk_sys_get_value(uint8_t *data)
{
  // data = [ value_id, value_data ]
  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);
  uint8_t  bit;

  switch (*value_id)
  {
    case id_qmk_system_dfu:
      {
        value_data[0] = 0;
        break;
      }
    default:
      {
        sys_eep_reset_expire();
        bit = sys_eep_reset_bit(*value_id);
        if (bit != 0U)
        {
          value_data[0] = (eep_reset_confirm & bit) ? 1U : 0U;
        }
        break;
      }
  }
}
