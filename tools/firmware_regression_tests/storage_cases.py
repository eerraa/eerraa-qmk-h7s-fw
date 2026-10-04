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
             'dynamic_keymap_macro_reset_checked', 'eeprom_note_change']
    out.write_text(prefix + '\n'.join(function(source, name) for name in names), encoding='utf-8')
    return out


def initialization_callers(root: Path, build: Path) -> Path:
    quantum = (root / 'src/ap/modules/qmk/quantum/eeconfig.c').read_text(encoding='utf-8')
    via = (root / 'src/ap/modules/qmk/quantum/via.c').read_text(encoding='utf-8')
    image = (root / 'src/ap/modules/qmk/port/platforms/eeprom.c').read_text(encoding='utf-8')
    user = (root / 'src/ap/modules/qmk/port/eeconfig_port.c').read_text(encoding='utf-8')
    header = (root / 'src/ap/modules/qmk/quantum/eeconfig.h').read_text(encoding='utf-8')
    import re
    constants = header[header.index('#define EECONFIG_MAGIC ('):header.index('// Size of EEPROM being used for core')]
    prefix = r"""#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#define VIA_ENABLE
#define BOOTMODE_ENABLE
#define G_TERM_ENABLE
#define TAPDANCE_ENABLE
#define MOUSEKEY_ENABLE
#define RGBLIGHT_ENABLE
#define EECONFIG_KB_DATA_SIZE 0
#define EECONFIG_USER_DATA_SIZE 512
#define EECONFIG_USER_DATA_VERSION 512
#define EECONFIG_USER_DATABLOCK ((void *)(uintptr_t)37U)
#define EECONFIG_USER_BOOTMODE ((void *)(uintptr_t)65U)
#define VIA_EEPROM_MAGIC_ADDR 549U
#define VIA_EEPROM_LAYOUT_OPTIONS_ADDR 552U
#ifndef VIA_EEPROM_LAYOUT_OPTIONS_SIZE
#define VIA_EEPROM_LAYOUT_OPTIONS_SIZE 1U
#endif
#define VIA_EEPROM_LAYOUT_OPTIONS_DEFAULT 0U
#define USB_BOOT_MODE_DEFAULT_VALUE 3U
#define QMK_BUILDDATE "2025-01-01"
static unsigned calls, fail_at, keymap_defaults, kb_defaults, publications, config_bumps, guard_staged;
static unsigned stage_count[6], publish_count[6];
static uint32_t runtime[6];
static bool macro_ok = true;
static uint8_t ram[4096], durable[4096];
static bool layout_options_staged;
static uint32_t layout_options_before_reset, callback_value;
typedef uint32_t layer_state_t;
static layer_state_t default_layer_state;
static bool eeprom_flush_pending(void) {
    calls++;
    if (calls == fail_at) return false;
    memcpy(durable, ram, sizeof(ram));
    return true;
}
static uint8_t eeprom_read_byte(const uint8_t *p) { return ram[(uintptr_t)p]; }
static void via_eeprom_note_change(uint32_t address, uint32_t length);
static void eeprom_update_byte(void *p, uint8_t v) {
    uint8_t before=ram[(uintptr_t)p]; ram[(uintptr_t)p]=v;
    if (before!=v) via_eeprom_note_change((uintptr_t)p,1U);
}
static void eeprom_update_word(void *p, uint16_t v) { memcpy(ram+(uintptr_t)p,&v,2); }
static void eeprom_update_dword(void *p, uint32_t v) {
    memcpy(ram+(uintptr_t)p,&v,4);
    if ((uintptr_t)p == 65U) stage_count[0]++;
}
static void eeprom_update_block(const void *p, void *a, uint32_t n) { memcpy(ram+(uintptr_t)a,p,n); }
static bool eepromResetGuardInvalidate(void) {
    if (!eeprom_flush_pending()) return false;
    memset(ram+73U,0,8);
    return eeprom_flush_pending();
}
static void eeprom_write_reset_guard(void) {
    assert(kb_defaults == 1 && keymap_defaults == 1 && publications == 0);
    memset(ram+73U,0xA5,8); guard_staged=1;
}
static void eeconfig_disable(void) { ram[0]=ram[1]=0xFF; }
static void eeconfig_enable(void) { ram[0]=0xE6; ram[1]=0xFE; }
static void eeconfig_init_kb(void) { kb_defaults++; }
static void dynamic_keymap_reset(void) { keymap_defaults++; ram[800]=0; }
static bool dynamic_keymap_macro_reset_checked(void) { ram[1024]=0; return macro_ok; }
static void stage(unsigned i) { stage_count[i]++; ram[300U+i]=(uint8_t)(10U+i); }
static void publish(unsigned i) {
    assert(memcmp(ram,durable,sizeof(ram)) == 0);
    publish_count[i]++; runtime[i]=durable[300U+i];
}
static void debounce_profile_storage_stage_defaults(void) { stage(1); }
static void tapping_term_storage_stage_defaults(void) { stage(2); }
static void tapdance_storage_stage_defaults(void) { stage(3); }
static void mousekey_config_storage_stage_defaults(void) { stage(4); }
static void rgb_sleep_storage_stage_defaults(void) { stage(5); }
static void bootmode_publish_defaults(void) { publish_count[0]++; runtime[0]=USB_BOOT_MODE_DEFAULT_VALUE; }
static void debounce_profile_storage_publish(void) { publish(1); }
static void tapping_term_init(void) { publish(2); }
static void tapdance_init(void) { publish(3); }
static void mousekey_config_init(void) { publish(4); }
static void rgb_sleep_storage_publish(void) { publish(5); }
static void via_set_layout_options_kb(uint32_t value) { callback_value=value; }
static void era_state_sync_bump_config(void) { config_bumps++; publications++; }
static uint32_t via_get_layout_options(void);
static void via_store_layout_options(uint32_t value);
static void eeconfig_publish_quantum_defaults(void);
typedef struct { int argc; bool (*isStr)(int,const char *); } cli_args_t;
static char cli_result[80];
static bool cli_is_str(int i,const char *text) { return strcmp(text,i==0?"clear":"eeprom")==0; }
static void cliPrintf(const char *text,...) { snprintf(cli_result,sizeof(cli_result),"%s",text); }
"""
    prefix += constants
    names = ['via_eeprom_note_change', 'via_eeprom_set_valid', 'via_get_layout_options', 'via_store_layout_options',
             'via_set_layout_options', 'eeconfig_init_via_checked', 'eeconfig_publish_via_defaults']
    body = '\n'.join(function(via,n) for n in names)
    body += '\n' + function(quantum,'eeconfig_update_user_datablock')
    body += '\n' + function(user,'eeconfig_init_user_datablock')
    body += '\n' + function(user,'eeconfig_publish_user_datablock')
    body += '\n' + function(quantum,'eeconfig_init_quantum_checked')
    body += '\n' + function(quantum,'eeconfig_publish_quantum_defaults')
    body += '\n' + function(image,'eeprom_apply_factory_defaults')
    qmk = (root/'src/ap/modules/qmk/qmk.c').read_text(encoding='utf-8')
    body += '\n' + function(qmk,'cliQmk')
    main = r"""
static uint32_t mask(void) { return UINT32_MAX >> ((4U-VIA_EEPROM_LAYOUT_OPTIONS_SIZE)*8U); }
static void reset_case(void) {
    memset(ram,0x83,sizeof(ram)); memcpy(durable,ram,sizeof(ram));
    calls=fail_at=keymap_defaults=kb_defaults=publications=config_bumps=guard_staged=0;
    memset(stage_count,0,sizeof(stage_count)); memset(publish_count,0,sizeof(publish_count));
    for(unsigned i=0;i<6;i++) runtime[i]=90U+i;
    macro_ok=true; default_layer_state=128U; layout_options_staged=false; callback_value=0x83838383U&mask();
}
static void assert_old(void) {
    assert(config_bumps==0 && publications==0 && default_layer_state==128U);
    assert(via_get_layout_options()==(0x83838383U&mask()) && callback_value==(0x83838383U&mask()));
    for(unsigned i=0;i<6;i++) assert(runtime[i]==90U+i && publish_count[i]==0U && stage_count[i]<=1U);
}
static void assert_published(void) {
    assert(config_bumps==1 && publications==1 && default_layer_state==1U);
    assert(via_get_layout_options()==0 && callback_value==0);
    for(unsigned i=0;i<6;i++) assert(stage_count[i]==1 && publish_count[i]==1);
    assert(runtime[0]==USB_BOOT_MODE_DEFAULT_VALUE);
    for(unsigned i=1;i<6;i++) assert(runtime[i]==10U+i);
}
int main(void) {
    reset_case(); assert(eeprom_apply_factory_defaults(true)); assert_published();
    unsigned barriers=calls; assert(guard_staged && durable[73U]==0xA5);
    for(unsigned boundary=1;boundary<=barriers;boundary++) {
        reset_case(); fail_at=boundary;
        assert(!eeprom_apply_factory_defaults(true)); assert_old();
        assert(!guard_staged || boundary==barriers);
        fail_at=0; assert(eeprom_flush_pending()); assert_old();
        memset(stage_count,0,sizeof(stage_count)); keymap_defaults=kb_defaults=0;
        assert(eeprom_apply_factory_defaults(true)); assert_published();
    }
    reset_case(); macro_ok=false;
    assert(!eeprom_apply_factory_defaults(true)); assert_old();
    assert(!guard_staged); assert(eeprom_flush_pending()); assert_old();
    reset_case(); fail_at=5;
    cli_args_t args={.argc=2,.isStr=cli_is_str}; cliQmk(&args);
    assert(strcmp(cli_result,"EEPROM clear failed\n")==0); assert_old();
    reset_case(); cliQmk(&args); assert(strcmp(cli_result,"Clearing EEPROM\n")==0); assert_published();
    reset_case(); via_set_layout_options(0xFEDCBA98U);
    assert(callback_value==0xFEDCBA98U && via_get_layout_options()==(0xFEDCBA98U&mask()) && config_bumps==1U);
    via_set_layout_options(0xFEDCBA98U);
    assert(config_bumps==1U);  // Same GET-visible layout is not another invalidation.
    assert(eeprom_flush_pending()); layout_options_staged=false;
    assert(via_get_layout_options()==(0xFEDCBA98U&mask()));
    eeprom_update_byte((void *)VIA_EEPROM_LAYOUT_OPTIONS_ADDR,0x31U);
    assert(config_bumps==2U);
    eeprom_update_byte((void *)VIA_EEPROM_LAYOUT_OPTIONS_ADDR,0x31U); assert(config_bumps==2U);
    assert((via_get_layout_options() >> ((VIA_EEPROM_LAYOUT_OPTIONS_SIZE-1U)*8U))==0x31U);
    reset_case(); fail_at=5;
    assert(!eeprom_apply_factory_defaults(true)); assert_old();
    via_set_layout_options(0xFEDCBA98U);
    assert(via_get_layout_options()==(0xFEDCBA98U&mask()) && config_bumps==1U && !layout_options_staged);
    reset_case(); fail_at=1;
    assert(!eeconfig_init_via_checked()); assert_old();
    fail_at=0; assert(eeconfig_init_via_checked()); assert_old();
    eeconfig_publish_via_defaults(); assert(via_get_layout_options()==0 && config_bumps==1U);
    printf("PASS: actual USER512 factory/quantum/VIA/CLI owners: layout%u, %u barriers, late completion retains GET, successful retry publishes once, normal SET masks/reloads\n", VIA_EEPROM_LAYOUT_OPTIONS_SIZE,barriers);
    return 0;
}
"""
    out = build / 'test_eeprom_initialization.c'
    out.write_text(prefix + body + main, encoding='utf-8')
    return out


def default_provider_callers(root: Path, build: Path) -> Path:
    """Run the complete production debounce provider, including real dirty flags."""
    import re
    source = (root/'src/ap/modules/qmk/port/debounce_profile.c').read_text(encoding='utf-8')
    source = re.sub(r'^\s*#include[^\n]*\n', '', source, flags=re.M)
    prefix = r"""#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "debounce_profile.h"
static uint16_t timer_read(void) { return 0; }
static uint16_t timer_elapsed(uint16_t value) { return value; }
#include "eeconfig.h"
#include "via.h"
#define EECONFIG_USER_DEBOUNCE ((void *)(uintptr_t)81U)
static uint8_t storage[4096];
static unsigned config_bumps, writes, applies;
static debounce_runtime_config_t current;
static const debounce_runtime_config_t board_default={DEBOUNCE_RUNTIME_TYPE_SYM_DEFER_PK,5,5};
void eeprom_read_block(void *p,const void *a,uint32_t n) { memcpy(p,storage+(uintptr_t)a,n); }
void eeprom_update_block(const void *p,void *a,size_t n) { memcpy(storage+(uintptr_t)a,p,n); writes++; }
void era_state_sync_bump_config(void) { config_bumps++; }
const debounce_runtime_config_t *debounce_runtime_get_default_config(void) { return &board_default; }
bool debounce_runtime_apply_config(const debounce_runtime_config_t *c) { current=*c; applies++; return true; }
void logPrintf(const char *format,...) { (void)format; }
"""
    main = r"""
static void set(uint8_t id,uint8_t value) {
    uint8_t packet[32]={id_custom_set_value,0,id,value};
    assert(debounce_profile_handle_via_command(packet,32));
}
int main(void) {
    debounce_profile_init(); debounce_profile_apply_current();
    set(id_qmk_debounce_time_single,17U);
    debounce_profile_storage_t before_store=debounce_profile_storage;
    debounce_profile_state_t before_state=debounce_profile_state;
    uint8_t before_dirty=dirty_debounce_profile;
    unsigned bumps=config_bumps, applied=applies;
    assert(before_dirty && before_state.initialized);
    debounce_profile_storage_stage_defaults();
    assert(memcmp(&before_store,&debounce_profile_storage,sizeof(before_store))==0);
    assert(memcmp(&before_state,&debounce_profile_state,sizeof(before_state))==0);
    assert(dirty_debounce_profile==before_dirty && config_bumps==bumps && applies==applied);
    assert(current.pre_ms==17U && current.post_ms==17U);
    debounce_profile_storage_t candidate; memcpy(&candidate,storage+81U,sizeof(candidate));
    assert(candidate.pre_ms==5U && candidate.post_ms==5U);
    debounce_profile_save(false);
    assert(!dirty_debounce_profile && config_bumps==bumps);
    memcpy(&candidate,storage+81U,sizeof(candidate)); assert(candidate.pre_ms==17U);
    set(id_qmk_debounce_time_single,17U); assert(config_bumps==bumps);
    debounce_profile_save(false);
    debounce_profile_storage_stage_defaults();
    assert(!dirty_debounce_profile && current.pre_ms==17U);
    debounce_profile_storage_publish();
    assert(debounce_profile_current()->pre_ms==5U && current.pre_ms==5U && applies==applied+2U);
    assert(config_bumps==bumps && !dirty_debounce_profile);
    printf("PASS: complete production debounce staging: runtime/GET/initialized/dirty preserved, ordinary SAVE/no-op SET retained, final publication reloads board defaults (%u writes)\n",writes);
    return 0;
}
"""
    out=build/'test_eeprom_default_provider.c'
    out.write_text(prefix+source+main,encoding='utf-8')
    return out
