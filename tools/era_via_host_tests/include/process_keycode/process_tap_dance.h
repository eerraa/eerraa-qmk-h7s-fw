#pragma once

/* Use the production type in both include/ and the equally deep sandbox/.
 * The VIA fixture replaces hardware, not the TD execution layout. */
#undef QK_TAP_DANCE_GET_INDEX
#include "../../../../src/ap/modules/qmk/quantum/process_keycode/process_tap_dance.h"
