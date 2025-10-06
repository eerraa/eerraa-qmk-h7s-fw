#include "led_port.h"
#include "color.h"
#include "eeconfig.h"
#include "rgblight.h"

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

enum via_qmk_led_value {
  id_qmk_led_enable     = 1,
  id_qmk_led_brightness = 2,
  id_qmk_led_color      = 3,
};

#define INDICATOR_LED_END(start, count) \
  ((uint8_t)(((start) + (count)) > RGBLIGHT_LED_COUNT ? RGBLIGHT_LED_COUNT : ((start) + (count))))  // V251009R4 인디케이터 범위 상한 캐싱

typedef void (*indicator_flush_fn_t)(bool);

typedef struct
{
  led_config_t         default_config;
  uint8_t              start;
  uint8_t              end;       // V251009R4 인디케이터 범위 상한 캐시
  uint8_t              host_mask;
  indicator_flush_fn_t flush;
} indicator_profile_t;

static void via_qmk_led_get_value(uint8_t led_type, uint8_t *data);
static void via_qmk_led_set_value(uint8_t led_type, uint8_t *data);
static void via_qmk_led_save(uint8_t led_type);
static void refresh_indicator_display(void);
static bool indicator_config_valid(uint8_t led_type, bool *needs_migration);
static bool should_light_indicator(uint8_t led_type, const indicator_profile_t *profile, led_t led_state);
static void mark_indicator_color_dirty(uint8_t led_type);          // V251009R3 인디케이터 색상 캐시 무효화
static RGB  get_indicator_rgb(uint8_t led_type);                   // V251009R3 인디케이터 색상 캐시 조회

static led_config_t led_config[LED_TYPE_MAX_CH];
static led_t       host_led_state      = {0};  // V251008R9 호스트 LED 상태 동기화
static led_t       indicator_led_state = {0};  // V251009R1 인디케이터 갱신 상태 캐시
static RGB         indicator_rgb_cache[LED_TYPE_MAX_CH];           // V251009R3 인디케이터 색상 캐시
static bool        indicator_rgb_dirty[LED_TYPE_MAX_CH] = {0};     // V251009R3 색상 캐시 동기화 플래그

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

static led_config_t *led_config_from_type(uint8_t led_type)
{
  if (led_type >= LED_TYPE_MAX_CH) {
    return NULL;  // V251009R4 LED 타입 범위 가드 통합
  }

  return &led_config[led_type];
}

static const indicator_profile_t *indicator_profile_from_type(uint8_t led_type)
{
  if (led_type >= LED_TYPE_MAX_CH) {
    return NULL;  // V251009R4 인디케이터 프로파일 가드 통합
  }

  return &indicator_profiles[led_type];
}

void usbHidSetStatusLed(uint8_t led_bits)
{
  host_led_state.raw = led_bits;  // V251008R9 호스트 LED 수신 즉시 버퍼링
  led_set(led_bits);
}

static void refresh_indicator_display(void)
{
  // V251008R8 인디케이터 변경 사항 즉시 재합성
  if (!is_rgblight_initialized) {
    return;
  }

  rgblight_set();
}

static void flush_indicator_config(uint8_t led_type)
{
  const indicator_profile_t *profile = indicator_profile_from_type(led_type);
  if (profile == NULL) {
    return;
  }

  profile->flush(true);  // V251009R2 인디케이터 메타데이터 테이블화, V251009R4 LED 타입 가드 헬퍼 적용
}

static void mark_indicator_color_dirty(uint8_t led_type)
{
  if (led_type >= LED_TYPE_MAX_CH) {
    return;
  }

  indicator_rgb_dirty[led_type] = true;  // V251009R3 인디케이터 색상 캐시 무효화
}

static RGB get_indicator_rgb(uint8_t led_type)
{
  RGB rgb = {0, 0, 0};

  led_config_t *config = led_config_from_type(led_type);
  if (config == NULL) {
    return rgb;
  }

  if (indicator_rgb_dirty[led_type]) {
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

  for (uint8_t i = 0; i < LED_TYPE_MAX_CH; i++) {
    bool needs_migration = false;
    led_config_t *config = led_config_from_type(i);
    const indicator_profile_t *profile = indicator_profile_from_type(i);

    if (config == NULL || profile == NULL) {
      continue;  // V251009R6 LED 타입 가드 일관화
    }

    mark_indicator_color_dirty(i);  // V251009R3 초기화 시 색상 캐시 재계산 예약
    // V251008R8 인디케이터 기본값/구버전 데이터 정리
    if (!indicator_config_valid(i, &needs_migration)) {
      *config = profile->default_config;  // V251009R2 기본 설정 테이블 적용, V251009R6 포인터 경유 저장
      flush_indicator_config(i);
      continue;
    }

    if (needs_migration) {
      flush_indicator_config(i);
    }
  }
}

void led_update_ports(led_t led_state)
{
  host_led_state = led_state;  // V251008R9 QMK 경로에서 전달되는 LED 동기화
  if (indicator_led_state.raw == led_state.raw) {
    return;  // V251009R1 호스트 LED 변화 없을 때 중복 갱신 방지
  }

  indicator_led_state = led_state;  // V251009R1 인디케이터 상태 캐시 갱신
  refresh_indicator_display();
}

uint8_t host_keyboard_leds(void)
{
  return host_led_state.raw;  // V251008R9 인디케이터 계산용 호스트 LED 조회
}

void via_qmk_led_command(uint8_t led_type, uint8_t *data, uint8_t length)
{
  if (led_type >= LED_TYPE_MAX_CH) {
    data[0] = id_unhandled;
    return;
  }

  uint8_t *command_id        = &(data[0]);
  uint8_t *value_id_and_data = &(data[2]);

  switch (*command_id) {
    case id_custom_set_value:
      via_qmk_led_set_value(led_type, value_id_and_data);
      break;
    case id_custom_get_value:
      via_qmk_led_get_value(led_type, value_id_and_data);
      break;
    case id_custom_save:
      via_qmk_led_save(led_type);
      break;
    default:
      *command_id = id_unhandled;
      break;
  }
}

static bool indicator_config_valid(uint8_t led_type, bool *needs_migration)
{
  // V251008R8 인디케이터 EEPROM 무결성 및 구버전 레이아웃 변환
  if (needs_migration != NULL) {
    *needs_migration = false;
  }

  led_config_t *config = led_config_from_type(led_type);
  if (config == NULL) {
    return false;  // V251009R6 LED 타입 가드 일관화
  }

  uint32_t raw = config->raw;
  if (raw == UINT32_MAX) {
    return false;
  }

  if (config->enable <= 1) {
    return true;
  }

  uint8_t legacy_enable = raw & 0x03;
  if (legacy_enable <= 1) {
    config->enable = legacy_enable;
    if (needs_migration != NULL) {
      *needs_migration = true;
    }
    return true;
  }

  return false;
}

static bool should_light_indicator(uint8_t led_type, const indicator_profile_t *profile, led_t led_state)
{
  const led_config_t *config = led_config_from_type(led_type);
  if (config == NULL || profile == NULL) {
    return false;
  }

  if (!config->enable) {
    return false;
  }

  return (led_state.raw & profile->host_mask) != 0;  // V251009R2 호스트 LED 비트 매핑 단순화, V251009R4 프로파일 포인터 재사용
}

void via_qmk_led_get_value(uint8_t led_type, uint8_t *data)
{
  led_config_t *config = led_config_from_type(led_type);
  if (config == NULL) {
    return;
  }

  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);

  switch (*value_id) {
    case id_qmk_led_enable:
      value_data[0] = config->enable;
      break;
    case id_qmk_led_brightness:
      value_data[0] = config->hsv.v;
      break;
    case id_qmk_led_color:
      value_data[0] = config->hsv.h;
      value_data[1] = config->hsv.s;
      break;
  }
}

void via_qmk_led_set_value(uint8_t led_type, uint8_t *data)
{
  led_config_t *config = led_config_from_type(led_type);
  if (config == NULL) {
    return;
  }

  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);
  bool     needs_refresh = false;  // V251009R1 설정 변경 시에만 RGBlight 갱신

  switch (*value_id) {
    case id_qmk_led_enable: {
      uint8_t enable = value_data[0] ? 1 : 0;
      if (config->enable != enable) {
        config->enable = enable;
        needs_refresh  = true;
      }
      break;
    }
    case id_qmk_led_brightness:
      if (config->hsv.v != value_data[0]) {
        config->hsv.v = value_data[0];
        mark_indicator_color_dirty(led_type);  // V251009R3 밝기 변경 시 색상 캐시 무효화
        needs_refresh  = true;
      }
      break;
    case id_qmk_led_color: {
      uint8_t hue        = value_data[0];
      uint8_t saturation = value_data[1];
      if (config->hsv.h != hue || config->hsv.s != saturation) {
        config->hsv.h = hue;
        config->hsv.s = saturation;
        mark_indicator_color_dirty(led_type);  // V251009R3 색상 변경 시 캐시 재계산
        needs_refresh  = true;
      }
      break;
    }
  }

  if (needs_refresh) {
    refresh_indicator_display();
  }
}

void via_qmk_led_save(uint8_t led_type)
{
  flush_indicator_config(led_type);  // V251009R4 LED 타입 가드 헬퍼 경유 저장
}

bool rgblight_indicators_kb(void)
{
  // V251008R8 BRICK60 인디케이터 RGBlight 오버레이
  led_t led_state = host_keyboard_led_state();

  for (uint8_t i = 0; i < LED_TYPE_MAX_CH; i++) {
    const indicator_profile_t *profile = indicator_profile_from_type(i);
    if (!should_light_indicator(i, profile, led_state)) {
      continue;
    }

    RGB rgb = get_indicator_rgb(i);  // V251009R3 캐시된 RGB 조회
    uint8_t start = profile->start;                                  // V251009R2 범위 메타데이터 통합
    uint8_t limit = profile->end;                                    // V251009R4 인디케이터 범위 상한 캐시 사용

    for (uint8_t led_index = start; led_index < limit; led_index++) {
      rgblight_set_color_buffer_at(led_index, rgb.r, rgb.g, rgb.b);
    }
  }

  return true;
}
