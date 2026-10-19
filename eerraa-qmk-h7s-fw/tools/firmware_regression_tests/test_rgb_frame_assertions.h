static void loop_once(void) { rgblight_task(); ws2812Task(); }
static void run_us(uint32_t delta) {
    while (delta) { uint32_t step=delta>100?100:delta; mock_advance_us(step); loop_once(); delta-=step; }
}
static void pixels(uint8_t r,uint8_t g,uint8_t b) {
    assert(mock_wire_count);
    for (unsigned i=HW_WS2812_RGB;i<HW_WS2812_MAX_CH;++i) {
        const uint8_t *p=&mock_wire[mock_wire_count-1][i*3]; assert(p[0]==g && p[1]==r && p[2]==b);
    }
}
static void setup(uint8_t mode,uint8_t speed) {
    mock_reset(); assert(ws2812Init()); mock_advance_us(2000); ws2812Task();
    memset(led,0,sizeof(led)); memset(rgblight_frame,0,sizeof(rgblight_frame));
    memset(rgblight_indicator_state,0,sizeof(rgblight_indicator_state));
    memset(rgblight_indicator_range_table,0,sizeof(rgblight_indicator_range_table));
    rgblight_indicator_render_callback=NULL;
    rgblight_host_led_pending=rgblight_render_pending=output_suspended=false;
    rgblight_host_led_raw_buffer=host_bits=0;
    rgblight_task_slice_armed=false;
    rgblight_effect_pulse_reset_state();
    rgblight_config=(rgblight_config_t){.enable=true,.mode=mode,.speed=speed,.val=76};
    rgblight_status=(rgblight_status_t){.base_mode=mode,.timer_enabled=true};
    rgblight_ranges=(rgblight_ranges_t){0,RGBLIGHT_LED_COUNT,0,RGBLIGHT_LED_COUNT,RGBLIGHT_LED_COUNT};
    rgblight_indicator_range_t ranges[4]={{0,0},{0,RGBLIGHT_LED_COUNT},{0,RGBLIGHT_LED_COUNT},{0,RGBLIGHT_LED_COUNT}};
    rgblight_indicator_set_ranges(ranges,4);
    rgblight_indicator_update_config((rgblight_indicator_config_t){.target=RGBLIGHT_INDICATOR_TARGET_CAPS,.hue=85,.sat=255,.val=200});
    rgblight_effect_pulse_on_base_mode_update(); loop_once(); run_us(4000);
}
static void key(bool pressed) { rgblight_handle_physical_key(pressed,0,0,sync_timer_read32()); loop_once(); }
static void host(uint8_t bits) { usbHidSetStatusLed(bits); loop_once(); }
static void test_host_phases(void) {
    const unsigned speeds[]={0,15,255};
    const unsigned holds[]={1,4,5,6,20,40,260,280};
    const unsigned offsets[]={0,1,100,500,1000,1600,2000};
    for(unsigned s=0;s<3;++s)for(unsigned h=0;h<8;++h)for(unsigned d=0;d<7;++d) {
        setup(RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD,speeds[s]); key(true); run_us(holds[h]*1000); key(false);
        run_us(offsets[d]); unsigned frames=mock_wire_count; host(2); run_us(5000);
        assert(mock_wire_count>frames); pixels(0,200,0);
        assert(rgblight_indicator_state[0].active);
        host(0); run_us(300000); pixels(76,76,76);
    }
}
static void test_minimum_and_busy(void) {
    for(unsigned mode=RGBLIGHT_MODE_PULSE_OFF_PRESS;mode<=RGBLIGHT_MODE_PULSE_ON_PRESS_HOLD;++mode) {
        setup(mode,0);
        rgblight_sethsv_eeprom_helper(0,0,91,false); loop_once();
        unsigned before=mock_wire_count; key(true); run_us(100); key(false); run_us(30000);
        bool default_on=mode==RGBLIGHT_MODE_PULSE_OFF_PRESS||mode==RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD;
        int start=-1,end=-1;
        for(unsigned i=before;i<mock_wire_count;++i) {
            bool on=mock_wire[i][HW_WS2812_RGB*3]!=0;
            if(start<0 && on!=default_on)start=(int)i;
            else if(start>=0 && on==default_on){end=(int)i;break;}
        }
        assert(start>=0 && end>start);
        assert(mock_wire_ns[end]-mock_wire_ns[start]>=5000000U);
        assert(!rgblight_pulse_effect_state.latched);
    }
    setup(RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD,0); mock_fail_starts=10;
    unsigned before=mock_wire_count; key(true); run_us(100);key(false);run_us(30000);
    assert(mock_wire_count>=before+2); pixels(76,76,76);
}
static void test_preemption_config_and_base(void) {
    setup(RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD,0); key(true); run_us(100); host(2); run_us(4000); pixels(0,200,0);
    assert(!rgblight_pulse_effect_state.present_pending);
    for(unsigned i=0;i<RGBLIGHT_LED_COUNT;++i)assert(led[i].r==0 && led[i].g==0 && led[i].b==0);
    key(false); run_us(10000); host(0);run_us(5000);pixels(76,76,76);
    setup(RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD,15); key(true);run_us(40000);
    rgblight_sethsv_eeprom_helper(0,0,123,false);loop_once();run_us(5000);pixels(0,0,0);
    assert(rgblight_pulse_effect_state.pressed_count == 1);
    key(false);run_us(5000);pixels(123,123,123);
    host(2);run_us(5000);pixels(0,200,0);
    rgblight_sethsv_eeprom_helper(0,0,45,false);loop_once();run_us(5000);pixels(0,200,0);
    host(0);run_us(5000);pixels(45,45,45);
    rgblight_indicator_range_t ranges[4]={{0,0},{2,3},{0,0},{0,0}};
    rgblight_indicator_set_ranges(ranges,4);host(2);run_us(5000);
    assert(led[2].r==45 && led[2].g==45);
    host(0);run_us(5000);pixels(45,45,45);
    setup(RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD,0);key(true);run_us(100);rgblight_set_output_suspend_state(true);loop_once();run_us(5000);pixels(0,0,0);
    key(false);run_us(10000);rgblight_set_output_suspend_state(false);loop_once();run_us(5000);pixels(76,76,76);
}
static void inject_caps(void) { usbHidSetStatusLed(2); }
static void test_mailbox(void) {
    setup(RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD,0); usbHidSetStatusLed(0);
    mock_before_mask=inject_caps; rgblight_consume_host_led_queue();
    assert(rgblight_indicator_state[0].active);
    host(0); usbHidSetStatusLed(0); mock_after_restore=inject_caps;
    rgblight_consume_host_led_queue(); assert(rgblight_host_led_pending);
    rgblight_consume_host_led_queue(); assert(rgblight_indicator_state[0].active);
    led_update_ports((led_t){.raw=0});rgblight_consume_host_led_queue();
    assert(rgblight_indicator_state[0].active && host_bits==2);
    mock_mask=1; usbHidSetStatusLed(0);rgblight_consume_host_led_queue();assert(mock_mask==1);mock_mask=0;
    run_us(5000);pixels(76,76,76);
}
static void test_wrap_and_retrigger(void) {
    setup(RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD,0);mock_now_ns=(uint64_t)(UINT32_MAX-2000)*1000U;key(true);run_us(100);key(false);run_us(30000);pixels(76,76,76);
    setup(RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD,0);key(true);run_us(2000);
    rgblight_handle_physical_key(true,0,1,sync_timer_read32());loop_once();key(false);run_us(10000);pixels(0,0,0);
    rgblight_handle_physical_key(false,0,1,sync_timer_read32());loop_once();run_us(5000);pixels(76,76,76);
}
static void test_external_indicators(void) {
#if HW_WS2812_RGB == 2
    setup(RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD,0);
    rgblight_indicator_range_t ranges[4]={{0}};
    rgblight_indicator_set_ranges_at(0,ranges,4);
    rgblight_indicator_set_ranges_at(1,ranges,4);
    rgblight_indicator_update_config_at(1,(rgblight_indicator_config_t){.target=RGBLIGHT_INDICATOR_TARGET_SCROLL,.val=100,.sat=255});
    rgblight_indicator_set_render_callback(indicator_render);
    host(6);run_us(5000);pixels(76,76,76);
    assert(mock_wire[mock_wire_count-1][0]==200 && mock_wire[mock_wire_count-1][3]==100);
    rgblight_set_output_suspend_state(true);loop_once();run_us(5000);
    for(unsigned i=0;i<HW_WS2812_MAX_CH*3;++i)assert(mock_wire[mock_wire_count-1][i]==0);
    rgblight_set_output_suspend_state(false);loop_once();run_us(5000);
    assert(mock_wire[mock_wire_count-1][0]==200 && mock_wire[mock_wire_count-1][3]==100);
#endif
}
static void test_static_off_and_delayed_task(void) {
    setup(RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD,0);
    for(unsigned speed=0;speed<=255;++speed) { rgblight_config.speed=speed; assert(rgblight_effect_pulse_duration_ms()==5+speed); }
    rgblight_config.mode=RGBLIGHT_MODE_STATIC_LIGHT;
    rgblight_sethsv_eeprom_helper(0,0,61,false);loop_once();run_us(5000);
    rgblight_status.timer_enabled=false;
    host(2);run_us(5000);pixels(0,200,0);host(0);run_us(5000);pixels(61,61,61);
    rgblight_config.enable=false;rgblight_request_render();loop_once();run_us(5000);pixels(0,0,0);
    host(2);run_us(5000);pixels(0,200,0);host(0);run_us(5000);pixels(0,0,0);

    setup(RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD,0); unsigned before=mock_wire_count;
    rgblight_handle_physical_key(true,0,0,sync_timer_read32());
    mock_advance_us(8000); // No RGB task before the physical deadline.
    rgblight_handle_physical_key(false,0,0,sync_timer_read32());loop_once();run_us(30000);
    assert(mock_wire_count>=before+2);
    assert(mock_wire[mock_wire_count-2][HW_WS2812_RGB*3]==0);
    assert(mock_wire_ns[mock_wire_count-1]-mock_wire_ns[mock_wire_count-2]>=5000000U);

    setup(RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD,0);mock_fail_starts=3;key(true);
    rgblight_config.mode=RGBLIGHT_MODE_PULSE_ON_PRESS;rgblight_sethsv_eeprom_helper(0,0,91,false);loop_once();
    run_us(30000);pixels(0,0,0);assert(!rgblight_pulse_effect_state.latched);
    setup(RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD,0);mock_now_ns=(uint64_t)(UINT32_MAX-2)*1000000U;
    key(true);run_us(100);key(false);run_us(30000);pixels(76,76,76);
}

static void test_overlay_union(void) {
#if RGBLIGHT_INDICATOR_SLOT_COUNT >= 2
    setup(RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD,0);
    rgblight_indicator_range_t left[4]={{0,0},{0,15},{0,0},{0,0}};
    rgblight_indicator_range_t right[4]={{0,0},{0,0},{15,15},{0,0}};
    rgblight_indicator_set_ranges_at(0,left,4);rgblight_indicator_set_ranges_at(1,right,4);
    rgblight_indicator_update_config_at(1,(rgblight_indicator_config_t){.target=RGBLIGHT_INDICATOR_TARGET_SCROLL,.val=200,.sat=255});
    host(6);run_us(5000);assert(!rgblight_pulse_output_visible());pixels(0,200,0);
    key(true);run_us(100);key(false);run_us(10000);assert(!rgblight_pulse_effect_state.present_pending);
    host(2);run_us(5000);assert(rgblight_pulse_output_visible());
    key(true);run_us(100);key(false);run_us(30000);
    for(unsigned i=0;i<RGBLIGHT_LED_COUNT;++i)assert(led[i].r==76 && led[i].g==76);
    host(0);run_us(5000);pixels(76,76,76);
#endif
}

int main(void) {
    test_host_phases(); test_minimum_and_busy(); test_preemption_config_and_base();
    test_mailbox(); test_wrap_and_retrigger(); test_external_indicators();
    test_static_off_and_delayed_task(); test_overlay_union();
    printf("PASS: production RGB/Pulse/mailbox/compositor/board adapter/WS2812 chain: %u LEDs, offset %u; 168 host phase cases, >=5ms wire dwell, preemption/config/base isolation, IRQ races, sleep and wrap\n", HW_WS2812_MAX_CH,HW_WS2812_RGB);
}
