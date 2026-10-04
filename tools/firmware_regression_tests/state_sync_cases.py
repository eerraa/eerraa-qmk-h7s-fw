"""Production GET-visible storage and core RGB publication boundaries."""
from pathlib import Path
from source_cases import function


def rgb_publication_support(source: str, callback: str = 'static void rgblight_note_config_change(void) {}') -> str:
    if 'static void rgblight_publish_config(void)' not in source:
        return ''
    return '#ifndef RGBLIGHT_LIMIT_VAL\n#define RGBLIGHT_LIMIT_VAL 255\n#endif\nstatic uint64_t rgblight_published_config;\n' + callback + '\n' + function(source, 'rgblight_publish_config')


def storage(root: Path, build: Path, source_root: Path | None = None) -> Path:
    selected = source_root or root
    source = (selected/'src/ap/modules/qmk/quantum/dynamic_keymap.c').read_text(encoding='utf-8')
    via = (selected/'src/ap/modules/qmk/quantum/via.c').read_text(encoding='utf-8')
    prefix = r"""#define main chain_existing_main
#define eeconfig_init_quantum_checked chain_defaults_adapter
#include "test_eeprom_chain.c"
#undef eeconfig_init_quantum_checked
#undef main
#define VIA_ENABLE
#define VIA_EEPROM_LAYOUT_OPTIONS_ADDR 224U
#ifndef VIA_EEPROM_LAYOUT_OPTIONS_SIZE
#define VIA_EEPROM_LAYOUT_OPTIONS_SIZE 4U
#endif
static bool layout_options_staged;
static uint32_t layout_options_before_reset;
static uint32_t config_revision, layout_callback;
static void era_state_sync_bump_config(void) { config_revision++; }
static void via_set_layout_options_kb(uint32_t value) { layout_callback=value; }
void via_eeprom_note_change(uint32_t address, uint32_t length);
#define ENCODER_MAP_ENABLE
#define DYNAMIC_KEYMAP_EEPROM_ADDR 256U
#define DYNAMIC_KEYMAP_LAYER_COUNT 2
#define MATRIX_ROWS 2
#define MATRIX_COLS 2
#define NUM_ENCODERS 1
#define DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR 272U
#define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR 1024U
#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE 2048U
#define KC_NO 0U
static uint16_t keycode_at_keymap_location_raw(uint8_t layer, uint8_t row, uint8_t col) { return 0x100U + layer*4U + row*2U + col; }
static uint16_t keycode_at_encodermap_location_raw(uint8_t layer, uint8_t id, bool clockwise) { return 0x200U + layer*2U + id + !clockwise; }
"""
    names = ['dynamic_keymap_get_keycode','dynamic_keymap_set_keycode',
             'dynamic_keymap_get_encoder','dynamic_keymap_set_encoder','dynamic_keymap_reset',
             'dynamic_keymap_get_buffer','dynamic_keymap_set_buffer',
             'dynamic_keymap_macro_get_buffer','dynamic_keymap_macro_set_buffer_checked','dynamic_keymap_macro_reset_checked']
    body = source[source.index('void *dynamic_keymap_key_to_eeprom_address('):source.index('uint16_t dynamic_keymap_get_keycode(')]
    body += source[source.index('void *dynamic_keymap_encoder_to_eeprom_address('):source.index('uint16_t dynamic_keymap_get_encoder(')]
    body += '\n'.join(function(source, name) for name in names)
    observer = 'eeprom_note_change' if 'void eeprom_note_change(' in source else 'eeprom_note_commit'
    if 'void via_eeprom_note_change(' in via:
        body = function(via,'via_eeprom_note_change') + '\n' + body
    else:
        body = 'void via_eeprom_note_change(uint32_t address,uint32_t length) { (void)address; (void)length; }\n' + body
    body += '\n' + '\n'.join(function(via,n) for n in ['via_get_layout_options','via_store_layout_options','via_set_layout_options','eeconfig_publish_via_defaults'])
    body += '\n' + function(source, observer)
    body += '\nbool eeconfig_init_quantum_checked(void) { dynamic_keymap_reset(); return true; }\n'
    out = build/'test_state_sync_storage.c'
    out.write_text(prefix + body + '\n#include "test_state_sync_storage.h"\n', encoding='utf-8')
    return out


def rgb(root: Path, build: Path, source_root: Path | None = None) -> Path:
    from rgb_frame_cases import generate
    selected = source_root or root
    source = (selected/'src/ap/modules/qmk/quantum/rgblight/rgblight.c').read_text(encoding='utf-8')
    out = generate(root, build, source_root=selected)
    text = out.read_text(encoding='utf-8')
    text = text.replace('#include "test_rgb_frame_assertions.h"', '#define main frame_existing_main\n#include "test_rgb_frame_assertions.h"\n#undef main')
    text = text.replace('#define RGBLIGHT_ENABLE', '#define RGBLIGHT_ENABLE\n#define VELOCIKEY_ENABLE')
    text = text.replace('typedef int animation_status_t;', 'typedef struct { bool restart; } animation_status_t;\nstatic animation_status_t animation_status;\nstatic uint16_t timer_read(void) { return (uint16_t)(mock_now_ns / 1000000U); }\nstatic uint16_t timer_elapsed(uint16_t before) { return timer_read()-before; }\nstatic uint8_t typing_speed;\n#define TYPING_SPEED_MAX_VALUE 200\nstatic bool rgblight_eeprom_dirty;\nstatic uint16_t rgblight_eeprom_timer;\nstatic uint64_t stored_rgb;\nuint64_t eeconfig_read_rgblight(void) { return stored_rgb; }\nstatic bool is_static_effect(uint8_t mode) { return mode == 1U; }\nstatic void rgblight_timer_init(void) {}\nstatic void rgblight_timer_disable(void) {}\nstatic void rgblight_timer_enable(void) {}\n#define RGBLIGHT_MODES 46\n#define RGBLIGHT_DEFAULT_ON 1\n#define RGBLIGHT_DEFAULT_MODE 1\n#define RGBLIGHT_DEFAULT_HUE 0\n#define RGBLIGHT_DEFAULT_SAT 255\n#define RGBLIGHT_DEFAULT_VAL RGBLIGHT_LIMIT_VAL\n#define RGBLIGHT_DEFAULT_SPD 15\n')
    names = ['rgblight_velocikey_enabled','rgblight_velocikey_accelerate','rgblight_velocikey_decelerate',
             'rgblight_velocikey_set','rgblight_velocikey_toggle','eeconfig_update_rgblight_current',
             'rgblight_check_config','eeconfig_update_rgblight_default','rgblight_init','rgblight_reload_from_eeprom',
             'rgblight_update_qword','rgblight_mode_eeprom_helper','rgblight_mode','rgblight_mode_noeeprom',
             'rgblight_enable','rgblight_enable_noeeprom','rgblight_disable','rgblight_disable_noeeprom',
             'rgblight_increase_speed_helper','rgblight_decrease_speed_helper',
             'rgblight_set_speed_eeprom_helper','rgblight_set_speed_noeeprom',
             'rgblight_get_mode','rgblight_get_speed','rgblight_get_hue','rgblight_get_sat','rgblight_get_val']
    bodies = [function(source,n) for n in names]
    prototypes = '\n'.join(f[:f.index('{')].strip()+';' for f in bodies)
    text = text.replace('static void rgblight_note_config_change(void) {}', 'void rgblight_note_config_change(void);')
    if 'rgblight_mode_transition_sat' in source:
        bodies.insert(0, function(source, 'rgblight_mode_transition_sat'))
        bodies.insert(0, function(source, 'rgblight_mode_carries_hue'))
        prototypes += '\n' + '\n'.join(f[:f.index('{')].strip()+';' for f in bodies[:2])
    text = text.replace('typedef struct { bool restart; } animation_status_t;', prototypes+'\n#define RGBLIGHT_SPLIT_SET_CHANGE_MODE\n#define RGBLIGHT_SPLIT_SET_CHANGE_MODEHSVS\nstatic void eeconfig_debug_rgblight(void) {}\ntypedef struct { bool restart; } animation_status_t;')
    text += '\n'+'\n'.join(bodies)+'\n#include "test_state_sync_rgb.h"\n'
    target = build/'test_state_sync_rgb.c'
    target.write_text(text, encoding='utf-8')
    return target
