#include "indicator_port.h"

#include "color.h"
#include "eeconfig.h"
#include "host.h"
#include "led.h"
#include "port.h"
#include "rgblight.h"

// BRICK65: Dual independent indicators on Index 0 and 1
#define INDICATOR_COUNT 2

static rgblight_indicator_config_t indicator_config[INDICATOR_COUNT];

_Static_assert(sizeof(rgblight_indicator_config_t) == sizeof(uint32_t),
               "EECONFIG out of spec.");

static void indicator_via_get_value(uint8_t channel, uint8_t *data);
static void indicator_via_set_value(uint8_t channel, uint8_t *data);
static void indicator_via_save(uint8_t channel);
static bool indicator_target_from_host(uint8_t target, led_t host_state);

// EECONFIG helpers for two separate slots
// EECONFIG helpers for two separate slots
EECONFIG_DEBOUNCE_HELPER(indicator_0, EECONFIG_USER_INDICATOR, indicator_config[0]);
EECONFIG_DEBOUNCE_HELPER(indicator_1, (void*)((uint32_t)EECONFIG_USER_INDICATOR + 4), indicator_config[1]);

static void indicator_apply_defaults(uint8_t index)
{
  indicator_config[index].target = RGBLIGHT_INDICATOR_TARGET_CAPS; // Default to Caps for both initially? Or Off?
  if (index == 1) indicator_config[index].target = RGBLIGHT_INDICATOR_TARGET_SCROLL; // Default Ind 2 to Scroll

  HSV default_hsv         = {HSV_GREEN};
  indicator_config[index].val    = default_hsv.v;
  indicator_config[index].hue    = default_hsv.h;
  indicator_config[index].sat    = default_hsv.s;
}

void usbHidSetStatusLed(uint8_t led_bits)
{
  host_keyboard_leds_update(led_bits);
  led_t host_state = {.raw = led_bits};
  rgblight_indicator_post_host_event(host_state);
}

void led_init_ports(void)
{
  // 1. Isolate Underglow (Index 2~32) from Indicators (Index 0, 1)
  // This ensures standard rgblight effects only play on LEDs 2+.
  // Total LEDs = 33. Indicators = 2. Underglow = 31.
  rgblight_set_effect_range(2, 31); 

  // 2. Load Configs
  eeconfig_init_indicator_0();
  eeconfig_init_indicator_1();

  for (int i = 0; i < INDICATOR_COUNT; i++)
  {
    if (indicator_config[i].raw == 0 || indicator_config[i].target > RGBLIGHT_INDICATOR_TARGET_NUM)
    {
      indicator_apply_defaults(i);
      if (i == 0) eeconfig_flush_indicator_0(true);
      else        eeconfig_flush_indicator_1(true);
    }
  }
}

void led_update_ports(led_t led_state)
{
  // Manual Indicator Logic for BRICK65
  // Since we have two independent indicators that need full color control,
  // and rgblight's built-in indicator feature is single-channel focused,
  // we manually set LED 0 and 1 here.

  for (int i = 0; i < INDICATOR_COUNT; i++)
  {
    bool on = indicator_target_from_host(indicator_config[i].target, led_state);
    
    if (on)
    {
      // Convert HSV to RGB
      HSV hsv = {
        .h = indicator_config[i].hue,
        .s = indicator_config[i].sat,
        .v = indicator_config[i].val
      };
      RGB rgb = rgblight_hsv_to_rgb(hsv);
      
      // Set LED directly. Index i maps to LED i (0 or 1).
      // Bypass rgblight_setrgb_at to ensure it works even if global RGB is disabled.
      led[i].r = rgb.r;
      led[i].g = rgb.g;
      led[i].b = rgb.b;
#ifdef RGBW
      led[i].w = 0;
#endif
    }
    else
    {
      // Turn off if target condition not met
      led[i].r = 0;
      led[i].g = 0;
      led[i].b = 0;
#ifdef RGBW
      led[i].w = 0;
#endif
    }
  }
  
  // Trigger render (this will also render the underglow effect on 2-32)
  rgblight_set();
}

static bool indicator_target_from_host(uint8_t target, led_t host_state)
{
  switch (target)
  {
    case RGBLIGHT_INDICATOR_TARGET_CAPS:   return host_state.caps_lock;
    case RGBLIGHT_INDICATOR_TARGET_SCROLL: return host_state.scroll_lock;
    case RGBLIGHT_INDICATOR_TARGET_NUM:    return host_state.num_lock;
    default:                               return false;
  }
}

void indicator_port_via_command(uint8_t *data, uint8_t length)
{
  (void)length;
  uint8_t *command_id = &(data[0]);
  uint8_t *channel_id = &(data[1]); // 0 or 1

  // Map channel_id to indicator index
  uint8_t index = *channel_id;
  if (index >= INDICATOR_COUNT) return;

  switch (*command_id)
  {
    case id_custom_set_value:
      indicator_via_set_value(index, &(data[2]));
      break;
    case id_custom_get_value:
      indicator_via_get_value(index, &(data[2]));
      break;
    case id_custom_save:
      indicator_via_save(index);
      break;
    default:
      *command_id = id_unhandled;
      break;
  }
}

static void indicator_via_get_value(uint8_t index, uint8_t *data)
{
  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);

  switch (*value_id)
  {
    case id_qmk_custom_ind_selec: // Reused ID for both channels
      value_data[0] = indicator_config[index].target;
      break;
    case id_qmk_custom_ind_brightness:
      value_data[0] = indicator_config[index].val;
      break;
    case id_qmk_custom_ind_color:
      value_data[0] = indicator_config[index].hue;
      value_data[1] = indicator_config[index].sat;
      break;
  }
}

static void indicator_via_set_value(uint8_t index, uint8_t *data)
{
  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);
  uint32_t prev_raw   = indicator_config[index].raw;

  switch (*value_id)
  {
    case id_qmk_custom_ind_selec:
      indicator_config[index].target = value_data[0];
      if (indicator_config[index].target > RGBLIGHT_INDICATOR_TARGET_NUM)
        indicator_config[index].target = RGBLIGHT_INDICATOR_TARGET_OFF;
      break;
    case id_qmk_custom_ind_brightness:
      indicator_config[index].val = value_data[0];
      break;
    case id_qmk_custom_ind_color:
      indicator_config[index].hue = value_data[0];
      indicator_config[index].sat = value_data[1];
      break;
    default:
      return;
  }

  if (indicator_config[index].raw == prev_raw) return;
  
  led_t current_state;
  current_state.raw = host_keyboard_leds();
  led_update_ports(current_state);
}

static void indicator_via_save(uint8_t index)
{
  if (index == 0) eeconfig_flush_indicator_0(true);
  else            eeconfig_flush_indicator_1(true);
}
