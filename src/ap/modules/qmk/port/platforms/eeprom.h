// Copyright 2018-2022 QMK
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once


#include "hw_def.h"



void     eeprom_init(void);
void     eeprom_update(void);
bool     eeprom_is_pending(void);
void     eeprom_flush_pending(void);
void     eeprom_task(void);
void     eeprom_req_clean(void);
uint8_t  eeprom_read_byte(const uint8_t *addr);
uint16_t eeprom_read_word(const uint16_t *addr);
uint32_t eeprom_read_dword(const uint32_t *addr);
void     eeprom_read_block(void *buf, const void *addr, uint32_t len);
void     eeprom_write_byte(uint8_t *addr, uint8_t value);
void     eeprom_write_word(uint16_t *addr, uint16_t value);
void     eeprom_write_dword(uint32_t *addr, uint32_t value);
void     eeprom_write_block(const void *buf, void *addr, size_t len);
void     eeprom_update_byte(uint8_t *addr, uint8_t value);
void     eeprom_update_word(uint16_t *addr, uint16_t value);
void     eeprom_update_dword(uint32_t *addr, uint32_t value);
void     eeprom_update_block(const void *buf, void *addr, size_t len);
bool     eeprom_apply_factory_defaults(bool restore_factory_reset_sentinel);  // V251112R3: AUTO_FACTORY_RESET/VIA 공용 초기화
uint32_t eeprom_get_write_pending_count(void);                       // V251112R2: EEPROM 큐 현재 사용량 조회
uint32_t eeprom_get_write_pending_max(void);                         // V251112R2: 부트 후 최고 사용량 조회
uint32_t eeprom_get_write_overflow_count(void);                      // V251112R2: 직접 기록된 횟수 조회
bool     eeprom_is_burst_mode_active(void);                          // V251112R5: 버스트 모드 상태 확인
uint8_t  eeprom_get_burst_extra_calls(void);                         // V251112R5: 버스트 모드 시 추가 실행 횟수
