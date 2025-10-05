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

static led_config_t led_config[LED_TYPE_MAX_CH];

static const led_config_t indicator_defaults[LED_TYPE_MAX_CH] = {
  [LED_TYPE_CAPS] = {.enable = true, .hsv = {0,   255, 255}},
  [LED_TYPE_SCROLL] = {.enable = true, .hsv = {170, 255, 255}},
  [LED_TYPE_NUM] = {.enable = true, .hsv = {85,  255, 255}},
};

static const struct
{
  uint8_t start;
  uint8_t count;
} indicator_ranges[LED_TYPE_MAX_CH] = {
  [LED_TYPE_CAPS] = {0, 10},
  [LED_TYPE_SCROLL] = {10, 10},
  [LED_TYPE_NUM] = {20, 10},
};

EECONFIG_DEBOUNCE_HELPER(led_caps,   EECONFIG_USER_LED_CAPS,   led_config[LED_TYPE_CAPS]);
EECONFIG_DEBOUNCE_HELPER(led_scroll, EECONFIG_USER_LED_SCROLL, led_config[LED_TYPE_SCROLL]);
EECONFIG_DEBOUNCE_HELPER(led_num,    EECONFIG_USER_LED_NUM,    led_config[LED_TYPE_NUM]);

void usbHidSetStatusLed(uint8_t led_bits)
{
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
  switch (led_type) {
    case LED_TYPE_CAPS:
      eeconfig_flush_led_caps(true);
      break;
    case LED_TYPE_SCROLL:
      eeconfig_flush_led_scroll(true);
      break;
    case LED_TYPE_NUM:
      eeconfig_flush_led_num(true);
      break;
  }
}

void led_init_ports(void)
{
  eeconfig_init_led_caps();
  eeconfig_init_led_scroll();
  eeconfig_init_led_num();

  for (uint8_t i = 0; i < LED_TYPE_MAX_CH; i++) {
    bool needs_migration = false;
    // V251008R8 인디케이터 기본값/구버전 데이터 정리
    if (!indicator_config_valid(i, &needs_migration)) {
      led_config[i] = indicator_defaults[i];
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
  (void)led_state;
  refresh_indicator_display();
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
  if (!led_config[led_type].enable) {
    return false;
  }

  switch (led_type) {
    case LED_TYPE_CAPS:
      return led_state.caps_lock;
    case LED_TYPE_SCROLL:
      return led_state.scroll_lock;
    case LED_TYPE_NUM:
      return led_state.num_lock;
    default:
      return false;
  }
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

  switch (*value_id) {
    case id_qmk_led_enable:
      led_config[led_type].enable = value_data[0] ? 1 : 0;
      break;
    case id_qmk_led_brightness:
      led_config[led_type].hsv.v = value_data[0];
      break;
    case id_qmk_led_color:
      led_config[led_type].hsv.h = value_data[0];
      led_config[led_type].hsv.s = value_data[1];
      break;
  }

  refresh_indicator_display();
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

    RGB rgb = hsv_to_rgb(led_config[i].hsv);
    uint8_t start = indicator_ranges[i].start;
    uint8_t count = indicator_ranges[i].count;

    for (uint8_t offset = 0; offset < count; offset++) {
      uint8_t led_index = start + offset;
      if (led_index >= RGBLIGHT_LED_COUNT) {
        break;
      }
      rgblight_set_color_buffer_at(led_index, rgb.r, rgb.g, rgb.b);
    }
  }

  return true;
}
