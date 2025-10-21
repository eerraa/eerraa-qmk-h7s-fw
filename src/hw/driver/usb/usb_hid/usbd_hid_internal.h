#pragma once

#include "hw_def.h"

#include <stdbool.h>  // V251010R9: 타이머 보정 상태 구조체에서 bool을 직접 사용
#include <stdint.h>   // V251010R9: 타이머 보정 상태 공유 구조체 정의용 기본 정수형 포함

#define HID_KEYBOARD_REPORT_SIZE (HW_KEYS_PRESS_MAX + 2U)
#define KEY_TIME_LOG_MAX         32  // V251009R9: 계측 모듈과 본체에서 공유

typedef struct
{
  uint16_t current_ticks;       // V251010R9: 현재 CCR1 값(타겟 지연 틱)
  uint16_t default_ticks;       // V251010R9: 기본 타겟 값(120us)
  uint16_t min_ticks;           // V251010R9: 허용되는 최소 타겟 틱
  uint16_t max_ticks;           // V251010R9: 허용되는 최대 타겟 틱
  uint16_t guard_us;            // V251010R9: 보정 허용 오차(us)
  uint32_t last_delay_us;       // V251010R9: 직전 펄스 지연 측정(us)
  int32_t  last_error_us;       // V251010R9: 직전 오차(us)
  int32_t  integral_accum;      // V251010R9: 적분 누산 값(오차 합)
  int32_t  integral_limit;      // V251010R9: 적분 포화 한계
  uint32_t expected_interval_us;// V251010R9: 현재 속도에서 기대되는 SOF 간격(us)
  uint32_t target_delay_us;     // V251010R9: 목표 지연(us)
  uint32_t update_count;        // V251010R9: 성공적인 보정 적용 횟수
  uint32_t guard_fault_count;   // V251010R9: 가드 초과로 리셋된 횟수
  uint32_t reset_count;         // V251010R9: 모드 변경 포함 전체 초기화 횟수
  uint8_t  kp_shift;            // V251010R9: 비례항 시프트(1/2^kp_shift)
  uint8_t  integral_shift;      // V251010R9: 적분항 시프트(1/2^integral_shift)
  uint8_t  speed;               // V251010R9: 현재 추적 중인 속도(0=없음,1=HS,2=FS)
  bool     ready;               // V251010R9: SOF 샘플 확보 후 보정 활성 여부
} usb_hid_timer_sync_state_t;
