#pragma once

#include <stdbool.h>

void rgblight_suspend(void);
void rgblight_wakeup(void);
void rgblight_set_output_suspend_state(bool suspended);
void rgblight_disable_noeeprom(void);
bool rgblight_is_enabled(void);
