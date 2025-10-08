#include <stdint.h>

#include "led_port_internal.h"

// V251010R5 인디케이터 합성 모듈 분리 및 캐시 관리 일원화

static RGB indicator_rgb_cache[LED_TYPE_MAX_CH];
static bool indicator_rgb_dirty[LED_TYPE_MAX_CH] = {0};

static led_config_t led_config[LED_TYPE_MAX_CH];
static uint8_t indicator_last_active_mask = 0;                                  // V251010R7 마지막 DMA 커밋 시 활성화된 호스트 LED 마스크

EECONFIG_DEBOUNCE_HELPER(led_caps,   EECONFIG_USER_LED_CAPS,   led_config[LED_TYPE_CAPS]);      // V251010R5 인디케이터 EEPROM 매크로 분리 재정의
EECONFIG_DEBOUNCE_HELPER(led_scroll, EECONFIG_USER_LED_SCROLL, led_config[LED_TYPE_SCROLL]);    // V251010R5 인디케이터 EEPROM 매크로 분리 재정의
EECONFIG_DEBOUNCE_HELPER(led_num,    EECONFIG_USER_LED_NUM,    led_config[LED_TYPE_NUM]);       // V251010R5 인디케이터 EEPROM 매크로 분리 재정의

static const indicator_profile_t indicator_profiles[LED_TYPE_MAX_CH] = {
  [LED_TYPE_CAPS] = {
    .default_config = {.enable = true, .hsv = {0, 255, 255}},
    .start          = 0,
    .end            = INDICATOR_LED_END(0, 10),
    .host_mask      = (1 << 1),
    .flush          = eeconfig_flush_led_caps,
  },
  [LED_TYPE_SCROLL] = {
    .default_config = {.enable = true, .hsv = {170, 255, 255}},
    .start          = 10,
    .end            = INDICATOR_LED_END(10, 10),
    .host_mask      = (1 << 2),
    .flush          = eeconfig_flush_led_scroll,
  },
  [LED_TYPE_NUM] = {
    .default_config = {.enable = true, .hsv = {85, 255, 255}},
    .start          = 20,
    .end            = INDICATOR_LED_END(20, 10),
    .host_mask      = (1 << 0),
    .flush          = eeconfig_flush_led_num,
  },
};

static void refresh_indicator_display(void)
{
  if (!is_rgblight_initialized)
  {
    return;  // V251010R5 RGBlight 초기화 전에는 합성하지 않음
  }

  rgblight_set();
}

led_config_t *led_port_config_from_type(uint8_t led_type)
{
  if (led_type >= LED_TYPE_MAX_CH)
  {
    return NULL;  // V251010R5 LED 타입 범위 가드 유지
  }

  return &led_config[led_type];
}

const indicator_profile_t *led_port_indicator_profile_from_type(uint8_t led_type)
{
  if (led_type >= LED_TYPE_MAX_CH)
  {
    return NULL;  // V251010R5 인디케이터 프로파일 범위 가드 유지
  }

  return &indicator_profiles[led_type];
}

void led_port_indicator_mark_color_dirty(uint8_t led_type)
{
  if (led_type >= LED_TYPE_MAX_CH)
  {
    return;  // V251010R5 색상 캐시 범위 가드 일원화
  }

  indicator_rgb_dirty[led_type] = true;
}

RGB led_port_indicator_get_rgb(uint8_t led_type, const led_config_t *config)
{
  RGB rgb = {0, 0, 0};

  if (led_type >= LED_TYPE_MAX_CH || config == NULL)
  {
    return rgb;  // V251010R5 인디케이터 캐시 입력 값 검증
  }

  if (indicator_rgb_dirty[led_type])
  {
    indicator_rgb_cache[led_type] = hsv_to_rgb(config->hsv);  // V251010R5 HSV→RGB 재계산 경로 통일
    indicator_rgb_dirty[led_type] = false;
  }

  return indicator_rgb_cache[led_type];
}

void led_port_indicator_refresh(void)
{
  led_t led_state = led_port_host_cached_state();                               // V251010R7 현재 호스트 LED 상태 조회
  uint8_t active_mask = 0;
  bool    has_dirty   = false;                                                 // V251010R8 더티 캐시 존재 여부 추적

  for (uint8_t i = 0; i < LED_TYPE_MAX_CH; i++)
  {
    const led_config_t        *config  = led_port_config_from_type(i);
    const indicator_profile_t *profile = led_port_indicator_profile_from_type(i);

    if (config == NULL || profile == NULL)
    {
      continue;                                                                // V251010R7 설정/프로파일 범위 가드 유지
    }

    if (indicator_rgb_dirty[i])
    {
      has_dirty = true;                                                        // V251010R8 색상 재계산 필요 시 커밋 강제
    }

    if (led_port_should_light_indicator(config, profile, led_state))
    {
      active_mask |= profile->host_mask;                                       // V251010R7 실제 활성화된 호스트 LED 비트 누적
    }
  }

  if (active_mask == 0 && indicator_last_active_mask == 0)
  {
    return;                                                                    // V251010R7 비점등 상태 유지 시 DMA 커밋 생략
  }

  if (!has_dirty && active_mask == indicator_last_active_mask)
  {
    return;                                                                    // V251010R8 색상/활성 비트 변화 없을 때 DMA 커밋 생략
  }

  refresh_indicator_display();
  indicator_last_active_mask = active_mask;                                    // V251010R7 DMA 커밋 후 마지막 활성 상태 갱신
}

bool led_port_indicator_config_valid(uint8_t led_type, bool *needs_migration)
{
  if (needs_migration != NULL)
  {
    *needs_migration = false;
  }

  led_config_t *config = led_port_config_from_type(led_type);
  if (config == NULL)
  {
    return false;  // V251010R5 LED 타입 범위 가드 재사용
  }

  uint32_t raw = config->raw;
  if (raw == UINT32_MAX)
  {
    return false;
  }

  if (config->enable <= 1)
  {
    return true;
  }

  uint8_t legacy_enable = raw & 0x03;
  if (legacy_enable <= 1)
  {
    config->enable = legacy_enable;
    if (needs_migration != NULL)
    {
      *needs_migration = true;
    }
    return true;
  }

  return false;
}

void led_port_indicator_flush_config(uint8_t led_type)
{
  const indicator_profile_t *profile = led_port_indicator_profile_from_type(led_type);
  if (profile == NULL)
  {
    return;  // V251010R5 인디케이터 플러시 범위 가드 유지
  }

  profile->flush(true);
}

bool led_port_should_light_indicator(const led_config_t *config, const indicator_profile_t *profile, led_t led_state)
{
  if (config == NULL || profile == NULL)
  {
    return false;  // V251010R5 인디케이터 활성화 판단 입력 검증
  }

  if (!config->enable)
  {
    return false;
  }

  return (led_state.raw & profile->host_mask) != 0;
}

void led_init_ports(void)
{
  eeconfig_init_led_caps();
  eeconfig_init_led_scroll();
  eeconfig_init_led_num();

  for (uint8_t i = 0; i < LED_TYPE_MAX_CH; i++)
  {
    bool           needs_migration = false;
    led_config_t  *config          = led_port_config_from_type(i);
    const indicator_profile_t *profile = led_port_indicator_profile_from_type(i);

    if (config == NULL || profile == NULL)
    {
      continue;  // V251010R5 인디케이터 초기화 범위 가드 유지
    }

    led_port_indicator_mark_color_dirty(i);

    if (!led_port_indicator_config_valid(i, &needs_migration))
    {
      *config = profile->default_config;  // V251010R5 기본 설정 적용 경로 유지
      led_port_indicator_flush_config(i);
      continue;
    }

    if (needs_migration)
    {
      led_port_indicator_flush_config(i);
    }
  }
}

bool rgblight_indicators_kb(void)
{
  led_t led_state = led_port_host_cached_state();

  for (uint8_t i = 0; i < LED_TYPE_MAX_CH; i++)
  {
    const led_config_t        *config  = led_port_config_from_type(i);
    const indicator_profile_t *profile = led_port_indicator_profile_from_type(i);
    if (config == NULL || profile == NULL)
    {
      continue;  // V251010R5 인디케이터 합성 입력 검증
    }

    if (!led_port_should_light_indicator(config, profile, led_state))
    {
      continue;
    }

    RGB rgb = led_port_indicator_get_rgb(i, config);
    uint8_t start = profile->start;
    uint8_t limit = profile->end;

    for (uint8_t led_index = start; led_index < limit; led_index++)
    {
      rgblight_set_color_buffer_at(led_index, rgb.r, rgb.g, rgb.b);
    }
  }

  return true;
}
