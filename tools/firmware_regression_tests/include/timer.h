#pragma once
#include <stdint.h>
typedef uint16_t fast_timer_t;
uint16_t timer_read_fast(void);
#define TIMER_DIFF_FAST(a,b) ((uint16_t)((a)-(b)))
