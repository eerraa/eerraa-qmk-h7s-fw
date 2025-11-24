
#pragma once


#include "via_hid.h"
#include "via.h"
#include "eeconfig.h"
#include "kill_switch.h"
#include "kkuk.h"
#include "tapping_term.h"
#include "tapdance.h"



#define QMK_BUILDDATE   "2025-06-27-17:35:30"

#define EECONFIG_USER_LED_CAPS            ((void *)((uint32_t)EECONFIG_USER_DATABLOCK +  0))  // 4B  // V251016R8: Brick60 인디케이터 슬롯 복원
#define EECONFIG_USER_LED_SCROLL          ((void *)((uint32_t)EECONFIG_USER_DATABLOCK +  4))  // 4B
#define EECONFIG_USER_KILL_SWITCH_LR      ((void *)((uint32_t)EECONFIG_USER_DATABLOCK +  8))  // 8B
#define EECONFIG_USER_KILL_SWITCH_UD      ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 16))  // 8B
#define EECONFIG_USER_KKUK                ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 24))  // 4B
#define EECONFIG_USER_BOOTMODE            ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 28))  // 4B
#define EECONFIG_USER_USB_INSTABILITY     ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 32))  // 4B  // V251108R1: USB 모니터 토글 저장 슬롯
#define EECONFIG_USER_EEPROM_CLEAR_FLAG   ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 36))  // 4B  // V251112R1: 자동 초기화 플래그
#define EECONFIG_USER_EEPROM_CLEAR_COOKIE ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 40))  // 4B  // V251112R1: 자동 초기화 쿠키 기록 슬롯
#define EECONFIG_USER_DEBOUNCE            ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 44))  // 8B  // V251115R1: VIA 디바운스 프로필 저장 슬롯
#define EECONFIG_USER_TAPPING_TERM        ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 52))  // 12B  // V251123R4: VIA TAPPING 설정 슬롯
#define EECONFIG_USER_TAPDANCE            ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 64))  // 88B  // V251124R8: VIA TAPDANCE 슬롯

typedef struct
{
  uint8_t enable;
  uint8_t reserved[3];
} usb_monitor_config_t;  // V251108R1: USB 안정성 모니터 구성 페이로드
