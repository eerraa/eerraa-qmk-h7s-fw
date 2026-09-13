#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "bsp.h"
#include QMK_KEYMAP_CONFIG_H


// ---------------------------------------------------------------------------
// 펌웨어/보드 식별 정보
// ---------------------------------------------------------------------------
#define _DEF_FIRMWARE_VERSION       "V260913R1"   // V260913R1: Pulse 계열 최소 지속 시간 20→5 ms(범위 5~260 ms, 기본 speed 15로 기본 펄스 20 ms 유지), 펄스 만료 판정을 애니메이션 타이머에서 RGB task 1 ms 게이트로 이관 / V260913R1: RGB 설정 커밋과 Pulse 출력 평가를 분리(VIA·키코드 변경이 직전 값으로 그려지던 결함), 흰색에서 색상 효과 진입 시 채도 복원을 출발 모드와 무관하게 적용
#define _DEF_BOARD_NAME             "ERA-QMK-H7S-FW"  // V251125R3: 사용자 표시용 보드명 ERA로 변경


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
