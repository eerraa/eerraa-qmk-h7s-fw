#include "indicator_port.h"

#include "color.h"
#include "eeconfig.h"
#include "led.h"
#include "port.h"
#include "rgblight.h"

// V251012R3: rgblight 내부 정의를 그대로 사용해 구성 슬롯 중복 정의를 제거한다.
static rgblight_indicator_config_t indicator_config = {.raw = 0};

enum
{
  BRICK60_INDICATOR_START_INDEX = 0,
  BRICK60_INDICATOR_END_INDEX   = 29,
  BRICK60_INDICATOR_LED_COUNT   = (BRICK60_INDICATOR_END_INDEX - BRICK60_INDICATOR_START_INDEX + 1),
};  // V251016R8: Brick60 인디케이터는 RGB 0~29번을 고정 사용

static const rgblight_indicator_range_t brick60_indicator_ranges[] = {
  [RGBLIGHT_INDICATOR_TARGET_OFF] = {.start = 0, .count = 0},
  [RGBLIGHT_INDICATOR_TARGET_CAPS] = {
    .start = BRICK60_INDICATOR_START_INDEX,
    .count = BRICK60_INDICATOR_LED_COUNT,
  },
  [RGBLIGHT_INDICATOR_TARGET_SCROLL] = {
    .start = BRICK60_INDICATOR_START_INDEX,
    .count = BRICK60_INDICATOR_LED_COUNT,
  },
  [RGBLIGHT_INDICATOR_TARGET_NUM] = {
    .start = BRICK60_INDICATOR_START_INDEX,
    .count = BRICK60_INDICATOR_LED_COUNT,
  },
};  // V251016R8: Caps/Scroll/Num 인디케이터 모두 0~29번 RGB를 공유

_Static_assert(sizeof(rgblight_indicator_config_t) == sizeof(uint32_t),
               "EECONFIG out of spec.");  // V251012R3: 슬롯 크기 검증 유지

static void indicator_via_get_value(uint8_t *data);
static void indicator_via_set_value(uint8_t *data);
static void indicator_via_save(void);
static void indicator_apply_led_ranges(void);  // V251016R8: 인디케이터 대상별 LED 범위 등록
static bool indicator_target_from_host(uint8_t target, led_t host_state);  // V251016R8: Brick60 전용 타깃 판별기

EECONFIG_DEBOUNCE_HELPER(indicator, EECONFIG_USER_LED_CAPS, indicator_config);

static void indicator_apply_defaults(void)
{
  indicator_config.target = RGBLIGHT_INDICATOR_TARGET_CAPS;
  HSV default_hsv         = {HSV_GREEN};
  indicator_config.val    = default_hsv.v;
  indicator_config.hue    = default_hsv.h;  // V251012R2: HSV_GREEN 기본값 적용
  indicator_config.sat    = default_hsv.s;
}

// V251012R2: USB HID 호스트 LED 이벤트를 QMK LED 파이프라인으로 전달한다.
void usbHidSetStatusLed(uint8_t led_bits)
{
  led_set(led_bits);
}

void led_init_ports(void)
{
  rgblight_indicator_set_target_callback(indicator_target_from_host);  // V251016R8: Brick60 호스트 LED 매핑 등록

  indicator_apply_led_ranges();

  eeconfig_init_indicator();

  if (indicator_config.raw == 0 || indicator_config.target > RGBLIGHT_INDICATOR_TARGET_NUM)
  {
    indicator_apply_defaults();  // V251012R2: 초기값 또는 손상된 데이터 복원
    eeconfig_flush_indicator(true);
  }

  rgblight_indicator_update_config(indicator_config);  // V251012R4: 기본 구성 및 저장된 구성을 즉시 반영
  // V251012R7: rgblight 초기화 직후 재평가가 진행되므로 이 시점에서 동기화 호출은 생략
}

void led_update_ports(led_t led_state)
{
  rgblight_indicator_apply_host_led(led_state);  // V251012R2: 호스트 LED → rgblight 인디케이터 연동
}

static bool indicator_target_from_host(uint8_t target, led_t host_state)
{
  switch (target)
  {
    case RGBLIGHT_INDICATOR_TARGET_CAPS:
    {
      return host_state.caps_lock;
    }
    case RGBLIGHT_INDICATOR_TARGET_SCROLL:
    {
      return host_state.scroll_lock;
    }
    case RGBLIGHT_INDICATOR_TARGET_NUM:
    {
      return host_state.num_lock;
    }
    default:
    {
      return false;
    }
  }
}

static void indicator_apply_led_ranges(void)
{
  uint8_t range_count = (uint8_t)(sizeof(brick60_indicator_ranges) / sizeof(brick60_indicator_ranges[0]));

  rgblight_indicator_set_ranges(brick60_indicator_ranges, range_count);  // V251016R8: Caps/Scroll/Num 범위를 rgblight에 전달
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
  uint32_t prev_raw   = indicator_config.raw;  // V251013R5: 중복 갱신 여부 확인

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

  if (indicator_config.raw == prev_raw)
  {
    return;  // V251013R5: 동일 값 반복 전달 시 rgblight 경로 호출 생략
  }

  rgblight_indicator_update_config(indicator_config);  // V251012R4: VIA 변경 사항을 즉시 rgblight에 전달
}

static void indicator_via_save(void)
{
  eeconfig_flush_indicator(true);
}
