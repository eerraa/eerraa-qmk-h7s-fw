#pragma once

#include <stdbool.h>

void rgblight_suspend(void);
void rgblight_wakeup(void);
void rgblight_disable_noeeprom(void);
bool rgblight_is_enabled(void);
