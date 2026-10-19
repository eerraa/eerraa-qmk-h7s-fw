#pragma once

#include <stdbool.h>
#include <stdint.h>

bool usbIsSuspended(void);
bool usbHostSeen(void);
uint32_t usbSofCount(void);
