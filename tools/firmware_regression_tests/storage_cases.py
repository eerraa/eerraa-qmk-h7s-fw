"""Compile complete production storage callers against the physical bus fixture."""
from pathlib import Path
from source_cases import function

def macro_callers(root: Path, build: Path) -> Path:
    source = (root / 'src/ap/modules/qmk/quantum/dynamic_keymap.c').read_text(encoding='utf-8')
    out = build / 'eeprom_macro_callers.c'
    prefix = """#include "hw_def.h"
#include "qmk/port/platforms/eeprom.h"
#define DYNAMIC_KEYMAP_EEPROM_ADDR 256U
#define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR 1024U
#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE 2048U
void era_state_sync_bump_keymap(void);
void era_state_sync_bump_macro(void);
"""
    names = ['dynamic_keymap_macro_get_buffer', 'dynamic_keymap_macro_set_buffer_checked',
             'dynamic_keymap_macro_reset_checked', 'eeprom_note_commit']
    out.write_text(prefix + '\n'.join(function(source, name) for name in names), encoding='utf-8')
    return out


def initialization_callers(root: Path, build: Path) -> Path:
    quantum = (root / 'src/ap/modules/qmk/quantum/eeconfig.c').read_text(encoding='utf-8')
    via = (root / 'src/ap/modules/qmk/quantum/via.c').read_text(encoding='utf-8')
    image = (root / 'src/ap/modules/qmk/port/platforms/eeprom.c').read_text(encoding='utf-8')
    import re
    qbody = function(quantum, 'eeconfig_init_quantum_checked')
    constants = sorted(set(re.findall(r'EECONFIG_[A-Z_]+', qbody)) - {'EECONFIG_KB_DATA_SIZE', 'EECONFIG_USER_DATA_SIZE'})
    prefix = """#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define VIA_ENABLE
#define EECONFIG_KB_DATA_SIZE 0
#define EECONFIG_USER_DATA_SIZE 0
#define VIA_EEPROM_LAYOUT_OPTIONS_DEFAULT 0
static unsigned calls, fail_at, defaults, validity, guard_staged, guard_durable;
static bool macro_ok = true;
typedef uint32_t layer_state_t;
static layer_state_t default_layer_state;
static bool eeprom_flush_pending(void) {
    calls++;
    if (calls == fail_at) return false;
    if (guard_staged) guard_durable = 1;
    return true;
}
static bool eepromResetGuardInvalidate(void) { guard_staged=guard_durable=0; return eeprom_flush_pending(); }
static void eeprom_write_reset_guard(void) { assert(defaults == 2 && validity == 2); guard_staged=1; }
static void eeconfig_disable(void) { validity=0; }
static void eeconfig_enable(void) { validity++; }
static void eeconfig_init_kb(void) { defaults++; }
static void via_eeprom_set_valid(bool v) { if (v) validity++; }
static void via_set_layout_options(uint32_t v) { (void)v; }
static void dynamic_keymap_reset(void) { defaults++; }
static bool dynamic_keymap_macro_reset_checked(void) { return macro_ok; }
static void eeprom_update_byte(void *p, uint8_t v) { (void)p; (void)v; }
static void eeprom_update_word(void *p, uint16_t v) { (void)p; (void)v; }
static void eeprom_update_dword(void *p, uint32_t v) { (void)p; (void)v; }
static void eeprom_update_block(const void *p, void *a, uint32_t n) { (void)p; (void)a; (void)n; }
"""
    prefix += ''.join(f'#define {name} ((void *)(uintptr_t){i})\n' for i, name in enumerate(constants))
    main = """
int main(void) {
    assert(eeprom_apply_factory_defaults(true));
    assert(guard_durable);
    unsigned barriers=calls;
    for (unsigned boundary=1; boundary<=barriers; boundary++) {
        calls=defaults=validity=guard_staged=guard_durable=0; fail_at=boundary;
        assert(!eeprom_apply_factory_defaults(true));
        assert(!guard_durable);
    }
    calls=defaults=validity=guard_staged=guard_durable=fail_at=0; macro_ok=false;
    assert(!eeprom_apply_factory_defaults(true) && !guard_durable);
    printf("PASS: actual factory -> quantum -> VIA owners, %u barrier failure boundaries and macro failure\\n",barriers);
    return 0;
}
"""
    body = function(via, 'eeconfig_init_via_checked') + '\n' + qbody + '\n' + function(image, 'eeprom_apply_factory_defaults')
    out = build / 'test_eeprom_initialization.c'
    out.write_text(prefix + body + main, encoding='utf-8')
    return out
