#pragma once


#include QMK_KEYMAP_CONFIG_H


#ifdef TAPDANCE_ENABLE

#include <stdbool.h>
#include <stdint.h>


#define TAPDANCE_SLOT_COUNT    8    // V251124R8: VIA Tap Dance 슬롯 수 고정
#define TAPDANCE_ACTION_COUNT  4


void     tapdance_init(void);
bool     tapdance_handle_via_command(uint8_t *data, uint8_t length);
void     tapdance_storage_apply_defaults(void);
void     tapdance_storage_flush(bool force);
uint16_t tapdance_get_term_ms(uint16_t keycode);

#endif
