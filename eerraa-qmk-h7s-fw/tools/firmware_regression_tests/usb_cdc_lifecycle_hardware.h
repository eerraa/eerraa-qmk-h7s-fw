#pragma once
#include "hw_def.h"

/* CDC uses only the IN endpoint's maxpacket to choose a bulk ZLP. This is a
 * controller adapter, not an emulation of the HAL or the H7RS register block. */
typedef struct {
  struct { uint32_t maxpacket; } IN_ep[16];
} PCD_HandleTypeDef;
