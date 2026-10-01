static bool hid_output_down(uint16_t code) {
  if (IS_MOUSE_KEYCODE(code)) return (mouse_codes_down & (1UL << (code - KC_MS_UP))) != 0;
  if (IS_CONSUMER_KEYCODE(code)) return last_consumer == KEYCODE2CONSUMER(code);
  return last_system == KEYCODE2SYSTEM(code);
}

/* Product TD, LT/MT queue, action resources and report constructors.
 * Clock/matrix, EEPROM and physical USB/LED completion are adapters. */
static void check_td_lifetime(const char *name) {
  reset_fixture(TD(0), 46, 999); tick(999);
  automatic_caps_feedback = false;
  if (!strcmp(name, "capture") || !strcmp(name, "partial") || !strcmp(name, "partial_reverse")) {
    tapdance_state[0].actions[1] = !strcmp(name, "capture") ? KC_LCTL : ((MOD_LCTL << 8) | KC_LSFT);
    tapdance_state[1] = (tapdance_slot_state_t){{KC_X, !strcmp(name, "capture") ? MO(2) : KC_LCTL, KC_NO, KC_NO}, 200};
    other_keycode = TD(1);
    scan(1000,0,true); tick(1201); scan(1210,1,true); tick(1411);
    const bool reverse = !strcmp(name,"partial_reverse");
    scan(1420,reverse ? 1 : 0,false); task();
    assert((get_mods() | get_weak_mods()) == (reverse ? (MOD_BIT(KC_LCTL) | MOD_BIT(KC_LSFT)) : (!strcmp(name,"capture") ? 0 : MOD_BIT(KC_LCTL))));
    scan(1430,reverse ? 0 : 1,false); task(); assert((get_mods() | get_weak_mods()) == 0 && layer_state == 0);
  } else if (!strncmp(name,"ordinary_layer",14) || !strncmp(name,"modtap",6)) {
    const bool mt = !strncmp(name,"modtap",6);
    const bool reverse = strstr(name,"_reverse") != NULL;
    tapdance_state[0].actions[1] = mt ? MT(MOD_LCTL, KC_A) : MO(1);
    other_keycode = mt ? KC_LCTL : MO(1);
    scan(1000,1,true); scan(1010,0,true); tick(1211);
    scan(1220,reverse ? 0 : 1,false); task();
    assert(mt ? get_mods() == MOD_BIT(KC_LCTL) : layer_state == 2);
    scan(1230,reverse ? 1 : 0,false); task(); assert(get_mods() == 0 && layer_state == 0);
  } else if (!strncmp(name,"usage_",6)) {
    const bool td_first = name[6] == '1', td_release_first = name[7] == '1';
    tapdance_state[0].actions[1] = KC_A;
    if(td_first) { scan(1000,0,true); tick(1201); }
    else scan(1000,1,true);
    assert(is_key_pressed(KC_A)); require_shared_a = true;
    if(td_first) scan(1210,1,true);
    else {scan(1010,0,true); tick(1211);}
    /* The joining down is its own keystroke; the first up never releases A. */
    assert(shared_a_gaps == 1);
    scan(1220,td_release_first ? 0 : 1,false); task();
    assert(is_key_pressed(KC_A) && shared_a_gaps == 1);
    require_shared_a = false;
    scan(1230,td_release_first ? 1 : 0,false); task(); assert(!is_key_pressed(KC_A));
  } else if (!strcmp(name,"clear_pending")) {
    scan(1000,0,true); clear_keyboard(); tick(1201);
    assert(layer_state == 0 && caps_press_count == 0);
    scan(1210,0,false); task(); assert(layer_state == 0 && caps_press_count == 0);
  } else if (!strcmp(name,"clear_queue") || !strcmp(name,"clear_queue_pair")) {
    mapped_keycode=LT(1,KC_CAPS); other_keycode=TD(0);
    tapdance_state[0].actions[0]=KC_A; tapdance_state[0].actions[1]=MO(2); tapdance_state[0].term_ms=50;
    scan(1000,0,true); scan(1010,1,true);
    if(!strcmp(name,"clear_queue_pair")) scan(1011,1,false);
    forbid_a = true; clear_keyboard(); tick(1201);
    assert(forbidden_a_reports == 0);
    assert((layer_state & 4) == 0 && !is_key_pressed(KC_A));
    scan(1220,1,false); scan(1230,0,false); task(); assert(layer_state == 0);
  } else if (!strcmp(name,"init")) {
    scan(1000,0,true); tick(1201); assert(layer_state == 2);
    tapdance_init(); assert(layer_state == 0);
    mapped_keycode=KC_X; scan(1220,0,false); task(); assert(layer_state == 0 && !is_key_pressed(KC_X));
  } else if (!strcmp(name,"capacity")) {
    other_keycode=TD(0);
    for(unsigned i=0;i<MATRIX_ROWS*MATRIX_COLS;++i) {
      now_ms=1000+300*i;
      action_exec((keyevent_t){.key={.row=i/MATRIX_COLS,.col=i%MATRIX_COLS},.type=KEY_EVENT,.time=now_ms,.pressed=true});
      now_ms+=201; tap_dance_task(); assert(layer_state==2);
    }
    for(unsigned i=0;i<MATRIX_ROWS*MATRIX_COLS;++i) {
      ++now_ms;
      action_exec((keyevent_t){.key={.row=i/MATRIX_COLS,.col=i%MATRIX_COLS},.type=KEY_EVENT,.time=now_ms,.pressed=false});
      assert(layer_state==(i+1<MATRIX_ROWS*MATRIX_COLS ? 2 : 0));
    }
  } else if (!strcmp(name,"toggle")) {
    scan(1000,0,true); tick(1201);
    other_keycode=QK_TOGGLE_LAYER|1; scan(1210,1,true); scan(1211,1,false);
    scan(1220,0,false); assert(layer_state==2); layer_clear();
  } else if (!strcmp(name,"oneshot")) {
    tapdance_state[0].actions[0]=QK_ONE_SHOT_MOD|MOD_LCTL;
    scan(1000,0,true); scan(1010,0,false); assert(get_oneshot_mods()==MOD_BIT(KC_LCTL));
    tapdance_init(); assert(get_oneshot_mods()==MOD_BIT(KC_LCTL));
    scan(1020,1,true); assert(keyboard_report->mods==MOD_BIT(KC_LCTL));
    scan(1021,1,false); assert(get_mods()==0 && get_oneshot_mods()==0 && keyboard_report->mods==0);
  } else if (!strcmp(name,"oneshot_layer")) {
    tapdance_state[0].actions[0]=QK_ONE_SHOT_LAYER|1;
    scan(1000,0,true); scan(1010,0,false); assert(layer_state==2);
    scan(1020,1,true); scan(1021,1,false); assert(layer_state==0);
  } else if (!strcmp(name,"old_epoch")) {
    keyrecord_t old={.event={.key={.row=0,.col=0},.type=KEY_EVENT,.time=1000,.pressed=true}};
    old.tap_dance_epoch_valid=true;old.tap_dance_epoch=tap_dance_input_epoch();
    pre_process_record_quantum(&old); clear_keyboard();
    scan(1010,0,true); tick(1211); assert(layer_state==2);
    process_record(&old); old.event.pressed=false; process_record(&old); assert(layer_state==2);
    scan(1220,0,false); assert(layer_state==0);
  } else if (!strcmp(name,"caps_feedback")) {
    const unsigned speeds[]={0,15,255};
    for(unsigned dance=0;dance<2;++dance)for(unsigned speed=0;speed<3;++speed)for(unsigned phase=0;phase<4;++phase) {
      reset_fixture(dance ? TD(0) : LT(1,KC_CAPS),46,50000); tick(50000);
      automatic_caps_feedback=false; rgblight_config.speed=speeds[speed];
      scan(50010,0,true); if(phase&1) task();
      if(phase>=2) {scan(50020,0,false); if(phase&1) task();}
      unsigned before_frames=frame_count;
      now_ms=phase>=2 ? 50021 : 50011;
      host_led_raw=2; rgblight_indicator_post_host_event((led_t){2}); rgblight_task();
      assert(indicator_on && visible_frame==2 && frame_count>before_frames);
      before_frames=frame_count;
      now_ms++;host_led_raw=0;rgblight_indicator_post_host_event((led_t){0});rgblight_task();
      /* Caps OFF removes only the overlay. A live physical hold or an
       * unexpired pulse still owns its OFF background; neither delays OFF. */
      const bool pulse_active = phase < 2 || now_ms - 50010U < 5U + speeds[speed];
      assert(!indicator_on && frame_count>before_frames);
      assert(visible_frame==(pulse_active ? 0 : 1));
      if(phase<2) {scan(50020,0,false); task();}
      tick(50400); assert(visible_frame==1 && caps_press_count==1 && caps_release_count==1);
      assert(physical_color_writes==0);
    }
  } else if (!strcmp(name,"oneshot_mod_consumed")) {
    tapdance_state[0].actions[0]=KC_A;
    set_oneshot_mods(MOD_BIT(KC_LCTL)); last_a_mods=0;
    scan(1000,0,true); scan(1010,0,false); task();
    assert(last_a_mods==MOD_BIT(KC_LCTL));
    assert(get_mods()==0 && get_oneshot_mods()==0 && !is_key_pressed(KC_A));
  } else if (!strcmp(name,"oneshot_layer_consumed")) {
    tapdance_state[0].actions[0]=KC_A;
    set_oneshot_layer(1,ONESHOT_START); clear_oneshot_layer_state(ONESHOT_PRESSED);
    assert(layer_state==2);
    scan(1000,0,true); scan(1010,0,false); task();
    assert(!is_oneshot_layer_active() && layer_state==0 && !is_key_pressed(KC_A));
  } else if (!strcmp(name,"interrupt_relookup")) {
    /* Interruption selects Tap; give that path the layer action. */
    tapdance_state[0].actions[0]=MO(1);
    layered_other_keycode=KC_B;
    scan(1000,0,true); scan(1010,1,true);
    assert(layer_state==2 && is_key_pressed(KC_B) && !is_key_pressed(KC_A));
    scan(1020,1,false); scan(1030,0,false);
    assert(layer_state==0 && !is_key_pressed(KC_B));
  } else if (!strcmp(name,"cancel_keeps_waiting")) {
    mapped_keycode=LT(2,KC_CAPS); other_keycode=TD(0); third_keycode=KC_B;
    scan(1000,0,true); scan(1010,1,true); scan(1020,2,true);
    clear_keyboard(); tick(1201);
    assert(is_key_pressed(KC_B) && !(layer_state & 2U));
    scan(1230,2,false); scan(1240,1,false); scan(1250,0,false);
    assert(!is_key_pressed(KC_B) && layer_state==0);
  } else if (!strcmp(name,"lighting_boundary")) {
    scan(1000,0,true); tick(1201); assert(layer_state==2);
    rgblight_config.enable=false;
    rgblight_sethsv_eeprom_helper(0,0,120,false); task(); assert(layer_state==2);
    rgblight_config.enable=true; rgblight_config.mode=43;
    rgblight_sethsv_eeprom_helper(0,0,120,false); task(); assert(layer_state==2);
    rgblight_set_output_suspend_state(true); task(); assert(layer_state==2);
    rgblight_set_output_suspend_state(false); task(); assert(layer_state==2);
    scan(1300,0,false); task(); assert(layer_state==0);
  } else if (!strcmp(name,"term_reconfigure")) {
    scan(1000,0,true); tapdance_state[0].term_ms=400;
    tick(1201); assert(layer_state==0);
    tick(1400); assert(layer_state==0);
    tick(1401); assert(layer_state==2);
    tapdance_state[0].actions[1]=MO(2);
    scan(1450,0,false); assert(layer_state==0);
  } else if (!strcmp(name,"layer_override") || !strcmp(name,"layer_clear")) {
    tapdance_state[0].actions[1]=MO(2);
    tapdance_state[1]=(tapdance_slot_state_t){{KC_X,KC_LCTL,KC_NO,KC_NO},200};
    other_keycode=TD(1); third_keycode=KC_B;
    scan(1000,0,true); tick(1201); scan(1210,1,true); tick(1411); scan(1420,2,true);
    assert(layer_state==4 && get_mods()==MOD_BIT(KC_LCTL) && is_key_pressed(KC_B));
    const bool clear=!strcmp(name,"layer_clear");
    if(clear) layer_clear(); else layer_move(1);
    assert(layer_state==(clear ? 0U : 2U));
    assert(get_mods()==MOD_BIT(KC_LCTL) && is_key_pressed(KC_B));
    layer_on(2); /* A new ordinary contribution must outlive the old TD release. */
    scan(1430,0,false); assert(layer_state==(clear ? 4U : 6U));
    layer_off(2); scan(1440,1,false); scan(1450,2,false);
    assert(get_mods()==0 && !is_key_pressed(KC_B)); layer_clear();
    scan(1500,0,true); tick(1701); assert(layer_state==4);
    scan(1710,0,false); assert(layer_state==0);
  } else if (!strcmp(name,"tri_no_leak") || !strcmp(name,"tri_shared") || !strcmp(name,"tri_active")) {
    const bool shared=!strcmp(name,"tri_shared"), active=!strcmp(name,"tri_active");
    other_keycode=MO(1);
    scan(1000,0,true); tick(1201); assert(layer_state==2);
    if(shared) scan(1210,1,true);
    if(active) layer_on(2);
    update_tri_layer(1,2,3); assert(layer_state==(active ? 14U : 2U));
    scan(1220,0,false);
    if(active) update_tri_layer(1,2,3);
    assert(layer_state==(shared ? 2U : (active ? 4U : 0U)));
    if(shared) scan(1230,1,false);
    if(active) {layer_off(2);update_tri_layer(1,2,3);}
    assert(layer_state==0);
  } else if (!strcmp(name,"hid_shared")) {
    /* Mouse and extra-report usages shared with a normal key follow the last owner. */
    const uint16_t outputs[]={KC_MS_BTN1,KC_MS_UP,KC_AUDIO_VOL_UP,KC_SYSTEM_SLEEP};
    for(unsigned i=0;i<8;++i) {
      const uint16_t out=outputs[i/2]; const bool td_release_first=i&1; const uint32_t t=2000U+1000U*i;
      reset_fixture(TD(0),46,t-1U); tick(t-1U); automatic_caps_feedback=false;
      tapdance_state[0].actions[1]=out; other_keycode=out;
      scan(t,0,true); tick(t+201U); scan(t+210U,1,true); assert(hid_output_down(out));
      scan(t+220U,td_release_first ? 0 : 1,false); task(); assert(hid_output_down(out));
      scan(t+230U,td_release_first ? 1 : 0,false); task(); assert(!hid_output_down(out));
    }
  } else if (!strcmp(name,"extra_current")) {
    /* A one-usage report: an earlier input's up must not clear a later input's usage. */
    for(unsigned td_first=0;td_first<2;++td_first) {
      const uint32_t t=2000U+1000U*td_first;
      reset_fixture(TD(0),46,t-1U); tick(t-1U); automatic_caps_feedback=false;
      tapdance_state[0].actions[1]=td_first ? KC_AUDIO_VOL_UP : KC_AUDIO_VOL_DOWN;
      other_keycode=td_first ? KC_AUDIO_VOL_DOWN : KC_AUDIO_VOL_UP;
      if(td_first) {scan(t,0,true); tick(t+201U); scan(t+210U,1,true);}
      else {scan(t,1,true); scan(t+10U,0,true); tick(t+211U);}
      assert(last_consumer==KEYCODE2CONSUMER(KC_AUDIO_VOL_DOWN));
      scan(t+220U,td_first ? 0 : 1,false); task(); assert(last_consumer==KEYCODE2CONSUMER(KC_AUDIO_VOL_DOWN));
      scan(t+230U,td_first ? 1 : 0,false); task(); assert(last_consumer==0);
    }
    /* Ordinary keys alone keep QMK's single-slot release. */
    reset_fixture(TD(0),46,4999); tick(4999);
    other_keycode=KC_AUDIO_VOL_UP; third_keycode=KC_AUDIO_VOL_DOWN;
    scan(5000,1,true); scan(5010,2,true); assert(last_consumer==KEYCODE2CONSUMER(KC_AUDIO_VOL_DOWN));
    scan(5020,1,false); assert(last_consumer==0); scan(5030,2,false);
  } else if (!strcmp(name,"osl_hold")) {
    /* The press consumes the one-shot layer; the dance still decides Tap or Hold. */
    tapdance_state[0].actions[1]=MO(2);
    set_oneshot_layer(1,ONESHOT_START); clear_oneshot_layer_state(ONESHOT_PRESSED);
    assert(layer_state==2);
    scan(1000,0,true); assert(!is_oneshot_layer_active() && layer_state==0);
    tick(1201); assert(layer_state==4 && caps_press_count==0);
    scan(1210,0,false); task(); assert(layer_state==0 && caps_press_count==0);
  } else if (!strcmp(name,"tap_edge")) {
    /* A tap on a usage another key holds is one host press; that key stays down. */
    tapdance_state[0].actions[0]=KC_A; other_keycode=KC_A;
    scan(1000,1,true); require_shared_a=true;
    scan(1010,0,true); scan(1020,0,false); task();
    assert(shared_a_gaps==1 && is_key_pressed(KC_A));
    require_shared_a=false; scan(1030,1,false); task(); assert(!is_key_pressed(KC_A));
    /* The same holds for an ordinary LT tap while a TD hold owns the usage. */
    tapdance_state[0].actions[1]=KC_A; other_keycode=LT(1,KC_A);
    scan(2000,0,true); tick(2201); assert(is_key_pressed(KC_A));
    shared_a_gaps=0; require_shared_a=true;
    scan(2210,1,true); scan(2220,1,false); task();
    assert(shared_a_gaps==1 && is_key_pressed(KC_A));
    require_shared_a=false; scan(2230,0,false); task(); assert(!is_key_pressed(KC_A));
  } else if (!strcmp(name,"osl_double_hold")) {
    /* A dance that taps OSL and then holds consumes the one-shot layer. */
    tapdance_state[0]=(tapdance_slot_state_t){{QK_ONE_SHOT_LAYER|1,KC_A,KC_B,KC_NO},200};
    scan(1000,0,true); scan(1010,0,false); tick(1030);
    scan(1040,0,true); tick(1241);
    assert(is_key_pressed(KC_A) && !is_oneshot_layer_active() && layer_state==0);
    scan(1300,0,false); tick(1320); assert(layer_state==0 && !is_key_pressed(KC_A));
  } else if (!strcmp(name,"rolling_edge") || !strcmp(name,"tap_code_edge")) {
    /* A second keystroke of a usage that a dance holds reaches the host. */
    const bool rolling=!strcmp(name,"rolling_edge");
    other_keycode=KC_A;
    if(rolling) {tapdance_state[0].actions[0]=KC_A; scan(1000,0,true);}
    else {tapdance_state[0].actions[1]=KC_A; scan(1000,0,true); tick(1201); assert(is_key_pressed(KC_A));}
    require_shared_a=true;
    if(rolling) scan(1010,1,true); else tap_code(KC_A);
    assert(shared_a_gaps==1 && is_key_pressed(KC_A));
    scan(1210,0,false); task(); assert(is_key_pressed(KC_A)==rolling);
    require_shared_a=false;
    if(rolling) {scan(1220,1,false); task();}
    assert(!is_key_pressed(KC_A));
  } else if (!strcmp(name,"queued_layer") || !strcmp(name,"queued_remap")) {
    /* A retired record keeps its TD identity only if VIA rewrote its entry;
     * otherwise it resolves on the layer the LT hold selected. */
    const bool remap=!strcmp(name,"queued_remap");
    mapped_keycode=LT(1,KC_Z); other_keycode=TD(0); layered_other_keycode=KC_C;
    scan(1000,0,true); tick(1010); scan(1020,1,true);
    clear_keyboard();
    if(remap) other_keycode=layered_other_keycode=KC_X;
    tick(1230);
    assert((layer_state & 2U) && !is_key_pressed(KC_C) == remap && !is_key_pressed(KC_X));
    scan(1300,1,false); scan(1310,0,false); task();
    assert(layer_state==0 && !is_key_pressed(KC_C) && !is_key_pressed(KC_X));
    assert(!tap_dance_states[1].in_use);
  } else if (!strcmp(name,"retro_release") || !strcmp(name,"retro_hold_lt")) {
    /* A dance between an LT hold and its release is an intervening key. */
    const bool hold_lt=!strcmp(name,"retro_hold_lt");
    fixture_retro=true;
    tapdance_state[1]=(tapdance_slot_state_t){{KC_B,KC_NO,KC_C,KC_NO},200};
    if(hold_lt) {tapdance_state[0]=(tapdance_slot_state_t){{KC_CAPS,LT(1,KC_A),KC_NO,KC_NO},200}; other_keycode=TD(1);}
    else {mapped_keycode=LT(1,KC_A); other_keycode=TD(1);}
    scan(1000,0,true); tick(1201); assert(layer_state==2);
    scan(1210,1,true); scan(1220,1,false);
    forbid_a=true; scan(1230,0,false); task(); tick(1500);
    assert(forbidden_a_reports==0);
  } else if (!strcmp(name,"double_cancel")) {
    /* A cancelled dance whose up crossed a second boundary is still retired. */
    mapped_keycode=LT(2,KC_B); other_keycode=TD(0);
    scan(1000,0,true); tick(1000); scan(1010,1,true); tick(1010);
    clear_keyboard(); scan(1020,1,false); tick(1020); clear_keyboard();
    scan(1030,0,false); tick(1030); tick(1500);
    assert(!tap_dance_states[1].in_use && caps_press_count==0);
  } else if (!strcmp(name,"mod_replay") || !strcmp(name,"mod_rolloff")) {
    /* A tap replays the modifiers held at its press, a TD hold's included. */
    const bool rolloff=!strcmp(name,"mod_rolloff");
    tapdance_state[0]=(tapdance_slot_state_t){{KC_ESC,KC_LCTL,KC_NO,KC_NO},200};
    tapdance_state[1]=rolloff ? (tapdance_slot_state_t){{KC_A,KC_X,KC_NO,KC_NO},200} : (tapdance_slot_state_t){{KC_A,KC_NO,KC_Z,KC_NO},200};
    other_keycode=TD(1); last_a_mods=0xFF;
    scan(1000,0,true); tick(1201); assert(get_mods()==MOD_BIT(KC_LCTL));
    scan(1210,1,true); tick(1211);
    if(rolloff) {scan(1220,0,false); tick(1221); scan(1230,1,false); tick(1231);}
    else {scan(1230,1,false); tick(1231); scan(1250,0,false); tick(1251);}
    tick(1500);
    assert(last_a_mods==MOD_BIT(KC_LCTL) && get_mods()==0 && get_weak_mods()==0 && !is_key_pressed(KC_A));
  } else if (!strcmp(name,"retro_swallow")) {
    /* A key pressed after an LT/MT hold cancels its retro tap, a swallowed
     * cancelled dance up notwithstanding. */
    fixture_retro=true;
    tapdance_state[0]=(tapdance_slot_state_t){{KC_ESC,KC_LSFT,KC_NO,KC_NO},200};
    other_keycode=MT(MOD_LSFT,KC_A); third_keycode=KC_B;
    scan(1000,0,true); tick(1201); scan(1210,1,true); tick(1411);
    scan(1420,2,true); tick(1421); scan(1430,2,false); tick(1431);
    clear_keyboard(); scan(1450,0,false); tick(1451);
    forbid_a=true; scan(1460,1,false); tick(1461); tick(1600);
    assert(forbidden_a_reports==0 && get_mods()==0);
  } else if (!strcmp(name,"stale_tombstone")) {
    /* A press at a cancelled position whose up was lost supersedes it. */
    mapped_keycode=LT(1,KC_Z); other_keycode=TD(0); layered_other_keycode=KC_LSFT;
    tap_dance_states[1]=(tap_dance_state_t){.key={.row=0,.col=1},.type=KEY_EVENT,.runtime_index=1,
                                            .pressed=true,.in_use=true,.cancelled=true,.index=0};
    scan(1000,0,true); tick(1010); scan(1020,1,true);
    clear_keyboard(); tick(1230);
    assert((layer_state & 2U) && get_mods()==MOD_BIT(KC_LSFT));
    scan(1300,1,false); scan(1310,0,false); task();
    assert(get_mods()==0 && layer_state==0 && !tap_dance_states[1].in_use);
  } else if (!strcmp(name,"report_only")) {
    tapdance_state[0].actions[1]=KC_A; scan(1000,0,true); tick(1201);
    report_keyboard_t saved=*keyboard_report; clear_keys(); send_keyboard_report();
    *keyboard_report=saved; send_keyboard_report(); assert(is_key_pressed(KC_A));
    scan(1210,0,false); assert(!is_key_pressed(KC_A));
  } else if (!strcmp(name,"retro_td_position") || !strcmp(name,"retro_td_col0") || !strcmp(name,"retro_td_lt") ||
             !strcmp(name,"retro_td_other") || !strcmp(name,"retro_td_off")) {
    /* A dance hold's LT/MT release follows the retro setting at any position:
     * the dance's synthesized records carry its own position. */
    const bool col0=!strcmp(name,"retro_td_col0"), other=!strcmp(name,"retro_td_other"), off=!strcmp(name,"retro_td_off");
    const uint8_t td_col=col0 ? 0 : 1, d_col=col0 ? 1 : 0;
    fixture_retro=!off; forbid_a=true;
    tapdance_state[1]=(tapdance_slot_state_t){{KC_ESC,!strcmp(name,"retro_td_lt") ? LT(1,KC_A) : MT(MOD_LCTL,KC_A),KC_NO,KC_NO},200};
    if(col0) {mapped_keycode=TD(1); other_keycode=KC_D;} else {mapped_keycode=KC_D; other_keycode=TD(1);}
    scan(1000,td_col,true); tick(1201);
    if(other) {scan(1210,d_col,true); scan(1220,d_col,false);}
    scan(1300,td_col,false); task(); tick(1400);
    assert((forbidden_a_reports>0) == !(other || off));
    assert(get_mods()==0 && layer_state==0 && !tap_dance_states[td_col].in_use);
  } else if (!strcmp(name,"retro_held_mods")) {
    /* A retro tap adds only the tap-time modifiers that are up now. */
    fixture_retro=true; mapped_keycode=KC_LCTL; other_keycode=MT(MOD_LSFT,KC_A);
    scan(1000,0,true); scan(1010,1,true); scan(1020,1,false); task(); tick(1300);
    scan(1310,1,true); tick(1600); scan(1610,1,false); task(); tick(1700);
    assert(get_mods()==MOD_BIT(KC_LCTL) && keyboard_report->mods==MOD_BIT(KC_LCTL));
    scan(1800,0,false); task(); assert(get_mods()==0);
  } else if (!strcmp(name,"retro_key_roll")) {
    /* A roll into a tap-hold key keeps its retro tap (QMK's retro rewrite). */
    fixture_retro=true; mapped_keycode=KC_B; other_keycode=MT(MOD_LSFT,KC_A); forbid_a=true;
    scan(1000,0,true); scan(1010,1,true); scan(1020,0,false); tick(1300);
    scan(1310,1,false); task(); tick(1400);
    assert(forbidden_a_reports>0 && get_mods()==0);
  } else if (!strcmp(name,"osl_layer_key")) {
    /* A layer key on a one-shot layer gets no early synthetic up. */
    mapped_keycode=QK_ONE_SHOT_LAYER | 1; other_keycode=layered_other_keycode=QK_LAYER_TAP_TOGGLE | 2;
    scan(1000,0,true); scan(1010,0,false); task(); assert(layer_state & 2U);
    scan(1100,1,true); scan(1110,1,false); task(); tick(1400);
    assert(layer_state==0);
  } else if (!strcmp(name,"fallback_mods") || !strcmp(name,"fallback_mods_second")) {
    /* A Double Hold's embedded tap carries the modifiers of either press. */
    const bool second=!strcmp(name,"fallback_mods_second");
    mapped_keycode=KC_LSFT; other_keycode=TD(1); last_a_mods=0xFF;
    tapdance_state[1]=(tapdance_slot_state_t){{KC_A,KC_X,KC_Z,KC_NO},200};
    scan(1000,0,true); scan(1010,1,true); scan(1020,1,false);
    if(!second) scan(1030,0,false);
    scan(1040,1,true);
    if(second) scan(1050,0,false);
    tick(1300);
    assert(last_a_mods==MOD_BIT(KC_LSFT) && is_key_pressed(KC_X) && keyboard_report->mods==0);
    scan(1400,1,false); task(); tick(1500);
    assert(get_mods()==0 && get_weak_mods()==0 && !is_key_pressed(KC_X));
  } else if (!strcmp(name,"quantum_tap") || !strcmp(name,"quantum_hold")) {
    /* A dance action without a QMK action reaches the quantum handlers. */
    const bool hold=!strcmp(name,"quantum_hold");
    other_keycode=TD(1);
    tapdance_state[1]=hold ? (tapdance_slot_state_t){{KC_A,QK_USER_1,KC_NO,KC_NO},200}
                           : (tapdance_slot_state_t){{QK_USER_1,KC_NO,KC_NO,KC_NO},200};
    scan(1000,1,true);
    if(hold) {tick(1300); assert(quantum_log_len==1 && quantum_log[0]==QK_USER_1 && quantum_log_down[0]);}
    scan(1400,1,false); task(); tick(1500);
    assert(quantum_log_len==2 && quantum_log[0]==QK_USER_1 && quantum_log_down[0]);
    assert(quantum_log[1]==QK_USER_1 && !quantum_log_down[1] && !tap_dance_states[1].in_use);
  } else if (!strcmp(name,"quantum_nested")) {
    /* A dance never starts another dance from its output. */
    other_keycode=TD(1); forbid_a=true;
    tapdance_state[1]=(tapdance_slot_state_t){{TD(2),KC_NO,KC_NO,KC_NO},200};
    tapdance_state[2]=(tapdance_slot_state_t){{KC_A,KC_NO,KC_NO,KC_NO},200};
    scan(1000,1,true); scan(1010,1,false); task(); tick(1500);
    assert(quantum_log_len==0 && forbidden_a_reports==0);
  } else if (!strcmp(name,"quantum_clear") || !strcmp(name,"quantum_clear_hold")) {
    /* A keyboard clear from a dance's own output waits for that dance; the
     * held output it cancels still receives its up. */
    const bool hold=!strcmp(name,"quantum_clear_hold");
    mapped_keycode=TD(2); other_keycode=TD(1);
    tapdance_state[1]=hold ? (tapdance_slot_state_t){{KC_B,QK_USER_0,KC_NO,KC_NO},200}
                           : (tapdance_slot_state_t){{QK_USER_0,KC_NO,KC_NO,KC_NO},200};
    tapdance_state[2]=(tapdance_slot_state_t){{KC_B,KC_LCTL,KC_NO,KC_NO},200};
    scan(1000,0,true); tick(1201); assert(get_mods()==MOD_BIT(KC_LCTL));
    scan(1210,1,true);
    if(hold) {tick(1450); assert(tap_dance_states[1].in_use && !tap_dance_states[1].cancelled && quantum_log_len==1);}
    else scan(1220,1,false);
    task();
    assert(get_mods()==0 && tap_dance_states[0].cancelled);
    if(hold) {scan(1460,1,false); task();}
    assert(quantum_log_len==2 && quantum_log_down[0] && !quantum_log_down[1]);
    scan(1500,0,false); task(); tick(1700);
    assert(get_mods()==0 && !tap_dance_states[0].in_use && !tap_dance_states[1].in_use);
  } else if (!strcmp(name,"own_clear_hold")) {
    /* A clear from the Double Hold fallback's own tap retires the other dances
     * but not this one: its hold continues, and its newer up is still its
     * owned release after a remap. */
    other_keycode=TD(1);
    tapdance_state[1]=(tapdance_slot_state_t){{QK_USER_0,KC_X,KC_Z,KC_NO},200};
    scan(1000,1,true); scan(1010,1,false); scan(1020,1,true); tick(1300);
    assert(is_key_pressed(KC_X) && tap_dance_states[1].in_use && !tap_dance_states[1].cancelled);
    other_keycode=KC_B;
    scan(1400,1,false); task(); tick(1500);
    assert(!is_key_pressed(KC_X) && !tap_dance_states[1].in_use);
  } else if (!strcmp(name,"dispatch_epoch")) {
    /* A dance started after a clear inside the same dispatch owns its up. */
    mapped_keycode=LT(1,KC_D); other_keycode=TD(1); third_keycode=TD(2); forbid_a=true;
    tapdance_state[1]=(tapdance_slot_state_t){{QK_USER_0,KC_NO,KC_NO,KC_NO},200};
    tapdance_state[2]=(tapdance_slot_state_t){{KC_A,KC_LCTL,KC_NO,KC_NO},200};
    scan(1000,0,true); scan(1020,1,true); scan(1040,2,true); scan(1060,2,false); tick(1250);
    scan(1300,0,false); task(); tick(1340);
    scan(1350,1,false); task(); tick(1700);
    assert(forbidden_a_reports>0 && get_mods()==0 && keyboard_report->mods==0);
    assert(!tap_dance_states[1].in_use && !tap_dance_states[2].in_use);
  } else if (!strcmp(name,"own_clear_chord")) {
    /* The fallback's clearing tap retires another dance's Ctrl before this
     * dance's hold, so the host never sees Ctrl with the held key. */
    mapped_keycode=TD(2); other_keycode=TD(1); last_a_mods=0xFF;
    tapdance_state[1]=(tapdance_slot_state_t){{QK_USER_0,KC_A,KC_Z,KC_NO},200};
    tapdance_state[2]=(tapdance_slot_state_t){{KC_B,KC_LCTL,KC_NO,KC_NO},200};
    scan(1000,0,true); tick(1201); assert(get_mods()==MOD_BIT(KC_LCTL));
    scan(1210,1,true); scan(1220,1,false); scan(1230,1,true); tick(1500);
    assert(last_a_mods==0 && is_key_pressed(KC_A) && get_mods()==0);
    scan(1600,1,false); scan(1610,0,false); task(); tick(1800);
    assert(!is_key_pressed(KC_A) && get_mods()==0 && !tap_dance_states[0].in_use && !tap_dance_states[1].in_use);
  } else if (!strcmp(name,"tap_only_release")) {
    /* No later press can change a tap-only outcome: its release decides it. */
    other_keycode=TD(1); last_a_mods=0xFF;
    tapdance_state[1]=(tapdance_slot_state_t){{KC_A,KC_NO,KC_NO,KC_NO},200};
    scan(1000,1,true); scan(1010,1,false); task();
    assert(last_a_mods!=0xFF && !tap_dance_states[1].in_use);
  } else if (!strcmp(name,"storage_reset")) {
    /* A TD storage reset retires held dances, as the EERRAA reset does. */
    tapdance_state[0]=(tapdance_slot_state_t){{KC_B,KC_LCTL,KC_NO,KC_NO},200};
    scan(1000,0,true); tick(1201); assert(get_mods()==MOD_BIT(KC_LCTL));
    tapdance_storage_apply_defaults();
    assert(get_mods()==0 && tap_dance_states[0].cancelled);
    scan(1300,0,false); task(); tick(1400);
    assert(get_mods()==0 && !tap_dance_states[0].in_use);
  } else { assert(!"unknown lifetime case"); }
  printf("PASS lifetime %s\n",name);
}
