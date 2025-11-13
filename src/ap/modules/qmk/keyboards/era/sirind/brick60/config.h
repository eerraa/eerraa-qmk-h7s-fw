#pragma once


#define KBD_NAME                    "BRICK60"

#define USB_VID                     0x4552
#define USB_PID                     0x0022


// hw_def.h
//
// #define _DEF_ENABLE_MATRIX_TIMING_PROBE   0     // MATRIX 계측을 개발 빌드에서 강제 활성화하려면 정의
// #define _DEF_ENABLE_USB_HID_TIMING_PROBE  0     // HID 계측을 개발 빌드에서 강제 활성화하려면 정의
// #define _USE_HW_VCOM
#define _USE_HW_WS2812
#define     HW_WS2812_MAX_CH        30
#define     HW_WS2812_CAPS          0
#define     HW_WS2812_RGB           0
#define     HW_WS2812_RGB_CNT       30
#define AUTO_EEPROM_CLEAR_ENABLE    1  // V251112R3: Brick60 기본 빌드에서 EEPROM 자동 초기화 활성화 테스트


// eeprom
//
#define EEPROM_CHIP_ZD24C128 
#define EECONFIG_USER_DATA_SIZE     512               
#define TOTAL_EEPROM_BYTE_COUNT     4096

#define DYNAMIC_KEYMAP_LAYER_COUNT  8

#define MATRIX_ROWS                 5
#define MATRIX_COLS                 15

#define DEBOUNCE                    20


// #define DEBUG_KEY_SEND
#define GRAVE_ESC_ENABLE
#define KILL_SWITCH_ENABLE
#define KKUK_ENABLE
// V251108R1: Brick60 기본 빌드에서 BootMode/USB 모니터 VIA 채널을 활성화
#define USB_MONITOR_ENABLE 1
#define BOOTMODE_ENABLE    1
#if defined(USB_MONITOR_ENABLE) && !defined(BOOTMODE_ENABLE)
#  define BOOTMODE_ENABLE 1
#endif

// Tapping Term 테스트
//
#define TAPPING_TERM 200

// V251016R8: Brick60 전용 RGB 인디케이터 기능 플래그를 INDICATOR_ENABLE로 선언
#define INDICATOR_ENABLE

// RGB LIGHT : set(RGBLIGHT_ENABLE true)
//
#define EEPROM_ENABLE
#define RGBLIGHT_SLEEP
#define RGBLIGHT_DEFAULT_ON         true
#define RGBLIGHT_DEFAULT_HUE        0
#define RGBLIGHT_DEFAULT_SAT        0
#define RGBLIGHT_DEFAULT_VAL        128
#define RGBLIGHT_LED_COUNT          HW_WS2812_RGB_CNT
#define RGBLIGHT_LIMIT_VAL          200
#define RGBLIGHT_SAT_STEP           8
#define RGBLIGHT_VAL_STEP           8
#define RGBLIGHT_EFFECT_RGB_TEST
#define RGBLIGHT_EFFECT_BREATHING
#define RGBLIGHT_EFFECT_SNAKE
#define RGBLIGHT_EFFECT_STATIC_GRADIENT

#define RGBLIGHT_EFFECT_ALTERNATING // Added
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
