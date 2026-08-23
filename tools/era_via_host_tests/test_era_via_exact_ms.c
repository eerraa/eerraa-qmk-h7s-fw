#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "via.h"
#include "tapping_term.h"
#include "tapdance.h"
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

static void test_mousekey(void) {
    uint32_t pre;

    mousekey_config_init();

    /* 기본값이 엔진 변수까지 그대로 내려갔는가 */
    expect_eq_u8("mk default step 4px", mk_move_delta, 4);
    expect_eq_u8("mk default interval 10ms", mk_interval, 10);
    expect_eq_u8("mk default ratio 4", mk_max_speed, 4);
    expect_eq_u8("mk default ramp 100 events", mk_time_to_max, 100);
    expect_eq_u8("mk default top 16px", mousekey_get(id_qmk_mousekey_cursor_max_speed), 16);
    expect_eq_u8("mk default accel 1.0s = 20 units", mousekey_get(id_qmk_mousekey_cursor_acceleration), 20);

    /* 첫 스텝을 바꿔도 최고 속도는 제자리 */
    expect_true("mk start 8px SET", mousekey_set(id_qmk_mousekey_cursor_min_speed, 8));
    expect_eq_u8("mk start reads 8px", mousekey_get(id_qmk_mousekey_cursor_min_speed), 8);
    expect_eq_u8("mk top held at 16px across start change", mousekey_get(id_qmk_mousekey_cursor_max_speed), 16);
    expect_eq_u8("mk ratio recomputed to 2", mk_max_speed, 2);

    /* 최고 속도는 반올림이라 상한에 정확히 닿는다 (내림이면 120으로 떨어진다) */
    expect_true("mk top 127px SET", mousekey_set(id_qmk_mousekey_cursor_max_speed, 127));
    expect_eq_u8("mk ratio 16 by rounding", mk_max_speed, 16);
    expect_eq_u8("mk top reads back 127px", mousekey_get(id_qmk_mousekey_cursor_max_speed), 127);

    /* 갱신 주기를 바꿔도 램프 시간은 유지된다 */
    expect_true("mk start back to 4px", mousekey_set(id_qmk_mousekey_cursor_min_speed, 4));
    expect_true("mk accel 1.5s SET", mousekey_set(id_qmk_mousekey_cursor_acceleration, 30));
    expect_eq_u8("mk accel reads 1.5s", mousekey_get(id_qmk_mousekey_cursor_acceleration), 30);
    expect_true("mk interval 16ms SET", mousekey_set(id_qmk_mousekey_cursor_interval, 16));
    expect_eq_u8("mk interval reads 16ms", mousekey_get(id_qmk_mousekey_cursor_interval), 16);
    expect_eq_u8("mk accel still 1.5s after rate change", mousekey_get(id_qmk_mousekey_cursor_acceleration), 30);
    expect_eq_u8("mk ramp recomputed to 94 events at 16ms", mk_time_to_max, 94);
    expect_true("mk interval 20ms SET", mousekey_set(id_qmk_mousekey_cursor_interval, 20));
    expect_eq_u8("mk accel still 1.5s at 20ms", mousekey_get(id_qmk_mousekey_cursor_acceleration), 30);
    expect_eq_u8("mk ramp 75 events at 20ms", mk_time_to_max, 75);

    /* mk_time_to_max 가 1바이트라 빠른 주기에서는 긴 램프가 닿지 않는다.
       5ms에서 상한은 255 x 5 = 1275ms이므로 1.5s 요청은 잘리고,
       되읽기는 요청값이 아니라 엔진이 실제로 든 짧은 램프를 정직하게 보고한다.
       (JSON은 200/s와 2.0s를 함께 제시하므로 이 조합은 실제로 사용자가 만날 수 있다.) */
    expect_true("mk interval 5ms SET", mousekey_set(id_qmk_mousekey_cursor_interval, 5));
    expect_eq_u8("mk ramp clamps to 255 events at 5ms", mk_time_to_max, 255);
    expect_eq_u8("mk clamped ramp reads back honestly (1.275s -> 26)", mousekey_get(id_qmk_mousekey_cursor_acceleration), 26);

    /* 페이지가 제시하는 (가속, 주기) 조합 중 1바이트 이벤트 수에 담기는 것은 모두
       정확히 왕복해야 한다. 50ms 표시 단위를 고른 이유가 이것이다.
       담기지 않는 조합은 잘린 값을 정직하게 되돌려야 하고, 절대 요청값을 되돌리면 안 된다. */
    {
        static const uint8_t ramps[]     = {10, 15, 20, 25, 30, 40};
        static const uint8_t intervals[] = {5, 8, 10, 16, 20};
        size_t i, j;
        bool   exact_ok  = true;
        bool   honest_ok = true;
        for (i = 0; i < sizeof(intervals); i++) {
            mousekey_set(id_qmk_mousekey_cursor_interval, intervals[i]);
            for (j = 0; j < sizeof(ramps); j++) {
                uint32_t want_ms = (uint32_t)ramps[j] * 50u;
                uint32_t events  = (want_ms + intervals[i] / 2u) / intervals[i];
                uint8_t  got;
                mousekey_set(id_qmk_mousekey_cursor_acceleration, ramps[j]);
                got = mousekey_get(id_qmk_mousekey_cursor_acceleration);
                if (events <= 255u) {
                    if (got != ramps[j]) {
                        exact_ok = false;
                    }
                } else {
                    /* 잘렸다면 되읽기는 반드시 요청보다 짧아야 한다 */
                    if (got >= ramps[j]) {
                        honest_ok = false;
                    }
                }
            }
        }
        expect_true("mk representable accel x rate round-trips exactly", exact_ok);
        expect_true("mk unrepresentable accel reads back shorter, never the request", honest_ok);
    }

    /* 가속 off는 최고 속도가 아니라 첫 스텝 속도로 고정한다 */
    mousekey_set(id_qmk_mousekey_cursor_interval, 10);
    mousekey_set(id_qmk_mousekey_cursor_min_speed, 4);
    mousekey_set(id_qmk_mousekey_cursor_max_speed, 32);
    expect_true("mk accel off SET", mousekey_set(id_qmk_mousekey_cursor_acceleration, 0));
    expect_eq_u8("mk accel off reads 0", mousekey_get(id_qmk_mousekey_cursor_acceleration), 0);
    expect_eq_u8("mk accel off drives ratio 1", mk_max_speed, 1);
    expect_eq_u8("mk accel off keeps step at 4px", mk_move_delta, 4);
    expect_eq_u8("mk accel off keeps stored top 32px", mousekey_get(id_qmk_mousekey_cursor_max_speed), 32);
    expect_true("mk accel restored SET", mousekey_set(id_qmk_mousekey_cursor_acceleration, 20));
    expect_eq_u8("mk ratio 8 recovered", mk_max_speed, 8);

    /* 클램프된 값을 에코한다 */
    expect_eq_u8("mk interval 0 clamps to 1 in echo", mousekey_set_echo(id_qmk_mousekey_cursor_interval, 0), 1);
    expect_eq_u8("mk wheel interval 0 clamps to 1", mousekey_set_echo(id_qmk_mousekey_wheel_interval, 0), 1);

    /* 휠 가속은 한 드롭다운이 두 값을 옮긴다 */
    expect_true("mk wheel accel off SET", mousekey_set(id_qmk_mousekey_wheel_acceleration, 0));
    expect_eq_u8("mk wheel accel off reads 0", mousekey_get(id_qmk_mousekey_wheel_acceleration), 0);
    expect_eq_u8("mk wheel off max speed 1", mk_wheel_max_speed, 1);
    expect_eq_u8("mk wheel off ramp 0", mk_wheel_time_to_max, 0);
    expect_true("mk wheel accel strong SET", mousekey_set(id_qmk_mousekey_wheel_acceleration, 2));
    expect_eq_u8("mk wheel strong reads 2", mousekey_get(id_qmk_mousekey_wheel_acceleration), 2);
    expect_eq_u8("mk wheel strong max speed", mk_wheel_max_speed, MOUSEKEY_WHEEL_MAX_SPEED);
    expect_true("mk out-of-range wheel accel falls to strong", mousekey_set(id_qmk_mousekey_wheel_acceleration, 9));
    expect_eq_u8("mk out-of-range wheel accel reads 2", mousekey_get(id_qmk_mousekey_wheel_acceleration), 2);

    /* 값이 실제로 바뀐 SET에서만 CONFIG revision이 오른다 */
    mousekey_set(id_qmk_mousekey_cursor_min_speed, 4);
    pre = era_state_sync_config_revision();
    expect_true("mk no-op SET handled", mousekey_set(id_qmk_mousekey_cursor_min_speed, 4));
    expect_true("mk no-op SET does not bump CONFIG", era_state_sync_config_revision() == pre);
    expect_true("mk changing SET handled", mousekey_set(id_qmk_mousekey_cursor_min_speed, 2));
    expect_true("mk changing SET bumps CONFIG", era_state_sync_config_revision() != pre);

    /* 모르는 value id는 거절한다 */
    expect_true("mk unknown value id rejected", mousekey_set(99, 1) == false);
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

int main(void) {
    uint8_t  report[32];
    uint32_t before;
    const uint16_t td_values[8] = {101, 137, 141, 163, 187, 203, 499, 500};

    tapping_term_init();
    tapdance_init();

    expect_true("exact SET 137", tapping_exact_set(137));
    expect_eq_u16("exact GET 137", tapping_exact_get(), 137);
    expect_eq_u8("legacy GET 137 -> 12 (120ms floor-20)", tapping_legacy_get(), 12);

    expect_true("reject exact SET 99", tapping_exact_set(99) == false);
    expect_eq_u16("store unchanged after 99", tapping_exact_get(), 137);
    expect_true("reject exact SET 501", tapping_exact_set(501) == false);
    expect_eq_u16("store unchanged after 501", tapping_exact_get(), 137);

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

    expect_true("td reject 99", tapdance_exact_set(0, 99) == false);
    expect_eq_u16("td0 unchanged after 99", tapdance_exact_get(0), 101);

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
