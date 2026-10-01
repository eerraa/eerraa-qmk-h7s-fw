'실제 H7S 합성기/호스트 mailbox/Pulse와 전체 WS2812 드라이버를 연결한다.'
from pathlib import Path
import re

from rgb_modes import pulse_defines


def function(source: str, name: str) -> str:
    pattern = rf'^(?:(?:static|inline)\s+)*(?:void|bool|uint\d+_t|rgb_led_t|rgblight_indicator_range_t)\s+{name}\([^;]*?\)\s*\{{'
    m = re.search(pattern, source, re.M)
    assert m, name
    end, depth = m.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[m.start():end] + '\n'


def generate(root: Path, build: Path, board: str = 'brick60', source_root: Path | None = None) -> Path:
    selected = source_root or root
    qmk = selected/'src/ap/modules/qmk'
    read = lambda p: p.read_text(encoding='utf-8')
    rgb = read(qmk/'quantum/rgblight/rgblight.c')
    rh = read(qmk/'quantum/rgblight/rgblight.h')
    ch = read(qmk/'quantum/color.h')
    dh = read(qmk/'quantum/rgblight/rgblight_drivers.h')
    config = read(qmk/f'keyboards/era/sirind/{board}/config.h')
    port = read(qmk/f'keyboards/era/sirind/{board}/port/indicator_port.c')
    driver = read(qmk/f'keyboards/era/sirind/{board}/port/driver/rgblight_drivers.c')
    macros = '\n'.join(re.findall(r'^#define\s+(?:HW_WS2812_\w+|RGBLIGHT_INDICATOR_SLOT_COUNT|MATRIX_ROWS|MATRIX_COLS)\s+[^\n]+', config, re.M))
    pre = '''#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <string.h>
#define PACKED __attribute__((packed))
#define RGBLIGHT_ENABLE
#define RGBLIGHT_SLEEP
#define RGBLIGHT_USE_TIMER
#define INDICATOR_ENABLE
#define RGBLIGHT_LIMIT_VAL 200
#define RGBLIGHT_MODE_STATIC_LIGHT 1
#define RGBLIGHT_EFFECT_PULSE_ON_PRESS
#define RGBLIGHT_EFFECT_PULSE_OFF_PRESS
#define RGBLIGHT_EFFECT_PULSE_ON_PRESS_HOLD
#define RGBLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD
#define dprintf(...) ((void)0)
''' + pulse_defines(qmk)
    constants = rh[rh.index('#ifndef RGBLIGHT_EFFECT_PULSE_DURATION_MIN_MS'):rh.index('#ifndef RGBLIGHT_SAT_STEP')]
    indicator_types = rh[rh.index('enum rgblight_indicator_target'):rh.index('#ifdef RGBLIGHT_LAYERS', rh.index('enum rgblight_indicator_target'))]
    runtime_types = rh[rh.index('typedef union {\n    uint64_t raw;'):rh.index('/* === Utility Functions ===*/')]
    colors = ch[ch.index('#define WS2812_BYTE_ORDER_RGB'):]
    driver_type = dh[dh.index('typedef struct {'):]
    declarations = rgb[rgb.index('#define RGBLIGHT_INDICATOR_RANGE_TABLE_LENGTH'):rgb.index('#if defined(RGBLIGHT_EFFECT_PULSE_ON_PRESS)')]
    pulse = rgb[rgb.index('#if defined(RGBLIGHT_EFFECT_PULSE_ON_PRESS)'):rgb.index('static volatile bool    rgblight_host_led_pending')]
    mailbox = '\n'.join(re.findall(r'^static (?:volatile )?(?:bool|uint8_t)\s+rgblight_(?:host_led_pending|host_led_raw_buffer|render_pending)[^\n]*', rgb, re.M))
    functions = [function(rgb, n) for n in (
        'rgblight_request_render','rgblight_indicator_sanitize_range','rgblight_indicator_target_active_default',
        'rgblight_indicator_slot_valid','rgblight_indicator_any_active','rgblight_indicator_any_pending_render',
        'rgblight_pulse_output_visible','rgblight_indicator_apply_target_range','rgblight_indicator_target_active',
        'rgblight_indicator_should_enable','rgblight_indicator_compute_color','rgblight_indicator_apply_overlay',
        'rgblight_indicator_restore_pulse_effect','rgblight_indicator_commit_state','rgblight_indicator_set_target_callback',
        'rgblight_indicator_set_ranges','rgblight_indicator_set_ranges_at','rgblight_indicator_update_config',
        'rgblight_indicator_update_config_at','rgblight_indicator_apply_host_led','rgblight_indicator_post_host_event',
        'rgblight_indicator_request_host_refresh','rgblight_indicator_set_render_callback',
        'rgblight_set_clipping_range','rgblight_set_effect_range','rgblight_setrgb','rgblight_sethsv_noeeprom_old',
        'rgblight_sethsv_eeprom_helper','rgblight_set_output_suspend_state','rgblight_render_frame','rgblight_set',
        'rgblight_handle_physical_key','rgblight_consume_host_led_queue','rgblight_flush_render_queue',
        'rgblight_task_periodic_due','rgblight_task')]
    prototypes = '\n'.join(f[:f.index('{')].rstrip()+';' for f in functions)
    support = '''
typedef int animation_status_t;
rgblight_config_t rgblight_config;
rgblight_status_t rgblight_status;
rgblight_ranges_t rgblight_ranges;
static bool output_suspended;
static bool is_rgblight_initialized = true;
static const bool rgblight_indicator_supported = true;
rgb_led_t led[RGBLIGHT_LED_COUNT];
static rgb_led_t rgblight_frame[RGBLIGHT_LED_COUNT];
static uint32_t rgblight_task_next_run;
static bool rgblight_task_slice_armed;
static uint8_t mode_base_table[47] = {[1]=1, [43]=43, [44]=44, [45]=45, [46]=46};
static uint32_t sync_timer_read32(void) { return (uint32_t)(mock_now_ns / 1000000U); }
static bool timer_expired32(uint32_t now, uint32_t due) { return (int32_t)(now-due)>=0; }
static void eeconfig_flush_rgblight_current(bool force) { (void)force; }
static void eeconfig_update_rgblight(uint64_t raw) { (void)raw; }
/* Colour conversion is an adapter; state/config, pixel composition and GRB encoding are production. */
RGB rgblight_hsv_to_rgb(HSV hsv) {
    return hsv.s == 0 ? (RGB){.r=hsv.v, .g=hsv.v, .b=hsv.v} : (RGB){.g=hsv.v};
}
static void sethsv(uint8_t h, uint8_t s, uint8_t v, rgb_led_t *out) {
    *out=rgblight_hsv_to_rgb((HSV){h,s,v>RGBLIGHT_LIMIT_VAL?RGBLIGHT_LIMIT_VAL:v});
}
static uint8_t host_bits;
static void host_keyboard_leds_update(uint8_t value) { host_bits=value; }
'''
    ports = function(port, 'usbHidSetStatusLed') + function(port, 'led_update_ports')
    if board == 'brick65':
        ports += re.search(r'enum\s*\{.*?\};', port, re.S)[0]+'\n'+function(port, 'indicator_render')
    chunks = [pre, macros, constants, '#define RGBLIGHT_LED_COUNT HW_WS2812_RGB_CNT\n',
              '#include "hw_def.h"\n', f'#include "{(selected/"src/hw/driver/ws2812.c").as_posix()}"\n',
              colors, f'#include "{(qmk/"quantum/led.h").as_posix()}"\n', driver_type, indicator_types, runtime_types, support, declarations,
              prototypes, pulse, mailbox,
              'static rgblight_indicator_target_callback_t rgblight_indicator_target_callback = rgblight_indicator_target_active_default;\n',
              '\n'.join(functions),
              'static void rgblight_timer_task(void) { if (rgblight_indicator_any_pending_render()) rgblight_request_render(); }\n',
              driver[driver.index('void ws2812_setleds'):], ports,
              '#include "test_rgb_frame_assertions.h"\n']
    out = build/f'test_rgb_frames_{board}.c'
    out.write_text('\n'.join(chunks), encoding='utf-8')
    return out
