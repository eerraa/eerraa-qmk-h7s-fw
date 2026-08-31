#pragma once

#include "action.h"
#include "keycodes.h"

#define PACKED __attribute__((__packed__))
#ifndef QK_TAP_DANCE_GET_INDEX
#    define QK_TAP_DANCE_GET_INDEX(code) ((uint8_t)((code) & 0xFF))
#endif

#include "via.h"
#include "eeconfig.h"

uint32_t last_matrix_activity_elapsed(void);
