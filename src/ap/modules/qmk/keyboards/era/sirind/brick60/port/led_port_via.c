#include "led_port_internal.h"

// V251010R5 VIA LED 커맨드 전용 모듈

static bool via_qmk_led_get_value(uint8_t led_type, uint8_t *data, uint8_t length);
static bool via_qmk_led_set_value(uint8_t led_type, uint8_t *data, uint8_t length);

void via_qmk_led_command(uint8_t led_type, uint8_t *data, uint8_t length)
{
  if (data == NULL || length == 0)
  {
    return;  // V251009R8 VIA 명령 유효성 검사: 데이터 포인터/길이 확인
  }

  if (led_type >= LED_TYPE_MAX_CH || length < 3)
  {
    data[0] = id_unhandled;
    return;
  }

  uint8_t *command_id        = &(data[0]);
  uint8_t *value_id_and_data = &(data[2]);
  uint8_t  payload_length    = length - 2;  // V251009R9 VIA 페이로드 가용 길이 계산

  switch (*command_id)
  {
    case id_custom_set_value:
      if (!via_qmk_led_set_value(led_type, value_id_and_data, payload_length))
      {
        *command_id = id_unhandled;  // V251009R9 VIA 서브커맨드 실패 시 에러 보고
      }
      break;
    case id_custom_get_value:
      if (!via_qmk_led_get_value(led_type, value_id_and_data, payload_length))
      {
        *command_id = id_unhandled;  // V251009R9 VIA 서브커맨드 실패 시 에러 보고
      }
      break;
    case id_custom_save:
      led_port_flush_indicator_config(led_type);
      break;
    default:
      *command_id = id_unhandled;
      break;
  }
}

static bool via_qmk_led_get_value(uint8_t led_type, uint8_t *data, uint8_t length)
{
  if (data == NULL || length == 0)
  {
    return false;  // V251009R9 VIA 응답 버퍼 가용성 검증
  }

  led_config_t *config = led_port_config_from_type(led_type);
  if (config == NULL)
  {
    return false;
  }

  uint8_t *value_id     = &(data[0]);
  uint8_t *value_data   = &(data[1]);
  uint8_t  value_length = (length > 0) ? (length - 1) : 0;

  switch (*value_id)
  {
    case id_qmk_led_enable:
      if (value_length < 1)
      {
        return false;  // V251009R9 VIA 응답 길이 부족 시 실패 처리
      }
      value_data[0] = config->enable;
      return true;
    case id_qmk_led_brightness:
      if (value_length < 1)
      {
        return false;  // V251009R9 VIA 응답 길이 부족 시 실패 처리
      }
      value_data[0] = config->hsv.v;
      return true;
    case id_qmk_led_color:
      if (value_length < 2)
      {
        return false;  // V251009R9 VIA 응답 길이 부족 시 실패 처리
      }
      value_data[0] = config->hsv.h;
      value_data[1] = config->hsv.s;
      return true;
    default:
      break;
  }

  return false;
}

static bool via_qmk_led_set_value(uint8_t led_type, uint8_t *data, uint8_t length)
{
  if (data == NULL || length == 0)
  {
    return false;  // V251009R9 VIA 설정 페이로드 가용성 검증
  }

  led_config_t *config = led_port_config_from_type(led_type);
  if (config == NULL)
  {
    return false;
  }

  uint8_t *value_id     = &(data[0]);
  uint8_t *value_data   = &(data[1]);
  uint8_t  value_length = (length > 0) ? (length - 1) : 0;
  bool     needs_refresh = false;  // V251009R1 설정 변경 시에만 RGBlight 갱신

  switch (*value_id)
  {
    case id_qmk_led_enable:
    {
      if (value_length < 1)
      {
        return false;  // V251009R9 VIA 설정 길이 부족 시 실패 처리
      }
      uint8_t enable = value_data[0] ? 1 : 0;
      if (config->enable != enable)
      {
        config->enable = enable;
        needs_refresh  = true;
      }
      break;
    }
    case id_qmk_led_brightness:
      if (value_length < 1)
      {
        return false;  // V251009R9 VIA 설정 길이 부족 시 실패 처리
      }
      if (config->hsv.v != value_data[0])
      {
        config->hsv.v = value_data[0];
        led_port_mark_indicator_color_dirty(led_type);  // V251009R3 밝기 변경 시 색상 캐시 무효화
        needs_refresh  = true;
      }
      break;
    case id_qmk_led_color:
    {
      if (value_length < 2)
      {
        return false;  // V251009R9 VIA 설정 길이 부족 시 실패 처리
      }
      uint8_t hue        = value_data[0];
      uint8_t saturation = value_data[1];
      if (config->hsv.h != hue || config->hsv.s != saturation)
      {
        config->hsv.h = hue;
        config->hsv.s = saturation;
        led_port_mark_indicator_color_dirty(led_type);  // V251009R3 색상 변경 시 캐시 재계산
        needs_refresh  = true;
      }
      break;
    }
    default:
      return false;  // V251009R9 알 수 없는 VIA 서브커맨드 거부
  }

  if (needs_refresh)
  {
    led_port_refresh_indicator_display();
  }

  return true;
}

