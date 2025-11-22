#pragma once

#include "hw_def.h"

#define HID_KEYBOARD_REPORT_SIZE (HW_KEYS_PRESS_MAX + 2U)
#define KEY_TIME_LOG_MAX         32  // V251009R9: 계측 모듈과 본체에서 공유
