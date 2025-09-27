# CODEX_MAP::eerraa-qmk-h7s-fw

## PROJECT_SNAPSHOT
- target_mcu: STM32H7S (internal HS PHY)
- usb_polling_rates: [hs_8000hz, hs_4000hz, hs_2000hz, fs_1000hz_fallback]
- qmk_port_goal: "Align standard QMK logic with STM32H7S bandwidth/timing constraints"
- backlog:
  - V250924R3_host_matrix: "Catalog host compatibility heuristics"
  - V250924R3_auto_recovery: "Evaluate long-duration downgrade recovery"

## FIRMWARE_FEATURES
- usb_instability_monitor:
    version: V250924R4
    metrics: [microframe_gap_tracking, holdoff_timer, warmup_timer, decay_timer]
    scheduling: "SOF ISR lightweight, downgrade logic accurate"
    downgrade_pipeline: staged_queue_with_confirmation_delay_and_eeprom_persistence
- polling_rate_downgrade_queue: staged_pipeline_preventing_reset_loops

## FIRMWARE_POLICIES
- usb_phy_mode: high_speed_only_until_fs_1khz_validation
- timing_constraints: preserve_8000hz_scheduling → inspect src/ap/modules/qmk/port/sys_port.* , src/hw/driver/
- upstream_sync_rule: diff_against_quantum_then_reapply_port_overrides

## COLLAB_RULES_FOR_CODEX
- response_language: korean_only
- analysis_checklist:
  1. inspect `_DEF_FIRMWARE_VERSION` + board macros @ src/hw/hw_def.h
  2. follow init_chain src/main.c → src/ap/ap.c
  3. review layout modules @ src/ap/modules/qmk/{port,keyboards,quantum}
- changelog_notation: VYYMMDDRn (increment Rn per same-day revision)
- firmware_version_policy: keep `_DEF_FIRMWARE_VERSION` synced with newest VYYMMDDRn
- critical_review_targets: [src/ap/modules/qmk/port/sys_port.* , src/hw/driver/]

## BUILD_PIPELINE
- uf2_conversion: handled_in_cmake_targets
- standard_build:
  - step1: cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/sirind/brick60'
  - step2: cmake --build build -j10

## DIRECTORY_INDEX
```text
ROOT
├─ CMakeLists.txt              :: global_cmake
├─ LICENSE                     :: project_license
├─ README.md                   :: codex_reference
├─ prj/                        :: ide_configs
├─ src/                        :: firmware_sources
└─ tools/                      :: toolchain_and_uf2

prj/
└─ vscode/
   ├─ baram-45k.code-workspace           :: vscode_workspace_baram_45k
   └─ baram-60mx-6.25u.code-workspace    :: vscode_workspace_baram_60mx_6.25u

src/
├─ main.c , main.h             :: system_entrypoint + shared_decls
├─ ap/                         :: application_layer + qmk_integration
├─ bsp/                        :: board_support_package
├─ common/                     :: shared_defs + errors + hw_helpers
├─ hw/                         :: hardware_abstraction + firmware_version
└─ lib/                        :: external_libraries

src/ap/
├─ ap.c , ap.h                 :: main_loop + app_interface
├─ ap_def.h                    :: app_constants
└─ modules/                    :: functional_modules (includes qmk)

src/ap/modules/qmk/
├─ CMakeLists.txt              :: module_build_config
├─ keyboards/                  :: board_layouts + metadata
├─ port/                       :: stm32h7s_abstraction_for_qmk
├─ quantum/                    :: qmk_core_logic
├─ qmk.c                       :: firmware_qmk_bridge
└─ qmk.h                       :: module_interface

src/ap/modules/qmk/port/
├─ kill_switch.* , kkuk.*
├─ matrix.{c,h} , override.{c,h}
├─ platforms/* , protocol/*
├─ sys_port.* , ver_port.*
└─ via_hid.* , version.h
purpose: override_qmk_for_stm32h7s_timers_gpio_usb + safety_comm_features

src/ap/modules/qmk/quantum/
- input_processing: action* , keycode* , keymap_* , process_keycode*
- device_features: mousekey* , joystick* , painter , pointing_device* , digitizer*
- feedback_modules: rgb_matrix , rgblight , led* , haptic* , audio , backlight
- state_management: eeconfig* , dynamic_keymap* , wear_leveling , sync_timer*
- communication: via* , virtser , raw_hid , usb*
- utilities: util.h , wait.* , timer.* , bootmagic , command*

src/bsp/
├─ bsp.c , bsp.h               :: board_init + handler_registration
├─ device/                     :: clock + pinmap_configs
├─ ldscript/                   :: linker_scripts + memory_layout
└─ startup/                    :: startup_asm + init

src/common/
├─ core/                       :: core_utilities
├─ def.h                       :: global_macros
├─ err_code.h                  :: error_tables
└─ hw/                         :: shared_hw_utilities

src/hw/
├─ hw.c , hw.h                 :: hardware_layer_init + module_registry
├─ hw_def.h                    :: board_defs + `_DEF_FIRMWARE_VERSION`
└─ driver/                     :: usb_gpio_i2c_drivers

src/lib/
├─ ST/                         :: st_hal + cmsis
└─ lib8tion/                   :: lib8tion_utils

tools/
├─ W25Q16JV_BARAM-QMK-H7S.stldr      :: stlink_loader_config
├─ arm-none-eabi-gcc.cmake           :: toolchain_options
└─ uf2/
   ├─ uf2conv.py                      :: uf2_converter
   └─ uf2families.json                :: uf2_board_ids
```

## ADDITIONAL_NOTES
- usb_monitor_profile:
    version: V250924R4
    holdoff: 750ms
    warmup: 2048_hs_microframes_or_2.75s_timeout
    downgrade_caps: {hs_events:12 , fs_events:6}
    activation: confirmation_delay_required
    bookkeeping: microsecond_metrics + millis() persistence
- follow_up_research: [host_compatibility_validation , long_window_recovery]
