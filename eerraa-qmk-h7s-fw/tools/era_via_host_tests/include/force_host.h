#pragma once
#ifndef QMK_KEYMAP_CONFIG_H
#    define QMK_KEYMAP_CONFIG_H "host_config.h"
#endif
#ifndef QMK_KEYBOARD_H
#    define QMK_KEYBOARD_H "quantum.h"
#endif

/* Value/protocol-only fixture geometry, not a firmware board definition. */
#ifndef MATRIX_ROWS
#    define MATRIX_ROWS 5
#endif
#ifndef MATRIX_COLS
#    define MATRIX_COLS 15
#endif
