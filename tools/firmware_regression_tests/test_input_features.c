#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "quantum.h"

void kill_switch_init(void);
void via_qmk_kill_swtich_command(uint8_t type, uint8_t *data, uint8_t length);
void kkuk_init(void);
void kkuk_idle(void);
bool kkuk_process(uint16_t keycode, keyrecord_t *record);
void via_qmk_kkuk_command(uint8_t *data, uint8_t length);

enum { MODE_LAST = 1, MODE_NEUTRAL, MODE_FIRST, MODE_KEY_0, MODE_KEY_1 };

static report_keyboard_t report_store;
report_keyboard_t *keyboard_report = &report_store;
static uint32_t now_ms;
static unsigned sent_count;
static report_keyboard_t sent_reports[64];
static unsigned revision_count;

uint32_t millis(void) { return now_ms; }
void logPrintf(const char *fmt, ...) { (void)fmt; }
void era_state_sync_bump_config(void) { revision_count++; }

static bool report_has(const report_keyboard_t *report, uint8_t key) {
    for (size_t i = 0; i < sizeof(report->keys); i++) if (report->keys[i] == key) return true;
    return false;
}
void add_key(uint8_t key) {
    if (key == KC_NO || report_has(&report_store, key)) return;
    for (size_t i = 0; i < sizeof(report_store.keys); i++) if (report_store.keys[i] == KC_NO) { report_store.keys[i] = key; return; }
}
void del_key(uint8_t key) {
    for (size_t i = 0; i < sizeof(report_store.keys); i++) if (report_store.keys[i] == key) report_store.keys[i] = KC_NO;
}
void add_mods(uint8_t mods) { report_store.mods |= mods; }
void del_mods(uint8_t mods) { report_store.mods &= (uint8_t)~mods; }
void clear_keys(void) { memset(report_store.keys, 0, sizeof(report_store.keys)); }
/* What the host receives: the production 6KRO sender passes a copy through the filter. */
void send_keyboard_report(void) {
    assert(sent_count < (sizeof(sent_reports) / sizeof(sent_reports[0])));
    report_keyboard_t sent = report_store;
    keyboard_report_filter(&sent);
    sent_reports[sent_count++] = sent;
}
static void reset_report(void) { memset(&report_store, 0, sizeof(report_store)); sent_count = 0; }
static const report_keyboard_t *host(void) { assert(sent_count > 0U); return &sent_reports[sent_count - 1U]; }
static bool never_both(uint8_t a, uint8_t b) {
    for (unsigned i = 0; i < sent_count; i++) if (report_has(&sent_reports[i], a) && report_has(&sent_reports[i], b)) return false;
    return true;
}

static void kkuk_set(uint8_t value_id, uint8_t value) {
    uint8_t data[32] = {id_custom_set_value, 12U, value_id, value};
    via_qmk_kkuk_command(data, sizeof(data));
}
static uint8_t kkuk_get(uint8_t value_id) {
    uint8_t data[32] = {id_custom_get_value, 12U, value_id, 0U};
    via_qmk_kkuk_command(data, sizeof(data));
    return data[3];
}
static uint8_t socd_set(uint8_t pair, uint8_t value_id, uint16_t value) {
    uint8_t data[32] = {id_custom_set_value, (uint8_t)(10U + pair), value_id, (uint8_t)(value >> 8), (uint8_t)value};
    if (value_id == 1U || value_id == 4U) data[3] = (uint8_t)value;
    via_qmk_kill_swtich_command(pair, data, sizeof(data));
    return data[3];
}
static uint16_t socd_get(uint8_t pair, uint8_t value_id) {
    uint8_t data[32] = {id_custom_get_value, (uint8_t)(10U + pair), value_id, 0U, 0U};
    via_qmk_kill_swtich_command(pair, data, sizeof(data));
    return (value_id == 1U || value_id == 4U) ? data[3] : (uint16_t)(((uint16_t)data[3] << 8) | data[4]);
}
static void socd_pair(uint8_t pair, uint16_t key0, uint16_t key1, uint8_t mode) {
    socd_set(pair, 2U, key0);
    socd_set(pair, 3U, key1);
    socd_set(pair, 4U, mode);
    socd_set(pair, 1U, 1U);
}
/* Any owner of a usage -- a physical key, a Tap Dance, a macro -- reaches the report the same way. */
static void output(uint16_t keycode, bool pressed) {
    if (IS_BASIC_KEYCODE(keycode)) {
        if (pressed) add_key((uint8_t)keycode); else del_key((uint8_t)keycode);
    } else if (IS_MODIFIER_KEYCODE(keycode)) {
        if (pressed) add_mods(MOD_BIT(keycode)); else del_mods(MOD_BIT(keycode));
    }
    send_keyboard_report();
}
static void dispatch_kkuk(uint16_t keycode, bool pressed, uint8_t row, uint8_t col) {
    keyrecord_t record = {.event = {.key = {.col = col, .row = row}, .pressed = pressed}};
    kkuk_process(keycode, &record);
}

static void test_kkuk_timing_and_validation(void) {
    kkuk_init();
    kkuk_set(2U, 5U);
    kkuk_set(3U, 5U);
    kkuk_set(1U, 1U);
    reset_report();
    now_ms = 1000U;
    dispatch_kkuk(KC_A, true, 0U, 0U);
    dispatch_kkuk(KC_D, true, 0U, 1U);
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
    for (unsigned i = 0; i < 256U; i++) dispatch_kkuk(KC_A, true, (uint8_t)(i / MATRIX_COLS), (uint8_t)(i % MATRIX_COLS));
    now_ms = 3050U; kkuk_idle();
    now_ms = 3100U; kkuk_idle();
    assert(sent_count == 2U); /* key count saturates instead of wrapping to zero */
    kkuk_set(1U, 0U);
}

/* KKUK leaves SOCD keys out of its count. A key counted at its press must be
   uncounted at its release even if SOCD claimed it in between, or one held key
   later reads as two and KKUK repeats. */
static void test_kkuk_count_survives_socd_change(void) {
    kill_switch_init();
    kkuk_init();
    kkuk_set(2U, 5U);
    kkuk_set(3U, 5U);
    kkuk_set(1U, 0U);
    kkuk_set(1U, 1U);
    reset_report();
    now_ms = 5000U;
    dispatch_kkuk(KC_B, true, 1U, 0U);
    socd_pair(0U, KC_B, KC_D, MODE_LAST);
    dispatch_kkuk(KC_B, false, 1U, 0U);
    dispatch_kkuk(KC_C, true, 1U, 1U);
    sent_count = 0U;
    now_ms = 5100U; kkuk_idle();
    now_ms = 5200U; kkuk_idle();
    assert(sent_count == 0U);
    dispatch_kkuk(KC_C, false, 1U, 1U);
    kkuk_set(1U, 0U);
    socd_set(0U, 1U, 0U);
}

static void test_socd_modes(void) {
    kill_switch_init();
    for (uint8_t mode = MODE_LAST; mode <= MODE_KEY_1; mode++) {
        for (int a_first = 0; a_first < 2; a_first++) {
            socd_pair(0U, KC_A, KC_D, mode);
            reset_report();
            uint8_t first = a_first ? KC_A : KC_D, second = a_first ? KC_D : KC_A;
            output(first, true);
            output(second, true);
            uint8_t expect = mode == MODE_LAST ? second : mode == MODE_FIRST ? first : mode == MODE_KEY_0 ? KC_A : mode == MODE_KEY_1 ? KC_D : KC_NO;
            assert(report_has(host(), KC_A) == (expect == KC_A));
            assert(report_has(host(), KC_D) == (expect == KC_D));
            output(second, false);
            assert(report_has(host(), first)); /* releasing one reports the key still held */
            output(first, false);
            assert(never_both(KC_A, KC_D));
        }
    }

    socd_pair(0U, KC_A, KC_D, MODE_LAST);
    reset_report();
    output(KC_A, true);
    output(KC_D, true);
    output(KC_A, false);
    output(KC_A, true); /* a re-press is the newest input again */
    assert(report_has(host(), KC_A) && !report_has(host(), KC_D));
    output(KC_A, false);
    output(KC_D, false);
    assert(never_both(KC_A, KC_D));
    socd_set(0U, 1U, 0U);
}

static void test_socd_config_changes_and_validation(void) {
    kill_switch_init();
    socd_pair(0U, KC_A, KC_D, MODE_LAST);
    reset_report();
    output(KC_A, true);
    output(KC_D, true);
    assert(!report_has(host(), KC_A));
    unsigned before = sent_count;
    socd_set(0U, 1U, 0U);
    assert(sent_count == before + 1U); /* disabling reaches the host without input */
    assert(report_has(host(), KC_A) && report_has(host(), KC_D));
    socd_set(0U, 1U, 1U);
    assert(!(report_has(host(), KC_A) && report_has(host(), KC_D))); /* re-enabling applies to keys already held */
    socd_set(0U, 4U, MODE_NEUTRAL);
    assert(!report_has(host(), KC_A) && !report_has(host(), KC_D));
    output(KC_A, false);
    output(KC_D, false);

    assert(socd_set(0U, 4U, 9U) == MODE_NEUTRAL); /* an unknown mode keeps the setting and the answer shows it */
    assert(socd_get(0U, 4U) == MODE_NEUTRAL);
    socd_set(0U, 1U, 0xFFU);
    assert(socd_get(0U, 1U) == 1U);

    socd_pair(0U, 0x5207U, KC_A, MODE_LAST); /* non-reportable 16-bit keycode with low byte KC_D */
    assert(!kill_switch_is_use(0x5207U) && !kill_switch_is_use(KC_A));
    reset_report();
    output(KC_D, true);
    output(KC_A, true);
    assert(report_has(host(), KC_D) && report_has(host(), KC_A)); /* an invalid pair never truncates 0x5207 to KC_D */
    output(KC_A, false);
    output(KC_D, false);

    socd_pair(0U, KC_A, KC_A, MODE_NEUTRAL);
    assert(!kill_switch_is_use(KC_A));

    socd_pair(0U, KC_A, KC_D, MODE_NEUTRAL);
    socd_pair(1U, KC_A, KC_S, MODE_NEUTRAL); /* a key in both pairs: both stand down */
    assert(!kill_switch_is_use(KC_D) && !kill_switch_is_use(KC_S));
    reset_report();
    output(KC_A, true);
    output(KC_D, true);
    output(KC_S, true);
    assert(report_has(host(), KC_A) && report_has(host(), KC_D) && report_has(host(), KC_S));
    output(KC_A, false);
    output(KC_D, false);
    output(KC_S, false);
    socd_set(0U, 1U, 0U);
    socd_set(1U, 1U, 0U);
}

static void test_socd_modifier_pair(void) {
    kill_switch_init();
    socd_pair(0U, KC_LEFT_CTRL, KC_RIGHT_CTRL, MODE_LAST);
    reset_report();
    output(KC_LEFT_CTRL, true);
    assert(host()->mods == MOD_BIT(KC_LEFT_CTRL));
    output(KC_RIGHT_CTRL, true);
    assert(host()->mods == MOD_BIT(KC_RIGHT_CTRL));
    output(KC_RIGHT_CTRL, false);
    assert(host()->mods == MOD_BIT(KC_LEFT_CTRL));
    output(KC_LEFT_CTRL, false);
    assert(host()->mods == 0U);
    socd_set(0U, 1U, 0U);
}


static void test_kkuk_preserves_active_socd_keys(void) {
    kill_switch_init();
    for (uint8_t mode = MODE_LAST; mode <= MODE_KEY_1; mode++) {
        for (int both = 0; both < 2; both++) {
            kkuk_set(1U, 0U);
            socd_pair(0U, KC_A, KC_D, mode);
            socd_pair(1U, KC_F, KC_S, MODE_LAST);
            kkuk_set(2U, 5U); kkuk_set(3U, 5U); kkuk_set(1U, 1U);
            reset_report(); now_ms = 10000U;
            output(KC_A, true); /* a usage from any report owner, including TD/macro */
            if (both) output(KC_D, true);
            output(KC_LEFT_CTRL, true);
            output(KC_F, true);
            dispatch_kkuk(KC_B, true, 0U, 1U); output(KC_B, true);
            dispatch_kkuk(KC_C, true, 0U, 2U); output(KC_C, true);
            const uint8_t winner = !both ? KC_A : mode == MODE_NEUTRAL ? KC_NO :
                mode == MODE_FIRST || mode == MODE_KEY_0 ? KC_A : KC_D;
            now_ms += 50U; kkuk_idle();
            unsigned before = sent_count;
            now_ms += 50U; kkuk_idle();
            assert(sent_count == before + 2U);
            for (unsigned i = before; i < sent_count; i++) {
                assert(report_has(&sent_reports[i], KC_A) == (winner == KC_A));
                assert(report_has(&sent_reports[i], KC_D) == (winner == KC_D));
                assert(sent_reports[i].mods == MOD_BIT(KC_LEFT_CTRL));
                assert(report_has(&sent_reports[i], KC_F));
            }
            assert(!report_has(&sent_reports[before], KC_B) && !report_has(&sent_reports[before], KC_C));
            assert(report_has(host(), KC_B) && report_has(host(), KC_C));
            /* A later real release must stay released across the next pulse. */
            output(KC_A, false); if (both) output(KC_D, false);
            before = sent_count; now_ms += 50U; kkuk_idle();
            assert(sent_count == before + 2U);
            for (unsigned i = before; i < sent_count; i++) {
                assert(!report_has(&sent_reports[i], KC_A) && !report_has(&sent_reports[i], KC_D));
            }
            output(KC_LEFT_CTRL, false); output(KC_F, false); output(KC_B, false); output(KC_C, false);
            kkuk_set(1U, 0U);
        }
    }
    socd_set(0U, 1U, 0U); socd_set(1U, 1U, 0U);
    for (int overlap = 0; overlap < 2; overlap++) {
        if (overlap) {
            socd_pair(0U, KC_A, KC_D, MODE_LAST);
            socd_pair(1U, KC_A, KC_S, MODE_LAST);
        }
        reset_report(); now_ms = 20000U; kkuk_set(1U, 1U);
        output(KC_A, true);
        dispatch_kkuk(KC_B, true, 0U, 1U); output(KC_B, true);
        dispatch_kkuk(KC_C, true, 0U, 2U); output(KC_C, true);
        now_ms += 50U; kkuk_idle(); unsigned before = sent_count;
        now_ms += 50U; kkuk_idle();
        assert(sent_count == before + 2U);
        assert(!report_has(&sent_reports[before], KC_A)); /* disabled/ambiguous pairs are ordinary keys */
        output(KC_A, false); output(KC_B, false); output(KC_C, false);
        kkuk_set(1U, 0U);
    }
    socd_set(0U, 1U, 0U); socd_set(1U, 1U, 0U);
}

int main(void) {
    test_kkuk_timing_and_validation();
    test_kkuk_count_survives_socd_change();
    test_socd_modes();
    test_socd_config_changes_and_validation();
    test_socd_modifier_pair();
    test_kkuk_preserves_active_socd_keys();
    printf("input feature tests passed\n");
    return 0;
}
