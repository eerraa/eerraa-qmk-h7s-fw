#pragma once

#include "led_port.h"
#include "color.h"
#include "eeconfig.h"
#include "rgblight.h"

// V251010R5 인디케이터 상태 공유 구조체 정의 및 모듈 분할

typedef union
{
  uint32_t raw;

  struct PACKED
  {
    uint8_t enable;
    HSV     hsv;
  };
} led_config_t;

_Static_assert(sizeof(led_config_t) == sizeof(uint32_t), "EECONFIG out of spec.");

typedef void (*indicator_flush_fn_t)(bool);

typedef struct
{
  led_config_t         default_config;
  uint8_t              start;
  uint8_t              end;
  uint8_t              host_mask;
  indicator_flush_fn_t flush;
} indicator_profile_t;

#define INDICATOR_LED_END(start, count) \
  ((uint8_t)(((start) + (count)) > RGBLIGHT_LED_COUNT ? RGBLIGHT_LED_COUNT : ((start) + (count))))

led_config_t *led_port_config_from_type(uint8_t led_type);
const indicator_profile_t *led_port_indicator_profile_from_type(uint8_t led_type);
void led_port_indicator_mark_color_dirty(uint8_t led_type);
RGB  led_port_indicator_get_rgb(uint8_t led_type, const led_config_t *config);
void led_port_indicator_refresh(bool force_flush);
bool led_port_indicator_config_valid(uint8_t led_type, bool *needs_migration);
void led_port_indicator_flush_config(uint8_t led_type);
bool led_port_should_light_indicator(const led_config_t *config, const indicator_profile_t *profile, led_t led_state);

led_t led_port_host_cached_state(void);
void  led_port_host_store_cached_state(led_t led_state);
bool  led_port_host_apply_pending(void);
