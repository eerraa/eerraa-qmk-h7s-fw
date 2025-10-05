
#pragma once


#include "via_hid.h"
#include "via.h"
#include "eeconfig.h"
#include "kill_switch.h"
#include "kkuk.h"



#define QMK_BUILDDATE   "2025-06-27-17:35:30"


#define EECONFIG_USER_LED_CAPS_OFFSET        0U
#define EECONFIG_USER_LED_SCROLL_OFFSET      4U
#define EECONFIG_USER_LED_NUM_OFFSET         8U   // V251009R1 넘버락 인디케이터 슬롯 재배치
#define EECONFIG_USER_KILL_SWITCH_LR_OFFSET  12U  // V251009R1 킬 스위치 좌우 슬롯 재정렬
#define EECONFIG_USER_KILL_SWITCH_UD_OFFSET  20U  // V251009R1 킬 스위치 상하 슬롯 재정렬
#define EECONFIG_USER_KKUK_OFFSET            28U  // V251009R1 꾸욱 설정 슬롯 이동
#define EECONFIG_USER_BOOTMODE_OFFSET        32U  // V251009R1 Boot mode selection 슬롯 이동

#if (EECONFIG_USER_DATA_SIZE) > 0
_Static_assert((EECONFIG_USER_BOOTMODE_OFFSET + sizeof(uint32_t)) <= (EECONFIG_USER_DATA_SIZE),
               "EECONFIG_USER 영역이 사용자 데이터 크기를 초과했습니다."); // V251009R1 사용자 EEPROM 슬롯 재배치 범위 검증
#endif

#define EECONFIG_USER_LED_CAPS        ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + EECONFIG_USER_LED_CAPS_OFFSET))
#define EECONFIG_USER_LED_SCROLL      ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + EECONFIG_USER_LED_SCROLL_OFFSET))
#define EECONFIG_USER_LED_NUM         ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + EECONFIG_USER_LED_NUM_OFFSET))
#define EECONFIG_USER_KILL_SWITCH_LR  ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + EECONFIG_USER_KILL_SWITCH_LR_OFFSET))
#define EECONFIG_USER_KILL_SWITCH_UD  ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + EECONFIG_USER_KILL_SWITCH_UD_OFFSET))
#define EECONFIG_USER_KKUK            ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + EECONFIG_USER_KKUK_OFFSET))
#define EECONFIG_USER_BOOTMODE        ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + EECONFIG_USER_BOOTMODE_OFFSET))
