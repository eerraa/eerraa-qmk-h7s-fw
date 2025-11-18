#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "bsp.h"
#include QMK_KEYMAP_CONFIG_H


// ---------------------------------------------------------------------------
// 펌웨어/보드 식별 정보
// ---------------------------------------------------------------------------
#define _DEF_FIRMWARE_VERSION       "V251115R5"   // V251115R5: 디바운스 init 중복 free 제거
#define _DEF_BOARD_NAME             "BARAM-QMK-H7S-FW"


// ---------------------------------------------------------------------------
// 로그 및 디버그 기본값
// ---------------------------------------------------------------------------
#ifndef HW_LOG_ENABLE_DEFAULT
#define HW_LOG_ENABLE_DEFAULT       0             // V251113R1: 릴리스 빌드는 UART 로그 비활성 상태로 시작
#endif

#ifndef LOG_LEVEL_VERBOSE
#define LOG_LEVEL_VERBOSE           0             // V251112R9: 기본 빌드 로그 레벨을 표준으로 유지
#endif

#ifndef DEBUG_LOG_EEPROM
#define DEBUG_LOG_EEPROM            0             // V251112R9: EEPROM 상세 로그 토글 기본 비활성화
#endif


// ---------------------------------------------------------------------------
// 자동 팩토리 리셋 및 버전 쿠키
// ---------------------------------------------------------------------------
#ifndef AUTO_FACTORY_RESET_ENABLE
#define AUTO_FACTORY_RESET_ENABLE   0             // V251112R3: 자동 팩토리 리셋 빌드 가드 기본 비활성화
#endif

#define AUTO_FACTORY_RESET_FLAG_MAGIC   0x56434C52U  // V251112R3: "VCLR"
#define AUTO_FACTORY_RESET_FLAG_RESET   0x00000000U  // V251112R3: 플래그 초기값

#define __EE_BCD_BYTE(a, b) \
  ((uint32_t)((((a) - '0') & 0x0F) << 4) | (((b) - '0') & 0x0F))

#define __EE_VERSION_LEN        (sizeof(_DEF_FIRMWARE_VERSION) - 1)
#define __EE_SAFE_CHAR(idx)     (((idx) < __EE_VERSION_LEN) ? _DEF_FIRMWARE_VERSION[idx] : '0')
#define __EE_REV_HIGH_CHAR()    ((__EE_VERSION_LEN > 9) ? _DEF_FIRMWARE_VERSION[8] : '0')
#define __EE_REV_LOW_CHAR()     ((__EE_VERSION_LEN > 9) ? _DEF_FIRMWARE_VERSION[9] : (__EE_VERSION_LEN > 8 ? _DEF_FIRMWARE_VERSION[8] : '0'))

#define AUTO_FACTORY_RESET_COOKIE_DEFAULT                                      \
  ( (__EE_BCD_BYTE(__EE_SAFE_CHAR(1), __EE_SAFE_CHAR(2)) << 24) | \
    (__EE_BCD_BYTE(__EE_SAFE_CHAR(3), __EE_SAFE_CHAR(4)) << 16) | \
    (__EE_BCD_BYTE(__EE_SAFE_CHAR(5), __EE_SAFE_CHAR(6)) << 8)  | \
    (__EE_BCD_BYTE(__EE_REV_HIGH_CHAR(), __EE_REV_LOW_CHAR()) << 0) )

#ifndef AUTO_FACTORY_RESET_COOKIE
#define AUTO_FACTORY_RESET_COOKIE   AUTO_FACTORY_RESET_COOKIE_DEFAULT  // V251112R3: 펌웨어 버전 기반 기본 쿠키
#endif


// ---------------------------------------------------------------------------
// 계측 기능 (보드/빌드 오버라이드 가능)
// ---------------------------------------------------------------------------
#ifndef _DEF_ENABLE_MATRIX_TIMING_PROBE
#define _DEF_ENABLE_MATRIX_TIMING_PROBE   0       // V251010R4: 기본값은 비활성화, 필요 시 보드/빌드에서 재정의
#endif

#ifndef _DEF_ENABLE_USB_HID_TIMING_PROBE
#define _DEF_ENABLE_USB_HID_TIMING_PROBE  0       // V251009R5: usbd_hid 계측 기본 비활성화, 필요 시 빌드 옵션으로만 활성화
#endif


// ---------------------------------------------------------------------------
// 하드웨어 사용 선언 (기능별 분리 헤더)
// ---------------------------------------------------------------------------
#include "hw_caps_core.h"
#include "hw_caps_led.h"
#include "hw_caps_uart.h"
#include "hw_caps_i2c.h"
#include "hw_caps_eeprom.h"
#include "hw_caps_rtc.h"
#include "hw_caps_reset.h"
#include "hw_caps_keys.h"
#include "hw_caps_usb.h"
#include "hw_caps_cli.h"
#include "hw_caps_log.h"


#endif
