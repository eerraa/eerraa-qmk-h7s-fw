#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "via.h"
#include "keycodes.h"
#include "tapping_term.h"
#include "tapdance.h"
#include "port.h"
#include "era_state_sync.h"
#include "mousekey_config.h"
#include "mousekey.h"

extern uint8_t  mk_delay;
extern uint8_t  mk_interval;
extern uint8_t  mk_max_speed;
extern uint8_t  mk_time_to_max;
extern uint8_t  mk_wheel_delay;
extern uint8_t  mk_wheel_interval;
extern uint8_t  mk_wheel_max_speed;
extern uint8_t  mk_wheel_time_to_max;
extern uint8_t  mk_move_delta;
extern uint8_t  mk_wheel_delta;

static int g_failures = 0;

static void expect_true(const char *name, bool cond) {
    if (!cond) {
        printf("FAIL %s\n", name);
        g_failures++;
    } else {
        printf("PASS %s\n", name);
    }
}

static void expect_eq_u16(const char *name, uint16_t got, uint16_t want) {
    if (got != want) {
        printf("FAIL %s got=%u want=%u\n", name, got, want);
        g_failures++;
    } else {
        printf("PASS %s\n", name);
    }
}

static void expect_eq_u8(const char *name, uint8_t got, uint8_t want) {
    if (got != want) {
        printf("FAIL %s got=%u want=%u\n", name, got, want);
        g_failures++;
    } else {
        printf("PASS %s\n", name);
    }
}

static void zero_report(uint8_t *data) {
    memset(data, 0, 32);
}

static uint16_t be16(uint8_t hi, uint8_t lo) {
    return ((uint16_t)hi << 8) | lo;
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static bool tapping_exact_set(uint16_t ms) {
    uint8_t report[32];
    zero_report(report);
    report[0] = id_custom_set_value;
    report[1] = id_qmk_tapping;
    report[2] = id_qmk_tapping_global_term_exact;
    report[3] = (uint8_t)(ms >> 8);
    report[4] = (uint8_t)(ms & 0xFF);
    return tapping_term_handle_via_command(report, 32);
}

static uint16_t tapping_exact_get(void) {
    uint8_t report[32];
    zero_report(report);
    report[0] = id_custom_get_value;
    report[1] = id_qmk_tapping;
    report[2] = id_qmk_tapping_global_term_exact;
    tapping_term_handle_via_command(report, 32);
    return be16(report[3], report[4]);
}

static uint8_t tapping_legacy_get(void) {
    uint8_t report[32];
    zero_report(report);
    report[0] = id_custom_get_value;
    report[1] = id_qmk_tapping;
    report[2] = id_qmk_tapping_global_term;
    tapping_term_handle_via_command(report, 32);
    return report[3];
}

static bool tapdance_exact_set(uint8_t slot, uint16_t ms) {
    uint8_t report[32];
    zero_report(report);
    report[0] = id_custom_set_value;
    report[1] = id_qmk_tapdance;
    report[2] = (uint8_t)(id_qmk_tapdance_1_term_exact + slot);
    report[3] = (uint8_t)(ms >> 8);
    report[4] = (uint8_t)(ms & 0xFF);
    return tapdance_handle_via_command(report, 32);
}

static uint16_t tapdance_exact_get(uint8_t slot) {
    uint8_t report[32];
    zero_report(report);
    report[0] = id_custom_get_value;
    report[1] = id_qmk_tapdance;
    report[2] = (uint8_t)(id_qmk_tapdance_1_term_exact + slot);
    tapdance_handle_via_command(report, 32);
    return be16(report[3], report[4]);
}

static bool mousekey_set(uint8_t value_id, uint8_t value) {
    uint8_t report[32];
    zero_report(report);
    report[0] = id_custom_set_value;
    report[1] = id_qmk_mousekey;
    report[2] = value_id;
    report[3] = value;
    return mousekey_config_handle_via_command(report, 32);
}

static uint8_t mousekey_get(uint8_t value_id) {
    uint8_t report[32];
    zero_report(report);
    report[0] = id_custom_get_value;
    report[1] = id_qmk_mousekey;
    report[2] = value_id;
    mousekey_config_handle_via_command(report, 32);
    return report[3];
}

static uint8_t mousekey_set_echo(uint8_t value_id, uint8_t value) {
    uint8_t report[32];
    zero_report(report);
    report[0] = id_custom_set_value;
    report[1] = id_qmk_mousekey;
    report[2] = value_id;
    report[3] = value;
    mousekey_config_handle_via_command(report, 32);
    return report[3];
}

static uint16_t mouse_exact(uint8_t command, uint8_t id, uint16_t value) {
    uint8_t data[32] = {command, id_qmk_mousekey, id, value >> 8, value & 255};
    expect_true("MOUSE precision handled", mousekey_config_handle_via_command(data, sizeof(data)));
    if (command == id_custom_get_value) expect_eq_u8("MOUSE marker", data[5], 0xE4);
    return ((uint16_t)data[3] << 8) | data[4];
}
static void test_mousekey(void) {
    mousekey_config_init();
    expect_eq_u16("default real cursor time", mk_cursor_ramp_ms, 1000);
    expect_eq_u8("default actual target", mk_cursor_top, 16);
    uint8_t cap[32] = {id_custom_get_value, id_qmk_mousekey, 7};
    expect_true("MOUSE capability", mousekey_config_handle_via_command(cap, sizeof(cap)));
    expect_eq_u8("cap marker", cap[3], 0xE4); expect_eq_u8("cap revision", cap[4], 1);
    mouse_exact(id_custom_set_value, 8, 7);
    mouse_exact(id_custom_set_value, 9, 17);
    mouse_exact(id_custom_set_value, 10, 137);
    expect_eq_u16("exact start", mouse_exact(id_custom_get_value, 8, 0), 7);
    expect_eq_u16("exact target", mouse_exact(id_custom_get_value, 9, 0), 17);
    expect_eq_u16("exact time", mouse_exact(id_custom_get_value, 10, 0), 137);
    expect_eq_u8("runtime target is not ratio-rounded", mk_cursor_top, 17);
    expect_eq_u16("runtime time", mk_cursor_ramp_ms, 137);
    expect_eq_u8("stock closest target", mousekey_get(2), 16);
    expect_eq_u8("stock closest ramp", mousekey_get(3), 10);
    expect_eq_u16("stock read never changes exact target", mouse_exact(id_custom_get_value, 9, 0), 17);
    mouse_exact(id_custom_set_value, 10, 65535);
    mouse_exact(id_custom_set_value, 11, 1);
    expect_eq_u16("fast rate never truncates ramp", mk_cursor_ramp_ms, 65535);
    mouse_exact(id_custom_set_value, 13, 6); mouse_exact(id_custom_set_value, 14, 137);
    expect_eq_u8("wheel tie projects Mild", mousekey_get(6), 1);
    mouse_exact(id_custom_set_value, 13, 7);
    expect_eq_u8("wheel nearest Strong", mousekey_get(6), 2);
    mouse_exact(id_custom_set_value, 14, 0);
    expect_eq_u8("wheel zero ramp Off", mousekey_get(6), 0);
    uint8_t invalid[32] = {id_custom_set_value, id_qmk_mousekey, 9, 0, 128};
    expect_true("invalid exact rejected", !mousekey_config_handle_via_command(invalid, sizeof(invalid)));
    expect_eq_u8("invalid exact unhandled", invalid[0], id_unhandled);
    expect_eq_u8("failed SET leaves target", mk_cursor_top, 17);
    mousekey_config_storage_flush(true);
    mouse_exact(id_custom_set_value, 9, 27);
    mousekey_config_init();
    expect_eq_u8("SAVE survives reload", mk_cursor_top, 17);
    expect_eq_u16("long ramp survives reload", mk_cursor_ramp_ms, 65535);
    mousekey_set(3, 30); mousekey_set(4, 5);
    expect_eq_u16("stock ramp remains full duration", mk_cursor_ramp_ms, 1500);
    expect_eq_u8("stock ramp roundtrip", mousekey_get(3), 30);
}

static void test_state_sync_invalid(void) {
    uint8_t report[32];

    /* 봉투의 예약 구간이 0이 아니면 INVALID로 답한다 */
    zero_report(report);
    report[0] = 0x02;
    report[1] = ERA_STATE_SYNC_KEYBOARD_VALUE;
    report[2] = ERA_STATE_SYNC_ENVELOPE_VERSION;
    report[4] = 0x12;
    report[5] = 0x34;
    report[9] = 0x01;  /* 예약 구간 오염 */
    expect_true("dirty envelope handled", era_state_sync_via_command(report, 32));
    expect_eq_u8("dirty envelope is INVALID", report[3], ERA_STATE_SYNC_STATUS_INVALID);
    expect_eq_u8("INVALID echoes tag hi", report[4], 0x12);
    expect_eq_u8("INVALID echoes tag lo", report[5], 0x34);

    /* status 바이트가 0이 아닌 요청도 INVALID */
    zero_report(report);
    report[0] = 0x02;
    report[1] = ERA_STATE_SYNC_KEYBOARD_VALUE;
    report[2] = ERA_STATE_SYNC_ENVELOPE_VERSION;
    report[3] = 0x01;
    expect_true("dirty status handled", era_state_sync_via_command(report, 32));
    expect_eq_u8("dirty status is INVALID", report[3], ERA_STATE_SYNC_STATUS_INVALID);

    /* 32바이트가 아니면 봉투가 아니다 */
    zero_report(report);
    report[0] = 0x02;
    report[1] = ERA_STATE_SYNC_KEYBOARD_VALUE;
    report[2] = ERA_STATE_SYNC_ENVELOPE_VERSION;
    expect_true("31-byte report rejected", era_state_sync_via_command(report, 31) == false);
}

#include "test_full_term_range.h"

static uint8_t direct_command(uint8_t command, uint8_t slot, uint8_t value, uint8_t length) {
    uint8_t data[32] = {command, id_qmk_tapdance, (uint8_t)(49U + slot), value};
    bool ok = tapdance_handle_via_command(data, length);
    if (command == id_custom_get_value) {
        expect_true("direct GET handled", ok);
        expect_eq_u8("direct support marker", data[4], 0xD2);
        return data[3];
    }
    return ok;
}

static void test_direct_mode(void) {
    uint8_t saved[88], after[88];
    tapdance_storage_apply_defaults();
    tapdance_storage_flush(true);
    eeprom_read_block(saved, EECONFIG_USER_TAPDANCE, sizeof(saved));
    for (uint8_t slot = 0; slot < 8; ++slot) {
        expect_eq_u8("legacy slot stays ordinary", direct_command(id_custom_get_value, slot, 0, 32), 0);
        uint32_t before = era_state_sync_config_revision();
        expect_true("direct SET invalid refused", !direct_command(id_custom_set_value, slot, 2, 32));
        expect_true("direct SET short refused", !direct_command(id_custom_set_value, slot, 1, 3));
        expect_true("invalid direct does not bump", before == era_state_sync_config_revision());
        expect_true("direct SET on", direct_command(id_custom_set_value, slot, 1, 32));
        expect_true("direct mutation bumps", before != era_state_sync_config_revision());
        before = era_state_sync_config_revision();
        expect_true("direct SET same", direct_command(id_custom_set_value, slot, 1, 32));
        expect_true("direct unchanged no bump", before == era_state_sync_config_revision());
    }
    tapdance_init();
    expect_eq_u8("unsaved direct rolls back", direct_command(id_custom_get_value, 7, 0, 32), 0);
    for (uint8_t slot = 0; slot < 8; ++slot) direct_command(id_custom_set_value, slot, 1, 32);
    direct_command(id_custom_save, 0, 0, 32);
    tapdance_init();
    for (uint8_t slot = 0; slot < 8; ++slot)
        expect_eq_u8("saved direct reloads", direct_command(id_custom_get_value, slot, 0, 32), 1);
    eeprom_read_block(after, EECONFIG_USER_TAPDANCE, sizeof(after));
    for (size_t i = 0; i < sizeof(after); ++i)
        if (i != 81 && i != 82 && i != 83) expect_eq_u8("shipped storage unchanged", after[i], saved[i]);
    expect_eq_u8("mode bits stored", after[81], 0x55);
    expect_eq_u8("mode tag stored", after[82], 0xd2);
    expect_eq_u8("upper mode bits stored", after[83], 0x55);
    direct_command(id_custom_set_value, 3, 0, 32);
    expect_eq_u8("disable one slot", direct_command(id_custom_get_value, 3, 0, 32), 0);
    expect_eq_u8("neighbor preserved", direct_command(id_custom_get_value, 4, 0, 32), 1);
    tapdance_storage_apply_defaults();
    for (uint8_t slot = 0; slot < 8; ++slot)
        expect_eq_u8("factory reset disables direct", direct_command(id_custom_get_value, slot, 0, 32), 0);
    for (uint8_t slot = 0; slot < 8; ++slot) {
        uint8_t action[32] = {id_custom_set_value, id_qmk_tapdance, (uint8_t)(slot * 5U + 2U), 0, 1};
        expect_true("inherit hold SET", tapdance_handle_via_command(action, 32));
        expect_true("immediate mode SET", direct_command(id_custom_set_value, slot, 2, 32));
        expect_eq_u8("immediate mode GET", direct_command(id_custom_get_value, slot, 0, 32), 2);
        action[4] = 0;
        expect_true("immediate explicit first-hold silence refused", !tapdance_handle_via_command(action, 32));
        expect_true("unknown mode refused", !direct_command(id_custom_set_value, slot, 3, 32));
    }
    direct_command(id_custom_save, 0, 0, 32);
    tapdance_init();
    for (uint8_t slot = 0; slot < 8; ++slot)
        expect_eq_u8("immediate mode reload", direct_command(id_custom_get_value, slot, 0, 32), 2);
    tapdance_storage_apply_defaults();

}


/* Reproduce the official-VIA screenshot: Caps tap + GUI hold cannot enter On press.
 * Invalid SET must preserve the actual setting even if the host displays it optimistically. */
static void test_on_press_conflict(void) {
    tapdance_storage_apply_defaults();
    for (uint8_t slot = 0; slot < 8; ++slot) {
        uint8_t tap[32] = {id_custom_set_value, id_qmk_tapdance, (uint8_t)(slot * 5U + 1U), 0, KC_CAPS};
        uint8_t hold[32] = {id_custom_set_value, id_qmk_tapdance, (uint8_t)(slot * 5U + 2U), 0, KC_LGUI};
        expect_true("Caps tap accepted", tapdance_handle_via_command(tap, 32));
        expect_true("GUI hold accepted", tapdance_handle_via_command(hold, 32));
        expect_true("after-decision accepts Caps/GUI", direct_command(id_custom_set_value, slot, 1, 32));
        direct_command(id_custom_save, slot, 0, 32);
        uint8_t saved[88], after[88];
        eeprom_read_block(saved, EECONFIG_USER_TAPDANCE, sizeof(saved));
        uint32_t revision = era_state_sync_config_revision();
        uint8_t mode[32] = {id_custom_set_value, id_qmk_tapdance, (uint8_t)(49U + slot), 2};
        uint8_t request[32];
        memcpy(request, mode, sizeof(mode));
        expect_true("On press with GUI hold rejected", !tapdance_handle_via_command(mode, 32));
        expect_eq_u8("On press conflict returns unhandled", mode[0], id_unhandled);
        expect_true("conflict retains request bytes", memcmp(mode + 1, request + 1, 31) == 0);
        expect_eq_u8("conflict keeps previous mode", direct_command(id_custom_get_value, slot, 0, 32), 1);
        hold[0] = id_custom_get_value;
        expect_true("GUI hold readable after rejection", tapdance_handle_via_command(hold, 32));
        expect_eq_u8("GUI hold remains assigned", hold[4], KC_LGUI);
        expect_true("rejected mode does not bump revision", era_state_sync_config_revision() == revision);
        direct_command(id_custom_save, slot, 0, 32);
        eeprom_read_block(after, EECONFIG_USER_TAPDANCE, sizeof(after));
        expect_true("VIA follow-up SAVE preserves rejected setting", memcmp(saved, after, sizeof(saved)) == 0);

        hold[0] = id_custom_set_value; hold[3] = 0; hold[4] = KC_TRNS;
        expect_true("set Transparent first", tapdance_handle_via_command(hold, 32));
        expect_true("then On press succeeds", direct_command(id_custom_set_value, slot, 2, 32));
        hold[0] = id_custom_set_value; hold[3] = 0; hold[4] = KC_LGUI;
        expect_true("GUI assignment while On press rejected", !tapdance_handle_via_command(hold, 32));
        expect_eq_u8("reverse conflict returns unhandled", hold[0], id_unhandled);
        expect_true("leave On press before assigning hold", direct_command(id_custom_set_value, slot, 1, 32));
        hold[0] = id_custom_set_value;
        expect_true("GUI hold succeeds after mode change", tapdance_handle_via_command(hold, 32));
    }
    tapdance_storage_apply_defaults();
}

static void test_advanced_timing(void) {
    uint8_t report[32];
    tapdance_storage_apply_defaults(); tapdance_storage_flush(true);
    for (uint8_t slot = 0; slot < 8; ++slot) {
        zero_report(report); report[0] = id_custom_get_value; report[1] = id_qmk_tapdance; report[2] = 49 + slot;
        expect_true("mode capability GET", tapdance_handle_via_command(report, 32));
        expect_eq_u8("timing capability marker", report[5], 0xd3);
        report[0] = id_custom_set_value; report[2] = 57 + slot; report[3] = 0xff; report[4] = 0xff;
        expect_true("hold time short refused", !tapdance_handle_via_command(report, 4));
        report[0] = id_custom_set_value;
        expect_true("hold time maximum", tapdance_handle_via_command(report, 32));
        report[0] = id_custom_get_value;
        expect_true("hold time GET", tapdance_handle_via_command(report, 32));
        expect_eq_u16("hold time exact", be16(report[3], report[4]), 65535);
        expect_eq_u8("hold time marker", report[5], 0xd3);
        report[0] = id_custom_set_value; report[2] = 65 + slot; report[3] = 2;
        expect_true("invalid hold flag refused", !tapdance_handle_via_command(report, 32));
        report[0] = id_custom_set_value; report[3] = 1;
        expect_true("hold flag short refused", !tapdance_handle_via_command(report, 3));
        expect_true("hold flag SET", tapdance_handle_via_command(report, 32));
    }
    tapdance_init();
    report[0] = id_custom_get_value; report[2] = 64;
    tapdance_handle_via_command(report, 32);
    expect_eq_u16("unsaved advanced rolls back", be16(report[3], report[4]), 0);
    report[0] = id_custom_set_value; report[3] = 0; report[4] = 180;
    tapdance_handle_via_command(report, 32);
    report[2] = 72; report[3] = 1; tapdance_handle_via_command(report, 32);
    report[0] = id_custom_save; tapdance_handle_via_command(report, 32); tapdance_init();
    report[0] = id_custom_get_value; report[2] = 64; tapdance_handle_via_command(report, 32);
    expect_eq_u16("saved hold time reloads", be16(report[3], report[4]), 180);
    report[2] = 72; tapdance_handle_via_command(report, 32);
    expect_eq_u8("saved hold flag reloads", report[3], 1);
    expect_eq_u8("hold flag marker", report[4], 0xd3);
    report[0] = id_custom_set_value; report[2] = 64; report[3] = 0; report[4] = 0;
    expect_true("hold time zero follows shared term", tapdance_handle_via_command(report, 32));
    tapdance_storage_apply_defaults(); tapdance_storage_flush(true);
}

int main(void) {
    uint8_t  report[32];
    uint32_t before;
    const uint16_t td_values[8] = {101, 137, 141, 163, 187, 203, 499, 500};

    tapping_term_init();
    tapdance_init();

    expect_true("exact SET 137", tapping_exact_set(137));
    expect_eq_u16("exact GET 137", tapping_exact_get(), 137);
    expect_eq_u8("legacy GET 137 -> 12 (120ms floor-20)", tapping_legacy_get(), 12);

    expect_true("reject exact SET 0", tapping_exact_set(0) == false);
    expect_eq_u16("store unchanged after 0", tapping_exact_get(), 137);
    expect_true("accept exact SET 501", tapping_exact_set(501));
    expect_eq_u16("store accepts 501", tapping_exact_get(), 501);
    tapping_exact_set(137);

    zero_report(report);
    report[0] = id_custom_set_value;
    report[1] = id_qmk_tapping;
    report[2] = id_qmk_tapping_global_term_exact;
    report[3] = 0;
    report[4] = 137;
    expect_true("malformed exact length 4 rejected", tapping_term_handle_via_command(report, 4) == false);
    expect_eq_u16("store unchanged after malformed", tapping_exact_get(), 137);

    expect_true("exact SET 100", tapping_exact_set(100));
    expect_eq_u16("exact GET 100", tapping_exact_get(), 100);
    expect_eq_u8("legacy GET 100 -> 10", tapping_legacy_get(), 10);
    expect_true("exact SET 101", tapping_exact_set(101));
    expect_eq_u16("exact GET 101", tapping_exact_get(), 101);
    expect_eq_u8("legacy GET 101 -> 10", tapping_legacy_get(), 10);
    expect_true("exact SET 499", tapping_exact_set(499));
    expect_eq_u16("exact GET 499", tapping_exact_get(), 499);
    expect_eq_u8("legacy GET 499 -> 48 (480ms floor-20)", tapping_legacy_get(), 48);
    expect_true("exact SET 500", tapping_exact_set(500));
    expect_eq_u16("exact GET 500", tapping_exact_get(), 500);
    expect_eq_u8("legacy GET 500 -> 50", tapping_legacy_get(), 50);

    zero_report(report);
    report[0] = id_custom_set_value;
    report[1] = id_qmk_tapping;
    report[2] = id_qmk_tapping_global_term;
    report[3] = 14;
    expect_true("legacy SET 14 (140ms)", tapping_term_handle_via_command(report, 32));
    expect_eq_u16("exact GET after legacy 140", tapping_exact_get(), 140);
    expect_eq_u8("legacy GET 14", tapping_legacy_get(), 14);

    expect_true("exact SET 137 again", tapping_exact_set(137));
    zero_report(report);
    report[0] = id_custom_set_value;
    report[1] = id_qmk_tapping;
    report[2] = id_qmk_tapping_global_term;
    report[3] = 12;
    expect_true("legacy SET 12 after exact 137", tapping_term_handle_via_command(report, 32));
    expect_eq_u16("store now 120", tapping_exact_get(), 120);
    expect_eq_u8("legacy GET 12", tapping_legacy_get(), 12);

    for (uint8_t slot = 0; slot < 8; slot++) {
        char name[64];
        expect_true("td exact SET", tapdance_exact_set(slot, td_values[slot]));
        snprintf(name, sizeof(name), "td%d exact GET %u", slot, td_values[slot]);
        expect_eq_u16(name, tapdance_exact_get(slot), td_values[slot]);
    }
    expect_eq_u16("td0 still 101", tapdance_exact_get(0), 101);
    expect_eq_u16("td1 still 137", tapdance_exact_get(1), 137);

    zero_report(report);
    report[0] = id_custom_get_value;
    report[1] = id_qmk_tapdance;
    report[2] = 5; /* TD0 legacy term */
    tapdance_handle_via_command(report, 32);
    expect_eq_u8("td0 legacy GET 101 -> 10", report[3], 10);

    zero_report(report);
    report[0] = id_custom_get_value;
    report[1] = id_qmk_tapdance;
    report[2] = 10; /* TD1 legacy term */
    tapdance_handle_via_command(report, 32);
    expect_eq_u8("td1 legacy GET 137 -> 12", report[3], 12);

    expect_true("td reject 0", tapdance_exact_set(0, 0) == false);
    expect_eq_u16("td0 unchanged after 0", tapdance_exact_get(0), 101);

    {
        uint32_t pre = era_state_sync_config_revision();
        expect_true("no-op exact SET 120 handled", tapping_exact_set(120));
        expect_eq_u16("no-op exact keeps 120", tapping_exact_get(), 120);
        expect_true("no-op exact SET does not bump CONFIG", era_state_sync_config_revision() == pre);
        tapping_exact_set(137);
        expect_true("changing SET bumps CONFIG", era_state_sync_config_revision() != pre);
    }

    zero_report(report);
    report[0] = id_get_keyboard_value;
    report[1] = ERA_STATE_SYNC_KEYBOARD_VALUE;
    report[2] = ERA_STATE_SYNC_ENVELOPE_VERSION;
    report[4] = 0xAB;
    report[5] = 0xCD;
    before    = era_state_sync_config_revision();
    expect_true("GET 0x06 handled", era_state_sync_via_command(report, 32));
    expect_eq_u8("envelope cmd", report[0], id_get_keyboard_value);
    expect_eq_u8("envelope sel", report[1], ERA_STATE_SYNC_KEYBOARD_VALUE);
    expect_eq_u8("envelope ver", report[2], ERA_STATE_SYNC_ENVELOPE_VERSION);
    expect_eq_u8("envelope ok", report[3], ERA_STATE_SYNC_STATUS_OK);
    expect_eq_u8("tag hi", report[4], 0xAB);
    expect_eq_u8("tag lo", report[5], 0xCD);
    expect_eq_u8("domain mask", report[6], ERA_STATE_SYNC_DOMAIN_MASK_INITIAL);
    expect_true("keymap rev nonzero", be32(&report[8]) != 0);
    expect_true("macro rev nonzero", be32(&report[12]) != 0);
    expect_true("config rev nonzero", be32(&report[16]) != 0);
    expect_true("GET 0x06 does not bump", era_state_sync_config_revision() == before);

    test_state_sync_invalid();
    test_mousekey();
    test_full_term_range();
    test_direct_mode();
    test_on_press_conflict();
    test_advanced_timing();

    zero_report(report);
    report[0] = id_get_keyboard_value;
    report[1] = ERA_STATE_SYNC_KEYBOARD_VALUE;
    report[2] = 0x02;
    report[4] = 0x11;
    report[5] = 0x22;
    expect_true("unsupported version handled", era_state_sync_via_command(report, 32));
    expect_eq_u8("unsupported status", report[3], ERA_STATE_SYNC_STATUS_UNSUPPORTED_VERSION);
    expect_eq_u8("unsupported echoes tag", report[4], 0x11);

    if (g_failures != 0) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("all host tests passed\n");
    return 0;
}
