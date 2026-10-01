"""실제 matrix/QMK TD/LT/RGB 소스 구간을 한 호스트 fixture로 연결한다."""
import re
from pathlib import Path


def function(source, name):
    match = re.search(rf'^(?:__attribute__\(\(weak\)\)\s+)?(?:static\s+)?(?:inline\s+)?(?:void|bool|layer_state_t|uint\d+_t)\s+{name}\([^;]*?\)\s*\{{', source, re.M)
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
    td_header = read(selected / 'quantum/process_keycode/process_tap_dance.h')
    action_header = read(selected / 'quantum/action.h')
    util = read(selected / 'quantum/action_util.c')
    util_header = read(selected / 'quantum/action_util.h')
    layers = read(selected / 'quantum/action_layer.c')
    report = read(selected / 'port/protocol/report.c')
    suspend = read(selected / 'port/platforms/suspend.c')
    report_header = read(selected / 'port/protocol/report.h')
    strip_includes = lambda text: re.sub(r'^\s*#\s*include[^\n]*\n', '', text, flags=re.M)
    report_types = report_header[report_header.index('typedef struct {'):report_header.index('} PACKED report_nkro_t;') + len('} PACKED report_nkro_t;')]
    capacity = re.search(r'^#define HW_KEYS_PRESS_MAX[^\n]*', read((source_root or root) / 'src/hw/hw_caps_keys.h'), re.M).group(0)
    layer_part = layers[layers.index('layer_state_t layer_state ='):layers.index('/** \\brief Layer debug printing')]
    # Matrix/USB/EEPROM are adapters. Resource ownership and report construction are not.

    rgb_header = read(selected / 'quantum/rgblight/rgblight.h')
    led_header = read(selected / 'quantum/led.h')
    led_type = led_header[led_header.index('typedef union {'):led_header.index('} led_t;') + len('} led_t;')]
    indicator_types = rgb_header[rgb_header.index('enum rgblight_indicator_target'):rgb_header.index('void rgblight_indicator_update_config(')]
    indicator_state = re.search(r'typedef struct \{\s*rgblight_indicator_config_t[\s\S]*?\} rgblight_indicator_state_t;', rgb)[0]
    indicator_functions = [function(rgb, name) for name in (
        'rgblight_indicator_slot_valid', 'rgblight_indicator_target_active_default',
        'rgblight_indicator_target_active', 'rgblight_indicator_should_enable',
        'rgblight_indicator_any_active', 'rgblight_indicator_commit_state',
        'rgblight_indicator_apply_host_led')]
    indicator_declarations = '\n'.join(f[:f.index('{')].rstrip() + ';' for f in indicator_functions)

    chunks = [
        '#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n',
        '#define MATRIX_ROWS 5\n#define MATRIX_COLS 15\n#define PACKED __attribute__((packed))\n#define TAPDANCE_ENABLE\n#define NO_RESET\n#define NO_DEBUG\n#define DYNAMIC_TAPPING_TERM_ENABLE\n#define EXTRAKEY_ENABLE\n#define MOUSEKEY_ENABLE\n'
        # Production boards compile these per-key policies through G_TERM_ENABLE.
        '#define RETRO_TAPPING_PER_KEY\n#define PERMISSIVE_HOLD_PER_KEY\n#define HOLD_ON_OTHER_KEY_PRESS_PER_KEY\n',
        '#include "keycode.h"\n#include "action_code.h"\n',
        read(qmk / 'quantum/quantum_keycodes.h').split('#define SAFE_RANGE')[0].split('// clang-format off')[1],
        event_header[event_header.index('/* key matrix position */'):event_header.index('/* Common keypos_t object factory */')],
        action_header[action_header.index('#ifndef TAP_CODE_DELAY'):action_header.index('/* Execute action per keyevent */')],
        '#include "action_tapping.h"\n#include "tapping_term_policy.h"\n',
        td_header[td_header.index('#ifdef TAPDANCE_ENABLE'):td_header.index('#define ACTION_TAP_DANCE_DOUBLE')],
        capacity + '\n#define KEYBOARD_REPORT_KEYS HW_KEYS_PRESS_MAX\n#define NKRO_REPORT_BITS 30\n',
        next(line for line in td_header.splitlines() if line.startswith('#define TD_INDEX')) + '\n',
        read(qmk / 'quantum/keycode_config.h').split('typedef union {', 1)[1].split('} keymap_config_t;', 1)[0].join(('typedef union {', '} keymap_config_t;')),
        report_types,
        report_header[report_header.index('enum consumer_usages {'):report_header.index('};', report_header.index('enum desktop_usages {')) + 2],
        function(report_header, 'KEYCODE2SYSTEM'), function(report_header, 'KEYCODE2CONSUMER'),
        'void add_key_to_report(uint8_t); void del_key_from_report(uint8_t); void clear_keys_from_report(void); uint8_t has_anykey(void); bool is_key_pressed(uint8_t);\n',
        strip_includes(util_header).replace('#pragma once', ''),
        led_type, 'typedef struct { uint8_t r, g, b; } rgb_led_t;\n',
        indicator_types, indicator_state,
        'static rgblight_indicator_state_t rgblight_indicator_state[RGBLIGHT_INDICATOR_SLOT_COUNT];\n',
        '#define indicator_on (rgblight_indicator_state[0].active)\n',
        indicator_declarations,
        'static rgblight_indicator_target_callback_t rgblight_indicator_target_callback = rgblight_indicator_target_active_default;\n',
        '#include "test_rgb_input_support.h"\n',
        *indicator_functions,
        strip_includes(util[util.index('static uint8_t real_mods'):]),
        report[report.index('#ifdef RING_BUFFERED_6KRO_REPORT_ENABLE'):report.index('#ifdef MOUSE_ENABLE')],
        'layer_state_t default_layer_state;\n',
        function(layers, 'default_layer_state_set_user'),
        function(layers, 'default_layer_state_set_kb'),
        function(layers, 'default_layer_state_set'),
        *[function(layers, name) for name in ('default_layer_set', 'default_layer_or', 'default_layer_and', 'default_layer_xor')],
        strip_includes(layer_part),
        function(layers, "update_tri_layer_state"),
        function(layers, "update_tri_layer"),
        *[function(action, name) for name in ('register_mouse', 'send_system_usage', 'send_consumer_usage')],
        *[function(action, name) for name in ('register_code', 'unregister_code', 'register_mods', 'unregister_mods', 'register_weak_mods', 'unregister_weak_mods', 'clear_keyboard', 'clear_keyboard_but_mods', 'clear_keyboard_but_mods_and_keys')],
        *[function(quantum, name) for name in ('extract_mod_bits', 'do_code16', 'register_code16', 'unregister_code16')],
        function(quantum, 'translate_kb_to_tap_dance'),
        function(quantum, 'get_record_keycode'),
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
        'rgblight_set_output_suspend_state',
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
        'tapdance_should_finish_immediate', 'tapdance_run_action', 'tapdance_register_keycode', 'tapdance_unregister_keycode',
        'tapdance_tap_width_ms', 'tapdance_tap_keycode', 'tapdance_set_runtime', 'tapdance_on_each_tap',
        'tapdance_on_dance_finished', *(('tapdance_other_holds_action',) if 'static bool tapdance_other_holds_action(' in td else ()), 'tapdance_on_reset', 'tapdance_is_storage_valid', 'tapdance_apply_defaults_locked', 'tapdance_sync_state_from_storage', 'tapdance_init',
        'tapdance_storage_apply_defaults')]
    # The product links quantum.c's strong VIA-alias remap, not the core's weak identity.
    weak_remap = '__attribute__((weak)) uint16_t tap_dance_remap_keycode(uint16_t keycode) {\n    return keycode;\n}\n'
    core = dance[dance.index('static tap_dance_state_t *active_td;'):]
    assert core.count(weak_remap) == 1
    chunks += [core.replace(weak_remap, function(quantum, 'tap_dance_remap_keycode'))]
    chunks += [function(action, name) for name in (
        'is_tap_action', 'is_tap_record', 'process_record_tap_hint', 'process_action',
        'process_record_handler', 'process_record', 'action_exec')]
    chunks += [function(quantum, name) for name in (
        'pre_process_record_quantum', 'post_process_record_quantum', 'process_record_quantum')]
    chunks += [tapping[tapping.index('#ifndef NO_ACTION_TAPPING'):]]
    # The product's wakeup-key rule: a key pressed while the host sleeps is never typed.
    chunks += [re.search(r'^static matrix_row_t wakeup_matrix\[MATRIX_ROWS\];$', suspend, re.M).group(0) + '\n']
    chunks += [function(suspend, name) for name in (
        'suspend_wakeup_key_event', 'keypress_is_wakeup_key', 'wakeup_matrix_handle_key_event')]
    chunks += [function(keyboard, name) for name in ('switch_events', 'generate_tick_event', 'matrix_task')]
    chunks += ['#include "test_rgb_input_assertions.h"\n']
    output = build / 'test_rgb_physical_input.c'
    output.write_bytes('\n'.join(chunks).encode('utf-8'))
    return output
