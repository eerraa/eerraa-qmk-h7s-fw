/* 실제 QMK tapping 소스에 연결하며 액션 출력과 시계만 기록한다. */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "action.h"
#include "action_tapping.h"

static action_t key_actions[256];
static keyrecord_t emitted[8192];
static unsigned emitted_count, clear_count, hint_count;
static bool permissive_hold, hold_on_other_press;
static uint16_t configured_term = 200U;

uint16_t get_record_keycode(keyrecord_t *record, bool update_layer_cache) {
    (void)update_layer_cache;
    return key_actions[record->event.key.col].code;
}

uint16_t get_event_keycode(keyevent_t event, bool update_layer_cache) {
    keyrecord_t record = {.event = event};
    return get_record_keycode(&record, update_layer_cache);
}

uint16_t get_tapping_term(uint16_t keycode, keyrecord_t *record) {
    (void)keycode; (void)record;
    return configured_term;
}

bool get_permissive_hold(uint16_t keycode, keyrecord_t *record) {
    (void)keycode; (void)record;
    return permissive_hold;
}

bool get_hold_on_other_key_press(uint16_t keycode, keyrecord_t *record) {
    (void)keycode; (void)record;
    return hold_on_other_press;
}

action_t layer_switch_get_action(keypos_t key) {
    return key_actions[key.col];
}

bool is_tap_record(keyrecord_t *record) {
    action_t action = layer_switch_get_action(record->event.key);
    return action.kind.id == ACT_LAYER_TAP || action.kind.id == ACT_LMODS_TAP || action.kind.id == ACT_RMODS_TAP;
}

void process_record_tap_hint(keyrecord_t *record) {
    assert(IS_EVENT(record->event));
    hint_count++;
}

void process_record(keyrecord_t *record) {
    if (IS_NOEVENT(record->event)) return;
    assert(emitted_count < sizeof(emitted) / sizeof(emitted[0]));
    emitted[emitted_count++] = *record;
}

void clear_keyboard(void) { clear_count++; }

static keyrecord_t record_at(unsigned col, bool pressed, uint32_t time) {
    keyrecord_t record = {.event = {.key = {.row = 0, .col = col}, .pressed = pressed, .time = time, .type = KEY_EVENT}};
#ifdef TAPPING_FIXTURE_HAS_SCAN_TOKEN
    record.report_scan_token = 0x56780000U + col;
#endif
#ifdef TAPDANCE_ENABLE
    record.tap_dance_epoch = 0x12340000U + col;
    record.tap_dance_epoch_valid = true;
#endif
    return record;
}

static void event_at(unsigned col, bool pressed, uint32_t time) {
    action_tapping_process(record_at(col, pressed, time));
}

static void tick_at(uint32_t time) {
    action_tapping_process((keyrecord_t){.event = {.type = TICK_EVENT, .time = time}});
}

static void initialize(void) {
    for (unsigned col = 0; col < 256U; ++col) key_actions[col].code = ACTION_KEY(KC_A + col % 26U);
    key_actions[0].code = ACTION_LAYER_TAP(1, KC_SPACE);
    key_actions[1].code = ACTION_MODS_TAP_KEY(MOD_LCTL, KC_ENTER);
}

static void assert_record(unsigned index, unsigned col, bool pressed, uint32_t time) {
    assert(index < emitted_count);
    assert(emitted[index].event.key.col == col);
    assert(emitted[index].event.pressed == pressed);
    assert(emitted[index].event.time == time);
#ifdef TAPDANCE_ENABLE
    assert(emitted[index].tap_dance_epoch == 0x12340000U + col);
    assert(emitted[index].tap_dance_epoch_valid);
#endif
}

static void lt8(uint32_t start) {
    const uint32_t input_time = start + (configured_term > 1U);
    event_at(0, true, start);
    for (unsigned i = 0; i < 8U; ++i) event_at(2U + i, true, input_time);
    assert(emitted_count == 0U && clear_count == 0U);
    tick_at(start + configured_term);
    assert(emitted_count == 9U && emitted[0].tap.count == 0U);
    for (unsigned i = 0; i < 8U; ++i) assert_record(1U + i, 2U + i, true, input_time);
    event_at(0, false, start + configured_term + 1U);
    assert_record(9U, 0U, false, start + configured_term + 1U);
    assert(clear_count == 0U);
    puts("PASS tapping lt8: accepted plain-key prefix and original timestamps survive LT timeout");
}

static void lt_four_taps(uint32_t start) {
    event_at(0, true, start);
    for (unsigned i = 0; i < 4U; ++i) {
        event_at(2U + i, true, start + 1U + 12U * i);
        event_at(2U + i, false, start + 7U + 12U * i);
    }
    assert(emitted_count == 0U && clear_count == 0U);
    tick_at(start + configured_term);
    assert(emitted_count == 9U);
    for (unsigned i = 0; i < 4U; ++i) {
        assert_record(1U + 2U * i, 2U + i, true, start + 1U + 12U * i);
        assert_record(2U + 2U * i, 2U + i, false, start + 7U + 12U * i);
    }
    assert(clear_count == 0U);
    puts("PASS tapping lt4taps: all eight ordered edges in 48ms survive LT decision");
}

static void tap_with_waiting(uint32_t start) {
    event_at(0, true, start);
    for (unsigned i = 0; i < 16U; ++i) event_at(2U + i, true, start + 1U);
    event_at(0, false, start + 2U);
    assert(emitted_count == 18U && emitted[0].tap.count == 1U && emitted[17].tap.count == 1U);
    assert_record(0U, 0U, true, start);
    for (unsigned i = 0; i < 16U; ++i) assert_record(1U + i, 2U + i, true, start + 1U);
    assert_record(17U, 0U, false, start + 2U);
    assert(clear_count == 0U);
    puts("PASS tapping early-release: buffer pressure preserves LT tap and does not force hold");
}

static void deferred_second_tap(uint32_t start) {
    event_at(0, true, start);
    event_at(1, true, start + 1U);
    event_at(1, false, start + 2U);
    for (unsigned i = 0; i < 8U; ++i) event_at(2U + i, true, start + 3U);
    tick_at(start + 65536U);
    assert(emitted_count == 11U);
    assert(emitted[0].tap.count == 0U);
    assert_record(1U, 1U, true, start + 1U);
    assert_record(2U, 1U, false, start + 2U);
    assert(emitted[1].tap.count == 1U && emitted[2].tap.count == 1U);
    for (unsigned i = 0; i < 8U; ++i) assert_record(3U + i, 2U + i, true, start + 3U);
    assert(clear_count == 0U);
    puts("PASS tapping replay: second MT stays a tap after delayed replay past the old timer wrap");
}

static void policy_case(uint32_t start, bool press_policy) {
    hold_on_other_press = press_policy;
    permissive_hold = !press_policy;
    event_at(0, true, start);
    event_at(2, true, start + 1U);
    if (press_policy) {
        assert(emitted_count == 2U && emitted[0].tap.count == 0U);
    } else {
        assert(emitted_count == 0U);
        event_at(2, false, start + 2U);
        assert(emitted_count == 3U && emitted[0].tap.count == 0U);
        assert_record(2U, 2U, false, start + 2U);
    }
    assert(clear_count == 0U);
    puts("PASS tapping policy: configured permissive/other-press hold behavior remains effective");
}

#ifdef TAPPING_FIXTURE_HAS_STATS
static void capacity_boundary(uint32_t start, unsigned overflow_mode) {
    const action_tapping_stats_t before = action_tapping_get_stats();
    assert(before.capacity == 256U && before.waiting_count == 0U);
    event_at(0, true, start);
    for (unsigned i = 0; i < 256U; ++i) {
        event_at(2U + i % 200U, true, start + 1U + i);
        const action_tapping_stats_t stats = action_tapping_get_stats();
        assert(stats.waiting_count == i + 1U);
        assert(stats.overflow_count == before.overflow_count);
    }
    const action_tapping_stats_t full = action_tapping_get_stats();
    assert(full.waiting_count == full.capacity && full.high_watermark == full.capacity);
    assert(emitted_count == 0U && clear_count == 0U);
    if (overflow_mode) {
        event_at(overflow_mode == 2U ? 0U : 250U, overflow_mode != 2U, start + 258U);
        const action_tapping_stats_t after = action_tapping_get_stats();
        assert(after.overflow_count == before.overflow_count + 1U);
        assert(after.waiting_count == 0U && after.high_watermark == 256U);
        assert(clear_count == 1U && emitted_count == (overflow_mode == 2U));
        if (overflow_mode == 2U) {
            assert_record(0U, 0U, true, start);
            assert(emitted[0].tap.count == 1U);
        }
        tick_at(start + configured_term);
        assert(emitted_count == (overflow_mode == 2U));
        puts(overflow_mode == 2U
             ? "PASS tapping explicit release limit: LT release at a full ring records overflow after tap resolution and clears the waiting prefix"
             : "PASS tapping explicit limit: edge 257 records overflow and retains the existing clear recovery; event history beyond capacity is not preserved");
    } else {
        tick_at(start + configured_term);
        assert(emitted_count == 257U && emitted[0].tap.count == 0U);
        for (unsigned i = 0; i < 256U; ++i) assert_record(i + 1U, 2U + i % 200U, true, start + 1U + i);
        assert(action_tapping_get_stats().waiting_count == 0U);
        puts("PASS tapping capacity: exactly 256 deferred edges drain once in order with original timestamps");
    }
}
#endif

static void ring_wrap(uint32_t start) {
    for (unsigned cycle = 0; cycle < 40U; ++cycle) {
        emitted_count = 0U;
        const uint32_t time = start + cycle * 1000U;
        event_at(0, true, time);
        for (unsigned i = 0; i < 16U; ++i) event_at(2U + i, true, time + 1U);
        event_at(0, false, time + 2U);
        assert(emitted_count == 18U);
        for (unsigned i = 0; i < 16U; ++i) assert_record(i + 1U, i + 2U, true, time + 1U);
#ifdef TAPPING_FIXTURE_HAS_SCAN_TOKEN
        for (unsigned i = 0; i < emitted_count; ++i) assert(emitted[i].report_scan_token == 0U);
#endif
        tick_at(time + configured_term + 3U);
    }
    assert(clear_count == 0U);
    puts("PASS tapping ring wrap: repeated replay crosses index 255 with no duplicate action or physical scan token");
}

static void direct_token(uint32_t start) {
    event_at(2U, true, start);
    event_at(2U, false, start + 1U);
    assert(emitted_count == 2U);
#ifdef TAPPING_FIXTURE_HAS_SCAN_TOKEN
    assert(emitted[0].report_scan_token == 0x56780002U);
    assert(emitted[1].report_scan_token == 0x56780002U);
#endif
    puts("PASS tapping physical identity: immediate plain records retain their scan token");
}

static void next_case(uint32_t time) {
    tick_at(time);
    emitted_count = clear_count = hint_count = 0U;
    permissive_hold = hold_on_other_press = false;
    configured_term = 200U;
}

static void all_cases(void) {
    lt8(1000U);
    next_case(140000U);
    lt_four_taps(150000U);
    next_case(290000U);
    tap_with_waiting(300000U);
    next_case(440000U);
    configured_term = UINT16_MAX;
    deferred_second_tap(450000U);
    next_case(590000U);
    policy_case(600000U, false);
    next_case(740000U);
    policy_case(750000U, true);
    next_case(890000U);
    configured_term = UINT16_MAX;
    lt8(UINT32_MAX - 50U);
    next_case(140000U);
    configured_term = UINT16_MAX;
    deferred_second_tap(UINT32_MAX - 50U);
    next_case(290000U);
    configured_term = 1U;
    lt8(300000U);
    next_case(440000U);
    configured_term = 65534U;
    tap_with_waiting(450000U);
    next_case(590000U);
    ring_wrap(600000U);
    next_case(790000U);
    direct_token(800000U);
#ifdef TAPPING_FIXTURE_HAS_STATS
    next_case(940000U);
    configured_term = UINT16_MAX;
    capacity_boundary(950000U, 0U);
    next_case(1140000U);
    configured_term = UINT16_MAX;
    capacity_boundary(1150000U, 1U);
    next_case(1340000U);
    configured_term = UINT16_MAX;
    capacity_boundary(1350000U, 2U);
#endif
}

int main(int argc, char **argv) {
    initialize();
    if (argc == 1) {
        all_cases();
        return 0;
    }
    uint32_t start = argc >= 3 ? (uint32_t)strtoul(argv[2], NULL, 0) : 1000U;
    configured_term = argc >= 4 ? (uint16_t)strtoul(argv[3], NULL, 0) : 200U;
    if (!strcmp(argv[1], "lt8")) lt8(start);
    else if (!strcmp(argv[1], "lt4taps")) lt_four_taps(start);
    else if (!strcmp(argv[1], "early-release")) tap_with_waiting(start);
    else if (!strcmp(argv[1], "delayed-mt")) deferred_second_tap(start);
    else if (!strcmp(argv[1], "permissive")) policy_case(start, false);
    else if (!strcmp(argv[1], "other-press")) policy_case(start, true);
    else if (!strcmp(argv[1], "ring-wrap")) ring_wrap(start);
    else if (!strcmp(argv[1], "direct-token")) direct_token(start);
#ifdef TAPPING_FIXTURE_HAS_STATS
    else if (!strcmp(argv[1], "capacity")) capacity_boundary(start, 0U);
    else if (!strcmp(argv[1], "overflow")) capacity_boundary(start, 1U);
    else if (!strcmp(argv[1], "overflow-release")) capacity_boundary(start, 2U);
#endif
    else assert(!"unknown tapping admission case");
    return 0;
}
