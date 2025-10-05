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

static void via_qmk_led_get_value(uint8_t led_type, uint8_t *data);
static void via_qmk_led_set_value(uint8_t led_type, uint8_t *data);
static void via_qmk_led_save(uint8_t led_type);
static void refresh_indicator_display(void);
static bool indicator_config_valid(uint8_t led_type, bool *needs_migration);
static bool should_light_indicator(uint8_t led_type, led_t led_state);
static void mark_indicator_color_dirty(uint8_t led_type);          // V251009R3 인디케이터 색상 캐시 무효화
static RGB  get_indicator_rgb(uint8_t led_type);                   // V251009R3 인디케이터 색상 캐시 조회

static led_config_t led_config[LED_TYPE_MAX_CH];
static led_t       host_led_state      = {0};  // V251008R9 호스트 LED 상태 동기화
static led_t       indicator_led_state = {0};  // V251009R1 인디케이터 갱신 상태 캐시
static RGB         indicator_rgb_cache[LED_TYPE_MAX_CH];           // V251009R3 인디케이터 색상 캐시
static bool        indicator_rgb_dirty[LED_TYPE_MAX_CH] = {0};     // V251009R3 색상 캐시 동기화 플래그

typedef void (*indicator_flush_fn_t)(bool);

typedef struct
{
  led_config_t         default_config;
  uint8_t              start;
  uint8_t              count;
  uint8_t              host_mask;
  indicator_flush_fn_t flush;
} indicator_profile_t;

EECONFIG_DEBOUNCE_HELPER(led_caps,   EECONFIG_USER_LED_CAPS,   led_config[LED_TYPE_CAPS]);
EECONFIG_DEBOUNCE_HELPER(led_scroll, EECONFIG_USER_LED_SCROLL, led_config[LED_TYPE_SCROLL]);
EECONFIG_DEBOUNCE_HELPER(led_num,    EECONFIG_USER_LED_NUM,    led_config[LED_TYPE_NUM]);

static const indicator_profile_t indicator_profiles[LED_TYPE_MAX_CH] = {
  [LED_TYPE_CAPS] = {
    .default_config = {.enable = true, .hsv = {0, 255, 255}},
    .start          = 0,
    .count          = 10,
    .host_mask      = (1 << 1),
    .flush          = eeconfig_flush_led_caps,
  },
  [LED_TYPE_SCROLL] = {
    .default_config = {.enable = true, .hsv = {170, 255, 255}},
    .start          = 10,
    .count          = 10,
    .host_mask      = (1 << 2),
    .flush          = eeconfig_flush_led_scroll,
  },
  [LED_TYPE_NUM] = {
    .default_config = {.enable = true, .hsv = {85, 255, 255}},
    .start          = 20,
    .count          = 10,
    .host_mask      = (1 << 0),
    .flush          = eeconfig_flush_led_num,
  },
};

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
  if (led_type >= LED_TYPE_MAX_CH) {
    return;
  }

  indicator_profiles[led_type].flush(true);  // V251009R2 인디케이터 메타데이터 테이블화
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

  if (led_type >= LED_TYPE_MAX_CH) {
    return rgb;
  }

  if (indicator_rgb_dirty[led_type]) {
    indicator_rgb_cache[led_type] = hsv_to_rgb(led_config[led_type].hsv);  // V251009R3 HSV→RGB 1회 변환
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
    mark_indicator_color_dirty(i);  // V251009R3 초기화 시 색상 캐시 재계산 예약
    // V251008R8 인디케이터 기본값/구버전 데이터 정리
    if (!indicator_config_valid(i, &needs_migration)) {
      led_config[i] = indicator_profiles[i].default_config;  // V251009R2 기본 설정 테이블 적용
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

  uint32_t raw = led_config[led_type].raw;
  if (raw == UINT32_MAX) {
    return false;
  }

  if (led_config[led_type].enable <= 1) {
    return true;
  }

  uint8_t legacy_enable = raw & 0x03;
  if (legacy_enable <= 1) {
    led_config[led_type].enable = legacy_enable;
    if (needs_migration != NULL) {
      *needs_migration = true;
    }
    return true;
  }

  return false;
}

static bool should_light_indicator(uint8_t led_type, led_t led_state)
{
  if (led_type >= LED_TYPE_MAX_CH) {
    return false;
  }

  if (!led_config[led_type].enable) {
    return false;
  }

  return (led_state.raw & indicator_profiles[led_type].host_mask) != 0;  // V251009R2 호스트 LED 비트 매핑 단순화
}

void via_qmk_led_get_value(uint8_t led_type, uint8_t *data)
{
  if (led_type >= LED_TYPE_MAX_CH) {
    return;
  }

  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);

  switch (*value_id) {
    case id_qmk_led_enable:
      value_data[0] = led_config[led_type].enable;
      break;
    case id_qmk_led_brightness:
      value_data[0] = led_config[led_type].hsv.v;
      break;
    case id_qmk_led_color:
      value_data[0] = led_config[led_type].hsv.h;
      value_data[1] = led_config[led_type].hsv.s;
      break;
  }
}

void via_qmk_led_set_value(uint8_t led_type, uint8_t *data)
{
  if (led_type >= LED_TYPE_MAX_CH) {
    return;
  }

  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);
  bool     needs_refresh = false;  // V251009R1 설정 변경 시에만 RGBlight 갱신

  switch (*value_id) {
    case id_qmk_led_enable: {
      uint8_t enable = value_data[0] ? 1 : 0;
      if (led_config[led_type].enable != enable) {
        led_config[led_type].enable = enable;
        needs_refresh              = true;
      }
      break;
    }
    case id_qmk_led_brightness:
      if (led_config[led_type].hsv.v != value_data[0]) {
        led_config[led_type].hsv.v = value_data[0];
        mark_indicator_color_dirty(led_type);  // V251009R3 밝기 변경 시 색상 캐시 무효화
        needs_refresh              = true;
      }
      break;
    case id_qmk_led_color: {
      uint8_t hue        = value_data[0];
      uint8_t saturation = value_data[1];
      if (led_config[led_type].hsv.h != hue || led_config[led_type].hsv.s != saturation) {
        led_config[led_type].hsv.h = hue;
        led_config[led_type].hsv.s = saturation;
        mark_indicator_color_dirty(led_type);  // V251009R3 색상 변경 시 캐시 재계산
        needs_refresh              = true;
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
  if (led_type >= LED_TYPE_MAX_CH) {
    return;
  }

  flush_indicator_config(led_type);
}

bool rgblight_indicators_kb(void)
{
  // V251008R8 BRICK60 인디케이터 RGBlight 오버레이
  led_t led_state = host_keyboard_led_state();

  for (uint8_t i = 0; i < LED_TYPE_MAX_CH; i++) {
    if (!should_light_indicator(i, led_state)) {
      continue;
    }

    RGB rgb = get_indicator_rgb(i);  // V251009R3 캐시된 RGB 조회
    uint8_t  start = indicator_profiles[i].start;                    // V251009R2 범위 메타데이터 통합
    uint16_t limit = (uint16_t)start + indicator_profiles[i].count;  // V251009R2 루프 상한 사전 계산
    if (limit > RGBLIGHT_LED_COUNT) {
      limit = RGBLIGHT_LED_COUNT;                                    // V251009R2 LED 개수 초과 방지 조정
    }

    for (uint8_t led_index = start; led_index < limit; led_index++) {
      rgblight_set_color_buffer_at(led_index, rgb.r, rgb.g, rgb.b);
    }
  }

  return true;
}
