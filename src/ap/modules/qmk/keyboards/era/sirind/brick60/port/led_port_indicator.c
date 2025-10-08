#include "led_port_internal.h"
#include <limits.h>

// V251010R5 RGB 인디케이터 및 구성 관리 모듈

static led_config_t led_config[LED_TYPE_MAX_CH];
static led_t       indicator_led_state = {0};                 // V251010R5 인디케이터 전용 상태 캐시
static RGB         indicator_rgb_cache[LED_TYPE_MAX_CH];
static bool        indicator_rgb_dirty[LED_TYPE_MAX_CH] = {0};

#define INDICATOR_LED_END(start, count) \
  ((uint8_t)(((start) + (count)) > RGBLIGHT_LED_COUNT ? RGBLIGHT_LED_COUNT : ((start) + (count))))  // V251009R4 인디케이터 범위 상한 캐싱

EECONFIG_DEBOUNCE_HELPER(led_caps,   EECONFIG_USER_LED_CAPS,   led_config[LED_TYPE_CAPS]);
EECONFIG_DEBOUNCE_HELPER(led_scroll, EECONFIG_USER_LED_SCROLL, led_config[LED_TYPE_SCROLL]);
EECONFIG_DEBOUNCE_HELPER(led_num,    EECONFIG_USER_LED_NUM,    led_config[LED_TYPE_NUM]);

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

static bool indicator_config_valid(uint8_t led_type, bool *needs_migration);
static bool should_light_indicator(const led_config_t *config, const indicator_profile_t *profile, led_t led_state);
static RGB  get_indicator_rgb(uint8_t led_type, const led_config_t *config);
static void flush_indicator_config(uint8_t led_type);

void led_port_set_indicator_state(led_t state)
{
  indicator_led_state = state;                                // V251010R5 인디케이터 상태 캐시 분리
}

led_t led_port_get_indicator_state(void)
{
  return indicator_led_state;                                 // V251010R5 인디케이터 상태 조회
}

led_config_t *led_port_config_from_type(uint8_t led_type)
{
  if (led_type >= LED_TYPE_MAX_CH)
  {
    return NULL;  // V251009R4 LED 타입 범위 가드 통합
  }

  return &led_config[led_type];
}

const indicator_profile_t *led_port_indicator_profile_from_type(uint8_t led_type)
{
  if (led_type >= LED_TYPE_MAX_CH)
  {
    return NULL;  // V251009R4 인디케이터 프로파일 가드 통합
  }

  return &indicator_profiles[led_type];
}

void led_port_mark_indicator_color_dirty(uint8_t led_type)
{
  if (led_type >= LED_TYPE_MAX_CH)
  {
    return;
  }

  indicator_rgb_dirty[led_type] = true;  // V251009R3 인디케이터 색상 캐시 무효화
}

void led_port_refresh_indicator_display(void)
{
  if (!is_rgblight_initialized)
  {
    return;  // V251008R8 인디케이터 변경 사항 즉시 재합성
  }

  rgblight_set();
}

void led_port_flush_indicator_config(uint8_t led_type)
{
  flush_indicator_config(led_type);  // V251009R4 LED 타입 가드 헬퍼 경유 저장
}

static void flush_indicator_config(uint8_t led_type)
{
  const indicator_profile_t *profile = led_port_indicator_profile_from_type(led_type);
  if (profile == NULL)
  {
    return;
  }

  profile->flush(true);  // V251009R2 인디케이터 메타데이터 테이블화, V251009R4 LED 타입 가드 헬퍼 적용
}

static void mark_indicator_color_dirty_all(void)
{
  for (uint8_t i = 0; i < LED_TYPE_MAX_CH; i++)
  {
    led_port_mark_indicator_color_dirty(i);                    // V251009R3 초기화 시 색상 캐시 재계산 예약
  }
}

static bool indicator_config_valid(uint8_t led_type, bool *needs_migration)
{
  if (needs_migration != NULL)
  {
    *needs_migration = false;
  }

  led_config_t *config = led_port_config_from_type(led_type);
  if (config == NULL)
  {
    return false;  // V251009R6 LED 타입 가드 일관화
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

static bool should_light_indicator(const led_config_t *config, const indicator_profile_t *profile, led_t led_state)
{
  if (config == NULL || profile == NULL)
  {
    return false;  // V251009R7 구성/프로파일 포인터 직접 가드
  }

  if (!config->enable)
  {
    return false;
  }

  return (led_state.raw & profile->host_mask) != 0;  // V251009R2 호스트 LED 비트 매핑 단순화, V251009R4 프로파일 포인터 재사용
}

static RGB get_indicator_rgb(uint8_t led_type, const led_config_t *config)
{
  RGB rgb = {0, 0, 0};

  if (led_type >= LED_TYPE_MAX_CH)
  {
    return rgb;  // V251009R8 인디케이터 색상 캐시 범위 가드 추가
  }

  if (config == NULL)
  {
    return rgb;
  }

  if (indicator_rgb_dirty[led_type])
  {
    indicator_rgb_cache[led_type] = hsv_to_rgb(config->hsv);  // V251009R3 HSV→RGB 1회 변환, V251009R4 LED 설정 포인터 재사용
    indicator_rgb_dirty[led_type] = false;
  }

  return indicator_rgb_cache[led_type];
}

void led_init_ports(void)
{
  eeconfig_init_led_caps();
  eeconfig_init_led_scroll();
  eeconfig_init_led_num();

  mark_indicator_color_dirty_all();

  for (uint8_t i = 0; i < LED_TYPE_MAX_CH; i++)
  {
    bool needs_migration = false;
    led_config_t *config = led_port_config_from_type(i);
    const indicator_profile_t *profile = led_port_indicator_profile_from_type(i);

    if (config == NULL || profile == NULL)
    {
      continue;  // V251009R6 LED 타입 가드 일관화
    }

    if (!indicator_config_valid(i, &needs_migration))
    {
      *config = profile->default_config;  // V251009R2 기본 설정 테이블 적용, V251009R6 포인터 경유 저장
      flush_indicator_config(i);
      continue;
    }

    if (needs_migration)
    {
      flush_indicator_config(i);
    }
  }
}

void led_update_ports(led_t led_state)
{
  led_port_set_host_state(led_state);                          // V251010R5 호스트 LED 상태 캐시를 모듈 간 공유
  if (indicator_led_state.raw == led_state.raw)
  {
    return;  // V251009R1 호스트 LED 변화 없을 때 중복 갱신 방지
  }

  indicator_led_state = led_state;  // V251010R5 인디케이터 상태 캐시 갱신
  led_port_refresh_indicator_display();
}

bool rgblight_indicators_kb(void)
{
  led_t led_state = host_keyboard_led_state();

  for (uint8_t i = 0; i < LED_TYPE_MAX_CH; i++)
  {
    const led_config_t        *config  = led_port_config_from_type(i);
    const indicator_profile_t *profile = led_port_indicator_profile_from_type(i);
    if (config == NULL || profile == NULL)
    {
      continue;  // V251009R7 인디케이터 구성/프로파일 포인터 동시 가드
    }

    if (!should_light_indicator(config, profile, led_state))
    {
      continue;  // V251009R7 인디케이터 헬퍼에 구성 포인터 직접 전달
    }

    RGB rgb = get_indicator_rgb(i, config);  // V251009R7 인디케이터 구성 포인터 재사용
    uint8_t start = profile->start;                                   // V251009R2 범위 메타데이터 통합
    uint8_t limit = profile->end;                                     // V251009R4 인디케이터 범위 상한 캐시 사용

    for (uint8_t led_index = start; led_index < limit; led_index++)
    {
      rgblight_set_color_buffer_at(led_index, rgb.r, rgb.g, rgb.b);
    }
  }

  return true;
}

