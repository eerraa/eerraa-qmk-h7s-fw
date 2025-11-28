#pragma once

// ---------------------------------------------------------------------------
// 보드/USB 식별
// ---------------------------------------------------------------------------
#define KBD_NAME                    "BRICK65"
#define USB_VID                     0x4552
#define USB_PID                     0x0023


// ---------------------------------------------------------------------------
// hw_def.h / hw_caps_* 오버라이드 및 공용 토글
// ---------------------------------------------------------------------------
#define _DEF_ENABLE_MATRIX_TIMING_PROBE   0     // MATRIX 계측을 개발 빌드에서 강제 활성화하려면 정의
#define _DEF_ENABLE_USB_HID_TIMING_PROBE  0     // HID 계측을 개발 빌드에서 강제 활성화하려면 정의
#define _USE_HW_WS2812
#define     HW_WS2812_MAX_CH        33
#define     HW_WS2812_RGB           2
#define     HW_WS2812_RGB_CNT       33
#define AUTO_FACTORY_RESET_ENABLE   1 


// ---------------------------------------------------------------------------
// EEPROM 및 매트릭스 구성
// ---------------------------------------------------------------------------
#define EEPROM_CHIP_ZD24C128 
#define EECONFIG_USER_DATA_SIZE     512               
#define TOTAL_EEPROM_BYTE_COUNT     4096
#define DYNAMIC_KEYMAP_LAYER_COUNT  8

#define MATRIX_ROWS                 5
#define MATRIX_COLS                 15
#define DEBOUNCE                    5
#define DEBOUNCE_TYPE               sym_defer_pk


// ---------------------------------------------------------------------------
// 입력/유틸리티 기능 토글
// ---------------------------------------------------------------------------
#define GRAVE_ESC_ENABLE
#define KILL_SWITCH_ENABLE
#define KKUK_ENABLE
#define USB_MONITOR_ENABLE          1
#define BOOTMODE_ENABLE             1
#if defined(USB_MONITOR_ENABLE) && !defined(BOOTMODE_ENABLE)
#  define BOOTMODE_ENABLE           1
#endif
#define G_TERM_ENABLE
#ifdef G_TERM_ENABLE
#  define TAPPING_TERM_PER_KEY
#  define PERMISSIVE_HOLD_PER_KEY
#  define HOLD_ON_OTHER_KEY_PRESS_PER_KEY
#  define RETRO_TAPPING_PER_KEY
#endif
#define TAPDANCE_ENABLE
#ifdef TAPDANCE_ENABLE
#  define TAP_DANCE_ENABLE
#endif
// #define INDICATOR_ENABLE


// ---------------------------------------------------------------------------
// RGB 라이트 모듈
// ---------------------------------------------------------------------------
#define RGBLIGHT_ENABLE
#define EEPROM_ENABLE
#define RGBLIGHT_SLEEP
#define RGBLIGHT_DEFAULT_ON         true
#define RGBLIGHT_DEFAULT_HUE        0
#define RGBLIGHT_DEFAULT_SAT        0
#define RGBLIGHT_DEFAULT_VAL        128
#define RGBLIGHT_LED_COUNT          HW_WS2812_RGB_CNT
#define RGBLIGHT_LIMIT_VAL          255
#define RGBLIGHT_SAT_STEP           8
#define RGBLIGHT_VAL_STEP           8
#define RGBLIGHT_EFFECT_RGB_TEST
#define RGBLIGHT_EFFECT_BREATHING
#define RGBLIGHT_EFFECT_SNAKE
#define RGBLIGHT_EFFECT_STATIC_GRADIENT
#define RGBLIGHT_EFFECT_ALTERNATING
#define RGBLIGHT_EFFECT_CHRISTMAS
#define RGBLIGHT_EFFECT_KNIGHT
#define RGBLIGHT_EFFECT_RAINBOW_MOOD
#define RGBLIGHT_EFFECT_RAINBOW_SWIRL
#define RGBLIGHT_EFFECT_TWINKLE
#define RGBLIGHT_EFFECT_PULSE_ON_PRESS       // V251018R5: Pulse On Press 커스텀 이펙트 활성화
#define RGBLIGHT_EFFECT_PULSE_OFF_PRESS      // V251018R5: Pulse Off Press 커스텀 이펙트 활성화
#define RGBLIGHT_EFFECT_PULSE_ON_PRESS_HOLD  // V251018R5: Pulse On Press (Hold) 파생 이펙트 활성화
#define RGBLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD // V251018R5: Pulse Off Press (Hold) 파생 이펙트 활성화
#define VELOCIKEY_ENABLE