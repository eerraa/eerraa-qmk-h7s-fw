#pragma once


// ---------------------------------------------------------------------------
// 보드/USB 식별
// ---------------------------------------------------------------------------
#define KBD_NAME                    "BRICK60"
#define USB_VID                     0x4552
#define USB_PID                     0x0022


// ---------------------------------------------------------------------------
// hw_def.h / hw_caps_* 오버라이드 및 공용 토글
// ---------------------------------------------------------------------------
#define _DEF_ENABLE_MATRIX_TIMING_PROBE   0     // MATRIX 계측을 개발 빌드에서 강제 활성화하려면 정의
#define _DEF_ENABLE_USB_HID_TIMING_PROBE  0     // HID 계측을 개발 빌드에서 강제 활성화하려면 정의
// #define _USE_HW_VCOM                        // hw_caps_usb.h 참고
#define _USE_HW_WS2812                         // hw_caps_led.h 참고 (V251114R2: WS2812 캡 분리)
#define HW_WS2812_MAX_CH            30
#define HW_WS2812_CAPS              0
#define HW_WS2812_RGB               0
#define HW_WS2812_RGB_CNT           30
#define AUTO_FACTORY_RESET_ENABLE   1            // V251112R3: Brick60 기본 빌드에서 EEPROM 자동 초기화 활성화 테스트


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
#define DEBOUNCE_TYPE               sym_defer_pk   // V251114R3: CMake 없이 debounce 구현 선택


// ---------------------------------------------------------------------------
// 입력/유틸리티 기능 토글
// ---------------------------------------------------------------------------
// #define DEBUG_KEY_SEND
#define GRAVE_ESC_ENABLE
#define KILL_SWITCH_ENABLE
#define KKUK_ENABLE
#define USB_MONITOR_ENABLE          1           // V251108R1: Brick60 VIA 채널 USB 모니터 활성화 (검증을 위해 비활성화)
#define BOOTMODE_ENABLE             1
#if defined(USB_MONITOR_ENABLE) && !defined(BOOTMODE_ENABLE)
#  define BOOTMODE_ENABLE           1
#endif
#define G_TERM_ENABLE                           // V251123R4: VIA TAPPING term/옵션 제어 활성화
#ifdef G_TERM_ENABLE
#  define TAPPING_TERM_PER_KEY
#  define PERMISSIVE_HOLD_PER_KEY
#  define HOLD_ON_OTHER_KEY_PRESS_PER_KEY
#  define RETRO_TAPPING_PER_KEY
#endif
#define INDICATOR_ENABLE            // V251016R8: Brick60 전용 RGB 인디케이터 기능 플래그


// ---------------------------------------------------------------------------
// RGB 라이트 모듈
// ---------------------------------------------------------------------------
// V251114R3: RGBLIGHT_ENABLE/효과 선언을 config.h에서 직접 관리
#define RGBLIGHT_ENABLE
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
#define RGBLIGHT_EFFECT_ALTERNATING         // Added
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
