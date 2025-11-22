/* Copyright 2016-2017 Yang Liu
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "progmem.h"
#include "sync_timer.h"
#include "rgblight.h"
#include "port.h"  // V251016R8: 포트 계층 인디케이터 구성을 참조
#include "color.h"
#include "debug.h"
#include "util.h"
#include "led_tables.h"
#include <lib/lib8tion/lib8tion.h>
#ifdef EEPROM_ENABLE
#    include "eeprom.h"
#endif

#ifdef RGBLIGHT_SPLIT
/* for split keyboard */
#    define RGBLIGHT_SPLIT_SET_CHANGE_MODE rgblight_status.change_flags |= RGBLIGHT_STATUS_CHANGE_MODE
#    define RGBLIGHT_SPLIT_SET_CHANGE_HSVS rgblight_status.change_flags |= RGBLIGHT_STATUS_CHANGE_HSVS
#    define RGBLIGHT_SPLIT_SET_CHANGE_MODEHSVS rgblight_status.change_flags |= (RGBLIGHT_STATUS_CHANGE_MODE | RGBLIGHT_STATUS_CHANGE_HSVS)
#    define RGBLIGHT_SPLIT_SET_CHANGE_LAYERS rgblight_status.change_flags |= RGBLIGHT_STATUS_CHANGE_LAYERS
#    define RGBLIGHT_SPLIT_SET_CHANGE_TIMER_ENABLE rgblight_status.change_flags |= RGBLIGHT_STATUS_CHANGE_TIMER
#    define RGBLIGHT_SPLIT_ANIMATION_TICK rgblight_status.change_flags |= RGBLIGHT_STATUS_ANIMATION_TICK
#else
#    define RGBLIGHT_SPLIT_SET_CHANGE_MODE
#    define RGBLIGHT_SPLIT_SET_CHANGE_HSVS
#    define RGBLIGHT_SPLIT_SET_CHANGE_MODEHSVS
#    define RGBLIGHT_SPLIT_SET_CHANGE_LAYERS
#    define RGBLIGHT_SPLIT_SET_CHANGE_TIMER_ENABLE
#    define RGBLIGHT_SPLIT_ANIMATION_TICK
#endif

#define _RGBM_SINGLE_STATIC(sym) RGBLIGHT_MODE_##sym,
#define _RGBM_SINGLE_DYNAMIC(sym)
#define _RGBM_MULTI_STATIC(sym) RGBLIGHT_MODE_##sym,
#define _RGBM_MULTI_DYNAMIC(sym)
#define _RGBM_TMP_STATIC(sym, msym) RGBLIGHT_MODE_##sym,
#define _RGBM_TMP_DYNAMIC(sym, msym)
static uint8_t static_effect_table[] = {
#include "rgblight_modes.h"
};

#define _RGBM_SINGLE_STATIC(sym) RGBLIGHT_MODE_##sym,
#define _RGBM_SINGLE_DYNAMIC(sym) RGBLIGHT_MODE_##sym,
#define _RGBM_MULTI_STATIC(sym) RGBLIGHT_MODE_##sym,
#define _RGBM_MULTI_DYNAMIC(sym) RGBLIGHT_MODE_##sym,
#define _RGBM_TMP_STATIC(sym, msym) RGBLIGHT_MODE_##msym,
#define _RGBM_TMP_DYNAMIC(sym, msym) RGBLIGHT_MODE_##msym,
static uint8_t mode_base_table[] = {
    0, // RGBLIGHT_MODE_zero
#include "rgblight_modes.h"
};

#if !defined(RGBLIGHT_DEFAULT_MODE)
#    define RGBLIGHT_DEFAULT_MODE RGBLIGHT_MODE_STATIC_LIGHT
#endif

#if !defined(RGBLIGHT_DEFAULT_HUE)
#    define RGBLIGHT_DEFAULT_HUE 0
#endif

#if !defined(RGBLIGHT_DEFAULT_SAT)
#    define RGBLIGHT_DEFAULT_SAT UINT8_MAX
#endif

#if !defined(RGBLIGHT_DEFAULT_VAL)
#    define RGBLIGHT_DEFAULT_VAL RGBLIGHT_LIMIT_VAL
#endif

#if !defined(RGBLIGHT_DEFAULT_SPD)
#    define RGBLIGHT_DEFAULT_SPD 0
#endif

#if !defined(RGBLIGHT_DEFAULT_ON)
#    define RGBLIGHT_DEFAULT_ON true
#endif

static inline int is_static_effect(uint8_t mode) {
    return memchr(static_effect_table, mode, sizeof(static_effect_table)) != NULL;
}

#ifdef RGBLIGHT_LED_MAP
const uint8_t led_map[] PROGMEM = RGBLIGHT_LED_MAP;
#endif

#ifdef RGBLIGHT_EFFECT_STATIC_GRADIENT
__attribute__((weak)) const uint8_t RGBLED_GRADIENT_RANGES[] PROGMEM = {255, 170, 127, 85, 64};
#endif

rgblight_config_t rgblight_config;
rgblight_status_t rgblight_status         = {.timer_enabled = false};
bool              is_rgblight_initialized = false;

#ifdef RGBLIGHT_SLEEP
static bool is_suspended;
static bool pre_suspend_enabled;
#endif

#ifdef RGBLIGHT_USE_TIMER
animation_status_t animation_status = {};
#endif

#ifndef LED_ARRAY
rgb_led_t led[RGBLIGHT_LED_COUNT];
#    define LED_ARRAY led
#endif

#ifdef RGBLIGHT_LAYERS
rgblight_segment_t const *const *rgblight_layers = NULL;

static bool deferred_set_layer_state = false;
#endif

rgblight_ranges_t rgblight_ranges = {0, RGBLIGHT_LED_COUNT, 0, RGBLIGHT_LED_COUNT, RGBLIGHT_LED_COUNT};

#ifdef INDICATOR_ENABLE
static const bool rgblight_indicator_supported = true;   // V251120R1: INDICATOR_ENABLE 보드에서만 인디케이터 스케줄링을 활성화
#else
static const bool rgblight_indicator_supported = false;  // V251120R1: 인디케이터 미지원 보드는 스케줄링 경로를 건너뛰도록 분리
#endif

#define RGBLIGHT_INDICATOR_RANGE_TABLE_LENGTH (RGBLIGHT_INDICATOR_TARGET_NUM + 1)  // V251016R8: Caps/Scroll/Num + OFF
static rgblight_indicator_range_t rgblight_indicator_range_table[RGBLIGHT_INDICATOR_RANGE_TABLE_LENGTH] = {0};

#ifdef RGBLIGHT_USE_TIMER
#endif

// V251012R2: Brick60 인디케이터 상태를 rgblight 내부에서 추적하기 위한 구조체
typedef struct {
    rgblight_indicator_config_t config;
    led_t                       host_state;
    rgb_led_t                   color;        // V251012R4: HSV 변환 결과를 캐시해 재계산을 피한다
    rgblight_indicator_range_t  range;         // V251016R8: 범위 타깃 캐시 제거 후 테이블 결과만 추적
    bool                        overrides_all; // V251016R8: 전체 LED가 인디케이터에 의해 덮어쓰이는지 여부
    bool                        active;
    bool                        needs_render;
} rgblight_indicator_state_t;

static rgblight_indicator_state_t rgblight_indicator_state = {
    .config = {.raw = 0},
    .host_state = {.raw = 0},
    .color = {0},
    .range = {0, 0},
    .overrides_all = false,
    .active = false,
    .needs_render = false,
};

static void rgblight_sethsv_noeeprom_old(uint8_t hue, uint8_t sat, uint8_t val);  // V251018R5: Pulse 제어 블록에서 재사용

#if defined(RGBLIGHT_EFFECT_PULSE_ON_PRESS) || defined(RGBLIGHT_EFFECT_PULSE_OFF_PRESS) || defined(RGBLIGHT_EFFECT_PULSE_ON_PRESS_HOLD) || defined(RGBLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD)
typedef struct {
    bool     latched;
    bool     initialized;
    bool     output_on;
    bool     key_tracking_valid;
    uint8_t  key_row;
    uint8_t  key_col;
    uint32_t deadline_ms;
} rgblight_pulse_effect_state_t;  // V251018R5: Pulse 계열 이펙트 상태 추적

static rgblight_pulse_effect_state_t rgblight_pulse_effect_state = {
    .latched = false,
    .initialized = false,
    .output_on = false,
    .key_tracking_valid = false,
    .key_row = 0,
    .key_col = 0,
    .deadline_ms = 0,
};

static bool rgblight_effect_pulse_mode_active(void)
{
    bool active = false;
#    ifdef RGBLIGHT_EFFECT_PULSE_ON_PRESS
    active |= (rgblight_status.base_mode == RGBLIGHT_MODE_PULSE_ON_PRESS);
#    endif
#    ifdef RGBLIGHT_EFFECT_PULSE_OFF_PRESS
    active |= (rgblight_status.base_mode == RGBLIGHT_MODE_PULSE_OFF_PRESS);
#    endif
#    ifdef RGBLIGHT_EFFECT_PULSE_ON_PRESS_HOLD
    active |= (rgblight_status.base_mode == RGBLIGHT_MODE_PULSE_ON_PRESS_HOLD);
#    endif
#    ifdef RGBLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD
    active |= (rgblight_status.base_mode == RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD);
#    endif
    return active;
}

static bool rgblight_effect_pulse_hold_mode_active(void)
{
    bool hold = false;
#    ifdef RGBLIGHT_EFFECT_PULSE_ON_PRESS_HOLD
    hold |= (rgblight_status.base_mode == RGBLIGHT_MODE_PULSE_ON_PRESS_HOLD);
#    endif
#    ifdef RGBLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD
    hold |= (rgblight_status.base_mode == RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD);
#    endif
    return hold;
}

static bool rgblight_effect_pulse_default_on(void)
{
#    ifdef RGBLIGHT_EFFECT_PULSE_OFF_PRESS
    if (rgblight_status.base_mode == RGBLIGHT_MODE_PULSE_OFF_PRESS) {
        return true;
    }
#    endif
#    ifdef RGBLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD
    if (rgblight_status.base_mode == RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD) {
        return true;
    }
#    endif
    return false;
}

static uint16_t rgblight_effect_pulse_duration_ms(void)
{
    return (uint16_t)RGBLIGHT_EFFECT_PULSE_DURATION_MIN_MS +
           (uint16_t)rgblight_config.speed * (uint16_t)RGBLIGHT_EFFECT_PULSE_DURATION_STEP_MS;
}

static void rgblight_effect_pulse_reset_state(void)
{
    rgblight_pulse_effect_state.latched            = false;
    rgblight_pulse_effect_state.initialized        = false;
    rgblight_pulse_effect_state.output_on          = false;
    rgblight_pulse_effect_state.key_tracking_valid = false;
    rgblight_pulse_effect_state.key_row            = 0;
    rgblight_pulse_effect_state.key_col            = 0;
    rgblight_pulse_effect_state.deadline_ms        = 0;
}

static void rgblight_effect_pulse_apply_output(bool on)
{
    if (!rgblight_config.enable) {
        rgblight_pulse_effect_state.output_on   = on;
        rgblight_pulse_effect_state.initialized = true;
        return;
    }

    if (on) {
        rgblight_sethsv_noeeprom_old(rgblight_config.hue, rgblight_config.sat, rgblight_config.val);
    } else {
        rgblight_setrgb(0, 0, 0);
    }

    rgblight_pulse_effect_state.output_on   = on;
    rgblight_pulse_effect_state.initialized = true;
}

static void rgblight_effect_pulse_evaluate_output(void)
{
    if (!rgblight_effect_pulse_mode_active()) {
        return;
    }

    if (rgblight_pulse_effect_state.latched) {
        uint32_t now     = sync_timer_read32();
        bool     expired = (int32_t)(now - rgblight_pulse_effect_state.deadline_ms) >= 0;
        if (expired && !(rgblight_effect_pulse_hold_mode_active() && rgblight_pulse_effect_state.key_tracking_valid)) {
            rgblight_pulse_effect_state.latched     = false;
            rgblight_pulse_effect_state.deadline_ms = 0;
        }
    }

    bool default_on = rgblight_effect_pulse_default_on();
    bool target_on  = rgblight_pulse_effect_state.latched ? !default_on : default_on;

    if (!rgblight_pulse_effect_state.initialized || rgblight_pulse_effect_state.output_on != target_on) {
        rgblight_effect_pulse_apply_output(target_on);
    }
}

static void rgblight_effect_pulse_on_base_mode_update(void)
{
    if (!rgblight_effect_pulse_mode_active()) {
        rgblight_effect_pulse_reset_state();
        return;
    }

    rgblight_pulse_effect_state.latched            = false;
    rgblight_pulse_effect_state.deadline_ms        = 0;
    rgblight_pulse_effect_state.initialized        = false;
    rgblight_pulse_effect_state.key_tracking_valid = false;
    rgblight_effect_pulse_evaluate_output();
}

static void rgblight_effect_pulse_handle_keyevent(bool pressed, uint8_t row, uint8_t col)
{
    if (!rgblight_effect_pulse_mode_active() || !rgblight_config.enable) {
        if (!pressed) {
            rgblight_pulse_effect_state.key_tracking_valid = false;
        }
        return;
    }

    if (pressed) {
        uint32_t now                              = sync_timer_read32();
        rgblight_pulse_effect_state.latched       = true;
        rgblight_pulse_effect_state.deadline_ms   = now + rgblight_effect_pulse_duration_ms();
        rgblight_pulse_effect_state.key_row       = row;
        rgblight_pulse_effect_state.key_col       = col;
        rgblight_pulse_effect_state.key_tracking_valid = true;
        rgblight_effect_pulse_evaluate_output();
        return;
    }

    bool matches_tracked_key = rgblight_pulse_effect_state.key_tracking_valid &&
                               rgblight_pulse_effect_state.key_row == row &&
                               rgblight_pulse_effect_state.key_col == col;

    if (matches_tracked_key) {
        rgblight_pulse_effect_state.key_tracking_valid = false;

        if (rgblight_effect_pulse_hold_mode_active()) {
            uint32_t now = sync_timer_read32();
            if ((int32_t)(now - rgblight_pulse_effect_state.deadline_ms) >= 0) {
                rgblight_pulse_effect_state.latched     = false;
                rgblight_pulse_effect_state.deadline_ms = 0;
                rgblight_effect_pulse_evaluate_output();  // V251018R5: Hold 해제 시 즉시 상태 복구
            }
        }
    }
}

static void rgblight_effect_pulse_on_press(animation_status_t *anim)
{
    (void)anim;
    rgblight_effect_pulse_evaluate_output();  // V251018R5: Pulse On Press 상태 머신 처리
}

static void rgblight_effect_pulse_off_press(animation_status_t *anim)
{
    (void)anim;
    rgblight_effect_pulse_evaluate_output();  // V251018R5: Pulse Off Press 상태 머신 처리
}

static void rgblight_effect_pulse_on_press_hold(animation_status_t *anim)
{
    (void)anim;
    rgblight_effect_pulse_evaluate_output();  // V251018R5: Pulse On Press (Hold) 상태 머신 처리
}

static void rgblight_effect_pulse_off_press_hold(animation_status_t *anim)
{
    (void)anim;
    rgblight_effect_pulse_evaluate_output();  // V251018R5: Pulse Off Press (Hold) 상태 머신 처리
}
#else
static inline void rgblight_effect_pulse_on_base_mode_update(void) {}
static inline void rgblight_effect_pulse_handle_keyevent(bool pressed, uint8_t row, uint8_t col)
{
    (void)pressed;
    (void)row;
    (void)col;
}
static inline void rgblight_effect_pulse_evaluate_output(void) {}
#endif

static volatile bool    rgblight_host_led_pending    = false;  // V251018R1: USB IRQ에서 전달된 호스트 LED 버퍼 상태
static volatile uint8_t rgblight_host_led_raw_buffer = 0;
static bool             rgblight_render_pending      = false;  // V251018R1: rgblight_set 실행을 주 루프에서 단일 처리

static void rgblight_request_render(void)
{
    rgblight_render_pending = true;
}
// V251016R8: 포트가 제공한 범위를 LED 개수에 맞춰 보정한다.
static rgblight_indicator_range_t rgblight_indicator_sanitize_range(rgblight_indicator_range_t range)
{
  if (range.count == 0) {
    return (rgblight_indicator_range_t){0, 0};
  }

  uint16_t start = range.start;
  if (start >= RGBLIGHT_LED_COUNT) {
    return (rgblight_indicator_range_t){0, 0};
  }

  uint16_t end = start + range.count;
  if (end > RGBLIGHT_LED_COUNT) {
    end = RGBLIGHT_LED_COUNT;
  }

  uint16_t count = end - start;
  if (count == 0) {
    return (rgblight_indicator_range_t){0, 0};
  }

  rgblight_indicator_range_t sanitized = {
    .start = (uint8_t)start,
    .count = (uint8_t)count,
  };

  return sanitized;
}

static bool rgblight_indicator_target_active_default(uint8_t target, led_t host_state)
{
    switch (target) {
        case RGBLIGHT_INDICATOR_TARGET_CAPS:
            return host_state.caps_lock;
        case RGBLIGHT_INDICATOR_TARGET_SCROLL:
            return host_state.scroll_lock;
        case RGBLIGHT_INDICATOR_TARGET_NUM:
            return host_state.num_lock;
        default:
            return false;
    }
}

static rgblight_indicator_target_callback_t rgblight_indicator_target_callback =
    rgblight_indicator_target_active_default;  // V251016R8: 기본 Caps/Scroll/Num 매핑으로 초기화

static void rgblight_indicator_apply_target_range(uint8_t target)
{
    if (target > RGBLIGHT_INDICATOR_TARGET_NUM) {
        target = RGBLIGHT_INDICATOR_TARGET_OFF;  // V251016R8: 범위를 지정할 수 없는 타깃은 비활성 처리
    }

    rgblight_indicator_range_t previous_range = rgblight_indicator_state.range;
    bool                        previous_override = rgblight_indicator_state.overrides_all;
    rgblight_indicator_range_t  range = {0, 0};
    if (target < RGBLIGHT_INDICATOR_RANGE_TABLE_LENGTH) {
        range = rgblight_indicator_range_table[target];
    }

    bool overrides_all = false;
    if (range.count > 0 && range.start == 0) {
        uint16_t span = range.count;
        if (span >= RGBLIGHT_LED_COUNT) {
            overrides_all = true;
        }
    }

    rgblight_indicator_state.range        = range;
    rgblight_indicator_state.overrides_all = overrides_all;

    bool range_changed = (previous_range.start != range.start) || (previous_range.count != range.count);
    if (range_changed || previous_override != overrides_all) {
        if (rgblight_indicator_state.active) {
            rgblight_indicator_state.needs_render = true;  // V251016R8: 범위 변화 시 오버레이 재적용 요청
        }
    }
}

// V251012R2: 인디케이터 대상과 호스트 LED 상태를 비교하여 활성화 여부를 반환
static bool rgblight_indicator_target_active(uint8_t target, led_t host_state)
{
    return rgblight_indicator_target_callback(target, host_state);  // V251016R8: 포트 지정 콜백을 통해 판별
}

// V251015R4: 밝기 0 구성은 인디케이터 비활성화로 처리해 애니메이션 계산을 생략
static bool rgblight_indicator_should_enable(rgblight_indicator_config_t config, led_t host_state)
{
    if (config.val == 0) {
        return false;
    }

    return rgblight_indicator_target_active(config.target, host_state);
}

// V251012R4: 구성 변경 시 한 번만 HSV→RGB 변환을 수행해 캐시한다
static rgb_led_t rgblight_indicator_compute_color(rgblight_indicator_config_t config)
{
    rgb_led_t color = {0};

    if (config.val == 0) {
        return color;
    }

    uint8_t clamped_val = config.val > RGBLIGHT_LIMIT_VAL ? RGBLIGHT_LIMIT_VAL : config.val;

    HSV hsv = {
        .h = config.hue,
        .s = config.sat,
        .v = clamped_val,
    };

    RGB rgb = rgblight_hsv_to_rgb(hsv);

    color.r = rgb.r;
    color.g = rgb.g;
    color.b = rgb.b;

#ifdef RGBW
    color.w = 0;
#endif

    return color;
}

// V251016R9: 프레임 준비 로직이 호출부로 이관되어 오버레이 함수는 적용만 수행
static void rgblight_indicator_apply_overlay(void)
{
    // V251121R1: 전체 오버레이는 재렌더 플래그와 무관하게 항상 적용해 우선순위를 유지
    if (!rgblight_indicator_state.active) {
        rgblight_indicator_state.needs_render = false;
        return;
    }

    rgblight_indicator_range_t range = rgblight_indicator_state.range;
    if (range.count == 0) {
        rgblight_indicator_state.needs_render = false;
        return;
    }

    uint16_t start = range.start;
    uint16_t count = range.count;

    rgb_led_t  cached     = rgblight_indicator_state.color;
    rgb_led_t *target_led = &led[start];
    rgb_led_t *target_end = target_led + count;

    while (target_led < target_end) {
        *target_led++ = cached;
    }

    rgblight_indicator_state.needs_render = false;
}

static void rgblight_indicator_restore_pulse_effect(void)
{
#if defined(RGBLIGHT_EFFECT_PULSE_ON_PRESS) || defined(RGBLIGHT_EFFECT_PULSE_OFF_PRESS) || defined(RGBLIGHT_EFFECT_PULSE_ON_PRESS_HOLD) || defined(RGBLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD)
    if (rgblight_effect_pulse_mode_active()) {
        rgblight_pulse_effect_state.initialized = false;  // V251121R2: 인디케이터 종료 시 Pulse 계열 기본 출력 재적용
        rgblight_effect_pulse_evaluate_output();
    }
#endif
}

// V251012R3: 인디케이터 상태 전이를 공통화해 중복 로직과 불필요한 rgblight_set 호출을 축소
static void rgblight_indicator_commit_state(bool should_enable, bool request_render)
{
    if (!rgblight_indicator_supported) {
        return;  // V251120R1: 인디케이터 미지원 보드는 상태 머신을 비활성
    }

    bool was_active  = rgblight_indicator_state.active;
    bool was_pending = rgblight_indicator_state.needs_render;
    bool needs_render = should_enable && (request_render || !was_active || was_pending);

    rgblight_indicator_state.active       = should_enable;
    rgblight_indicator_state.needs_render = needs_render;

    if (should_enable) {
        if (!was_active || needs_render) {
            rgblight_request_render();
        }
        return;
    }

    if (was_active) {
        if (rgblight_config.enable && is_static_effect(rgblight_config.mode)) {
            rgblight_mode_noeeprom(rgblight_config.mode);  // V251018R3: 정적 효과는 즉시 재렌더해 원본 상태 복구
        } else {
#ifdef RGBLIGHT_USE_TIMER
            if (rgblight_status.timer_enabled) {
                animation_status.next_timer_due = sync_timer_read();  // V251122R7: 인디케이터 종료 시 베이스 이펙트를 즉시 재계산하도록 만료 시각을 당김
                rgblight_timer_task();  // V251122R7: 다음 틱을 기다리지 않고 오버레이가 남지 않게 즉시 프레임 재계산
            }
#endif
        }

        rgblight_indicator_restore_pulse_effect();  // V251121R2: CAPS OFF에서 Pulse 기본 상태를 즉시 재적용
        rgblight_request_render();  // V251120R1: CAPS OFF 시 기본 프레임을 즉시 큐잉해 잔상을 방지
    }
}

// V251016R8: 포트 계층이 인디케이터 타깃 판별 함수를 주입할 수 있도록 인터페이스 제공
void rgblight_indicator_set_target_callback(rgblight_indicator_target_callback_t callback)
{
    if (!rgblight_indicator_supported) {
        return;  // V251120R1: 인디케이터 미지원 보드에서는 콜백 설정을 무시
    }

    if (callback == NULL) {
        rgblight_indicator_target_callback = rgblight_indicator_target_active_default;
        return;
    }

    rgblight_indicator_target_callback = callback;
}

void rgblight_indicator_set_ranges(const rgblight_indicator_range_t *ranges, uint8_t length)
{
    if (!rgblight_indicator_supported) {
        return;  // V251120R1: 인디케이터 미지원 보드는 범위 설정을 비활성
    }

    uint8_t limit = RGBLIGHT_INDICATOR_RANGE_TABLE_LENGTH;

    for (uint8_t index = 0; index < limit; ++index) {
        if (ranges != NULL && index < length) {
            rgblight_indicator_range_table[index] = rgblight_indicator_sanitize_range(ranges[index]);
        } else {
            rgblight_indicator_range_table[index] = (rgblight_indicator_range_t){0, 0};
        }
    }

    rgblight_indicator_apply_target_range(rgblight_indicator_state.config.target);  // V251016R8: range_target 필드 제거로 즉시 재평가
}

// V251012R2: VIA 및 포트 계층에서 전달된 구성 변경을 반영
// V251012R6: 내부 상태만 사용하도록 구성 조회 API 정리
void rgblight_indicator_update_config(rgblight_indicator_config_t config)
{
    if (!rgblight_indicator_supported) {
        return;  // V251120R1: 인디케이터 미지원 보드는 구성 변경을 무시
    }

    if (rgblight_indicator_state.config.raw == config.raw) {
        return;  // V251012R5: 동일 구성 반복 시 재계산과 재렌더를 생략
    }

    rgblight_indicator_state.config = config;
    rgblight_indicator_apply_target_range(config.target);  // V251016R8: 선택된 인디케이터 범위를 즉시 반영
    if (config.target == RGBLIGHT_INDICATOR_TARGET_OFF) {
        rgblight_indicator_state.color = (rgb_led_t){0};  // V251016R7: 인디케이터 비활성 구성은 HSV 변환 없이 캐시를 초기화
    } else {
        rgblight_indicator_state.color = rgblight_indicator_compute_color(config);  // V251012R4: 렌더링 시 재사용할 색상 캐시
    }

    bool should_enable = rgblight_indicator_should_enable(config, rgblight_indicator_state.host_state);

    rgblight_indicator_commit_state(should_enable, true);  // V251012R3: 구성 변경 시 즉시 재렌더링
}

// V251012R2: 호스트 LED 상태 변화를 rgblight 인디케이터 파이프라인으로 전달
void rgblight_indicator_apply_host_led(led_t host_led_state)
{
    if (!rgblight_indicator_supported) {
        return;  // V251120R1: 인디케이터 미지원 보드에서는 호스트 LED 이벤트를 무시
    }

    if (rgblight_indicator_state.host_state.raw == host_led_state.raw) {
        return;  // V251012R5: 동일 상태 재전달 시 전이 검사를 건너뛰어 인터럽트 부하를 완화
    }

    rgblight_indicator_state.host_state = host_led_state;

    bool should_enable = rgblight_indicator_should_enable(rgblight_indicator_state.config, host_led_state);

    rgblight_indicator_commit_state(should_enable, false);  // V251012R3: 호스트 이벤트는 전이 감지만 수행
}

void rgblight_indicator_post_host_event(led_t host_led_state)
{
    if (!rgblight_indicator_supported) {
        return;  // V251120R1: 인디케이터 미지원 보드는 호스트 이벤트 큐를 사용하지 않음
    }

    rgblight_host_led_raw_buffer = host_led_state.raw;
    rgblight_host_led_pending    = true;
}  // V251018R1: IRQ 컨텍스트에서는 상태만 저장하고 실제 처리는 rgblight_task로 위임

void rgblight_set_clipping_range(uint8_t start_pos, uint8_t num_leds) {
    if (start_pos > RGBLIGHT_LED_COUNT) {
        return;  // V251013R8: 잘못된 시작 인덱스는 무시해 기존 범위를 유지
    }

    uint16_t end = (uint16_t)start_pos + num_leds;
    if (end > RGBLIGHT_LED_COUNT) {
        return;  // V251013R8: LED 개수를 넘는 범위는 적용하지 않는다
    }

    if (rgblight_ranges.clipping_start_pos == start_pos &&
        rgblight_ranges.clipping_num_leds == num_leds) {
        return;  // V251013R7: 동일 범위 반복 시 재렌더 예약을 생략해 타이머 깨우기 방지
    }

    rgblight_ranges.clipping_start_pos = start_pos;
    rgblight_ranges.clipping_num_leds  = num_leds;

    if (rgblight_indicator_state.active) {
        rgblight_indicator_state.needs_render = true;  // V251013R6: 범위 변경 시 인디케이터 버퍼 재적용 예약
                                                        // V251013R7: 값이 바뀐 경우에만 플래그를 세팅
    }
}

void rgblight_set_effect_range(uint8_t start_pos, uint8_t num_leds) {
    if (start_pos > RGBLIGHT_LED_COUNT) {
        return;  // V251013R9: 전체 LED 개수와 같은 시작 인덱스를 허용해 빈 범위 설정을 지원
    }

    uint16_t end = (uint16_t)start_pos + num_leds;
    if (end > RGBLIGHT_LED_COUNT) return;

    if (rgblight_ranges.effect_start_pos == start_pos &&
        rgblight_ranges.effect_num_leds == num_leds) {
        return;  // V251013R7: 동일 범위 반복 시 재렌더 예약을 생략
    }

    rgblight_ranges.effect_start_pos = start_pos;
    rgblight_ranges.effect_end_pos   = (uint8_t)end;
    rgblight_ranges.effect_num_leds  = num_leds;

    if (rgblight_indicator_state.active) {
        rgblight_indicator_state.needs_render = true;  // V251013R6: 효과 범위 갱신 시 재렌더링 플래그 설정
                                                        // V251013R7: 값이 실제로 변한 경우에만 트리거
    }
}

__attribute__((weak)) RGB rgblight_hsv_to_rgb(HSV hsv) {
    return hsv_to_rgb(hsv);
}

void sethsv_raw(uint8_t hue, uint8_t sat, uint8_t val, rgb_led_t *led1) {
    HSV hsv = {hue, sat, val};
    RGB rgb = rgblight_hsv_to_rgb(hsv);
    setrgb(rgb.r, rgb.g, rgb.b, led1);
}

void sethsv(uint8_t hue, uint8_t sat, uint8_t val, rgb_led_t *led1) {
    sethsv_raw(hue, sat, val > RGBLIGHT_LIMIT_VAL ? RGBLIGHT_LIMIT_VAL : val, led1);
}

void setrgb(uint8_t r, uint8_t g, uint8_t b, rgb_led_t *led1) {
    led1->r = r;
    led1->g = g;
    led1->b = b;
#ifdef RGBW
    led1->w = 0;
#endif
}

void rgblight_check_config(void) {
    /* Add some out of bound checks for RGB light config */

    if (rgblight_config.mode < RGBLIGHT_MODE_STATIC_LIGHT) {
        rgblight_config.mode = RGBLIGHT_MODE_STATIC_LIGHT;
    } else if (rgblight_config.mode > RGBLIGHT_MODES) {
        rgblight_config.mode = RGBLIGHT_MODES;
    }

    if (rgblight_config.val > RGBLIGHT_LIMIT_VAL) {
        rgblight_config.val = RGBLIGHT_LIMIT_VAL;
    }
}

uint64_t eeconfig_read_rgblight(void) {
#ifdef EEPROM_ENABLE
    return (uint64_t)((eeprom_read_dword(EECONFIG_RGBLIGHT)) | ((uint64_t)eeprom_read_byte(EECONFIG_RGBLIGHT_EXTENDED) << 32));
#else
    return 0;
#endif
}

void eeconfig_update_rgblight(uint64_t val) {
#ifdef EEPROM_ENABLE
    rgblight_check_config();
    eeprom_update_dword(EECONFIG_RGBLIGHT, val & 0xFFFFFFFF);
    eeprom_update_byte(EECONFIG_RGBLIGHT_EXTENDED, (val >> 32) & 0xFF);
#endif
}

void eeconfig_update_rgblight_current(void) {
    eeconfig_update_rgblight(rgblight_config.raw);
}

void eeconfig_update_rgblight_default(void) {
    rgblight_config.enable    = RGBLIGHT_DEFAULT_ON;
    rgblight_config.velocikey = 0;
    rgblight_config.mode      = RGBLIGHT_DEFAULT_MODE;
    rgblight_config.hue       = RGBLIGHT_DEFAULT_HUE;
    rgblight_config.sat       = RGBLIGHT_DEFAULT_SAT;
    rgblight_config.val       = RGBLIGHT_DEFAULT_VAL;
    rgblight_config.speed     = RGBLIGHT_DEFAULT_SPD;
    RGBLIGHT_SPLIT_SET_CHANGE_MODEHSVS;
    eeconfig_update_rgblight(rgblight_config.raw);
}

void eeconfig_debug_rgblight(void) {
    dprintf("rgblight_config EEPROM:\n");
    dprintf("rgblight_config.enable = %d\n", rgblight_config.enable);
    dprintf("rgblight_config.velocikey = %d\n", rgblight_config.velocikey);
    dprintf("rghlight_config.mode = %d\n", rgblight_config.mode);
    dprintf("rgblight_config.hue = %d\n", rgblight_config.hue);
    dprintf("rgblight_config.sat = %d\n", rgblight_config.sat);
    dprintf("rgblight_config.val = %d\n", rgblight_config.val);
    dprintf("rgblight_config.speed = %d\n", rgblight_config.speed);
}

void rgblight_init(void) {
    /* if already initialized, don't do it again.
       If you must do it again, extern this and set to false, first.
       This is a dirty, dirty hack until proper hooks can be added for keyboard startup. */
    if (is_rgblight_initialized) {
        return;
    }

    dprintf("rgblight_init start!\n");
    rgblight_config.raw = eeconfig_read_rgblight();
    RGBLIGHT_SPLIT_SET_CHANGE_MODEHSVS;
    if (!rgblight_config.mode) {
        dprintf("rgblight_init rgblight_config.mode = 0. Write default values to EEPROM.\n");
        eeconfig_update_rgblight_default();
        rgblight_config.raw = eeconfig_read_rgblight();
    }
    rgblight_check_config();

    eeconfig_debug_rgblight(); // display current eeprom values

    rgblight_timer_init(); // setup the timer

    if (rgblight_config.enable) {
        rgblight_mode_noeeprom(rgblight_config.mode);
    }

    is_rgblight_initialized = true;

    bool indicator_should_enable =
        rgblight_indicator_should_enable(rgblight_indicator_state.config,
                                         rgblight_indicator_state.host_state);

    rgblight_indicator_commit_state(indicator_should_enable,
                                    true); // V251012R8: 초기화 시점에서 직접 전이를 재평가해 즉시 렌더 요청
}

void rgblight_reload_from_eeprom(void) {
    /* Reset back to what we have in eeprom */
    rgblight_config.raw = eeconfig_read_rgblight();
    RGBLIGHT_SPLIT_SET_CHANGE_MODEHSVS;
    rgblight_check_config();
    eeconfig_debug_rgblight(); // display current eeprom values
    if (rgblight_config.enable) {
        rgblight_mode_noeeprom(rgblight_config.mode);
    }
}

uint64_t rgblight_read_qword(void) {
    return rgblight_config.raw;
}

void rgblight_update_qword(uint64_t qword) {
    RGBLIGHT_SPLIT_SET_CHANGE_MODEHSVS;
    rgblight_config.raw = qword;
    if (rgblight_config.enable)
        rgblight_mode_noeeprom(rgblight_config.mode);
    else {
        rgblight_timer_disable();
        rgblight_set();
    }
}

void rgblight_increase(void) {
    uint8_t mode = 0;
    if (rgblight_config.mode < RGBLIGHT_MODES) {
        mode = rgblight_config.mode + 1;
    }
    rgblight_mode(mode);
}
void rgblight_decrease(void) {
    uint8_t mode = 0;
    // Mode will never be < 1. If it ever is, eeprom needs to be initialized.
    if (rgblight_config.mode > RGBLIGHT_MODE_STATIC_LIGHT) {
        mode = rgblight_config.mode - 1;
    }
    rgblight_mode(mode);
}
void rgblight_step_helper(bool write_to_eeprom) {
    uint8_t mode = 0;
    mode         = rgblight_config.mode + 1;
    if (mode > RGBLIGHT_MODES) {
        mode = 1;
    }
    rgblight_mode_eeprom_helper(mode, write_to_eeprom);
}
void rgblight_step_noeeprom(void) {
    rgblight_step_helper(false);
}
void rgblight_step(void) {
    rgblight_step_helper(true);
}
void rgblight_step_reverse_helper(bool write_to_eeprom) {
    uint8_t mode = 0;
    mode         = rgblight_config.mode - 1;
    if (mode < 1) {
        mode = RGBLIGHT_MODES;
    }
    rgblight_mode_eeprom_helper(mode, write_to_eeprom);
}
void rgblight_step_reverse_noeeprom(void) {
    rgblight_step_reverse_helper(false);
}
void rgblight_step_reverse(void) {
    rgblight_step_reverse_helper(true);
}

uint8_t rgblight_get_mode(void) {
    if (!rgblight_config.enable) {
        return false;
    }

    return rgblight_config.mode;
}

void rgblight_mode_eeprom_helper(uint8_t mode, bool write_to_eeprom) {
    if (!rgblight_config.enable) {
        return;
    }
    if (mode < RGBLIGHT_MODE_STATIC_LIGHT) {
        rgblight_config.mode = RGBLIGHT_MODE_STATIC_LIGHT;
    } else if (mode > RGBLIGHT_MODES) {
        rgblight_config.mode = RGBLIGHT_MODES;
    } else {
        rgblight_config.mode = mode;
    }
    RGBLIGHT_SPLIT_SET_CHANGE_MODE;
    if (write_to_eeprom) {
        eeconfig_update_rgblight(rgblight_config.raw);
        dprintf("rgblight mode [EEPROM]: %u\n", rgblight_config.mode);
    } else {
        dprintf("rgblight mode [NOEEPROM]: %u\n", rgblight_config.mode);
    }
    if (is_static_effect(rgblight_config.mode)) {
        rgblight_timer_disable();
    } else {
        rgblight_timer_enable();
    }
#ifdef RGBLIGHT_USE_TIMER
    animation_status.restart = true;
#endif
    rgblight_sethsv_noeeprom(rgblight_config.hue, rgblight_config.sat, rgblight_config.val);
}

void rgblight_mode(uint8_t mode) {
    rgblight_mode_eeprom_helper(mode, true);
}

void rgblight_mode_noeeprom(uint8_t mode) {
    rgblight_mode_eeprom_helper(mode, false);
}

void rgblight_toggle(void) {
    dprintf("rgblight toggle [EEPROM]: rgblight_config.enable = %u\n", !rgblight_config.enable);
    if (rgblight_config.enable) {
        rgblight_disable();
    } else {
        rgblight_enable();
    }
}

void rgblight_toggle_noeeprom(void) {
    dprintf("rgblight toggle [NOEEPROM]: rgblight_config.enable = %u\n", !rgblight_config.enable);
    if (rgblight_config.enable) {
        rgblight_disable_noeeprom();
    } else {
        rgblight_enable_noeeprom();
    }
}

void rgblight_enable(void) {
    rgblight_config.enable = 1;
    // No need to update EEPROM here. rgblight_mode() will do that, actually
    // eeconfig_update_rgblight(rgblight_config.raw);
    dprintf("rgblight enable [EEPROM]: rgblight_config.enable = %u\n", rgblight_config.enable);
    rgblight_mode(rgblight_config.mode);
}

void rgblight_enable_noeeprom(void) {
    rgblight_config.enable = 1;
    dprintf("rgblight enable [NOEEPROM]: rgblight_config.enable = %u\n", rgblight_config.enable);
    rgblight_mode_noeeprom(rgblight_config.mode);
}

void rgblight_disable(void) {
    rgblight_config.enable = 0;
    eeconfig_update_rgblight(rgblight_config.raw);
    dprintf("rgblight disable [EEPROM]: rgblight_config.enable = %u\n", rgblight_config.enable);
    rgblight_timer_disable();
    RGBLIGHT_SPLIT_SET_CHANGE_MODE;
    rgblight_set();
}

void rgblight_disable_noeeprom(void) {
    rgblight_config.enable = 0;
    dprintf("rgblight disable [NOEEPROM]: rgblight_config.enable = %u\n", rgblight_config.enable);
    rgblight_timer_disable();
    RGBLIGHT_SPLIT_SET_CHANGE_MODE;
    rgblight_set();
}

void rgblight_enabled_noeeprom(bool state) {
    state ? rgblight_enable_noeeprom() : rgblight_disable_noeeprom();
}

bool rgblight_is_enabled(void) {
    return rgblight_config.enable;
}

void rgblight_increase_hue_helper(bool write_to_eeprom) {
    uint8_t hue = rgblight_config.hue + RGBLIGHT_HUE_STEP;
    rgblight_sethsv_eeprom_helper(hue, rgblight_config.sat, rgblight_config.val, write_to_eeprom);
}
void rgblight_increase_hue_noeeprom(void) {
    rgblight_increase_hue_helper(false);
}
void rgblight_increase_hue(void) {
    rgblight_increase_hue_helper(true);
}
void rgblight_decrease_hue_helper(bool write_to_eeprom) {
    uint8_t hue = rgblight_config.hue - RGBLIGHT_HUE_STEP;
    rgblight_sethsv_eeprom_helper(hue, rgblight_config.sat, rgblight_config.val, write_to_eeprom);
}
void rgblight_decrease_hue_noeeprom(void) {
    rgblight_decrease_hue_helper(false);
}
void rgblight_decrease_hue(void) {
    rgblight_decrease_hue_helper(true);
}
void rgblight_increase_sat_helper(bool write_to_eeprom) {
    uint8_t sat = qadd8(rgblight_config.sat, RGBLIGHT_SAT_STEP);
    rgblight_sethsv_eeprom_helper(rgblight_config.hue, sat, rgblight_config.val, write_to_eeprom);
}
void rgblight_increase_sat_noeeprom(void) {
    rgblight_increase_sat_helper(false);
}
void rgblight_increase_sat(void) {
    rgblight_increase_sat_helper(true);
}
void rgblight_decrease_sat_helper(bool write_to_eeprom) {
    uint8_t sat = qsub8(rgblight_config.sat, RGBLIGHT_SAT_STEP);
    rgblight_sethsv_eeprom_helper(rgblight_config.hue, sat, rgblight_config.val, write_to_eeprom);
}
void rgblight_decrease_sat_noeeprom(void) {
    rgblight_decrease_sat_helper(false);
}
void rgblight_decrease_sat(void) {
    rgblight_decrease_sat_helper(true);
}
void rgblight_increase_val_helper(bool write_to_eeprom) {
    uint8_t val = qadd8(rgblight_config.val, RGBLIGHT_VAL_STEP);
    rgblight_sethsv_eeprom_helper(rgblight_config.hue, rgblight_config.sat, val, write_to_eeprom);
}
void rgblight_increase_val_noeeprom(void) {
    rgblight_increase_val_helper(false);
}
void rgblight_increase_val(void) {
    rgblight_increase_val_helper(true);
}
void rgblight_decrease_val_helper(bool write_to_eeprom) {
    uint8_t val = qsub8(rgblight_config.val, RGBLIGHT_VAL_STEP);
    rgblight_sethsv_eeprom_helper(rgblight_config.hue, rgblight_config.sat, val, write_to_eeprom);
}
void rgblight_decrease_val_noeeprom(void) {
    rgblight_decrease_val_helper(false);
}
void rgblight_decrease_val(void) {
    rgblight_decrease_val_helper(true);
}

void rgblight_increase_speed_helper(bool write_to_eeprom) {
    if (rgblight_config.speed < 3) rgblight_config.speed++;
    // RGBLIGHT_SPLIT_SET_CHANGE_HSVS; // NEED?
    if (write_to_eeprom) {
        eeconfig_update_rgblight(rgblight_config.raw);
    }
}
void rgblight_increase_speed(void) {
    rgblight_increase_speed_helper(true);
}
void rgblight_increase_speed_noeeprom(void) {
    rgblight_increase_speed_helper(false);
}

void rgblight_decrease_speed_helper(bool write_to_eeprom) {
    if (rgblight_config.speed > 0) rgblight_config.speed--;
    // RGBLIGHT_SPLIT_SET_CHANGE_HSVS; // NEED??
    if (write_to_eeprom) {
        eeconfig_update_rgblight(rgblight_config.raw);
    }
}
void rgblight_decrease_speed(void) {
    rgblight_decrease_speed_helper(true);
}
void rgblight_decrease_speed_noeeprom(void) {
    rgblight_decrease_speed_helper(false);
}

static void rgblight_sethsv_noeeprom_old(uint8_t hue, uint8_t sat, uint8_t val) {
    if (rgblight_config.enable) {
        rgb_led_t tmp_led;
        sethsv(hue, sat, val, &tmp_led);
        rgblight_setrgb(tmp_led.r, tmp_led.g, tmp_led.b);
    }
}

void rgblight_sethsv_eeprom_helper(uint8_t hue, uint8_t sat, uint8_t val, bool write_to_eeprom) {
    if (rgblight_config.enable) {
#ifdef RGBLIGHT_SPLIT
        if (rgblight_config.hue != hue || rgblight_config.sat != sat || rgblight_config.val != val) {
            RGBLIGHT_SPLIT_SET_CHANGE_HSVS;
        }
#endif
        rgblight_status.base_mode = mode_base_table[rgblight_config.mode];
        rgblight_effect_pulse_on_base_mode_update();  // V251018R5: Pulse 계열 모드 전환 시 상태 초기화
        if (rgblight_config.mode == RGBLIGHT_MODE_STATIC_LIGHT) {
            // same static color
            rgb_led_t tmp_led;
#ifdef RGBLIGHT_LAYERS_RETAIN_VAL
            // needed for rgblight_layers_write() to get the new val, since it reads rgblight_config.val
            rgblight_config.val = val;
#endif
            sethsv(hue, sat, val, &tmp_led);
            rgblight_setrgb(tmp_led.r, tmp_led.g, tmp_led.b);
        } else {
            // all LEDs in same color
            if (1 == 0) { // dummy
            }
#ifdef RGBLIGHT_EFFECT_BREATHING
            else if (rgblight_status.base_mode == RGBLIGHT_MODE_BREATHING) {
                // breathing mode, ignore the change of val, use in memory value instead
                val = rgblight_config.val;
            }
#endif
#ifdef RGBLIGHT_EFFECT_RAINBOW_MOOD
            else if (rgblight_status.base_mode == RGBLIGHT_MODE_RAINBOW_MOOD) {
                // rainbow mood, ignore the change of hue
                hue = rgblight_config.hue;
            }
#endif
#ifdef RGBLIGHT_EFFECT_RAINBOW_SWIRL
            else if (rgblight_status.base_mode == RGBLIGHT_MODE_RAINBOW_SWIRL) {
                // rainbow swirl, ignore the change of hue
                hue = rgblight_config.hue;
            }
#endif
#ifdef RGBLIGHT_EFFECT_STATIC_GRADIENT
            else if (rgblight_status.base_mode == RGBLIGHT_MODE_STATIC_GRADIENT) {
                // static gradient
                uint8_t delta     = rgblight_config.mode - rgblight_status.base_mode;
                bool    direction = (delta % 2) == 0;

                uint8_t range = pgm_read_byte(&RGBLED_GRADIENT_RANGES[delta / 2]);
                for (uint8_t i = 0; i < rgblight_ranges.effect_num_leds; i++) {
                    uint8_t _hue = ((uint16_t)i * (uint16_t)range) / rgblight_ranges.effect_num_leds;
                    if (direction) {
                        _hue = hue + _hue;
                    } else {
                        _hue = hue - _hue;
                    }
                    dprintf("rgblight rainbow set hsv: %d,%d,%d,%u\n", i, _hue, direction, range);
                    sethsv(_hue, sat, val, (rgb_led_t *)&led[i + rgblight_ranges.effect_start_pos]);
                }
#    ifdef RGBLIGHT_LAYERS_RETAIN_VAL
                // needed for rgblight_layers_write() to get the new val, since it reads rgblight_config.val
                rgblight_config.val = val;
#    endif
                rgblight_set();
            }
#endif
        }
        rgblight_config.hue = hue;
        rgblight_config.sat = sat;
        rgblight_config.val = val;
        if (write_to_eeprom) {
            eeconfig_update_rgblight(rgblight_config.raw);
            dprintf("rgblight set hsv [EEPROM]: %u,%u,%u\n", rgblight_config.hue, rgblight_config.sat, rgblight_config.val);
        } else {
            dprintf("rgblight set hsv [NOEEPROM]: %u,%u,%u\n", rgblight_config.hue, rgblight_config.sat, rgblight_config.val);
        }
    }
}

void rgblight_sethsv(uint8_t hue, uint8_t sat, uint8_t val) {
    rgblight_sethsv_eeprom_helper(hue, sat, val, true);
}

void rgblight_sethsv_noeeprom(uint8_t hue, uint8_t sat, uint8_t val) {
    rgblight_sethsv_eeprom_helper(hue, sat, val, false);
}

uint8_t rgblight_get_speed(void) {
    return rgblight_config.speed;
}

void rgblight_set_speed_eeprom_helper(uint8_t speed, bool write_to_eeprom) {
    rgblight_config.speed = speed;
    if (write_to_eeprom) {
        eeconfig_update_rgblight(rgblight_config.raw);
        dprintf("rgblight set speed [EEPROM]: %u\n", rgblight_config.speed);
    } else {
        dprintf("rgblight set speed [NOEEPROM]: %u\n", rgblight_config.speed);
    }
}

void rgblight_set_speed(uint8_t speed) {
    rgblight_set_speed_eeprom_helper(speed, true);
}

void rgblight_set_speed_noeeprom(uint8_t speed) {
    rgblight_set_speed_eeprom_helper(speed, false);
}

uint8_t rgblight_get_hue(void) {
    return rgblight_config.hue;
}

uint8_t rgblight_get_sat(void) {
    return rgblight_config.sat;
}

uint8_t rgblight_get_val(void) {
    return rgblight_config.val;
}

HSV rgblight_get_hsv(void) {
    return (HSV){rgblight_config.hue, rgblight_config.sat, rgblight_config.val};
}

void rgblight_setrgb(uint8_t r, uint8_t g, uint8_t b) {
    if (!rgblight_config.enable) {
        return;
    }

    for (uint8_t i = rgblight_ranges.effect_start_pos; i < rgblight_ranges.effect_end_pos; i++) {
        led[i].r = r;
        led[i].g = g;
        led[i].b = b;
#ifdef RGBW
        led[i].w = 0;
#endif
    }
    rgblight_set();
}

void rgblight_setrgb_at(uint8_t r, uint8_t g, uint8_t b, uint8_t index) {
    if (!rgblight_config.enable || index >= RGBLIGHT_LED_COUNT) {
        return;
    }

    led[index].r = r;
    led[index].g = g;
    led[index].b = b;
#ifdef RGBW
    led[index].w = 0;
#endif
    rgblight_set();
}

void rgblight_sethsv_at(uint8_t hue, uint8_t sat, uint8_t val, uint8_t index) {
    if (!rgblight_config.enable) {
        return;
    }

    rgb_led_t tmp_led;
    sethsv(hue, sat, val, &tmp_led);
    rgblight_setrgb_at(tmp_led.r, tmp_led.g, tmp_led.b, index);
}

#if defined(RGBLIGHT_EFFECT_BREATHING) || defined(RGBLIGHT_EFFECT_RAINBOW_MOOD) || defined(RGBLIGHT_EFFECT_RAINBOW_SWIRL) || defined(RGBLIGHT_EFFECT_SNAKE) || defined(RGBLIGHT_EFFECT_KNIGHT) || defined(RGBLIGHT_EFFECT_TWINKLE)

static uint8_t get_interval_time(const uint8_t *default_interval_address, uint8_t velocikey_min, uint8_t velocikey_max) {
    return
#    ifdef VELOCIKEY_ENABLE
        rgblight_velocikey_enabled() ? rgblight_velocikey_match_speed(velocikey_min, velocikey_max) :
#    endif
                                     pgm_read_byte(default_interval_address);
}

#endif

void rgblight_setrgb_range(uint8_t r, uint8_t g, uint8_t b, uint8_t start, uint8_t end) {
    if (!rgblight_config.enable || start < 0 || start >= end || end > RGBLIGHT_LED_COUNT) {
        return;
    }

    for (uint8_t i = start; i < end; i++) {
        led[i].r = r;
        led[i].g = g;
        led[i].b = b;
#ifdef RGBW
        led[i].w = 0;
#endif
    }
    rgblight_set();
}

void rgblight_sethsv_range(uint8_t hue, uint8_t sat, uint8_t val, uint8_t start, uint8_t end) {
    if (!rgblight_config.enable) {
        return;
    }

    rgb_led_t tmp_led;
    sethsv(hue, sat, val, &tmp_led);
    rgblight_setrgb_range(tmp_led.r, tmp_led.g, tmp_led.b, start, end);
}

#ifndef RGBLIGHT_SPLIT
void rgblight_setrgb_master(uint8_t r, uint8_t g, uint8_t b) {
    rgblight_setrgb_range(r, g, b, 0, (uint8_t)RGBLIGHT_LED_COUNT / 2);
}

void rgblight_setrgb_slave(uint8_t r, uint8_t g, uint8_t b) {
    rgblight_setrgb_range(r, g, b, (uint8_t)RGBLIGHT_LED_COUNT / 2, (uint8_t)RGBLIGHT_LED_COUNT);
}

void rgblight_sethsv_master(uint8_t hue, uint8_t sat, uint8_t val) {
    rgblight_sethsv_range(hue, sat, val, 0, (uint8_t)RGBLIGHT_LED_COUNT / 2);
}

void rgblight_sethsv_slave(uint8_t hue, uint8_t sat, uint8_t val) {
    rgblight_sethsv_range(hue, sat, val, (uint8_t)RGBLIGHT_LED_COUNT / 2, (uint8_t)RGBLIGHT_LED_COUNT);
}
#endif // ifndef RGBLIGHT_SPLIT

#ifdef RGBLIGHT_LAYERS
void rgblight_set_layer_state(uint8_t layer, bool enabled) {
    rgblight_layer_mask_t mask = (rgblight_layer_mask_t)1 << layer;
    if (enabled) {
        rgblight_status.enabled_layer_mask |= mask;
    } else {
        rgblight_status.enabled_layer_mask &= ~mask;
    }
    RGBLIGHT_SPLIT_SET_CHANGE_LAYERS;

    // Calling rgblight_set() here (directly or indirectly) could
    // potentially cause timing issues when there are multiple
    // successive calls to rgblight_set_layer_state(). Instead,
    // set a flag and do it the next time rgblight_task() runs.

    deferred_set_layer_state = true;
}

bool rgblight_get_layer_state(uint8_t layer) {
    rgblight_layer_mask_t mask = (rgblight_layer_mask_t)1 << layer;
    return (rgblight_status.enabled_layer_mask & mask) != 0;
}

// Write any enabled LED layers into the buffer
static void rgblight_layers_write(void) {
#    ifdef RGBLIGHT_LAYERS_RETAIN_VAL
    uint8_t current_val = rgblight_get_val();
#    endif
    uint8_t i = 0;
    // For each layer
    for (const rgblight_segment_t *const *layer_ptr = rgblight_layers; i < RGBLIGHT_MAX_LAYERS; layer_ptr++, i++) {
        if (!rgblight_get_layer_state(i)) {
            continue; // Layer is disabled
        }
        const rgblight_segment_t *segment_ptr = pgm_read_ptr(layer_ptr);
        if (segment_ptr == NULL) {
            break; // No more layers
        }
        // For each segment
        while (1) {
            rgblight_segment_t segment;
            memcpy_P(&segment, segment_ptr, sizeof(rgblight_segment_t));
            if (segment.index == RGBLIGHT_END_SEGMENT_INDEX) {
                break; // No more segments
            }
            // Write segment.count LEDs
            rgb_led_t *const limit = &led[MIN(segment.index + segment.count, RGBLIGHT_LED_COUNT)];
            for (rgb_led_t *led_ptr = &led[segment.index]; led_ptr < limit; led_ptr++) {
#    ifdef RGBLIGHT_LAYERS_RETAIN_VAL
                sethsv(segment.hue, segment.sat, current_val, led_ptr);
#    else
                sethsv(segment.hue, segment.sat, segment.val, led_ptr);
#    endif
            }
            segment_ptr++;
        }
    }
}

#    ifdef RGBLIGHT_LAYER_BLINK
rgblight_layer_mask_t _blinking_layer_mask = 0;
static uint16_t       _repeat_timer;
static uint8_t        _times_remaining;
static uint16_t       _dur;

void rgblight_blink_layer(uint8_t layer, uint16_t duration_ms) {
    rgblight_blink_layer_repeat(layer, duration_ms, 1);
}

void rgblight_blink_layer_repeat(uint8_t layer, uint16_t duration_ms, uint8_t times) {
    if (times > UINT8_MAX / 2) {
        times = UINT8_MAX / 2;
    }

    _times_remaining = times * 2;
    _dur             = duration_ms;

    rgblight_set_layer_state(layer, true);
    _times_remaining--;
    _blinking_layer_mask |= (rgblight_layer_mask_t)1 << layer;
    _repeat_timer = sync_timer_read() + duration_ms;
}

void rgblight_unblink_layer(uint8_t layer) {
    rgblight_set_layer_state(layer, false);
    _blinking_layer_mask &= ~((rgblight_layer_mask_t)1 << layer);
}

void rgblight_unblink_all_but_layer(uint8_t layer) {
    for (uint8_t i = 0; i < RGBLIGHT_MAX_LAYERS; i++) {
        if (i != layer) {
            if ((_blinking_layer_mask & (rgblight_layer_mask_t)1 << i) != 0) {
                rgblight_unblink_layer(i);
            }
        }
    }
}

void rgblight_blink_layer_repeat_helper(void) {
    if (_blinking_layer_mask != 0 && timer_expired(sync_timer_read(), _repeat_timer)) {
        for (uint8_t layer = 0; layer < RGBLIGHT_MAX_LAYERS; layer++) {
            if ((_blinking_layer_mask & (rgblight_layer_mask_t)1 << layer) != 0) {
                if (_times_remaining % 2 == 1) {
                    rgblight_set_layer_state(layer, false);
                } else {
                    rgblight_set_layer_state(layer, true);
                }
            }
        }
        _times_remaining--;
        if (_times_remaining <= 0) {
            _blinking_layer_mask = 0;
        } else {
            _repeat_timer = sync_timer_read() + _dur;
        }
    }
}
#    endif

#endif

#ifdef RGBLIGHT_SLEEP

void rgblight_suspend(void) {
    rgblight_timer_disable();
    if (!is_suspended) {
        is_suspended        = true;
        pre_suspend_enabled = rgblight_config.enable;

#    ifdef RGBLIGHT_LAYER_BLINK
        // make sure any layer blinks don't come back after suspend
        rgblight_status.enabled_layer_mask &= ~_blinking_layer_mask;
        _blinking_layer_mask = 0;
#    endif

        rgblight_disable_noeeprom();
    }
}

void rgblight_wakeup(void) {
    is_suspended = false;

    if (pre_suspend_enabled) {
        rgblight_enable_noeeprom();
    }
#    ifdef RGBLIGHT_LAYERS_OVERRIDE_RGB_OFF
    // Need this or else the LEDs won't be set
    else if (rgblight_status.enabled_layer_mask != 0) {
        rgblight_set();
    }
#    endif

    rgblight_timer_enable();
}

#endif

// V251018R6: WS2812 전송 루틴을 별도 함수로 분리해 호출 컨텍스트를 제어
static void rgblight_render_frame(void)
{
    rgb_led_t *start_led;
    uint8_t    num_leds = rgblight_ranges.clipping_num_leds;
    bool       indicator_supported = rgblight_indicator_supported;  // V251120R1: INDICATOR_ENABLE 상태에 따라 인디케이터 경로 분기
    bool       indicator_active = indicator_supported && rgblight_indicator_state.active;
    rgblight_indicator_range_t indicator_range = rgblight_indicator_state.range;
    bool indicator_has_range = indicator_active && (indicator_range.count > 0);
    bool indicator_overrides = indicator_has_range && rgblight_indicator_state.overrides_all;
    bool indicator_ready     = indicator_active && indicator_has_range;

    if (!indicator_ready) {
        rgblight_indicator_state.needs_render = false;  // V251016R9: 비활성 프레임에서 대기 플래그 정리
    } else if (!indicator_overrides) {
        rgblight_indicator_state.needs_render = true;   // V251016R9: 부분 오버레이는 기본 이펙트 이후에 항상 재적용
    }

    bool indicator_should_apply = indicator_ready &&
                                  (indicator_overrides || rgblight_indicator_state.needs_render);  // V251121R1: 오버레이 우선순위를 항상 보장

    if (!indicator_overrides) {
        if (!rgblight_config.enable) {
            for (uint8_t i = rgblight_ranges.effect_start_pos; i < rgblight_ranges.effect_end_pos; i++) {
                led[i].r = 0;
                led[i].g = 0;
                led[i].b = 0;
#ifdef RGBW
                led[i].w = 0;
#endif
            }
        }

#ifdef RGBLIGHT_LAYERS
        if (rgblight_layers != NULL
#    if !defined(RGBLIGHT_LAYERS_OVERRIDE_RGB_OFF)
            && rgblight_config.enable
#    elif defined(RGBLIGHT_SLEEP)
            && !is_suspended
#    endif
        ) {
            rgblight_layers_write();
        }
#endif
    }

    if (indicator_should_apply) {
        rgblight_indicator_apply_overlay();  // V251121R1: 인디케이터 활성 시 베이스 프레임 위에 항상 최종 오버레이 적용
    }

#ifdef RGBLIGHT_LED_MAP
    rgb_led_t led0[RGBLIGHT_LED_COUNT];
    for (uint8_t i = 0; i < RGBLIGHT_LED_COUNT; i++) {
        led0[i] = led[pgm_read_byte(&led_map[i])];
    }
    start_led = led0 + rgblight_ranges.clipping_start_pos;
#else
    start_led = led + rgblight_ranges.clipping_start_pos;
#endif

#ifdef RGBW
    for (uint8_t i = 0; i < num_leds; i++) {
        convert_rgb_to_rgbw(&start_led[i]);
    }
#endif
    rgblight_driver.setleds(start_led, num_leds);
}

// V251018R6: 초기화 이후에는 렌더를 큐잉해 WS2812 전송을 주 루프에서만 수행
void rgblight_set(void)
{
    // V251122R3: 전면 인디케이터 중에도 렌더 큐잉을 유지해 오버레이 누락을 방지

    if (!is_rgblight_initialized) {
        rgblight_render_frame();
        return;
    }

    rgblight_request_render();
}

#ifdef RGBLIGHT_SPLIT
/* for split keyboard master side */
uint8_t rgblight_get_change_flags(void) {
    return rgblight_status.change_flags;
}

void rgblight_clear_change_flags(void) {
    rgblight_status.change_flags = 0;
}

void rgblight_get_syncinfo(rgblight_syncinfo_t *syncinfo) {
    syncinfo->config = rgblight_config;
    syncinfo->status = rgblight_status;
}

/* for split keyboard slave side */
void rgblight_update_sync(rgblight_syncinfo_t *syncinfo, bool write_to_eeprom) {
#    ifdef RGBLIGHT_LAYERS
    if (syncinfo->status.change_flags & RGBLIGHT_STATUS_CHANGE_LAYERS) {
        rgblight_status.enabled_layer_mask = syncinfo->status.enabled_layer_mask;
    }
#    endif
    if (syncinfo->status.change_flags & RGBLIGHT_STATUS_CHANGE_MODE) {
        if (syncinfo->config.enable) {
            rgblight_config.enable = 1; // == rgblight_enable_noeeprom();
            rgblight_mode_eeprom_helper(syncinfo->config.mode, write_to_eeprom);
        } else {
            rgblight_disable_noeeprom();
        }
    }
    if (syncinfo->status.change_flags & RGBLIGHT_STATUS_CHANGE_HSVS) {
        rgblight_sethsv_eeprom_helper(syncinfo->config.hue, syncinfo->config.sat, syncinfo->config.val, write_to_eeprom);
        // rgblight_config.speed = config->speed; // NEED???
    }
#    ifdef RGBLIGHT_USE_TIMER
    if (syncinfo->status.change_flags & RGBLIGHT_STATUS_CHANGE_TIMER) {
        if (syncinfo->status.timer_enabled) {
            rgblight_timer_enable();
        } else {
            rgblight_timer_disable();
        }
    }
#        ifndef RGBLIGHT_SPLIT_NO_ANIMATION_SYNC
    if (syncinfo->status.change_flags & RGBLIGHT_STATUS_ANIMATION_TICK) {
        animation_status.restart = true;
    }
#        endif /* RGBLIGHT_SPLIT_NO_ANIMATION_SYNC */
#    endif     /* RGBLIGHT_USE_TIMER */
}
#endif /* RGBLIGHT_SPLIT */

#ifdef RGBLIGHT_USE_TIMER

typedef void (*effect_func_t)(animation_status_t *anim);

// Animation timer -- use system timer (AVR Timer0)
void rgblight_timer_init(void) {
    rgblight_status.timer_enabled = false;
    RGBLIGHT_SPLIT_SET_CHANGE_TIMER_ENABLE;
}
void rgblight_timer_enable(void) {
    if (!is_static_effect(rgblight_config.mode)) {
        rgblight_status.timer_enabled = true;
    }
    animation_status.last_timer = sync_timer_read();
    animation_status.next_timer_due = animation_status.last_timer;  // V251122R1: 타이머 활성 시 만료 기준을 설정(0도 유효값으로 사용)
    RGBLIGHT_SPLIT_SET_CHANGE_TIMER_ENABLE;
    dprintf("rgblight timer enabled.\n");
}
void rgblight_timer_disable(void) {
    rgblight_status.timer_enabled = false;
    RGBLIGHT_SPLIT_SET_CHANGE_TIMER_ENABLE;
    dprintf("rgblight timer disable.\n");
}
void rgblight_timer_toggle(void) {
    dprintf("rgblight timer toggle.\n");
    if (rgblight_status.timer_enabled) {
        rgblight_timer_disable();
    } else {
        rgblight_timer_enable();
    }
}

void rgblight_show_solid_color(uint8_t r, uint8_t g, uint8_t b) {
    rgblight_enable();
    rgblight_mode(RGBLIGHT_MODE_STATIC_LIGHT);
    rgblight_setrgb(r, g, b);
}

static void rgblight_effect_dummy(animation_status_t *anim) {
    // do nothing
    /********
    dprintf("rgblight_task() what happened?\n");
    dprintf("is_static_effect %d\n", is_static_effect(rgblight_config.mode));
    dprintf("mode = %d, base_mode = %d, timer_enabled %d, ",
            rgblight_config.mode, rgblight_status.base_mode,
            rgblight_status.timer_enabled);
    dprintf("last_timer = %d\n",anim->last_timer);
    **/
}

void rgblight_timer_task(void) {
    bool indicator_supported = rgblight_indicator_supported;
    bool indicator_active    = indicator_supported && rgblight_indicator_state.active;

    if (indicator_active && rgblight_indicator_state.needs_render) { // V251018R1: 렌더 예약만 수행
        rgblight_request_render();
    }
    if (!rgblight_status.timer_enabled) {
        return;
    }
    if (rgblight_ranges.effect_num_leds == 0) {
        return;  // V251013R10: 빈 효과 범위에서는 애니메이션 루프를 건너뛰어 0으로 나누는 연산을 방지
    }

    // V251122R6: 캐시/만료 선행 분기 제거, Velocikey 등 런타임 변화에 즉시 반응하도록 복원
    // V251120R1: 인디케이터 오버레이 중에도 베이스 이펙트를 큐잉해 렌더 루프를 유지
    effect_func_t effect_func   = rgblight_effect_dummy;
    uint16_t      interval_time = 2000; // dummy interval
    uint8_t       delta         = rgblight_config.mode - rgblight_status.base_mode;
    animation_status.delta      = delta;

    // static light mode, do nothing here
    if (1 == 0) { // dummy
    }
#    ifdef RGBLIGHT_EFFECT_BREATHING
    else if (rgblight_status.base_mode == RGBLIGHT_MODE_BREATHING) {
        // breathing mode
        interval_time = get_interval_time(&RGBLED_BREATHING_INTERVALS[delta], 1, 100);
        effect_func   = rgblight_effect_breathing;
    }
#    endif
#    ifdef RGBLIGHT_EFFECT_RAINBOW_MOOD
    else if (rgblight_status.base_mode == RGBLIGHT_MODE_RAINBOW_MOOD) {
        // rainbow mood mode
        interval_time = get_interval_time(&RGBLED_RAINBOW_MOOD_INTERVALS[delta], 5, 100);
        effect_func   = rgblight_effect_rainbow_mood;
    }
#    endif
#    ifdef RGBLIGHT_EFFECT_RAINBOW_SWIRL
    else if (rgblight_status.base_mode == RGBLIGHT_MODE_RAINBOW_SWIRL) {
        // rainbow swirl mode
        interval_time = get_interval_time(&RGBLED_RAINBOW_SWIRL_INTERVALS[delta / 2], 1, 100);
        effect_func   = rgblight_effect_rainbow_swirl;
    }
#    endif
#    ifdef RGBLIGHT_EFFECT_SNAKE
    else if (rgblight_status.base_mode == RGBLIGHT_MODE_SNAKE) {
        // snake mode
        interval_time = get_interval_time(&RGBLED_SNAKE_INTERVALS[delta / 2], 1, 200);
        effect_func   = rgblight_effect_snake;
    }
#    endif
#    ifdef RGBLIGHT_EFFECT_KNIGHT
    else if (rgblight_status.base_mode == RGBLIGHT_MODE_KNIGHT) {
        // knight mode
        interval_time = get_interval_time(&RGBLED_KNIGHT_INTERVALS[delta], 5, 100);
        effect_func   = rgblight_effect_knight;
    }
#    endif
#    ifdef RGBLIGHT_EFFECT_CHRISTMAS
    else if (rgblight_status.base_mode == RGBLIGHT_MODE_CHRISTMAS) {
        // christmas mode
        interval_time = RGBLIGHT_EFFECT_CHRISTMAS_INTERVAL;
        effect_func   = (effect_func_t)rgblight_effect_christmas;
    }
#    endif
#    ifdef RGBLIGHT_EFFECT_RGB_TEST
    else if (rgblight_status.base_mode == RGBLIGHT_MODE_RGB_TEST) {
        // RGB test mode
        interval_time = pgm_read_word(&RGBLED_RGBTEST_INTERVALS[0]);
        effect_func   = (effect_func_t)rgblight_effect_rgbtest;
    }
#    endif
#    ifdef RGBLIGHT_EFFECT_ALTERNATING
    else if (rgblight_status.base_mode == RGBLIGHT_MODE_ALTERNATING) {
        interval_time = 500;
        effect_func   = (effect_func_t)rgblight_effect_alternating;
    }
#    endif
#    ifdef RGBLIGHT_EFFECT_TWINKLE
    else if (rgblight_status.base_mode == RGBLIGHT_MODE_TWINKLE) {
        interval_time = get_interval_time(&RGBLED_TWINKLE_INTERVALS[delta % 3], 5, 30);
        effect_func   = (effect_func_t)rgblight_effect_twinkle;
    }
#    endif
#    ifdef RGBLIGHT_EFFECT_PULSE_ON_PRESS
    else if (rgblight_status.base_mode == RGBLIGHT_MODE_PULSE_ON_PRESS) {
        interval_time = 5;  // V251018R5: Pulse On Press는 짧은 주기로 상태를 평가
        effect_func   = (effect_func_t)rgblight_effect_pulse_on_press;
    }
#    endif
#    ifdef RGBLIGHT_EFFECT_PULSE_OFF_PRESS
    else if (rgblight_status.base_mode == RGBLIGHT_MODE_PULSE_OFF_PRESS) {
        interval_time = 5;  // V251018R5: Pulse Off Press 역시 동일 주기 사용
        effect_func   = (effect_func_t)rgblight_effect_pulse_off_press;
    }
#    endif
#    ifdef RGBLIGHT_EFFECT_PULSE_ON_PRESS_HOLD
    else if (rgblight_status.base_mode == RGBLIGHT_MODE_PULSE_ON_PRESS_HOLD) {
        interval_time = 5;  // V251018R5: Pulse On Press (Hold) 확장 모드도 동일 주기 적용
        effect_func   = (effect_func_t)rgblight_effect_pulse_on_press_hold;
    }
#    endif
#    ifdef RGBLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD
    else if (rgblight_status.base_mode == RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD) {
        interval_time = 5;  // V251018R5: Pulse Off Press (Hold) 확장 모드도 동일 주기 적용
        effect_func   = (effect_func_t)rgblight_effect_pulse_off_press_hold;
    }
#    endif

    if (animation_status.restart) {
        animation_status.restart        = false;
        animation_status.last_timer     = sync_timer_read();
        animation_status.next_timer_due = animation_status.last_timer;  // V251122R6: 타이머 재시작 시 만료 기준 재설정(0 wrap 허용)
        animation_status.pos16          = 0;
    }

    uint16_t now = sync_timer_read();
    if (!timer_expired(now, animation_status.next_timer_due)) {
        return;
    }
#    if defined(RGBLIGHT_SPLIT) && !defined(RGBLIGHT_SPLIT_NO_ANIMATION_SYNC)
        static uint16_t report_last_timer = 0;
        static bool     tick_flag         = false;
        uint16_t        oldpos16;
        if (tick_flag) {
            tick_flag = false;
            if (timer_expired(now, report_last_timer)) {
                report_last_timer += 30000;
                dprintf("rgblight animation tick report to slave\n");  // V251120R1: 슬레이브 동기화 로깅
                RGBLIGHT_SPLIT_ANIMATION_TICK;
            }
        }
        oldpos16 = animation_status.pos16;
#    endif
    animation_status.last_timer     = animation_status.next_timer_due;
    animation_status.next_timer_due = animation_status.last_timer + interval_time;  // V251121R5: 다음 만료 시각 캐싱으로 호출당 타이머 계산 감소
    effect_func(&animation_status);
#    if defined(RGBLIGHT_SPLIT) && !defined(RGBLIGHT_SPLIT_NO_ANIMATION_SYNC)
        if (animation_status.pos16 == 0 && oldpos16 != 0) {
            tick_flag = true;
        }
#    endif
}

#endif /* RGBLIGHT_USE_TIMER */

#if defined(RGBLIGHT_EFFECT_BREATHING) || defined(RGBLIGHT_EFFECT_TWINKLE)

#    ifndef RGBLIGHT_EFFECT_BREATHE_CENTER
#        ifndef RGBLIGHT_BREATHE_TABLE_SIZE
#            define RGBLIGHT_BREATHE_TABLE_SIZE 256 // 256 or 128 or 64
#        endif
#        include <rgblight_breathe_table.h>
#    endif

static uint8_t breathe_calc(uint8_t pos) {
    // http://sean.voisen.org/blog/2011/10/breathing-led-with-arduino/
#    ifdef RGBLIGHT_EFFECT_BREATHE_TABLE
    return pgm_read_byte(&rgblight_effect_breathe_table[pos / table_scale]);
#    else
    return (exp(sin((pos / 255.0) * M_PI)) - RGBLIGHT_EFFECT_BREATHE_CENTER / M_E) * (RGBLIGHT_EFFECT_BREATHE_MAX / (M_E - 1 / M_E));
#    endif
}

#endif

// Effects
#ifdef RGBLIGHT_EFFECT_BREATHING

__attribute__((weak)) const uint8_t RGBLED_BREATHING_INTERVALS[] PROGMEM = {30, 20, 10, 5};

void rgblight_effect_breathing(animation_status_t *anim) {
    uint8_t val = breathe_calc(anim->pos);
    rgblight_sethsv_noeeprom_old(rgblight_config.hue, rgblight_config.sat, val);
    anim->pos = (anim->pos + 1);
}
#endif

#ifdef RGBLIGHT_EFFECT_RAINBOW_MOOD
__attribute__((weak)) const uint8_t RGBLED_RAINBOW_MOOD_INTERVALS[] PROGMEM = {120, 60, 30};

void rgblight_effect_rainbow_mood(animation_status_t *anim) {
    rgblight_sethsv_noeeprom_old(anim->current_hue, rgblight_config.sat, rgblight_config.val);
    anim->current_hue++;
}
#endif

#ifdef RGBLIGHT_EFFECT_RAINBOW_SWIRL
#    ifndef RGBLIGHT_RAINBOW_SWIRL_RANGE
#        define RGBLIGHT_RAINBOW_SWIRL_RANGE 255
#    endif

__attribute__((weak)) const uint8_t RGBLED_RAINBOW_SWIRL_INTERVALS[] PROGMEM = {100, 50, 20};

void rgblight_effect_rainbow_swirl(animation_status_t *anim) {
    uint8_t hue;
    uint8_t i;

    for (i = 0; i < rgblight_ranges.effect_num_leds; i++) {
        hue = (RGBLIGHT_RAINBOW_SWIRL_RANGE / rgblight_ranges.effect_num_leds * i + anim->current_hue);
        sethsv(hue, rgblight_config.sat, rgblight_config.val, (rgb_led_t *)&led[i + rgblight_ranges.effect_start_pos]);
    }
    rgblight_set();

    if (anim->delta % 2) {
        anim->current_hue++;
    } else {
        anim->current_hue--;
    }
}
#endif

#ifdef RGBLIGHT_EFFECT_SNAKE
__attribute__((weak)) const uint8_t RGBLED_SNAKE_INTERVALS[] PROGMEM = {100, 50, 20};

void rgblight_effect_snake(animation_status_t *anim) {
    static uint8_t pos = 0;
    uint8_t        i, j;
    int8_t         increment = 1;
    uint8_t        effect_span = rgblight_ranges.effect_num_leds;  // V251122R7: 효과 범위 기반 래핑을 위해 캐시

    if (anim->delta % 2) {
        increment = -1;
    }

#    if defined(RGBLIGHT_SPLIT) && !defined(RGBLIGHT_SPLIT_NO_ANIMATION_SYNC)
    if (anim->pos == 0) { // restart signal
        if (increment == 1) {
            pos = rgblight_ranges.effect_num_leds - 1;
        } else {
            pos = 0;
        }
        anim->pos = 1;
        }
#    endif

    for (i = 0; i < rgblight_ranges.effect_num_leds; i++) {
        rgb_led_t *ledp = led + i + rgblight_ranges.effect_start_pos;
        ledp->r         = 0;
        ledp->g         = 0;
        ledp->b         = 0;
#    ifdef RGBW
        ledp->w = 0;
#    endif
        for (j = 0; j < RGBLIGHT_EFFECT_SNAKE_LENGTH; j++) {
            int16_t k = (int16_t)pos + (int16_t)j * increment;
            if (k >= effect_span) {
                k = k % effect_span;  // V251122R7: 전체 LED가 아닌 효과 범위 기준으로 래핑
            }
            if (k < 0) {
                k = k + effect_span;  // V251122R7: 음수 래핑도 동일 기준 적용
            }
            if (i == k) {
                sethsv(rgblight_config.hue, rgblight_config.sat, (uint8_t)(rgblight_config.val * (RGBLIGHT_EFFECT_SNAKE_LENGTH - j) / RGBLIGHT_EFFECT_SNAKE_LENGTH), ledp);
            }
        }
    }
    rgblight_set();
    if (increment == 1) {
        if (pos - RGBLIGHT_EFFECT_SNAKE_INCREMENT < 0) {
            pos = rgblight_ranges.effect_num_leds - 1;
#    if defined(RGBLIGHT_SPLIT) && !defined(RGBLIGHT_SPLIT_NO_ANIMATION_SYNC)
            anim->pos = 0;
#    endif
        } else {
            pos -= RGBLIGHT_EFFECT_SNAKE_INCREMENT;
#    if defined(RGBLIGHT_SPLIT) && !defined(RGBLIGHT_SPLIT_NO_ANIMATION_SYNC)
            anim->pos = 1;
#    endif
        }
    } else {
        pos = (pos + RGBLIGHT_EFFECT_SNAKE_INCREMENT) % rgblight_ranges.effect_num_leds;
#    if defined(RGBLIGHT_SPLIT) && !defined(RGBLIGHT_SPLIT_NO_ANIMATION_SYNC)
        anim->pos = pos;
#    endif
    }
}
#endif

#ifdef RGBLIGHT_EFFECT_KNIGHT
__attribute__((weak)) const uint8_t RGBLED_KNIGHT_INTERVALS[] PROGMEM = {127, 63, 31};

void rgblight_effect_knight(animation_status_t *anim) {
    static int8_t low_bound  = 0;
    static int8_t high_bound = RGBLIGHT_EFFECT_KNIGHT_LENGTH - 1;
    static int8_t increment  = RGBLIGHT_EFFECT_KNIGHT_INCREMENT;
    uint8_t       i, cur;

#    if defined(RGBLIGHT_SPLIT) && !defined(RGBLIGHT_SPLIT_NO_ANIMATION_SYNC)
    if (anim->pos == 0) { // restart signal
        anim->pos  = 1;
        low_bound  = 0;
        high_bound = RGBLIGHT_EFFECT_KNIGHT_LENGTH - 1;
        increment  = 1;
    }
#    endif
    // Set all the LEDs to 0
    for (i = rgblight_ranges.effect_start_pos; i < rgblight_ranges.effect_end_pos; i++) {
        led[i].r = 0;
        led[i].g = 0;
        led[i].b = 0;
#    ifdef RGBW
        led[i].w = 0;
#    endif
    }
    // Determine which LEDs should be lit up
    for (i = 0; i < RGBLIGHT_EFFECT_KNIGHT_LED_NUM; i++) {
        cur = (i + RGBLIGHT_EFFECT_KNIGHT_OFFSET) % rgblight_ranges.effect_num_leds + rgblight_ranges.effect_start_pos;

        if (i >= low_bound && i <= high_bound) {
            sethsv(rgblight_config.hue, rgblight_config.sat, rgblight_config.val, (rgb_led_t *)&led[cur]);
        } else {
            led[cur].r = 0;
            led[cur].g = 0;
            led[cur].b = 0;
#    ifdef RGBW
            led[cur].w = 0;
#    endif
        }
    }
    rgblight_set();

    // Move from low_bound to high_bound changing the direction we increment each
    // time a boundary is hit.
    low_bound += increment;
    high_bound += increment;

    if (high_bound <= 0 || low_bound >= RGBLIGHT_EFFECT_KNIGHT_LED_NUM - 1) {
        increment = -increment;
#    if defined(RGBLIGHT_SPLIT) && !defined(RGBLIGHT_SPLIT_NO_ANIMATION_SYNC)
        if (increment == 1) {
            anim->pos = 0;
        }
#    endif
    }
}
#endif

#ifdef RGBLIGHT_EFFECT_CHRISTMAS
#    define CUBED(x) ((x) * (x) * (x))

/**
 * Christmas lights effect, with a smooth animation between red & green.
 */
void rgblight_effect_christmas(animation_status_t *anim) {
    static int8_t increment = 1;
    const uint8_t max_pos   = 32;
    const uint8_t hue_green = 85;

    uint32_t xa;
    uint8_t  hue, val;
    uint8_t  i;

    // The effect works by animating anim->pos from 0 to 32 and back to 0.
    // The pos is used in a cubic bezier formula to ease-in-out between red and green, leaving the interpolated colors visible as short as possible.
    xa  = CUBED((uint32_t)anim->pos);
    hue = ((uint32_t)hue_green) * xa / (xa + CUBED((uint32_t)(max_pos - anim->pos)));
    // Additionally, these interpolated colors get shown with a slightly darker value, to make them less prominent than the main colors.
    val = 255 - (3 * (hue < hue_green / 2 ? hue : hue_green - hue) / 2);

    for (i = 0; i < rgblight_ranges.effect_num_leds; i++) {
        uint8_t local_hue = (i / RGBLIGHT_EFFECT_CHRISTMAS_STEP) % 2 ? hue : hue_green - hue;
        sethsv(local_hue, rgblight_config.sat, val, (rgb_led_t *)&led[i + rgblight_ranges.effect_start_pos]);
    }
    rgblight_set();

    if (anim->pos == 0) {
        increment = 1;
    } else if (anim->pos == max_pos) {
        increment = -1;
    }
    anim->pos += increment;
}
#endif

#ifdef RGBLIGHT_EFFECT_RGB_TEST
__attribute__((weak)) const uint16_t RGBLED_RGBTEST_INTERVALS[] PROGMEM = {1024};

void rgblight_effect_rgbtest(animation_status_t *anim) {
    static uint8_t maxval = 0;
    uint8_t        g;
    uint8_t        r;
    uint8_t        b;

    if (maxval == 0) {
        rgb_led_t tmp_led;
        sethsv(0, 255, RGBLIGHT_LIMIT_VAL, &tmp_led);
        maxval = tmp_led.r;
    }
    g = r = b = 0;
    switch (anim->pos) {
        case 0:
            r = maxval;
            break;
        case 1:
            g = maxval;
            break;
        case 2:
            b = maxval;
            break;
    }
    rgblight_setrgb(r, g, b);
    anim->pos = (anim->pos + 1) % 3;
}
#endif

#ifdef RGBLIGHT_EFFECT_ALTERNATING
void rgblight_effect_alternating(animation_status_t *anim) {
    for (int i = 0; i < rgblight_ranges.effect_num_leds; i++) {
        rgb_led_t *ledp = led + i + rgblight_ranges.effect_start_pos;
        if (i < rgblight_ranges.effect_num_leds / 2 && anim->pos) {
            sethsv(rgblight_config.hue, rgblight_config.sat, rgblight_config.val, ledp);
        } else if (i >= rgblight_ranges.effect_num_leds / 2 && !anim->pos) {
            sethsv(rgblight_config.hue, rgblight_config.sat, rgblight_config.val, ledp);
        } else {
            sethsv(rgblight_config.hue, rgblight_config.sat, 0, ledp);
        }
    }
    rgblight_set();
    anim->pos = (anim->pos + 1) % 2;
}
#endif

#ifdef RGBLIGHT_EFFECT_TWINKLE
__attribute__((weak)) const uint8_t RGBLED_TWINKLE_INTERVALS[] PROGMEM = {30, 15, 5};

typedef struct PACKED {
    HSV     hsv;
    uint8_t life;
    uint8_t max_life;
} TwinkleState;

static TwinkleState led_twinkle_state[RGBLIGHT_LED_COUNT];

void rgblight_effect_twinkle(animation_status_t *anim) {
    const bool random_color = anim->delta / 3;
    const bool restart      = anim->pos == 0;
    anim->pos               = 1;

    const uint8_t bottom = breathe_calc(0);
    const uint8_t top    = breathe_calc(127);

    uint8_t frac(uint8_t n, uint8_t d) {
        return (uint16_t)255 * n / d;
    }
    uint8_t scale(uint16_t v, uint8_t scale) {
        return (v * scale) >> 8;
    }

    const uint8_t trigger = scale((uint16_t)0xFF * RGBLIGHT_EFFECT_TWINKLE_PROBABILITY, 127 + rgblight_config.val / 2);

    for (uint8_t i = 0; i < rgblight_ranges.effect_num_leds; i++) {
        TwinkleState *t = &(led_twinkle_state[i]);
        HSV *         c = &(t->hsv);

        if (!random_color) {
            c->h = rgblight_config.hue;
            c->s = rgblight_config.sat;
        }

        if (restart) {
            // Restart
            t->life = 0;
            c->v    = 0;
        } else if (t->life) {
            // This LED is already on, either brightening or dimming
            t->life--;
            uint8_t unscaled = frac(breathe_calc(frac(t->life, t->max_life)) - bottom, top - bottom);
            c->v             = scale(rgblight_config.val, unscaled);
        } else if ((rand() % 0xFF) < trigger) {
            // This LED is off, but was randomly selected to start brightening
            if (random_color) {
                c->h = rand() % 0xFF;
                uint8_t sat_half = rgblight_config.sat / 2;
                if (sat_half == 0) {
                    sat_half = 1;  // V251121R3: 포화도 0에서도 0으로 나누지 않도록 최소값 보정
                }
                c->s = (rand() % sat_half) + sat_half;
            }
            c->v        = 0;
            t->max_life = MAX(20, MIN(RGBLIGHT_EFFECT_TWINKLE_LIFE, rgblight_config.val));
            t->life     = t->max_life;
        } else {
            // This LED is off, and was NOT selected to start brightening
        }

        rgb_led_t *ledp = led + i + rgblight_ranges.effect_start_pos;
        sethsv(c->h, c->s, c->v, ledp);
    }

    rgblight_set();
}
#endif

void preprocess_rgblight(bool pressed, uint8_t row, uint8_t col) {
    rgblight_effect_pulse_handle_keyevent(pressed, row, col);  // V251018R5: Pulse 계열 입력 상태 갱신

    if (!pressed) {
        return;
    }
#ifdef VELOCIKEY_ENABLE
    if (rgblight_velocikey_enabled()) {
        rgblight_velocikey_accelerate();
    }
#endif
}

static void rgblight_consume_host_led_queue(void)
{
    if (!rgblight_indicator_supported) {
        rgblight_host_led_pending = false;  // V251120R1: 인디케이터 미지원 보드는 큐를 비우고 종료
        return;
    }

    if (!rgblight_host_led_pending) {
        return;
    }

    led_t pending = {.raw = rgblight_host_led_raw_buffer};
    rgblight_host_led_pending = false;
    rgblight_indicator_apply_host_led(pending);  // V251018R1: 큐에 적재된 상태를 메인 루프에서 처리
}

static void rgblight_flush_render_queue(void)
{
    if (!rgblight_render_pending || !is_rgblight_initialized) {
        return;
    }

    rgblight_render_pending = false;
    rgblight_render_frame();  // V251018R6: 예약된 프레임을 주 루프에서만 전송
}

void rgblight_task(void) {
    bool urgent_pending = rgblight_render_pending || rgblight_host_led_pending;
    if (!urgent_pending) {
        // V251122R6: 캐시 연동 게이트를 제거하고 단순 1ms 슬라이스로 복원해 스톨 리스크 해소
        static uint16_t rgblight_next_run = 0;  // V251121R5: 1kHz 슬라이스로 rgblight_task 호출 희박화
        uint16_t       now               = sync_timer_read();

        if (rgblight_next_run == 0) {
            rgblight_next_run = now;  // V251121R5: 초기 호출은 즉시 통과
        }

        if (!timer_expired(now, rgblight_next_run)) {
            return;  // V251121R5: 우선 이벤트가 없고 주기 전이면 조기 반환
        }

        rgblight_next_run = now + 1;  // V251121R5: 약 1ms 간격으로 평가
    }

    rgblight_consume_host_led_queue();
#ifdef RGBLIGHT_USE_TIMER
    rgblight_timer_task();
#endif
    rgblight_flush_render_queue();

#ifdef VELOCIKEY_ENABLE
    if (rgblight_velocikey_enabled()) {
        rgblight_velocikey_decelerate();
    }
#endif
}

#ifdef VELOCIKEY_ENABLE
#    define TYPING_SPEED_MAX_VALUE 200

static uint8_t typing_speed = 0;

bool rgblight_velocikey_enabled(void) {
    return rgblight_config.velocikey;
}

void rgblight_velocikey_toggle(void) {
    dprintf("rgblight velocikey toggle [EEPROM]: rgblight_config.velocikey = %u\n", !rgblight_config.velocikey);
    rgblight_config.velocikey = !rgblight_config.velocikey;
    eeconfig_update_rgblight_current();
}

void rgblight_velocikey_accelerate(void) {
    if (typing_speed < TYPING_SPEED_MAX_VALUE) typing_speed += (TYPING_SPEED_MAX_VALUE / 100);
}

void rgblight_velocikey_decelerate(void) {
    static uint16_t decay_timer = 0;

    if (timer_elapsed(decay_timer) > 500 || decay_timer == 0) {
        if (typing_speed > 0) typing_speed -= 1;
        // Decay a little faster at half of max speed
        if (typing_speed > TYPING_SPEED_MAX_VALUE / 2) typing_speed -= 1;
        // Decay even faster at 3/4 of max speed
        if (typing_speed > TYPING_SPEED_MAX_VALUE / 4 * 3) typing_speed -= 2;
        decay_timer = timer_read();
    }
}

uint8_t rgblight_velocikey_match_speed(uint8_t minValue, uint8_t maxValue) {
    return MAX(minValue, maxValue - (maxValue - minValue) * ((float)typing_speed / TYPING_SPEED_MAX_VALUE));
}

#endif
