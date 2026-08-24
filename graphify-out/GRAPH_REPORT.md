# Graph Report - eerraa-qmk-h7s-fw  (2026-08-25)

## Corpus Check
- 360 files · ~204,184 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 2194 nodes · 4930 edges · 138 communities (134 shown, 4 thin omitted)
- Extraction: 77% EXTRACTED · 23% INFERRED · 0% AMBIGUOUS · INFERRED: 1139 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `1c6955ef`
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
- rgblight 상태 조회·VIA
- 인디케이터 포트 (may65)
- 인디케이터 포트 (brick65)
- 포트 헤더 모음
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
- LCD 컨트롤러 헤더
- BootMode VIA 인코딩
- 링 버퍼
- I2C/SPI CLI 보조
- 버전 VIA 포트
- 시퀀서 트랙 활성화
- graphify 부트스트랩
- usb.h 다운그레이드 타입
- usb.h 디버그 타입
- usb.h BootMode 타입
- loaderDownToFlash
- print.c
- via_qmk_system
- debounce_profile_apply_defaults_locked
- asym_eager_defer_pk.c
- keys.c
- USBD_HandleTypeDef
- sym_eager_pk.c
- unregister_code16
- rgblight_hsv_to_rgb
- sym_defer_pk.c
- BSP_QSPI_GetID
- def.h
- loader.c
- cliCmd
- millis
- 영속 상태 계약
- era_doc_refs_selftest.py

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
- `cliLoopIdle()` --calls--> `qmkUpdate()`  [INFERRED]
  src/ap/ap.c → src/ap/modules/qmk/qmk.c
- `matrix_task()` --calls--> `matrix_print()`  [INFERRED]
  src/ap/modules/qmk/quantum/keyboard.c → src/ap/modules/qmk/port/matrix.c
- `eeconfig_update_debug()` --calls--> `eeprom_update_byte()`  [INFERRED]
  src/ap/modules/qmk/quantum/eeconfig.c → src/ap/modules/qmk/port/platforms/eeprom.c
- `eeconfig_update_handedness()` --calls--> `eeprom_update_byte()`  [INFERRED]
  src/ap/modules/qmk/quantum/eeconfig.c → src/ap/modules/qmk/port/platforms/eeprom.c
- `keyboard_init()` --calls--> `timer_init()`  [INFERRED]
  src/ap/modules/qmk/quantum/keyboard.c → src/ap/modules/qmk/port/platforms/timer.c

## Import Cycles
- None detected.

## Communities (138 total, 4 thin omitted)

### Community 0 - "디바운스 알고리즘/프로파일"
Cohesion: 0.16
Nodes (20): debounce_algo_entry_t, debounce_runtime_error_t, debounce_runtime_type_t, debounce_runtime_config_t, matrix_row_t, debounce(), debounce_free(), debounce_init() (+12 more)

### Community 1 - "프로젝트 문서·가이드"
Cohesion: 0.17
Nodes (16): 에이전트 작업 가이드 (AGENTS.md), Graphify 지식그래프 우선 탐색 워크플로, VYYMMDDRn 변경 이력 규칙, Claude Code 전용 지침 (CLAUDE.md), baram-qmk-h7s 최상위 CMake 빌드, KBD_NAME/_DEF_FIRMWARE_VERSION 추출 로직, UF2 변환 POST_BUILD 커맨드 (uf2conv.py, family 0xFFFF0002), Anti-Ghosting (동시 입력 반복 보정) (+8 more)

### Community 2 - "퀀텀 코어·부트로더 리셋"
Cohesion: 0.16
Nodes (33): Match, Path, agent_docs(), boards(), channel_map(), check_headers(), check_index(), check_menu() (+25 more)

### Community 4 - "QSPI 플래시 (W25Q16JV)"
Cohesion: 0.08
Nodes (46): QSPI_Info, qspi_info_t, cli_args_t, cliFlash(), flashErase(), flashInSector(), flashRead(), flashWrite() (+38 more)

### Community 5 - "USB 디스크립터 3종"
Cohesion: 0.07
Nodes (39): USBD_SpeedTypeDef, Get_SerialNum(), IntToUnicode(), USBD_CDC_ConfigStrDescriptor(), USBD_CDC_DeviceDescriptor(), USBD_CDC_InterfaceStrDescriptor(), USBD_CDC_LangIDStrDescriptor(), USBD_CDC_ManufacturerStrDescriptor() (+31 more)

### Community 6 - "rgblight 코어"
Cohesion: 0.07
Nodes (41): eeconfig_update_rgblight_current(), rgblight_blink_layer(), rgblight_blink_layer_repeat(), rgblight_blink_layer_repeat_helper(), rgblight_decrease_hue(), rgblight_decrease_hue_helper(), rgblight_decrease_hue_noeeprom(), rgblight_decrease_sat() (+33 more)

### Community 7 - "액션·호스트 리포트 전송"
Cohesion: 0.07
Nodes (43): oneshot_fullfillment_t, timer_read(), report_nkro_t, host_nkro_send(), has_anykey(), add_oneshot_locked_mods(), add_oneshot_mods(), clear_oneshot_layer_state() (+35 more)

### Community 8 - "시퀀서 테스트"
Cohesion: 0.05
Nodes (42): TEST_F(), TestGetActiveTracks, TestGetActiveTracksOutOfBound, TestGetBeatDuration, TestGetStepDuration120, TestGetStepDuration60, TestIncreaseTempoMax, TestIsStepOffForGivenTrack (+34 more)

### Community 9 - "keyboard 태스크·매트릭스 브리지"
Cohesion: 0.08
Nodes (30): matrix_row_t, matrix_get_row(), matrix_row_t, generate_tick_event(), get_cached_real_keys(), get_real_keys(), has_ghost_in_row(), housekeeping_task() (+22 more)

### Community 10 - "USB CDC 클래스"
Cohesion: 0.09
Nodes (34): USBD_HandleTypeDef, USBD_HandleTypeDef, CDC_Control_FS(), CDC_DeInit_FS(), CDC_Init_FS(), CDC_Receive_FS(), CDC_SoF_ISR(), CDC_Transmit_FS() (+26 more)

### Community 11 - "액션 레이어"
Cohesion: 0.16
Nodes (33): layer_state_t, clear_keyboard_but_mods(), clear_keyboard_but_mods_and_keys(), default_layer_and(), default_layer_debug(), default_layer_or(), default_layer_set(), default_layer_state_set() (+25 more)

### Community 12 - "액션 실행 코어"
Cohesion: 0.14
Nodes (15): action_t, keyrecord_t, debug_action(), get_hold_on_other_key_press(), get_retro_tapping(), is_tap_action(), is_tap_record(), post_process_record_quantum() (+7 more)

### Community 13 - "USB HID 클래스·인터벌"
Cohesion: 0.13
Nodes (23): usbDiagnosticsIsActive(), USBD_HandleTypeDef, __weak, HAL_TIM_Base_MspDeInit(), HAL_TIM_Base_MspInit(), HAL_TIM_PWM_PulseFinishedCallback(), USBD_HID_DataIn(), USBD_HID_EP0_RxReady() (+15 more)

### Community 14 - "HID 리포트 타입 정의"
Cohesion: 0.09
Nodes (26): digitizer_t, host_driver_t, joystick_t, report_digitizer_t, report_joystick_t, report_programmable_button_t, report_keyboard_t, report_mouse_t (+18 more)

### Community 15 - "send_string·키 전송 유틸"
Cohesion: 0.18
Nodes (23): keyrecord_t, kill_switch_process(), wait_ms(), led_t, host_keyboard_led_state(), register_code(), tap_code(), tap_code_delay() (+15 more)

### Community 16 - "usbd_conf (HAL PCD 연동)"
Cohesion: 0.14
Nodes (29): HAL_StatusTypeDef, PCD_HandleTypeDef, USBD_HandleTypeDef, USBD_StatusTypeDef, HAL_PCD_ConnectCallback(), HAL_PCD_DataInStageCallback(), HAL_PCD_DataOutStageCallback(), HAL_PCD_DisconnectCallback() (+21 more)

### Community 17 - "ST USB Device Core"
Cohesion: 0.16
Nodes (32): usbBegin(), USBD_ClassTypeDef, USBD_CompositeClassTypeDef, USBD_HandleTypeDef, USBD_SpeedTypeDef, USBD_StatusTypeDef, USBD_ClrClassConfig(), USBD_CoreFindEP() (+24 more)

### Community 18 - "서스펜드·LED 절전"
Cohesion: 0.07
Nodes (48): secure_status_t, kkuk_idle(), matrix_power_down(), matrix_power_up(), suspend_power_down(), suspend_power_down_kb(), suspend_power_down_user(), suspend_wakeup_condition() (+40 more)

### Community 19 - "USB 컴포지트 클래스"
Cohesion: 0.17
Nodes (25): __IO, USBD_ClassTypeDef, USBD_CompositeClassTypeDef, USBD_HandleTypeDef, USBD_CMPSIT_AddClass(), USBD_CMPSIT_AddConfDesc(), USBD_CMPSIT_AddToConfDesc(), USBD_CMPSIT_AssignEp() (+17 more)

### Community 20 - "CLI GUI"
Cohesion: 0.19
Nodes (28): cli_gui_api_t, cliPutch(), addCh_Or_InsCh(), addChar(), addPrintf(), addStr(), clear(), clearToEol() (+20 more)

### Community 21 - "매트릭스 스캔·계측"
Cohesion: 0.18
Nodes (12): cli_args_t, cliCmd(), matrixInstrumentationCaptureStart(), matrixInstrumentationGetScanTime(), matrixInstrumentationIsCompileEnabled(), matrixInstrumentationLogScan(), matrixInstrumentationReset(), matrix_info() (+4 more)

### Community 22 - "USB 코어·BootMode 관리"
Cohesion: 0.15
Nodes (24): bootmode_ensure_default_persisted(), bootmode_init(), cli_args_t, UsbBootMode_t, cliBoot(), usbBootModeApplyDefaults(), usbBootModeGet(), usbBootModeGetHsInterval() (+16 more)

### Community 23 - "eeconfig·EEPROM 읽기"
Cohesion: 0.16
Nodes (21): eeconfig_init_user_datablock(), eeprom_read_dword(), eeprom_update_dword(), eeconfig_init_kb(), eeconfig_init_kb_datablock(), eeconfig_init_user(), eeconfig_init_user_datablock(), eeconfig_is_kb_datablock_valid() (+13 more)

### Community 24 - "Tap Dance 포트"
Cohesion: 0.15
Nodes (30): tap_dance_state_t, tapdance_apply_defaults_locked(), tapdance_commit(), tapdance_field_index(), tapdance_get_value(), tapdance_handle_via_command(), tapdance_init(), tapdance_is_exact_term_id() (+22 more)

### Community 25 - "Tap Dance 퀀텀 처리"
Cohesion: 0.12
Nodes (29): tapdance_get_term_ms(), clear_weak_mods(), get_oneshot_mods(), get_weak_mods(), keyrecord_t, tap_dance_state_t, preprocess_tap_dance(), process_tap_dance() (+21 more)

### Community 26 - "로깅·컬러 유틸"
Cohesion: 0.09
Nodes (9): sendchar_func_t, HSV, RGB, rgb_led_t, convert_rgb_to_rgbw(), hsv_to_rgb(), hsv_to_rgb_impl(), hsv_to_rgb_nocie() (+1 more)

### Community 27 - "ap 메인 루프"
Cohesion: 0.18
Nodes (10): apMain(), ledInit(), ledOff(), ledOn(), ledToggle(), struct(), hwBlinkFactoryResetFailure(), hwRunFactoryResetWithRetry() (+2 more)

### Community 28 - "VIA 코어"
Cohesion: 0.13
Nodes (26): eeconfig_update_audio(), via_command_kb(), via_custom_value_command(), via_custom_value_command_kb(), via_get_layout_options(), via_init(), via_init_kb(), via_qmk_audio_command() (+18 more)

### Community 29 - "시퀀서 코어"
Cohesion: 0.11
Nodes (23): sequencer_resolution_t, get_beat_duration(), get_step_duration(), is_sequencer_on(), is_sequencer_step_on(), is_sequencer_track_active(), sequencer_decrease_resolution(), sequencer_decrease_tempo() (+15 more)

### Community 30 - "보드별 WS2812 드라이버"
Cohesion: 0.13
Nodes (16): rgb_led_t, ws2812_setleds(), rgb_led_t, ws2812_setleds(), rgb_led_t, ws2812_setleds(), rgb_led_t, ws2812_setleds() (+8 more)

### Community 31 - "키코드 변환·리포트"
Cohesion: 0.09
Nodes (6): KEYCODE2CONSUMER(), KEYCODE2SYSTEM(), keycode_config(), mod_config(), action_for_keycode(), action_t

### Community 32 - "I2C 드라이버"
Cohesion: 0.12
Nodes (18): I2C_HandleTypeDef, i2c_ready_wait_stats_t, delayUs(), HAL_I2C_ErrorCallback(), HAL_I2C_MspDeInit(), HAL_I2C_MspInit(), i2cBegin(), i2cGetChannelFromHandle() (+10 more)

### Community 33 - "CLI 코어"
Cohesion: 0.15
Nodes (18): cli_t, __WEAK, cliInit(), cliLineAdd(), cliLineChange(), cliLineClean(), cliLoopIdle(), cliMoveDown() (+10 more)

### Community 34 - "Key Override·원샷 모드"
Cohesion: 0.15
Nodes (24): key_override_t, timer_elapsed32(), register_weak_mods(), unregister_weak_mods(), add_weak_mods(), del_weak_mods(), get_mods(), send_keyboard_report() (+16 more)

### Community 35 - "CDC 상위 계층"
Cohesion: 0.14
Nodes (16): cdcAvailable(), cdcGetType(), cdcInit(), cdcIsConnect(), cdcRead(), cdcWrite(), cdcIfAvailable(), cdcIfGetType() (+8 more)

### Community 36 - "rgblight 인디케이터 설정"
Cohesion: 0.18
Nodes (19): rgblight_indicator_config_t, rgblight_indicator_range_t, led_t, rgblight_indicator_any_pending_render(), rgblight_indicator_apply_host_led(), rgblight_indicator_apply_target_range(), rgblight_indicator_commit_state(), rgblight_indicator_compute_color() (+11 more)

### Community 37 - "EEPROM 비동기 큐"
Cohesion: 0.17
Nodes (20): eeprom_write_t, qbuffer_t, eeprom_flush_pending(), eeprom_get_burst_extra_calls(), eeprom_is_burst_mode_active(), eeprom_is_pending(), eeprom_peek_queue_entry(), eeprom_task() (+12 more)

### Community 38 - "자동 팩토리 리셋·IRQ 핸들러"
Cohesion: 0.15
Nodes (11): eeprom_init(), BusFault_Handler(), HardFault_Handler(), MemManage_Handler(), UsageFault_Handler(), eepromAutoFactoryResetCheck(), eepromReadU32(), eepromScheduleDeferredFactoryReset() (+3 more)

### Community 39 - "USB 컨트롤 요청 (ctlreq)"
Cohesion: 0.44
Nodes (17): USBD_LL_SetupStage(), USBD_HandleTypeDef, USBD_SetupReqTypedef, USBD_StatusTypeDef, USBD_ClrFeature(), USBD_CtlError(), USBD_GetConfig(), USBD_GetDescriptor() (+9 more)

### Community 40 - "CLI 부가 명령·펌웨어 태그"
Cohesion: 0.20
Nodes (15): firm_tag_t, delay(), cli_args_t, cliMemoryDump(), cliPrintf(), cliShowCursor(), cliShowList(), guiMove() (+7 more)

### Community 41 - "ZD24C128 EEPROM 드라이버"
Cohesion: 0.19
Nodes (13): log_buf_t, apInit(), cliOpen(), cliOpenLog(), cliWrite(), cli_args_t, cliCmd(), logBoot() (+5 more)

### Community 42 - "플래시 EEPROM 에뮬레이션"
Cohesion: 0.21
Nodes (24): debounce_profile_status_t, debounce_profile_storage_t, debounce_runtime_config_t, debounce_profile_apply_current(), debounce_profile_apply_defaults_locked(), debounce_profile_clamp_delay(), debounce_profile_default_config(), debounce_profile_get_status() (+16 more)

### Community 43 - "다이나믹 키맵"
Cohesion: 0.20
Nodes (23): eeprom_update_byte(), dynamic_keymap_encoder_to_eeprom_address(), dynamic_keymap_get_buffer(), dynamic_keymap_get_encoder(), dynamic_keymap_get_keycode(), dynamic_keymap_get_layer_count(), dynamic_keymap_key_to_eeprom_address(), dynamic_keymap_macro_get_buffer() (+15 more)

### Community 44 - "HID 계측·micros"
Cohesion: 0.24
Nodes (22): usb_diagnostics_snapshot_t, usbDiagnosticsCapture(), usbDiagnosticsClear(), usbDiagnosticsEnterCritical(), usbDiagnosticsExitCritical(), usbDiagnosticsInit(), usbDiagnosticsObserveSpeed(), usbDiagnosticsOnReportQueueDepth() (+14 more)

### Community 45 - "rgblight 이펙트"
Cohesion: 0.11
Nodes (32): animation_status_t, breathe_calc(), rgblight_effect_alternating(), rgblight_effect_breathing(), rgblight_effect_christmas(), rgblight_effect_dummy(), rgblight_effect_knight(), rgblight_effect_pulse_apply_output() (+24 more)

### Community 46 - "SPI 드라이버"
Cohesion: 0.13
Nodes (8): SPI_HandleTypeDef, HAL_SPI_ErrorCallback(), HAL_SPI_MspDeInit(), HAL_SPI_MspInit(), HAL_SPI_RxCpltCallback(), spiDmaTxIsDone(), spiDmaTxStart(), spiDmaTxTransfer()

### Community 47 - "EEPROM 팩토리 기본값"
Cohesion: 0.23
Nodes (14): eeprom_apply_factory_defaults(), eeprom_read_block(), eeprom_restore_auto_factory_reset_sentinel(), eeprom_update_block(), eeprom_update_queue_watermark(), eeprom_update_word(), eeprom_write_block(), eeprom_write_byte() (+6 more)

### Community 48 - "kill_switch·kkuk"
Cohesion: 0.27
Nodes (8): via_custom_value_command_kb(), via_handle_usb_polling_channel(), kkuk_init(), kkuk_normalize_config(), via_qmk_kkuk_command(), via_qmk_kkuk_get_value(), via_qmk_kkuk_save(), via_qmk_kkuk_set_value()

### Community 50 - "rgblight 설정 저장"
Cohesion: 0.17
Nodes (17): eeprom_get_write_pending_count(), cli_args_t, cliEeprom(), EE_EndOfCleanup_UserCallback(), eepromFormat(), eepromGetLength(), eepromInit(), eepromInitMPU() (+9 more)

### Community 51 - "syscalls 스텁"
Cohesion: 0.11
Nodes (3): _exit(), _kill(), _write()

### Community 52 - "UART·CLI 입출력"
Cohesion: 0.13
Nodes (16): cliUpdate(), cliAvailable(), cliGetPort(), cliMain(), cliRead(), cdcGetBaud(), cli_args_t, cliUart() (+8 more)

### Community 53 - "인디케이터 포트 (intigrity80)"
Cohesion: 0.27
Nodes (11): led_t, indicator_apply_defaults(), indicator_apply_led_ranges(), indicator_port_via_command(), indicator_target_from_host(), indicator_via_color_value(), indicator_via_get_value(), indicator_via_save() (+3 more)

### Community 54 - "액션 태핑 코어"
Cohesion: 0.20
Nodes (19): action_exec(), keyevent_t, debug_event(), debug_record(), action_tapping_process(), keyevent_t, keyrecord_t, debug_tapping_key() (+11 more)

### Community 55 - "Tapping Term 포트"
Cohesion: 0.20
Nodes (18): keyrecord_t, get_hold_on_other_key_press(), get_permissive_hold(), get_retro_tapping(), get_tapping_term(), tapping_term_apply_defaults_locked(), tapping_term_commit(), tapping_term_get_value() (+10 more)

### Community 56 - "드라이버 초기화·로그"
Cohesion: 0.27
Nodes (11): cliAdd(), flashInit(), i2cInit(), logInit(), microsInit(), qspiInit(), resetInit(), rtcInit() (+3 more)

### Community 57 - "UF2 변환 도구"
Cohesion: 0.24
Nodes (14): Block, board_id(), convert_from_hex_to_uf2(), convert_from_uf2(), convert_to_carray(), convert_to_uf2(), get_drives(), is_hex() (+6 more)

### Community 58 - "키맵 인트로스펙션"
Cohesion: 0.19
Nodes (15): combo_t, combo_count(), combo_count_raw(), combo_get(), combo_get_raw(), encodermap_layer_count(), encodermap_layer_count_raw(), keycode_at_dip_switch_map_location() (+7 more)

### Community 59 - "process_rgb 키코드"
Cohesion: 0.13
Nodes (14): rgb_func_pointer, handleKeycodeRGB(), handleKeycodeRGBMode(), rgblight_decrease(), rgblight_disable(), rgblight_enable(), rgblight_get_mode(), rgblight_increase() (+6 more)

### Community 60 - "rgblight 동기화/토글"
Cohesion: 0.19
Nodes (16): rgblight_syncinfo_t, is_static_effect(), rgblight_disable_noeeprom(), rgblight_enable_noeeprom(), rgblight_enabled_noeeprom(), rgblight_get_syncinfo(), rgblight_mode_eeprom_helper(), rgblight_mode_transition_sat() (+8 more)

### Community 61 - "인디케이터 포트 (brick60)"
Cohesion: 0.22
Nodes (13): rgblight_indicator_target_callback_t, led_t, indicator_apply_defaults(), indicator_apply_led_ranges(), indicator_port_via_command(), indicator_target_from_host(), indicator_via_color_value(), indicator_via_get_value() (+5 more)

### Community 62 - "RTC 드라이버"
Cohesion: 0.19
Nodes (14): rtc_date_t, RTC_HandleTypeDef, rtc_info_t, rtc_time_t, cli_args_t, cliRtc(), HAL_RTC_MspDeInit(), HAL_RTC_MspInit() (+6 more)

### Community 63 - "인디케이터 포트 (sculpturei)"
Cohesion: 0.31
Nodes (10): rgb_led_t, indicator_apply_defaults(), indicator_port_via_command(), indicator_render(), indicator_render_range(), indicator_via_color_value(), indicator_via_get_value(), indicator_via_save() (+2 more)

### Community 64 - "rgblight 상태 조회·VIA"
Cohesion: 0.18
Nodes (13): rgblight_get_hue(), rgblight_get_layer_state(), rgblight_get_sat(), rgblight_get_speed(), rgblight_get_val(), rgblight_is_enabled(), rgblight_layers_write(), rgblight_set_speed() (+5 more)

### Community 65 - "인디케이터 포트 (may65)"
Cohesion: 0.24
Nodes (11): led_t, rgb_led_t, indicator_apply_defaults(), indicator_port_via_command(), indicator_render(), indicator_via_color_value(), indicator_via_get_value(), indicator_via_save() (+3 more)

### Community 66 - "인디케이터 포트 (brick65)"
Cohesion: 0.24
Nodes (11): rgblight_indicator_render_callback_t, rgb_led_t, indicator_apply_defaults(), indicator_port_via_command(), indicator_render(), indicator_via_color_value(), indicator_via_get_value(), indicator_via_save() (+3 more)

### Community 67 - "포트 헤더 모음"
Cohesion: 0.11
Nodes (9): kill_switch_init(), kill_switch_is_use(), keyrecord_t, kkuk_process(), cli_args_t, keyrecord_t, cliQmk(), keyboard_post_init_user() (+1 more)

### Community 68 - "보드별 상태 LED 콜백"
Cohesion: 0.25
Nodes (11): usbHidSetStatusLed(), usbHidSetStatusLed(), usbHidSetStatusLed(), led_t, led_update_ports(), usbHidSetStatusLed(), led_t, led_update_ports() (+3 more)

### Community 69 - "rgblight pulse 이펙트"
Cohesion: 0.18
Nodes (11): rgblight_indicator_state_t, eeconfig_flush_rgblight_current(), preprocess_rgblight(), rgblight_consume_host_led_queue(), rgblight_flush_render_queue(), rgblight_indicator_apply_overlay(), rgblight_render_frame(), rgblight_task() (+3 more)

### Community 70 - "USB 모니터 포트"
Cohesion: 0.24
Nodes (17): mousekey_config_storage_t, mousekey_config_apply_defaults_locked(), mousekey_config_apply_runtime(), mousekey_config_clamp(), mousekey_config_commit(), mousekey_config_effective_max_speed(), mousekey_config_get_value(), mousekey_config_handle_via_command() (+9 more)

### Community 71 - "버튼 드라이버"
Cohesion: 0.29
Nodes (8): cliKeepLoop(), buttonGetData(), buttonGetPin(), buttonGetPressed(), buttonGetPressedCount(), buttonInit(), cli_args_t, cliButton()

### Community 72 - "시퀀서 테스트 목"
Cohesion: 0.20
Nodes (7): sequencer_config_t, sequencer_state_t, SequencerTest, config_copy, state_copy, setUpMatrixScanSequencerTest(), testing::Test

### Community 73 - "시퀀서 페이즈 태스크"
Cohesion: 0.25
Nodes (9): is_sequencer_step_on_for_track(), sequencer_get_step_duration(), sequencer_phase_attack(), sequencer_phase_pause(), sequencer_phase_release(), sequencer_task(), midi_compute_note(), process_midi_basic_noteoff() (+1 more)

### Community 74 - "퀀텀 키보드 헤더"
Cohesion: 0.19
Nodes (14): eeprom_read_byte(), eeprom_read_word(), dynamic_keymap_macro_send(), eeconfig_is_disabled(), eeconfig_is_enabled(), eeconfig_read_audio(), eeconfig_read_debug(), eeconfig_read_default_layer() (+6 more)

### Community 76 - "rgblight 렌더 큐·오버레이"
Cohesion: 0.24
Nodes (16): timer_elapsed(), register_mouse(), adjust_speed(), report_mouse_t, calc_inertia(), mousekey_clear(), mousekey_debug(), mousekey_get_report() (+8 more)

### Community 77 - "타이머 포트·키보드 초기화"
Cohesion: 0.17
Nodes (12): fast_timer_t, timer_elapsed_fast(), timer_init(), timer_read32(), timer_read_fast(), matrix_row_t, debounce_asym_eager_defer_pk_init(), debounce_asym_eager_defer_pk_run() (+4 more)

### Community 78 - "eeconfig 업데이트"
Cohesion: 0.27
Nodes (14): action_t, keypos_t, layer_switch_get_action(), layer_switch_get_layer(), read_source_layers_cache(), read_source_layers_cache_impl(), store_or_get_action(), update_source_layers_cache() (+6 more)

### Community 79 - "BSP 클럭·MSP 초기화"
Cohesion: 0.24
Nodes (9): bspInit(), bspMpuInit(), Error_Handler(), SystemClock_Config(), HAL_MspInit(), USBD_SpeedTypeDef, HAL_PCD_MspInit(), HAL_PCD_ResetCallback() (+1 more)

### Community 80 - "USB IO 요청 (ioreq)"
Cohesion: 0.29
Nodes (12): USBD_SetupReqTypedef, USBD_CDC_Setup(), USBD_SetupReqTypedef, USBD_HID_Setup(), USBD_HandleTypeDef, USBD_StatusTypeDef, USBD_CtlContinueRx(), USBD_CtlContinueSendData() (+4 more)

### Community 81 - "via_hid RAW HID 브리지"
Cohesion: 0.47
Nodes (9): clear_keyboard(), clear_mods(), dynamic_macro_led_blink(), dynamic_macro_play(), dynamic_macro_record_end(), dynamic_macro_record_key(), dynamic_macro_record_start(), keyrecord_t (+1 more)

### Community 82 - "HID 리포트 전송 경로"
Cohesion: 0.19
Nodes (17): eeprom_get_write_overflow_count(), eeprom_get_write_pending_max(), cli_args_t, cliEeprom(), eepromFormat(), eepromGetLength(), eepromInit(), eepromIsErasing() (+9 more)

### Community 84 - "BootMode VIA 인코딩"
Cohesion: 0.29
Nodes (7): via_custom_value_command_kb(), via_handle_usb_polling_channel(), bootmode_decode_via_value(), bootmode_encode_via_value(), bootmode_sync_pending(), UsbBootMode_t, via_qmk_usb_bootmode_command()

### Community 85 - "링 버퍼"
Cohesion: 0.24
Nodes (15): usb_diagnostics_snapshot_t, UsbBootMode_t, era_usb_diagnostics_via_command(), eraUsbDiagnosticsBytesAreZero(), eraUsbDiagnosticsChunkCount(), eraUsbDiagnosticsEncodeCapabilities(), eraUsbDiagnosticsEncodeSnapshotChunk(), eraUsbDiagnosticsExpectedIntervalUs() (+7 more)

### Community 86 - "I2C/SPI CLI 보조"
Cohesion: 0.15
Nodes (14): bootloader_jump(), bootloader_jump_deferred(), bootloader_schedule_deferred_reset(), mcu_reset(), mcu_reset_deferred(), eeprom_req_clean(), reset_keyboard(), cli_args_t (+6 more)

### Community 87 - "버전 VIA 포트"
Cohesion: 0.17
Nodes (13): add_key_bit(), add_key_byte(), add_key_to_report(), report_keyboard_t, report_mouse_t, report_nkro_t, clear_keys_from_report(), del_key_bit() (+5 more)

### Community 88 - "시퀀서 트랙 활성화"
Cohesion: 0.23
Nodes (8): era_state_sync_bump_config(), era_state_sync_bump_keymap(), era_state_sync_bump_macro(), era_state_sync_next(), era_state_sync_put_be32(), era_state_sync_via_command(), via_set_layout_options(), via_set_layout_options_kb()

### Community 89 - "graphify 부트스트랩"
Cohesion: 0.67
Nodes (3): main(), 명령 실행, (returncode, stdout) 반환. 실패해도 예외를 올리지 않는다., sh()

### Community 115 - "usb.h 다운그레이드 타입"
Cohesion: 0.20
Nodes (10): 1. raw-HID TX 생산자는 하나뿐이다, 2. 채널 주소는 참조 QMK와 다르다, 3. exact-ms 값 계층, 4. 값이 실제로 바뀐 SET만 revision을 올린다, 5. selector `0x06` — state sync 봉투, 6-1. 진단 수치를 비교해도 되는 축, 6-2. 계측 비용의 상한, 6. selector `0x07` — 진단 봉투는 관측 전용이다 (+2 more)

### Community 116 - "usb.h 디버그 타입"
Cohesion: 0.29
Nodes (6): via_custom_value_command_kb(), via_handle_usb_polling_channel(), via_qmk_kill_switch_get_value(), via_qmk_kill_switch_save(), via_qmk_kill_switch_set_value(), via_qmk_kill_swtich_command()

### Community 119 - "loaderDownToFlash"
Cohesion: 0.22
Nodes (9): 1. 정본 규칙, 2. 문서 색인, 3. 보드, 4. VIA 채널, 5. EEPROM USER 슬롯, 6. graphify — 코드 구조는 그래프가 답한다, 7. 짝 저장소, 8. 문서 규칙 (+1 more)

### Community 120 - "print.c"
Cohesion: 0.22
Nodes (8): 1. 판정 대기 — 데이터가 결정한다, 2. 실기 미검증, 3. 되살리지 않기로 한 것, 4. 짝 저장소에 넘길 것, D-2. VIA 응답 스로틀의 기준점, D-3. 손실 경로 계수 범위, D-4. keyboard / EXK 드롭 분리, 아직 닫히지 않은 것

### Community 121 - "via_qmk_system"
Cohesion: 0.16
Nodes (9): via_custom_value_command_kb(), via_handle_usb_polling_channel(), via_custom_value_command_kb(), via_handle_usb_polling_channel(), via_qmk_sys_get_value(), via_qmk_sys_set_value(), via_qmk_system(), via_qmk_ver_get_value() (+1 more)

### Community 122 - "debounce_profile_apply_defaults_locked"
Cohesion: 0.33
Nodes (9): eeconfig_debug_rgblight(), eeconfig_read_rgblight(), eeconfig_update_rgblight(), eeconfig_update_rgblight_default(), rgblight_check_config(), rgblight_init(), rgblight_mode_noeeprom(), rgblight_reload_from_eeprom() (+1 more)

### Community 123 - "asym_eager_defer_pk.c"
Cohesion: 0.25
Nodes (8): 1. 인터페이스 구성, 2. 동시 입력은 20키다 — 6KRO가 아니다, 3. 부트 규격 편차는 알려진 상태이고 유지한다, 4. 자동 USB 복구는 폐기됐다 — 되살리지 마라, 5. 폴링 모드는 사용자 소유다, 6. 부트로더 → 펌웨어 인계는 100 ms 디태치가 필요하다, 7. 주기 작업의 만료 판정을 캐시하지 마라, USB 호스트 계약

### Community 124 - "keys.c"
Cohesion: 0.31
Nodes (4): keysInit(), keysInitDma(), keysInitGpio(), keysInitTimer()

### Community 125 - "USBD_HandleTypeDef"
Cohesion: 0.29
Nodes (5): raw_hid_send(), via_hid_init(), via_hid_print(), via_hid_receive(), usbHidSetViaReceiveFunc()

### Community 126 - "sym_eager_pk.c"
Cohesion: 0.43
Nodes (5): matrix_row_t, debounce_sym_eager_pk_init(), debounce_sym_eager_pk_run(), transfer_matrix_values(), update_debounce_counters()

### Community 127 - "unregister_code16"
Cohesion: 0.29
Nodes (7): 1. 세 가지를 돌린다, 2. 툴체인 전제 (Windows), 3. 호스트 테스트가 무는 것, 4. 빌드, 5. 실기에서만 판정되는 것, 6. 증상별 확인 순서, 검증 매뉴얼

### Community 128 - "rgblight_hsv_to_rgb"
Cohesion: 0.33
Nodes (7): HSV, RGB, rgb_led_t, rgblight_get_hsv(), rgblight_hsv_to_rgb(), sethsv_raw(), setrgb()

### Community 129 - "sym_defer_pk.c"
Cohesion: 0.39
Nodes (4): matrix_row_t, debounce_sym_defer_pk_run(), start_debounce_counters(), update_debounce_counters_and_transfer_if_expired()

### Community 130 - "BSP_QSPI_GetID"
Cohesion: 0.48
Nodes (6): keyevent_t, IS_COMBOEVENT(), IS_DIPSWITCHEVENT(), IS_ENCODEREVENT(), IS_KEYEVENT(), IS_NOEVENT()

### Community 131 - "def.h"
Cohesion: 0.33
Nodes (6): debounce_profile_values_t, debounce_profile_current(), matrix_init(), qmkInit(), keyboard_init(), led_init_ports()

### Community 133 - "cliCmd"
Cohesion: 0.33
Nodes (6): rgblight_sethsv_master(), rgblight_sethsv_range(), rgblight_sethsv_slave(), rgblight_setrgb_master(), rgblight_setrgb_range(), rgblight_setrgb_slave()

### Community 134 - "millis"
Cohesion: 0.47
Nodes (6): millis(), eepromWaitReady(), cli_args_t, cliI2C(), i2cIsDeviceReady(), spiTransferDMA()

### Community 135 - "영속 상태 계약"
Cohesion: 0.40
Nodes (4): 1. 슬롯 주소는 한 번 배포되면 움직이지 않는다, 2. 버전 쿠키를 올리면 모든 기기의 EEPROM이 초기화된다, 3. 쓰기 경로는 8 kHz 예산 안에서 돈다, 영속 상태 계약

### Community 137 - "era_doc_refs_selftest.py"
Cohesion: 1.00
Nodes (3): main(), probe(), run_checker()

## Knowledge Gaps
- **45 isolated node(s):** `config_copy`, `state_copy`, `1. 정본 규칙`, `2. 문서 색인`, `3. 보드` (+40 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **4 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `millis()` connect `millis` to `QSPI 플래시 (W25Q16JV)`, `액션·호스트 리포트 전송`, `USB HID 클래스·인터벌`, `서스펜드·LED 절전`, `매트릭스 스캔·계측`, `USB 코어·BootMode 관리`, `ap 메인 루프`, `보드별 WS2812 드라이버`, `I2C 드라이버`, `Key Override·원샷 모드`, `CDC 상위 계층`, `EEPROM 비동기 큐`, `CLI 부가 명령·펌웨어 태그`, `SPI 드라이버`, `EEPROM 팩토리 기본값`, `rgblight 설정 저장`, `UART·CLI 입출력`, `포트 헤더 모음`, `rgblight 렌더 큐·오버레이`, `타이머 포트·키보드 초기화`, `BSP 클럭·MSP 초기화`, `HID 리포트 전송 경로`, `I2C/SPI CLI 보조`?**
  _High betweenness centrality (0.130) - this node is a cross-community bridge._
- **Why does `logPrintf()` connect `자동 팩토리 리셋·IRQ 핸들러` to `def.h`, `QSPI 플래시 (W25Q16JV)`, `USB 디스크립터 3종`, `millis`, `USB CDC 클래스`, `USB HID 클래스·인터벌`, `send_string·키 전송 유틸`, `usbd_conf (HAL PCD 연동)`, `ST USB Device Core`, `매트릭스 스캔·계측`, `USB 코어·BootMode 관리`, `ap 메인 루프`, `I2C 드라이버`, `CLI 코어`, `CDC 상위 계층`, `EEPROM 비동기 큐`, `ZD24C128 EEPROM 드라이버`, `플래시 EEPROM 에뮬레이션`, `EEPROM 팩토리 기본값`, `kill_switch·kkuk`, `rgblight 설정 저장`, `UART·CLI 입출력`, `드라이버 초기화·로그`, `포트 헤더 모음`, `HID 리포트 전송 경로`, `I2C/SPI CLI 보조`, `keys.c`, `USBD_HandleTypeDef`?**
  _High betweenness centrality (0.121) - this node is a cross-community bridge._
- **Why does `timer_read()` connect `액션·호스트 리포트 전송` to `Key Override·원샷 모드`, `rgblight pulse 이펙트`, `millis`, `rgblight 코어`, `keyboard 태스크·매트릭스 브리지`, `시퀀서 페이즈 태스크`, `rgblight 렌더 큐·오버레이`, `타이머 포트·키보드 초기화`, `서스펜드·LED 절전`, `Tap Dance 포트`, `Tap Dance 퀀텀 처리`, `시퀀서 코어`?**
  _High betweenness centrality (0.092) - this node is a cross-community bridge._
- **Are the 19 inferred relationships involving `TEST_F()` (e.g. with `get_beat_duration()` and `get_step_duration()`) actually correct?**
  _`TEST_F()` has 19 INFERRED edges - model-reasoned connections that need verification._
- **Are the 41 inferred relationships involving `process_action()` (e.g. with `tapdance_register_keycode()` and `tapdance_unregister_keycode()`) actually correct?**
  _`process_action()` has 41 INFERRED edges - model-reasoned connections that need verification._
- **Are the 49 inferred relationships involving `logPrintf()` (e.g. with `cliUpdate()` and `debounce_profile_apply_current()`) actually correct?**
  _`logPrintf()` has 49 INFERRED edges - model-reasoned connections that need verification._
- **Are the 33 inferred relationships involving `cliPrintf()` (e.g. with `cliCmd()` and `host_consumer_send()`) actually correct?**
  _`cliPrintf()` has 33 INFERRED edges - model-reasoned connections that need verification._