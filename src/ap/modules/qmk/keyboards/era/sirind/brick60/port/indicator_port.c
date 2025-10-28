#include "indicator_port.h"

#include "color.h"
#include "eeconfig.h"
#include "port.h"
#include "rgblight.h"

// V251012R2: Brick60 RGB 인디케이터 구성은 4바이트 슬롯을 사용한다.
typedef union
{
  uint32_t raw;
  struct PACKED
  {
    uint8_t target;
    uint8_t val;
    uint8_t hue;
    uint8_t sat;
  };
} indicator_slot_t;

_Static_assert(sizeof(indicator_slot_t) == sizeof(uint32_t), "EECONFIG out of spec.");

static indicator_slot_t indicator_config = {.raw = 0};

static void indicator_via_get_value(uint8_t *data);
static void indicator_via_set_value(uint8_t *data);
static void indicator_via_save(void);

EECONFIG_DEBOUNCE_HELPER(indicator, EECONFIG_USER_LED_CAPS, indicator_config);

static void indicator_apply_config(void)
{
  rgblight_indicator_config_t runtime_config = {.raw = indicator_config.raw};
  rgblight_indicator_update_config(runtime_config);
}

static void indicator_apply_defaults(void)
{
  indicator_config.target = RGBLIGHT_INDICATOR_TARGET_CAPS;
  HSV default_hsv         = {HSV_GREEN};
  indicator_config.val    = default_hsv.v;
  indicator_config.hue    = default_hsv.h;  // V251012R2: HSV_GREEN 기본값 적용
  indicator_config.sat    = default_hsv.s;
}

void led_init_ports(void)
{
  eeconfig_init_indicator();

  if (indicator_config.raw == 0 || indicator_config.target > RGBLIGHT_INDICATOR_TARGET_NUM)
  {
    indicator_apply_defaults();  // V251012R2: 초기값 또는 손상된 데이터 복원
    eeconfig_flush_indicator(true);
  }

  indicator_apply_config();
  rgblight_indicator_sync_state();
}

void led_update_ports(led_t led_state)
{
  rgblight_indicator_apply_host_led(led_state);  // V251012R2: 호스트 LED → rgblight 인디케이터 연동
}

void indicator_port_via_command(uint8_t *data, uint8_t length)
{
  (void)length;

  uint8_t *command_id = &(data[0]);

  switch (*command_id)
  {
    case id_custom_set_value:
    {
      indicator_via_set_value(&(data[2]));
      break;
    }
    case id_custom_get_value:
    {
      indicator_via_get_value(&(data[2]));
      break;
    }
    case id_custom_save:
    {
      indicator_via_save();
      break;
    }
    default:
    {
      *command_id = id_unhandled;
      break;
    }
  }
}

static void indicator_via_get_value(uint8_t *data)
{
  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);

  switch (*value_id)
  {
    case id_qmk_custom_ind_selec:
    {
      value_data[0] = indicator_config.target;
      break;
    }
    case id_qmk_custom_ind_brightness:
    {
      value_data[0] = indicator_config.val;
      break;
    }
    case id_qmk_custom_ind_color:
    {
      value_data[0] = indicator_config.hue;
      value_data[1] = indicator_config.sat;
      break;
    }
  }
}

static void indicator_via_set_value(uint8_t *data)
{
  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);

  switch (*value_id)
  {
    case id_qmk_custom_ind_selec:
    {
      indicator_config.target = value_data[0];
      if (indicator_config.target > RGBLIGHT_INDICATOR_TARGET_NUM)
      {
        indicator_config.target = RGBLIGHT_INDICATOR_TARGET_OFF;
      }
      break;
    }
    case id_qmk_custom_ind_brightness:
    {
      indicator_config.val = value_data[0];
      break;
    }
    case id_qmk_custom_ind_color:
    {
      indicator_config.hue = value_data[0];
      indicator_config.sat = value_data[1];
      break;
    }
    default:
    {
      return;
    }
  }

  indicator_apply_config();
}

static void indicator_via_save(void)
{
  eeconfig_flush_indicator(true);
}
