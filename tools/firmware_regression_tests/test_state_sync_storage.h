int main(int argc, char **argv)
{
    memset(device, 0xFF, sizeof(device)); assert(eepromInit() && eeprom_init());
    auto_clock=true;
    if (argc<2 || (strcmp(argv[1],"macro")!=0 && strcmp(argv[1],"keymap")!=0)) {
    uint32_t c=config_revision;
    eeprom_write_byte(address(224U),0x12U);
    assert((via_get_layout_options() >> ((VIA_EEPROM_LAYOUT_OPTIONS_SIZE-1U)*8U))==0x12U && config_revision==c+1U);
    c=config_revision; eeprom_write_byte(address(224U),0x12U); assert(config_revision==c);
    assert(eeprom_flush_pending() && config_revision==c);
    via_set_layout_options(0xAB123456U); assert(config_revision==c+1U && layout_callback==0xAB123456U);
    c=config_revision; via_set_layout_options(0xAB123456U); assert(config_revision==c);
    layout_options_before_reset=via_get_layout_options(); layout_options_staged=true;
    eeprom_write_byte(address(224U),0U); assert(via_get_layout_options()==layout_options_before_reset && config_revision==c);
    assert(eeprom_flush_pending() && config_revision==c); eeconfig_publish_via_defaults(); assert(config_revision==c+1U);
    c=config_revision; eeconfig_publish_via_defaults(); assert(config_revision==c);
    eeprom_write_byte(address(223U),0x53U); assert(config_revision==c); assert(eeprom_flush_pending());
    }
    if (argc < 2 || strcmp(argv[1],"macro")!=0) {
    uint32_t k = keymap_revision;
    dynamic_keymap_set_keycode(0,0,0,0x1234U);
    assert(dynamic_keymap_get_keycode(0,0,0)==0x1234U && keymap_revision>k);
    k=keymap_revision; dynamic_keymap_set_keycode(0,0,0,0x1234U); assert(keymap_revision==k);
    assert(device[256U]==0xFFU); auto_clock=true; assert(eeprom_flush_pending()); assert(keymap_revision==k);
    uint8_t bytes[3]={0x43,0x21,0x59};
    dynamic_keymap_set_buffer(0,3,bytes); assert(keymap_revision>k);
    uint8_t got[3]; dynamic_keymap_get_buffer(0,3,got); assert(memcmp(bytes,got,3)==0);
    k=keymap_revision; dynamic_keymap_set_buffer(0,3,bytes); assert(keymap_revision==k);
    eeprom_update_byte(address(257U),0x72U); assert(keymap_revision==k+1U && dynamic_keymap_get_keycode(0,0,0)==0x4372U);
    k=keymap_revision; dynamic_keymap_set_encoder(1,0,false,0xAABB); assert(keymap_revision==k+2U && dynamic_keymap_get_encoder(1,0,false)==0xAABB);
    k=keymap_revision; dynamic_keymap_reset(); assert(keymap_revision>k && dynamic_keymap_get_keycode(1,1,1)==0x107U);
    k=keymap_revision; dynamic_keymap_reset(); assert(keymap_revision==k);
    dynamic_keymap_set_keycode(0,0,0,0xBABAU); k=keymap_revision; assert(eeprom_apply_factory_defaults(false));
    assert(keymap_revision>k && dynamic_keymap_get_keycode(0,0,0)==0x100U); k=keymap_revision;
    assert(eeprom_flush_pending() && keymap_revision==k);
    assert(eeprom_init() && keymap_revision==k);
    device[257U]^=0x42U; assert(eeprom_init() && keymap_revision==k+1U);
    k=keymap_revision; fail_read=true; assert(!eeprom_init() && keymap_revision>k && dynamic_keymap_get_keycode(0,0,0)==0xFFFFU);
    k=keymap_revision; assert(!eeprom_init() && keymap_revision==k); fail_read=false;
    assert(eeprom_init() && keymap_revision>k);

    }
    assert(dynamic_keymap_macro_reset_checked() && eeprom_flush_pending());
    uint32_t m=macro_revision; uint8_t opened=0xFFU,closed=0U,payload[3]={0x41,0x42,0};
    fail_start=true; assert(!dynamic_keymap_macro_set_buffer_checked(2047,1,&opened));
    assert(eeprom_read_byte(address(3071U))==0xFFU && macro_revision==m+1U);
    m=macro_revision; fail_start=false; assert(eeprom_flush_pending() && macro_revision==m);
    assert(dynamic_keymap_macro_set_buffer_checked(2047,1,&opened) && macro_revision==m);
    assert(dynamic_keymap_macro_set_buffer_checked(0,3,payload) && macro_revision==m+2U);
    uint8_t snapshot[3]; dynamic_keymap_macro_get_buffer(0,3,snapshot); assert(memcmp(snapshot,payload,3)==0);
    m=macro_revision; assert(dynamic_keymap_macro_set_buffer_checked(0,3,payload) && macro_revision==m);
    assert(dynamic_keymap_macro_set_buffer_checked(2047,1,&closed));
    assert(eeprom_commit_is_pending() && eeprom_read_byte(address(3071U))==0xFFU && macro_revision==m);
    corrupt_program=true; assert(!eeprom_flush_pending() && macro_revision==m && eeprom_read_byte(address(3071U))==0xFFU);
    corrupt_program=false; assert(eeprom_flush_pending() && eeprom_read_byte(address(3071U))==0U && macro_revision==m+1U);
    m=macro_revision; assert(dynamic_keymap_macro_set_buffer_checked(2047,1,&closed)); assert(eeprom_flush_pending() && macro_revision==m);

    /* Cancel a hidden close by writing its already-staged raw value. */
    assert(dynamic_keymap_macro_set_buffer_checked(2047,1,&opened));
    assert(dynamic_keymap_macro_set_buffer_checked(2047,1,&closed));
    auto_clock=false;
    for(unsigned i=0; i<1000U && !bus.active; ++i) eeprom_update();
    assert(bus.active && eeprom_commit_is_pending() && eeprom_read_byte(address(3071U))==0xFFU);
    m=macro_revision; eeprom_write_byte(address(3071U),0U);
    assert(!eeprom_commit_is_pending() && eeprom_read_byte(address(3071U))==0U && macro_revision==m+1U);
    auto_clock=true; assert(eeprom_flush_pending());
    puts("PASS: production KEYMAP setters/split byte/encoder/reset/reload/failure and MACRO opener/payload/hidden close/unhide/cancel; receipts and equal retries unchanged");
    return 0;
}
