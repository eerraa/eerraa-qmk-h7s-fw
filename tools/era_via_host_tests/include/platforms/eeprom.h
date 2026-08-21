#pragma once

#include <stdint.h>
#include <stddef.h>

void     eeprom_read_block(void *buf, const void *addr, uint32_t len);
void     eeprom_update_block(const void *buf, void *addr, size_t len);
uint8_t  eeprom_read_byte(const uint8_t *addr);
void     eeprom_update_byte(uint8_t *addr, uint8_t value);
