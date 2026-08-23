# Graph Report - eerraa-qmk-h7s-fw-via2  (2026-08-24)

## Corpus Check
- 364 files · ~204,758 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 2174 nodes · 4867 edges · 126 communities (123 shown, 3 thin omitted)
- Extraction: 76% EXTRACTED · 24% INFERRED · 0% AMBIGUOUS · INFERRED: 1149 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `696fbda7`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- 디바운스 알고리즘/프로파일
- 프로젝트 문서·가이드
- 퀀텀 코어·부트로더 리셋
- QSPI 플래시 (W25Q16JV)
- USB 디스크립터 3종
- rgblight 코어
- 액션·호스트 리포트 전송
- 시퀀서 테스트
- keyboard 태스크·매트릭스 브리지
- USB CDC 클래스
- 액션 레이어
- 액션 실행 코어
- USB HID 클래스·인터벌
- HID 리포트 타입 정의
- send_string·키 전송 유틸
- usbd_conf (HAL PCD 연동)
- ST USB Device Core
- 서스펜드·LED 절전
- USB 컴포지트 클래스
- CLI GUI
- 매트릭스 스캔·계측
- USB 코어·BootMode 관리
- eeconfig·EEPROM 읽기
- Tap Dance 포트
- Tap Dance 퀀텀 처리
- 로깅·컬러 유틸
- ap 메인 루프
- VIA 코어
- 시퀀서 코어
- 보드별 WS2812 드라이버
- 키코드 변환·리포트
- I2C 드라이버
- CLI 코어
- Key Override·원샷 모드
- CDC 상위 계층
- rgblight 인디케이터 설정
- EEPROM 비동기 큐
- 자동 팩토리 리셋·IRQ 핸들러
- USB 컨트롤 요청 (ctlreq)
- CLI 부가 명령·펌웨어 태그
- ZD24C128 EEPROM 드라이버
- 플래시 EEPROM 에뮬레이션
- 다이나믹 키맵
- HID 계측·micros
- rgblight 이펙트
- SPI 드라이버
- EEPROM 팩토리 기본값
- kill_switch·kkuk
- 퀀텀 헤더 모음
- rgblight 설정 저장
- syscalls 스텁
- UART·CLI 입출력
- 인디케이터 포트 (intigrity80)
- 액션 태핑 코어
- Tapping Term 포트
- 드라이버 초기화·로그
- UF2 변환 도구
- 키맵 인트로스펙션
- process_rgb 키코드
- rgblight 동기화/토글
- 인디케이터 포트 (brick60)
- RTC 드라이버
- 인디케이터 포트 (sculpturei)
- 인디케이터 포트 (may65)
- 인디케이터 포트 (brick65)
- 보드별 상태 LED 콜백
- rgblight pulse 이펙트
- USB 모니터 포트
- 버튼 드라이버
- 시퀀서 테스트 목
- 시퀀서 페이즈 태스크
- 퀀텀 키보드 헤더
- 공통 코어 유틸 (qbuffer/crc)
- rgblight 렌더 큐·오버레이
- 타이머 포트·키보드 초기화
- eeconfig 업데이트
- BSP 클럭·MSP 초기화
- USB IO 요청 (ioreq)
- via_hid RAW HID 브리지
- HID 리포트 전송 경로
- BootMode VIA 인코딩
- 링 버퍼
- I2C/SPI CLI 보조
- 버전 VIA 포트
- 시퀀서 트랙 활성화
- graphify 부트스트랩
- usb.h BootMode 타입
- debounce.h
- matrix_get_row
- timer.c
- asym_eager_defer_pk.c
- debounce.h
- rgblight_sethsv_range
- via_qmk_version
- is_sequencer_track_active

## God Nodes (most connected - your core abstractions)
1. `TEST_F()` - 63 edges
2. `process_action()` - 55 edges
3. `logPrintf()` - 51 edges
4. `cliPrintf()` - 40 edges
5. `millis()` - 32 edges
6. `raw_hid_receive()` - 30 edges
7. `hwInit()` - 26 edges
8. `timer_read()` - 23 edges
9. `era_usb_diagnostics_via_command()` - 20 edges
10. `register_code()` - 20 edges

## Surprising Connections (you probably didn't know these)
- `RGBLIGHT_ENABLE 자동 감지 및 rgblight 소스 포함` --references--> `rgblight 서브시스템`  [INFERRED]
  src/ap/modules/qmk/CMakeLists.txt → docs/rgblight.md
- `matrix_task()` --calls--> `matrix_print()`  [INFERRED]
  src/ap/modules/qmk/quantum/keyboard.c → src/ap/modules/qmk/port/matrix.c
- `eeconfig_update_debug()` --calls--> `eeprom_update_byte()`  [INFERRED]
  src/ap/modules/qmk/quantum/eeconfig.c → src/ap/modules/qmk/port/platforms/eeprom.c
- `eeconfig_update_handedness()` --calls--> `eeprom_update_byte()`  [INFERRED]
  src/ap/modules/qmk/quantum/eeconfig.c → src/ap/modules/qmk/port/platforms/eeprom.c
- `eeconfig_update_haptic()` --calls--> `eeprom_update_dword()`  [INFERRED]
  src/ap/modules/qmk/quantum/eeconfig.c → src/ap/modules/qmk/port/platforms/eeprom.c

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **VIA 커스텀 채널 기능군 (ch13 USB / ch15 Tapping / ch16 TapDance over RAW HID)** — docs_features_bootmode_via_channel13, docs_features_tapping_runtime_tapping_term, docs_features_tapdance_tap_dance_feature, docs_features_keyinput_via_raw_hid_path [INFERRED 0.85]

## Communities (126 total, 3 thin omitted)

### Community 0 - "디바운스 알고리즘/프로파일"
Cohesion: 0.19
Nodes (26): debounce_profile_status_t, debounce_profile_storage_t, debounce_profile_values_t, debounce_runtime_config_t, debounce_profile_apply_current(), debounce_profile_apply_defaults_locked(), debounce_profile_clamp_delay(), debounce_profile_current() (+18 more)

### Community 1 - "프로젝트 문서·가이드"
Cohesion: 0.06
Nodes (56): 에이전트 작업 가이드 (AGENTS.md), Graphify 지식그래프 우선 탐색 워크플로, VYYMMDDRn 변경 이력 규칙, Claude Code 전용 지침 (CLAUDE.md), baram-qmk-h7s 최상위 CMake 빌드, KBD_NAME/_DEF_FIRMWARE_VERSION 추출 로직, UF2 변환 POST_BUILD 커맨드 (uf2conv.py, family 0xFFFF0002), 결정 기록 (DECISIONS.md) (+48 more)

### Community 2 - "퀀텀 코어·부트로더 리셋"
Cohesion: 0.05
Nodes (63): bootloader_jump(), bootloader_jump_deferred(), bootloader_schedule_deferred_reset(), mcu_reset(), mcu_reset_deferred(), eeprom_req_clean(), via_qmk_sys_get_value(), via_qmk_sys_set_value() (+55 more)

### Community 4 - "QSPI 플래시 (W25Q16JV)"
Cohesion: 0.08
Nodes (48): QSPI_Info, qspi_info_t, cli_args_t, cliFlash(), flashErase(), flashInit(), flashInSector(), flashRead() (+40 more)

### Community 5 - "USB 디스크립터 3종"
Cohesion: 0.07
Nodes (39): USBD_SpeedTypeDef, Get_SerialNum(), IntToUnicode(), USBD_CDC_ConfigStrDescriptor(), USBD_CDC_DeviceDescriptor(), USBD_CDC_InterfaceStrDescriptor(), USBD_CDC_LangIDStrDescriptor(), USBD_CDC_ManufacturerStrDescriptor() (+31 more)

### Community 6 - "rgblight 코어"
Cohesion: 0.07
Nodes (41): eeconfig_update_rgblight_current(), rgblight_blink_layer(), rgblight_blink_layer_repeat(), rgblight_blink_layer_repeat_helper(), rgblight_decrease_hue(), rgblight_decrease_hue_helper(), rgblight_decrease_hue_noeeprom(), rgblight_decrease_sat() (+33 more)

### Community 7 - "액션·호스트 리포트 전송"
Cohesion: 0.07
Nodes (43): oneshot_fullfillment_t, timer_read(), report_keyboard_t, report_nkro_t, host_keyboard_send(), host_nkro_send(), has_anykey(), add_oneshot_locked_mods() (+35 more)

### Community 8 - "시퀀서 테스트"
Cohesion: 0.05
Nodes (42): TEST_F(), TestGetActiveTracks, TestGetActiveTracksOutOfBound, TestGetBeatDuration, TestGetStepDuration120, TestGetStepDuration60, TestIncreaseTempoMax, TestIsStepOffForGivenTrack (+34 more)

### Community 9 - "keyboard 태스크·매트릭스 브리지"
Cohesion: 0.10
Nodes (16): generate_tick_event(), housekeeping_task(), housekeeping_task_kb(), housekeeping_task_user(), is_keyboard_master(), keyboard_post_init_kb(), keyboard_post_init_user(), keyboard_task() (+8 more)

### Community 10 - "USB CDC 클래스"
Cohesion: 0.16
Nodes (20): debounce_algo_entry_t, debounce_runtime_error_t, debounce_runtime_type_t, debounce_runtime_config_t, matrix_row_t, debounce(), debounce_free(), debounce_init() (+12 more)

### Community 11 - "액션 레이어"
Cohesion: 0.20
Nodes (27): layer_state_t, default_layer_and(), default_layer_or(), default_layer_set(), default_layer_state_set(), default_layer_state_set_kb(), default_layer_state_set_user(), default_layer_xor() (+19 more)

### Community 12 - "액션 실행 코어"
Cohesion: 0.13
Nodes (20): action_t, keyrecord_t, debug_action(), get_hold_on_other_key_press(), get_retro_tapping(), is_tap_action(), is_tap_record(), default_layer_debug() (+12 more)

### Community 13 - "USB HID 클래스·인터벌"
Cohesion: 0.13
Nodes (37): usb_diagnostics_snapshot_t, UsbBootMode_t, era_usb_diagnostics_via_command(), eraUsbDiagnosticsBytesAreZero(), eraUsbDiagnosticsChunkCount(), eraUsbDiagnosticsEncodeCapabilities(), eraUsbDiagnosticsEncodeSnapshotChunk(), eraUsbDiagnosticsExpectedIntervalUs() (+29 more)

### Community 14 - "HID 리포트 타입 정의"
Cohesion: 0.06
Nodes (43): digitizer_t, host_driver_t, joystick_t, report_digitizer_t, report_joystick_t, report_programmable_button_t, timer_elapsed(), report_mouse_t (+35 more)

### Community 15 - "send_string·키 전송 유틸"
Cohesion: 0.12
Nodes (36): keyrecord_t, kill_switch_process(), kkuk_idle(), wait_ms(), led_t, host_consumer_send(), host_keyboard_led_state(), host_system_send() (+28 more)

### Community 16 - "usbd_conf (HAL PCD 연동)"
Cohesion: 0.12
Nodes (35): HAL_StatusTypeDef, PCD_HandleTypeDef, Error_Handler(), HAL_MspInit(), USBD_HandleTypeDef, USBD_SpeedTypeDef, USBD_StatusTypeDef, HAL_PCD_ConnectCallback() (+27 more)

### Community 17 - "ST USB Device Core"
Cohesion: 0.36
Nodes (4): matrix_row_t, debounce_sym_eager_pk_run(), transfer_matrix_values(), update_debounce_counters()

### Community 18 - "서스펜드·LED 절전"
Cohesion: 0.12
Nodes (23): matrix_power_down(), matrix_power_up(), suspend_power_down(), suspend_power_down_kb(), suspend_power_down_user(), suspend_wakeup_condition(), suspend_wakeup_init(), suspend_wakeup_init_kb() (+15 more)

### Community 19 - "USB 컴포지트 클래스"
Cohesion: 0.17
Nodes (25): __IO, USBD_ClassTypeDef, USBD_CompositeClassTypeDef, USBD_HandleTypeDef, USBD_CMPSIT_AddClass(), USBD_CMPSIT_AddConfDesc(), USBD_CMPSIT_AddToConfDesc(), USBD_CMPSIT_AssignEp() (+17 more)

### Community 20 - "CLI GUI"
Cohesion: 0.16
Nodes (33): cli_gui_api_t, cliPutch(), addCh_Or_InsCh(), addChar(), addPrintf(), addStr(), clear(), clearToEol() (+25 more)

### Community 21 - "매트릭스 스캔·계측"
Cohesion: 0.11
Nodes (16): cli_args_t, cliCmd(), matrixInstrumentationCaptureStart(), matrixInstrumentationGetScanTime(), matrixInstrumentationIsCompileEnabled(), matrixInstrumentationLogScan(), matrixInstrumentationReset(), matrix_info() (+8 more)

### Community 22 - "USB 코어·BootMode 관리"
Cohesion: 0.12
Nodes (28): bootmode_decode_via_value(), bootmode_encode_via_value(), bootmode_sync_pending(), UsbBootMode_t, via_qmk_usb_bootmode_command(), bootmode_ensure_default_persisted(), bootmode_init(), cli_args_t (+20 more)

### Community 23 - "eeconfig·EEPROM 읽기"
Cohesion: 0.11
Nodes (29): matrix_init(), eeprom_read_word(), eeprom_update_word(), cli_args_t, cliQmk(), eeconfig_disable(), eeconfig_enable(), eeconfig_init() (+21 more)

### Community 24 - "Tap Dance 포트"
Cohesion: 0.14
Nodes (31): tap_dance_state_t, tapdance_apply_defaults_locked(), tapdance_commit(), tapdance_field_index(), tapdance_get_term_ms(), tapdance_get_value(), tapdance_handle_via_command(), tapdance_init() (+23 more)

### Community 25 - "Tap Dance 퀀텀 처리"
Cohesion: 0.16
Nodes (32): usbBegin(), USBD_ClassTypeDef, USBD_CompositeClassTypeDef, USBD_HandleTypeDef, USBD_SpeedTypeDef, USBD_StatusTypeDef, USBD_ClrClassConfig(), USBD_CoreFindEP() (+24 more)

### Community 26 - "로깅·컬러 유틸"
Cohesion: 0.20
Nodes (7): HSV, RGB, rgb_led_t, convert_rgb_to_rgbw(), hsv_to_rgb(), hsv_to_rgb_impl(), hsv_to_rgb_nocie()

### Community 27 - "ap 메인 루프"
Cohesion: 0.11
Nodes (24): i2c_ready_wait_stats_t, log_buf_t, apInit(), cliAdd(), cliInit(), cliOpen(), cliOpenLog(), cliWrite() (+16 more)

### Community 28 - "VIA 코어"
Cohesion: 0.12
Nodes (27): eeconfig_update_audio(), via_command_kb(), via_custom_value_command(), via_custom_value_command_kb(), via_get_layout_options(), via_init(), via_init_kb(), via_qmk_audio_command() (+19 more)

### Community 29 - "시퀀서 코어"
Cohesion: 0.12
Nodes (20): sequencer_resolution_t, get_beat_duration(), get_step_duration(), is_sequencer_on(), is_sequencer_step_on(), sequencer_decrease_resolution(), sequencer_decrease_tempo(), sequencer_get_beat_duration() (+12 more)

### Community 30 - "보드별 WS2812 드라이버"
Cohesion: 0.13
Nodes (16): rgb_led_t, ws2812_setleds(), rgb_led_t, ws2812_setleds(), rgb_led_t, ws2812_setleds(), rgb_led_t, ws2812_setleds() (+8 more)

### Community 31 - "키코드 변환·리포트"
Cohesion: 0.25
Nodes (3): action_for_key(), keypos_t, keymap_key_to_keycode()

### Community 32 - "I2C 드라이버"
Cohesion: 0.12
Nodes (21): I2C_HandleTypeDef, millis(), eepromWaitReady(), cli_args_t, cliI2C(), delayUs(), HAL_I2C_ErrorCallback(), HAL_I2C_MspDeInit() (+13 more)

### Community 33 - "CLI 코어"
Cohesion: 0.14
Nodes (22): cli_t, cli_args_t, __WEAK, cliLineAdd(), cliLineChange(), cliLineClean(), cliLoopIdle(), cliMemoryDump() (+14 more)

### Community 34 - "Key Override·원샷 모드"
Cohesion: 0.17
Nodes (18): key_override_t, clear_suppressed_override_mods(), clear_weak_override_mods(), get_mods(), get_oneshot_locked_mods(), get_oneshot_mods(), get_weak_mods(), keyrecord_t (+10 more)

### Community 35 - "CDC 상위 계층"
Cohesion: 0.15
Nodes (15): cdcAvailable(), cdcGetType(), cdcInit(), cdcIsConnect(), cdcRead(), cdcWrite(), cdcIfAvailable(), cdcIfGetType() (+7 more)

### Community 36 - "rgblight 인디케이터 설정"
Cohesion: 0.18
Nodes (19): rgblight_indicator_config_t, rgblight_indicator_range_t, led_t, rgblight_indicator_any_pending_render(), rgblight_indicator_apply_host_led(), rgblight_indicator_apply_target_range(), rgblight_indicator_commit_state(), rgblight_indicator_compute_color() (+11 more)

### Community 37 - "EEPROM 비동기 큐"
Cohesion: 0.19
Nodes (17): eeprom_write_t, qbuffer_t, eeprom_flush_pending(), eeprom_is_pending(), eeprom_peek_queue_entry(), eeprom_update(), via_hid_task(), qbufferAvailable() (+9 more)

### Community 38 - "자동 팩토리 리셋·IRQ 핸들러"
Cohesion: 0.21
Nodes (29): USBD_SetupReqTypedef, USBD_CDC_Setup(), USBD_SetupReqTypedef, USBD_HID_Setup(), USBD_LL_SetupStage(), USBD_HandleTypeDef, USBD_SetupReqTypedef, USBD_StatusTypeDef (+21 more)

### Community 39 - "USB 컨트롤 요청 (ctlreq)"
Cohesion: 0.10
Nodes (32): USBD_HandleTypeDef, USBD_HandleTypeDef, CDC_Control_FS(), CDC_DeInit_FS(), CDC_Init_FS(), CDC_Receive_FS(), CDC_SoF_ISR(), CDC_Transmit_FS() (+24 more)

### Community 40 - "CLI 부가 명령·펌웨어 태그"
Cohesion: 0.22
Nodes (16): eeconfig_init_user_datablock(), eeprom_apply_factory_defaults(), eeprom_get_burst_extra_calls(), eeprom_get_write_pending_max(), eeprom_is_burst_mode_active(), eeprom_restore_auto_factory_reset_sentinel(), eeprom_update_block(), eeprom_update_dword() (+8 more)

### Community 41 - "ZD24C128 EEPROM 드라이버"
Cohesion: 0.22
Nodes (8): eeprom_init(), raw_hid_send(), via_hid_init(), via_hid_print(), via_hid_receive(), qmkInit(), qbufferCreateBySize(), usbHidSetViaReceiveFunc()

### Community 42 - "플래시 EEPROM 에뮬레이션"
Cohesion: 0.21
Nodes (16): eeprom_get_write_overflow_count(), cli_args_t, cliEeprom(), eepromFormat(), eepromGetLength(), eepromInit(), eepromIsErasing(), eepromIsInit() (+8 more)

### Community 43 - "다이나믹 키맵"
Cohesion: 0.21
Nodes (22): era_state_sync_bump_config(), eeprom_update_byte(), dynamic_keymap_encoder_to_eeprom_address(), dynamic_keymap_get_encoder(), dynamic_keymap_get_keycode(), dynamic_keymap_get_layer_count(), dynamic_keymap_key_to_eeprom_address(), dynamic_keymap_macro_get_buffer_size() (+14 more)

### Community 44 - "HID 계측·micros"
Cohesion: 0.12
Nodes (6): sendchar_func_t, keyboard_pre_init_kb(), keyboard_pre_init_user(), keyboard_setup(), matrix_setup(), print_set_sendchar()

### Community 45 - "rgblight 이펙트"
Cohesion: 0.11
Nodes (32): animation_status_t, breathe_calc(), rgblight_effect_alternating(), rgblight_effect_breathing(), rgblight_effect_christmas(), rgblight_effect_dummy(), rgblight_effect_knight(), rgblight_effect_pulse_apply_output() (+24 more)

### Community 46 - "SPI 드라이버"
Cohesion: 0.13
Nodes (9): SPI_HandleTypeDef, HAL_SPI_ErrorCallback(), HAL_SPI_MspDeInit(), HAL_SPI_MspInit(), HAL_SPI_RxCpltCallback(), spiDmaTxIsDone(), spiDmaTxStart(), spiDmaTxTransfer() (+1 more)

### Community 47 - "EEPROM 팩토리 기본값"
Cohesion: 0.13
Nodes (17): eeprom_read_block(), eeprom_read_byte(), eeprom_read_dword(), dynamic_keymap_get_buffer(), dynamic_keymap_macro_get_buffer(), dynamic_keymap_macro_send(), eeconfig_is_kb_datablock_valid(), eeconfig_is_user_datablock_valid() (+9 more)

### Community 48 - "kill_switch·kkuk"
Cohesion: 0.27
Nodes (9): kill_switch_is_use(), via_qmk_kill_switch_get_value(), via_qmk_kill_switch_save(), via_qmk_kill_switch_set_value(), via_qmk_kill_swtich_command(), keyrecord_t, kkuk_process(), keyrecord_t (+1 more)

### Community 49 - "퀀텀 헤더 모음"
Cohesion: 0.17
Nodes (17): eeprom_get_write_pending_count(), cli_args_t, cliEeprom(), EE_EndOfCleanup_UserCallback(), eepromFormat(), eepromGetLength(), eepromInit(), eepromInitMPU() (+9 more)

### Community 50 - "rgblight 설정 저장"
Cohesion: 0.33
Nodes (9): eeconfig_debug_rgblight(), eeconfig_read_rgblight(), eeconfig_update_rgblight(), eeconfig_update_rgblight_default(), rgblight_check_config(), rgblight_init(), rgblight_mode_noeeprom(), rgblight_reload_from_eeprom() (+1 more)

### Community 51 - "syscalls 스텁"
Cohesion: 0.11
Nodes (3): _exit(), _kill(), _write()

### Community 52 - "UART·CLI 입출력"
Cohesion: 0.13
Nodes (16): cliUpdate(), cliAvailable(), cliGetPort(), cliMain(), cliRead(), cdcGetBaud(), cli_args_t, cliUart() (+8 more)

### Community 53 - "인디케이터 포트 (intigrity80)"
Cohesion: 0.17
Nodes (15): rgblight_indicator_target_callback_t, led_t, indicator_apply_defaults(), indicator_apply_led_ranges(), indicator_port_via_command(), indicator_target_from_host(), indicator_via_color_value(), indicator_via_get_value() (+7 more)

### Community 54 - "액션 태핑 코어"
Cohesion: 0.14
Nodes (27): action_exec(), keyevent_t, debug_event(), debug_record(), process_record_tap_hint(), action_tapping_process(), keyevent_t, keyrecord_t (+19 more)

### Community 55 - "Tapping Term 포트"
Cohesion: 0.20
Nodes (18): keyrecord_t, get_hold_on_other_key_press(), get_permissive_hold(), get_retro_tapping(), get_tapping_term(), tapping_term_apply_defaults_locked(), tapping_term_commit(), tapping_term_get_value() (+10 more)

### Community 56 - "드라이버 초기화·로그"
Cohesion: 0.11
Nodes (16): firm_tag_t, apMain(), delay(), ledInit(), ledOff(), ledOn(), ledToggle(), struct() (+8 more)

### Community 57 - "UF2 변환 도구"
Cohesion: 0.24
Nodes (14): Block, board_id(), convert_from_hex_to_uf2(), convert_from_uf2(), convert_to_carray(), convert_to_uf2(), get_drives(), is_hex() (+6 more)

### Community 58 - "키맵 인트로스펙션"
Cohesion: 0.18
Nodes (15): combo_t, combo_count(), combo_count_raw(), combo_get(), combo_get_raw(), encodermap_layer_count(), encodermap_layer_count_raw(), keycode_at_dip_switch_map_location() (+7 more)

### Community 59 - "process_rgb 키코드"
Cohesion: 0.13
Nodes (14): rgb_func_pointer, handleKeycodeRGB(), handleKeycodeRGBMode(), rgblight_decrease(), rgblight_disable(), rgblight_enable(), rgblight_get_mode(), rgblight_increase() (+6 more)

### Community 60 - "rgblight 동기화/토글"
Cohesion: 0.19
Nodes (16): rgblight_syncinfo_t, is_static_effect(), rgblight_disable_noeeprom(), rgblight_enable_noeeprom(), rgblight_enabled_noeeprom(), rgblight_get_syncinfo(), rgblight_mode_eeprom_helper(), rgblight_mode_transition_sat() (+8 more)

### Community 61 - "인디케이터 포트 (brick60)"
Cohesion: 0.20
Nodes (13): led_t, indicator_apply_defaults(), indicator_apply_led_ranges(), indicator_port_via_command(), indicator_target_from_host(), indicator_via_color_value(), indicator_via_get_value(), indicator_via_save() (+5 more)

### Community 62 - "RTC 드라이버"
Cohesion: 0.33
Nodes (11): action_t, keypos_t, layer_switch_get_action(), layer_switch_get_layer(), read_source_layers_cache(), read_source_layers_cache_impl(), store_or_get_action(), update_source_layers_cache() (+3 more)

### Community 63 - "인디케이터 포트 (sculpturei)"
Cohesion: 0.22
Nodes (12): rgb_led_t, indicator_apply_defaults(), indicator_port_via_command(), indicator_render(), indicator_render_range(), indicator_via_color_value(), indicator_via_get_value(), indicator_via_save() (+4 more)

### Community 65 - "인디케이터 포트 (may65)"
Cohesion: 0.18
Nodes (13): rgblight_indicator_render_callback_t, rgb_led_t, indicator_apply_defaults(), indicator_port_via_command(), indicator_render(), indicator_via_color_value(), indicator_via_get_value(), indicator_via_save() (+5 more)

### Community 66 - "인디케이터 포트 (brick65)"
Cohesion: 0.22
Nodes (11): rgb_led_t, indicator_apply_defaults(), indicator_port_via_command(), indicator_render(), indicator_via_color_value(), indicator_via_get_value(), indicator_via_save(), indicator_via_set_value() (+3 more)

### Community 68 - "보드별 상태 LED 콜백"
Cohesion: 0.17
Nodes (11): 1. 목적과 비목표, 2. 측정 경계, 3. 런타임 구조와 비용, 4.1 요청, 4.2 공통 응답, 4. 32바이트 VIA 계약, 4.3 capabilities payload, 4.4 snapshot chunk (+3 more)

### Community 69 - "rgblight pulse 이펙트"
Cohesion: 0.35
Nodes (11): secure_status_t, clear_keyboard(), clear_mods(), dynamic_macro_led_blink(), dynamic_macro_play(), dynamic_macro_record_end(), dynamic_macro_record_key(), dynamic_macro_record_start() (+3 more)

### Community 70 - "USB 모니터 포트"
Cohesion: 0.27
Nodes (10): matrix_row_t, matrix_get_row(), matrix_row_t, get_cached_real_keys(), get_real_keys(), has_ghost_in_row(), keyboard_keymap_real_keys_invalidate(), mark_all_real_key_masks_dirty() (+2 more)

### Community 71 - "버튼 드라이버"
Cohesion: 0.29
Nodes (8): cliKeepLoop(), buttonGetData(), buttonGetPin(), buttonGetPressed(), buttonGetPressedCount(), buttonInit(), cli_args_t, cliButton()

### Community 72 - "시퀀서 테스트 목"
Cohesion: 0.20
Nodes (7): sequencer_config_t, sequencer_state_t, SequencerTest, config_copy, state_copy, setUpMatrixScanSequencerTest(), testing::Test

### Community 73 - "시퀀서 페이즈 태스크"
Cohesion: 0.29
Nodes (8): is_sequencer_step_on_for_track(), sequencer_phase_attack(), sequencer_phase_pause(), sequencer_phase_release(), sequencer_task(), midi_compute_note(), process_midi_basic_noteoff(), process_midi_basic_noteon()

### Community 74 - "퀀텀 키보드 헤더"
Cohesion: 0.24
Nodes (17): mousekey_config_storage_t, mousekey_config_apply_defaults_locked(), mousekey_config_apply_runtime(), mousekey_config_clamp(), mousekey_config_commit(), mousekey_config_effective_max_speed(), mousekey_config_get_value(), mousekey_config_handle_via_command() (+9 more)

### Community 76 - "rgblight 렌더 큐·오버레이"
Cohesion: 0.18
Nodes (13): rgblight_get_hue(), rgblight_get_layer_state(), rgblight_get_sat(), rgblight_get_speed(), rgblight_get_val(), rgblight_is_enabled(), rgblight_layers_write(), rgblight_set_speed() (+5 more)

### Community 77 - "타이머 포트·키보드 초기화"
Cohesion: 0.11
Nodes (27): micros(), usbDiagnosticsIsActive(), USBD_HandleTypeDef, __weak, HAL_TIM_Base_MspDeInit(), HAL_TIM_Base_MspInit(), HAL_TIM_PWM_PulseFinishedCallback(), USBD_HID_DataIn() (+19 more)

### Community 78 - "eeconfig 업데이트"
Cohesion: 0.25
Nodes (7): 1. 목적과 범위, 2. 인터페이스 구성, 3. 동시 입력은 20키다 (6KRO가 아니다), 4. 부트 규격과의 편차 (알려진 상태, 유지 결정), 5. NKRO를 도입하지 않는 이유 (2026-08-23 평가 결과), 6. 트러블슈팅, USB HID 리포트 구성과 동시 입력 (V260823R1)

### Community 79 - "BSP 클럭·MSP 초기화"
Cohesion: 0.21
Nodes (13): usbHidSetStatusLed(), led_t, led_update_ports(), usbHidSetStatusLed(), usbHidSetStatusLed(), led_t, led_update_ports(), usbHidSetStatusLed() (+5 more)

### Community 80 - "USB IO 요청 (ioreq)"
Cohesion: 0.27
Nodes (5): era_state_sync_bump_keymap(), era_state_sync_bump_macro(), era_state_sync_next(), era_state_sync_put_be32(), era_state_sync_via_command()

### Community 81 - "via_hid RAW HID 브리지"
Cohesion: 0.48
Nodes (6): kkuk_init(), kkuk_normalize_config(), via_qmk_kkuk_command(), via_qmk_kkuk_get_value(), via_qmk_kkuk_save(), via_qmk_kkuk_set_value()

### Community 82 - "HID 리포트 전송 경로"
Cohesion: 0.16
Nodes (10): BusFault_Handler(), HardFault_Handler(), MemManage_Handler(), UsageFault_Handler(), eepromAutoFactoryResetCheck(), eepromReadU32(), eepromScheduleDeferredFactoryReset(), eepromWriteFlag() (+2 more)

### Community 84 - "BootMode VIA 인코딩"
Cohesion: 0.21
Nodes (13): rtc_date_t, RTC_HandleTypeDef, rtc_info_t, rtc_time_t, cli_args_t, cliRtc(), HAL_RTC_MspDeInit(), HAL_RTC_MspInit() (+5 more)

### Community 85 - "링 버퍼"
Cohesion: 0.15
Nodes (12): 1. 목적과 범위, 2. 구성 파일, 3. 이 유닛은 런타임 상태를 소유하지 않는다, 4. EEPROM 슬롯, 5. VIA 매핑 (채널 17), 6-1. 최고 속도는 저장되지 않는다, 6-2. 가속은 페이지에서 시간, 엔진에서 이벤트 수, 6-3. 가속 Off는 "최고 속도 고정"이 아니라 "첫 스텝 고정" (+4 more)

### Community 86 - "I2C/SPI CLI 보조"
Cohesion: 0.18
Nodes (11): rgblight_indicator_state_t, eeconfig_flush_rgblight_current(), preprocess_rgblight(), rgblight_consume_host_led_queue(), rgblight_flush_render_queue(), rgblight_indicator_apply_overlay(), rgblight_render_frame(), rgblight_task() (+3 more)

### Community 87 - "버전 VIA 포트"
Cohesion: 0.28
Nodes (6): KEYCODE2CONSUMER(), KEYCODE2SYSTEM(), keycode_config(), mod_config(), action_for_keycode(), action_t

### Community 88 - "시퀀서 트랙 활성화"
Cohesion: 0.60
Nodes (4): matrix_row_t, debounce_sym_defer_pk_run(), start_debounce_counters(), update_debounce_counters_and_transfer_if_expired()

### Community 89 - "graphify 부트스트랩"
Cohesion: 0.67
Nodes (3): main(), 명령 실행, (returncode, stdout) 반환. 실패해도 예외를 올리지 않는다., sh()

### Community 119 - "debounce.h"
Cohesion: 0.33
Nodes (7): HSV, RGB, rgb_led_t, rgblight_get_hsv(), rgblight_hsv_to_rgb(), sethsv_raw(), setrgb()

### Community 122 - "matrix_get_row"
Cohesion: 0.20
Nodes (6): cliLoopIdle(), kill_switch_init(), eeprom_task(), idle_task(), keyboard_post_init_user(), qmkUpdate()

### Community 123 - "timer.c"
Cohesion: 0.28
Nodes (3): bspInit(), bspMpuInit(), SystemClock_Config()

### Community 124 - "asym_eager_defer_pk.c"
Cohesion: 0.60
Nodes (4): matrix_row_t, debounce_asym_eager_defer_pk_run(), transfer_matrix_values(), update_debounce_counters_and_transfer_if_expired()

### Community 125 - "debounce.h"
Cohesion: 0.16
Nodes (12): fast_timer_t, timer_elapsed32(), timer_elapsed_fast(), timer_init(), timer_read32(), timer_read_fast(), debounce_asym_eager_defer_pk_init(), debounce_sym_defer_pk_init() (+4 more)

### Community 126 - "rgblight_sethsv_range"
Cohesion: 0.33
Nodes (6): rgblight_sethsv_master(), rgblight_sethsv_range(), rgblight_sethsv_slave(), rgblight_setrgb_master(), rgblight_setrgb_range(), rgblight_setrgb_slave()

### Community 130 - "is_sequencer_track_active"
Cohesion: 0.50
Nodes (4): is_sequencer_track_active(), sequencer_set_track_activation(), sequencer_toggle_single_active_track(), sequencer_toggle_track_activation()

## Knowledge Gaps
- **42 isolated node(s):** `config_copy`, `state_copy`, `1. 목적과 범위`, `2. 구성 파일`, `3. 이 유닛은 런타임 상태를 소유하지 않는다` (+37 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **3 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `millis()` connect `I2C 드라이버` to `퀀텀 코어·부트로더 리셋`, `QSPI 플래시 (W25Q16JV)`, `액션·호스트 리포트 전송`, `HID 리포트 타입 정의`, `send_string·키 전송 유틸`, `매트릭스 스캔·계측`, `USB 코어·BootMode 관리`, `보드별 WS2812 드라이버`, `CDC 상위 계층`, `EEPROM 비동기 큐`, `CLI 부가 명령·펌웨어 태그`, `플래시 EEPROM 에뮬레이션`, `SPI 드라이버`, `kill_switch·kkuk`, `퀀텀 헤더 모음`, `UART·CLI 입출력`, `드라이버 초기화·로그`, `timer.c`, `debounce.h`?**
  _High betweenness centrality (0.138) - this node is a cross-community bridge._
- **Why does `logPrintf()` connect `HID 리포트 전송 경로` to `디바운스 알고리즘/프로파일`, `퀀텀 코어·부트로더 리셋`, `QSPI 플래시 (W25Q16JV)`, `USB 디스크립터 3종`, `send_string·키 전송 유틸`, `usbd_conf (HAL PCD 연동)`, `매트릭스 스캔·계측`, `USB 코어·BootMode 관리`, `Tap Dance 퀀텀 처리`, `ap 메인 루프`, `I2C 드라이버`, `CLI 코어`, `CDC 상위 계층`, `EEPROM 비동기 큐`, `USB 컨트롤 요청 (ctlreq)`, `CLI 부가 명령·펌웨어 태그`, `ZD24C128 EEPROM 드라이버`, `플래시 EEPROM 에뮬레이션`, `퀀텀 헤더 모음`, `UART·CLI 입출력`, `드라이버 초기화·로그`, `타이머 포트·키보드 초기화`, `via_hid RAW HID 브리지`, `matrix_get_row`?**
  _High betweenness centrality (0.130) - this node is a cross-community bridge._
- **Why does `timer_read()` connect `액션·호스트 리포트 전송` to `I2C 드라이버`, `Key Override·원샷 모드`, `퀀텀 코어·부트로더 리셋`, `rgblight 코어`, `keyboard 태스크·매트릭스 브리지`, `시퀀서 페이즈 태스크`, `HID 리포트 타입 정의`, `시퀀서 코어`, `I2C/SPI CLI 보조`, `Tap Dance 포트`, `debounce.h`?**
  _High betweenness centrality (0.095) - this node is a cross-community bridge._
- **Are the 19 inferred relationships involving `TEST_F()` (e.g. with `get_beat_duration()` and `get_step_duration()`) actually correct?**
  _`TEST_F()` has 19 INFERRED edges - model-reasoned connections that need verification._
- **Are the 41 inferred relationships involving `process_action()` (e.g. with `tapdance_register_keycode()` and `tapdance_unregister_keycode()`) actually correct?**
  _`process_action()` has 41 INFERRED edges - model-reasoned connections that need verification._
- **Are the 49 inferred relationships involving `logPrintf()` (e.g. with `cliUpdate()` and `debounce_profile_apply_current()`) actually correct?**
  _`logPrintf()` has 49 INFERRED edges - model-reasoned connections that need verification._
- **Are the 33 inferred relationships involving `cliPrintf()` (e.g. with `cliCmd()` and `host_consumer_send()`) actually correct?**
  _`cliPrintf()` has 33 INFERRED edges - model-reasoned connections that need verification._