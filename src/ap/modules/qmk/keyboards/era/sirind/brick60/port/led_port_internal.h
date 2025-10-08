#pragma once

#include "led_port.h"
#include "color.h"
#include "eeconfig.h"
#include "rgblight.h"

// V251010R5 LED 포트 모듈 분할 내부 공유 헤더

typedef union
{
  uint32_t raw;

  struct PACKED
  {
    uint8_t enable;
    HSV     hsv;
  };
} led_config_t;

typedef void (*indicator_flush_fn_t)(bool);

typedef struct
{
  led_config_t         default_config;
  uint8_t              start;
  uint8_t              end;
  uint8_t              host_mask;
  indicator_flush_fn_t flush;
} indicator_profile_t;

enum via_qmk_led_value
{
  id_qmk_led_enable     = 1,
  id_qmk_led_brightness = 2,
  id_qmk_led_color      = 3,
};

led_config_t *led_port_config_from_type(uint8_t led_type);
const indicator_profile_t *led_port_indicator_profile_from_type(uint8_t led_type);
void led_port_mark_indicator_color_dirty(uint8_t led_type);
void led_port_refresh_indicator_display(void);
void led_port_flush_indicator_config(uint8_t led_type);

led_t led_port_get_indicator_state(void);
void led_port_set_indicator_state(led_t state);

led_t led_port_get_host_state(void);
void led_port_set_host_state(led_t state);

