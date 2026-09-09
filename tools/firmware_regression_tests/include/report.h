#pragma once
#include <stdint.h>
#define REPORT_ID_MOUSE 2U
#define REPORT_ID_SYSTEM 3U
#define REPORT_ID_CONSUMER 4U
typedef struct __attribute__((packed)) { uint8_t report_id; uint16_t usage; } report_extra_t;
typedef struct __attribute__((packed)) { uint8_t report_id, buttons; int8_t x, y, v, h; } report_mouse_t;
