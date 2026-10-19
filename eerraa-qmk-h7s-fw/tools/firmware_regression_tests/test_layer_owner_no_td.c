/* action_layer.c and QMK headers are production. Target GPIO/timer/report
 * declarations and keymap lookup are adapters; no layer state is simulated. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "action_layer.h"
#include "keymap_common.h"

bool disable_action_cache;
action_t action_for_key(uint8_t layer, keypos_t key) {
    (void)layer; (void)key; return (action_t){.code = ACTION_NO};
}
uint16_t keymap_key_to_keycode(uint8_t layer, keypos_t key) {
    (void)layer; (void)key; return KC_NO;
}
uint8_t biton(uint8_t bits) {
    uint8_t index = 0; while (bits >>= 1) ++index; return index;
}
static void physical_layer(uint16_t owner, uint8_t layer, bool pressed) {
    layer_set_physical_owner(owner);
    if (pressed) layer_on(layer); else layer_off(layer);
    layer_set_physical_owner(UINT16_MAX);
}
static void check_helper(bool tri) {
    layer_clear();
    physical_layer(0, 1, true); assert(layer_state == 2U);
    if (tri) update_tri_layer(1, 2, 3);
    physical_layer(0, 1, false);
    assert(layer_state == 0U);
}
int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "plain")) { check_helper(false); return 0; }
    check_helper(false); check_helper(true);
    physical_layer(0, 1, true); physical_layer(1, 1, true);
    update_tri_layer(1, 2, 3); physical_layer(0, 1, false);
    assert(layer_state == 2U); physical_layer(1, 1, false); assert(layer_state == 0U);
    physical_layer(0, 1, true); physical_layer(1, 2, true);
    update_tri_layer(1, 2, 3); assert(layer_state == 14U);
    physical_layer(0, 1, false); update_tri_layer(1, 2, 3); assert(layer_state == 4U);
    physical_layer(1, 2, false); assert(layer_state == 0U);
    layer_on(1); physical_layer(0, 1, true); update_tri_layer(1, 2, 3);
    physical_layer(0, 1, false); assert(layer_state == 2U); layer_off(1); assert(layer_state == 0U);
    physical_layer(0, 1, true); layer_move(2); update_tri_layer(1, 2, 3);
    physical_layer(0, 1, false); assert(layer_state == 4U);
    layer_clear();
    puts("PASS: no-TD production layer ownership: tri helper, shared owners, persistent bits and replacement");
    return 0;
}
