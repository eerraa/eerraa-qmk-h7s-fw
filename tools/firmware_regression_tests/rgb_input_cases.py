"""실제 matrix/QMK TD/LT/RGB 소스 구간을 한 호스트 fixture로 연결한다."""
import re
from pathlib import Path


def function(source, name):
    match = re.search(rf'^(?:static\s+)?(?:inline\s+)?(?:void|bool|uint\d+_t)\s+{name}\([^;]*?\)\s*\{{', source, re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'


def generate(root: Path, build: Path, source_root: Path | None = None) -> Path:
    qmk = root / 'src/ap/modules/qmk'
    selected = (source_root or root) / 'src/ap/modules/qmk'
    read = lambda path: path.read_bytes().decode('utf-8').replace('\r\n', '\n')
    keyboard = read(selected / 'quantum/keyboard.c')
    quantum = read(selected / 'quantum/quantum.c')
    action = read(selected / 'quantum/action.c')
    tapping = read(selected / 'quantum/action_tapping.c')
    td = read(selected / 'port/tapdance.c')
    dance = read(selected / 'quantum/process_keycode/process_tap_dance.c')
    rgb = read(selected / 'quantum/rgblight/rgblight.c')
    event_header = read(qmk / 'quantum/keyboard.h')
    action_header = read(qmk / 'quantum/action.h')
    wait_port = read(selected / 'port/platforms/wait.c')
    host = read(selected / 'port/protocol/host.c')
    td_header = read(qmk / 'quantum/process_keycode/process_tap_dance.h')
    chunks = [
        '#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n',
        '#define NO_ACTION_ONESHOT\n#define NO_RESET\n#define NO_DEBUG\n#define DYNAMIC_TAPPING_TERM_ENABLE\n',
        '#include "keycodes.h"\n#include "action_code.h"\n',
        event_header[event_header.index('/* key matrix position */'):event_header.index('/* Common keypos_t object factory */')],
        action_header[action_header.index('#ifndef TAP_CODE_DELAY'):action_header.index('/* Execute action per keyevent */')],
        '#include "action_tapping.h"\n#include "tapping_term_policy.h"\n',
        td_header[td_header.index('typedef struct'):td_header.index('#define ACTION_TAP_DANCE_DOUBLE')],
        '#include "test_rgb_input_support.h"\n',
        function(wait_port, 'wait_ms'),
        function(host, 'host_keyboard_delay'),
        function(action, 'tap_code_wait'),
        action[action.index('__attribute__((weak)) void tap_code_delay('):action.index('__attribute__((weak)) void register_mods(')],
        quantum[quantum.index('__attribute__((weak)) void tap_code16_delay('):quantum.index('__attribute__((weak)) bool pre_process_record_kb(')],
        function(rgb, 'rgblight_velocikey_enabled'),
        function(rgb, 'rgblight_velocikey_accelerate'),
        function(rgb, 'rgblight_velocikey_decelerate'),
        rgb[rgb.index('#if defined(RGBLIGHT_EFFECT_PULSE_ON_PRESS)'):rgb.index('static volatile bool    rgblight_host_led_pending')],
        rgb[rgb.index('static volatile bool    rgblight_host_led_pending'):rgb.index('static uint8_t rgblight_mode_transition_sat')],
    ]
    chunks += [function(rgb, name) for name in (
        'rgblight_request_render', 'rgblight_indicator_restore_pulse_effect',
        'rgblight_sethsv_eeprom_helper',  # V260913R1: 실제 설정 커밋 경로. 커밋 뒤 RGB task가 그려야 한다.
        'rgblight_indicator_post_host_event',
        'rgblight_handle_physical_key' if 'rgblight_handle_physical_key' in rgb else 'preprocess_rgblight',
        'rgblight_consume_host_led_queue', 'rgblight_flush_render_queue')]
    chunks += [rgb[rgb.index('static uint32_t rgblight_task_next_run;'):rgb.index('#ifdef VELOCIKEY_ENABLE\n#    define TYPING_SPEED_MAX_VALUE')]]
    chunks += [
        td[td.index('#define TAPDANCE_SIGNATURE'):td.index('_Static_assert')],
        'static bool tapdance_keycode_is_valid(uint16_t keycode);\n',
        'static void tapdance_load_entry(uint8_t slot_index, tapdance_entry_t *entry);\n',
        'static uint8_t tapdance_step(const tap_dance_state_t *state);\n',
    ]
    chunks += [function(td, name) for name in (
        'tapdance_get_term_ms', 'tapdance_keycode_is_valid', 'tapdance_load_entry', 'tapdance_step',
        'tapdance_should_finish_immediate', 'tapdance_register_keycode', 'tapdance_unregister_keycode',
        'tapdance_tap_width_ms', 'tapdance_tap_keycode', 'tapdance_set_runtime', 'tapdance_on_each_tap',
        'tapdance_on_dance_finished', 'tapdance_on_reset')]
    chunks += [dance[dance.index('static uint16_t active_td;'):]]
    chunks += [function(action, name) for name in (
        'is_tap_action', 'is_tap_record', 'process_record_tap_hint', 'process_action',
        'process_record_handler', 'process_record', 'action_exec')]
    chunks += [function(quantum, name) for name in (
        'pre_process_record_quantum', 'post_process_record_quantum', 'process_record_quantum')]
    chunks += [tapping[tapping.index('#ifndef NO_ACTION_TAPPING'):]]
    chunks += [function(keyboard, name) for name in ('switch_events', 'generate_tick_event', 'matrix_task')]
    chunks += ['#include "test_rgb_input_assertions.h"\n']
    output = build / 'test_rgb_physical_input.c'
    output.write_bytes('\n'.join(chunks).encode('utf-8'))
    return output
