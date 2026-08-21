#pragma once
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t *)(a))
#include <stdint.h>
