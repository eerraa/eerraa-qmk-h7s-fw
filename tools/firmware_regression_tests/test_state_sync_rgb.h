#include "qmk/port/era_state_sync.h"
static void assert_config_change(uint32_t before) { assert(era_state_sync_config_revision()!=before); }
int main(void)
{
    rgblight_config=(rgblight_config_t){0};
    uint32_t c=era_state_sync_config_revision();
    rgblight_enable_noeeprom(); assert_config_change(c); assert(rgblight_get_mode()==1U);
    c=era_state_sync_config_revision(); rgblight_enable_noeeprom(); assert(era_state_sync_config_revision()==c);
    rgblight_sethsv_eeprom_helper(44,55,66,false); assert_config_change(c); assert(rgblight_get_hue()==44 && rgblight_get_sat()==55 && rgblight_get_val()==66);
    c=era_state_sync_config_revision(); rgblight_sethsv_eeprom_helper(44,55,66,false); assert(era_state_sync_config_revision()==c);
    rgblight_set_speed_noeeprom(19); assert_config_change(c); assert(rgblight_get_speed()==19);
    c=era_state_sync_config_revision(); rgblight_set_speed_noeeprom(19); assert(era_state_sync_config_revision()==c);
    rgblight_increase_speed_helper(false); assert(era_state_sync_config_revision()==c); /* clamped no-op */
    rgblight_decrease_speed_helper(false); assert_config_change(c);
    c=era_state_sync_config_revision(); rgblight_velocikey_set(true,false); assert_config_change(c); assert(rgblight_velocikey_enabled());
    c=era_state_sync_config_revision(); rgblight_velocikey_set(true,false); assert(era_state_sync_config_revision()==c);
    rgblight_velocikey_accelerate(); rgblight_velocikey_decelerate(); assert(era_state_sync_config_revision()==c);
    rgblight_velocikey_toggle(); assert_config_change(c);
    c=era_state_sync_config_revision(); rgblight_mode_noeeprom(RGBLIGHT_MODE_PULSE_ON_PRESS); assert_config_change(c);
    c=era_state_sync_config_revision(); rgblight_mode_noeeprom(RGBLIGHT_MODE_PULSE_ON_PRESS); assert(era_state_sync_config_revision()==c);
    rgblight_set_output_suspend_state(true); rgblight_set_output_suspend_state(false);
    rgblight_handle_physical_key(true,0,0,sync_timer_read32()); loop_once(); run_us(10000); rgblight_handle_physical_key(false,0,0,sync_timer_read32()); run_us(10000);
    assert(era_state_sync_config_revision()==c);
    eeconfig_update_rgblight_current(); assert(era_state_sync_config_revision()==c);
    rgblight_disable_noeeprom(); assert_config_change(c);
    c=era_state_sync_config_revision(); rgblight_disable_noeeprom(); assert(era_state_sync_config_revision()==c);
    rgblight_config_t disabled=rgblight_config; disabled.mode=2;
    rgblight_update_qword(disabled.raw); assert(era_state_sync_config_revision()==c && rgblight_get_mode()==0);
    disabled.speed++; rgblight_update_qword(disabled.raw); assert_config_change(c);
    stored_rgb=disabled.raw; c=era_state_sync_config_revision(); rgblight_reload_from_eeprom(); assert(era_state_sync_config_revision()==c);
    disabled.hue++; stored_rgb=disabled.raw; rgblight_reload_from_eeprom(); assert_config_change(c);
    c=era_state_sync_config_revision(); eeconfig_update_rgblight_default(); assert_config_change(c);
    c=era_state_sync_config_revision(); eeconfig_update_rgblight_default(); assert(era_state_sync_config_revision()==c);
    stored_rgb=rgblight_config.raw; is_rgblight_initialized=false; rgblight_init(); assert(era_state_sync_config_revision()==c);
    rgblight_config_t boot=rgblight_config; boot.speed++; stored_rgb=boot.raw; is_rgblight_initialized=false; rgblight_init(); assert_config_change(c);
    rgblight_config_t wrapped=rgblight_config;
    wrapped.enable=false; wrapped.val=254; wrapped.speed=0; rgblight_update_qword(wrapped.raw);
    c=era_state_sync_config_revision(); wrapped.val=53; wrapped.speed=1; rgblight_update_qword(wrapped.raw);
    assert((uint8_t)(254U*255U/RGBLIGHT_LIMIT_VAL)==(uint8_t)(53U*255U/RGBLIGHT_LIMIT_VAL));
    assert(era_state_sync_config_revision()!=c); assert(rgblight_get_speed()==1);
    puts("PASS: production core RGB enable/mode/HSV/speed/velocikey/qword/reload/default GET publications; same value/SAVE/animation/output gates excluded");
    return 0;
}
