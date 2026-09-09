#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "quantum.h"

void kill_switch_init(void);
bool kill_switch_process(uint16_t keycode, keyrecord_t *record);
void via_qmk_kill_swtich_command(uint8_t type, uint8_t *data, uint8_t length);
void kkuk_init(void);
void kkuk_idle(void);
bool kkuk_process(uint16_t keycode, keyrecord_t *record);
void via_qmk_kkuk_command(uint8_t *data, uint8_t length);

static report_keyboard_t report_store;
report_keyboard_t *keyboard_report = &report_store;
static uint32_t now_ms;
static unsigned sent_count;
static report_keyboard_t sent_reports[32];
static unsigned revision_count;

uint32_t millis(void) { return now_ms; }
void logPrintf(const char *fmt, ...) { (void)fmt; }
void era_state_sync_bump_config(void) { revision_count++; }

static bool has_key(uint8_t key) {
    for (size_t i = 0; i < sizeof(report_store.keys); i++) if (report_store.keys[i] == key) return true;
    return false;
}
void add_key(uint8_t key) {
    if (key == KC_NO || has_key(key)) return;
    for (size_t i = 0; i < sizeof(report_store.keys); i++) if (report_store.keys[i] == KC_NO) { report_store.keys[i] = key; return; }
}
void del_key(uint8_t key) {
    for (size_t i = 0; i < sizeof(report_store.keys); i++) if (report_store.keys[i] == key) report_store.keys[i] = KC_NO;
}
void add_mods(uint8_t mods) { report_store.mods |= mods; }
void del_mods(uint8_t mods) { report_store.mods &= (uint8_t)~mods; }
void clear_keys(void) { memset(report_store.keys, 0, sizeof(report_store.keys)); }
void send_keyboard_report(void) {
    assert(sent_count < (sizeof(sent_reports) / sizeof(sent_reports[0])));
    sent_reports[sent_count++] = report_store;
}
static void reset_report(void) { memset(&report_store, 0, sizeof(report_store)); sent_count = 0; }

static void kkuk_set(uint8_t value_id, uint8_t value) {
    uint8_t data[32] = {id_custom_set_value, 12U, value_id, value};
    via_qmk_kkuk_command(data, sizeof(data));
}
static uint8_t kkuk_get(uint8_t value_id) {
    uint8_t data[32] = {id_custom_get_value, 12U, value_id, 0U};
    via_qmk_kkuk_command(data, sizeof(data));
    return data[3];
}
static void socd_set(uint8_t pair, uint8_t value_id, uint16_t value) {
    uint8_t data[32] = {id_custom_set_value, (uint8_t)(10U + pair), value_id, (uint8_t)(value >> 8), (uint8_t)value};
    if (value_id == 1U) data[3] = (uint8_t)value;
    via_qmk_kill_swtich_command(pair, data, sizeof(data));
}
static uint16_t socd_get(uint8_t pair, uint8_t value_id) {
    uint8_t data[32] = {id_custom_get_value, (uint8_t)(10U + pair), value_id, 0U, 0U};
    via_qmk_kill_swtich_command(pair, data, sizeof(data));
    return value_id == 1U ? data[3] : (uint16_t)(((uint16_t)data[3] << 8) | data[4]);
}
static void dispatch_socd(uint16_t keycode, bool pressed) {
    keyrecord_t record = {.event = {.pressed = pressed}};
    kill_switch_process(keycode, &record);
    if (IS_BASIC_KEYCODE(keycode)) {
        if (pressed) add_key((uint8_t)keycode); else del_key((uint8_t)keycode);
    } else if (IS_MODIFIER_KEYCODE(keycode)) {
        if (pressed) add_mods(MOD_BIT(keycode)); else del_mods(MOD_BIT(keycode));
    }
}
static void dispatch_kkuk(uint16_t keycode, bool pressed) {
    keyrecord_t record = {.event = {.pressed = pressed}};
    kkuk_process(keycode, &record);
}

static void test_kkuk_timing_and_validation(void) {
    kkuk_init();
    kkuk_set(2U, 5U);
    kkuk_set(3U, 5U);
    kkuk_set(1U, 1U);
    reset_report();
    now_ms = 1000U;
    dispatch_kkuk(KC_A, true);
    dispatch_kkuk(KC_D, true);
    now_ms = 1049U; kkuk_idle(); assert(sent_count == 0U);
    now_ms = 1050U; kkuk_idle(); assert(sent_count == 0U); /* activation must not underflow a future timestamp */
    now_ms = 1099U; kkuk_idle(); assert(sent_count == 0U);
    now_ms = 1100U; kkuk_idle(); assert(sent_count == 2U);

    kkuk_set(2U, 0U);
    kkuk_set(3U, 0U);
    kkuk_set(1U, 0xFFU);
    assert(kkuk_get(2U) == 5U);
    assert(kkuk_get(3U) == 5U);
    assert(kkuk_get(1U) == 1U);

    kkuk_set(1U, 0U);
    kkuk_set(1U, 1U);
    sent_count = 0U;
    now_ms = 2000U;
    kkuk_idle();
    assert(sent_count == 0U); /* live enable starts a fresh tracking epoch */

    kkuk_set(1U, 0U);
    kkuk_set(1U, 1U);
    now_ms = 3000U;
    for (unsigned i = 0; i < 256U; i++) dispatch_kkuk(KC_A, true);
    now_ms = 3050U; kkuk_idle();
    now_ms = 3100U; kkuk_idle();
    assert(sent_count == 2U); /* key count saturates instead of wrapping to zero */
    kkuk_set(1U, 0U);
}

static void test_socd_modifier_and_validation(void) {
    kill_switch_init();
    socd_set(0U, 2U, KC_LEFT_CTRL);
    socd_set(0U, 3U, KC_RIGHT_CTRL);
    socd_set(0U, 1U, 1U);
    reset_report();
    dispatch_socd(KC_LEFT_CTRL, true);
    assert(report_store.mods == MOD_BIT(KC_LEFT_CTRL));
    dispatch_socd(KC_RIGHT_CTRL, true);
    assert(report_store.mods == MOD_BIT(KC_RIGHT_CTRL));
    dispatch_socd(KC_RIGHT_CTRL, false);
    assert(report_store.mods == MOD_BIT(KC_LEFT_CTRL));
    dispatch_socd(KC_LEFT_CTRL, false);
    assert(report_store.mods == 0U);

    socd_set(0U, 2U, 0x5207U); /* non-reportable 16-bit keycode with low byte KC_D */
    socd_set(0U, 3U, KC_A);
    assert(!kill_switch_is_use(0x5207U));
    assert(!kill_switch_is_use(KC_A));
    reset_report();
    add_key(KC_D);
    dispatch_socd(0x5207U, true);
    dispatch_socd(KC_A, true);
    assert(has_key(KC_D)); /* invalid config must never truncate 0x5207 to KC_D */
    dispatch_socd(KC_A, false);
    dispatch_socd(0x5207U, false);

    socd_set(0U, 2U, KC_A);
    socd_set(0U, 3U, KC_D);
    socd_set(0U, 1U, 1U);
    reset_report();
    dispatch_socd(KC_A, true);
    dispatch_socd(KC_D, true);
    assert(!has_key(KC_A) && has_key(KC_D));
    unsigned sent_before_disable = sent_count;
    socd_set(0U, 1U, 0U);
    assert(has_key(KC_A) && has_key(KC_D));
    assert(sent_count == sent_before_disable + 1U); /* disabling restores the key SOCD had suppressed */

    socd_set(0U, 1U, 1U);
    reset_report();
    dispatch_socd(KC_A, true);
    add_key(KC_F); /* unrelated physical key */
    socd_set(0U, 2U, KC_F); /* live remap must clear stale pressed[0] */
    dispatch_socd(KC_D, true);
    assert(has_key(KC_F));
    dispatch_socd(KC_D, false);
    dispatch_socd(KC_A, false);

    socd_set(0U, 1U, 0xFFU);
    assert(socd_get(0U, 1U) == 1U);
}

int main(void) {
    test_kkuk_timing_and_validation();
    test_socd_modifier_and_validation();
    printf("input feature tests passed\n");
    return 0;
}
