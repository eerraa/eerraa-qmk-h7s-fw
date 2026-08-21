#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "action.h"

typedef struct {
    uint16_t interrupting_keycode;
    uint8_t  count;
    uint8_t  weak_mods;
    bool     pressed : 1;
    bool     finished : 1;
    bool     interrupted : 1;
} tap_dance_state_t;

typedef void (*tap_dance_user_fn_t)(tap_dance_state_t *state, void *user_data);

typedef struct {
    tap_dance_state_t state;
    struct {
        tap_dance_user_fn_t on_each_tap;
        tap_dance_user_fn_t on_dance_finished;
        tap_dance_user_fn_t on_reset;
        tap_dance_user_fn_t on_each_release;
    } fn;
    void *user_data;
} tap_dance_action_t;
