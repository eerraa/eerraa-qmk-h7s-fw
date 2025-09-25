# eerraa-qmk-h7s-fw

PROJECT_CONTEXT
- TARGET_MCU: STM32H7S series
- USB_PHY_MODE: internal High-speed PHY only
- BASE_REPORT_RATE_OBJECTIVE: 8000Hz interrupt-driven input at High-speed
- STATUS_EXTENDED_REPORT_RATES: High-speed 4000Hz + High-speed 2000Hz + Full-speed 1000Hz implemented
- IMPLEMENTED_FEATURES:
  - USB_INSTABILITY_MONITOR: microframe gap scoring with warm-up holdoff + timeout guarding enumeration jitter (V250924R3)
  - POLLING_RATE_DOWNGRADE: staged downgrade queue with confirmation delay + EEPROM persistence to avoid repeated resets
- OPEN_FOLLOW-UPS:
  - HOST_COMPAT_MATRIX: collect enumeration logs across Windows/macOS/Linux for the V250924R3 monitor heuristics
  - AUTO_RECOVERY_POLICY: evaluate restoring higher rates after sustained stability windows
- QMK_PORT_SUMMARY: QMK logic ported and tuned for high-bandwidth USB keyboard firmware

CODEX_RULES
- RESPONSE_LANGUAGE: Korean (all answers must be in Korean)
- ANALYSIS_SEQUENCE:
  1. VERIFY_VERSION_AND_BUILD_PARAMS: inspect `_DEF_FIRMWARE_VERSION` and board macros in `src/hw/hw_def.h`
  2. TRACE_ENTRYPOINT: follow init path `src/main.c` -> `src/ap/ap.c`
  3. MAP_QMK_LAYERS: review `src/ap/modules/qmk/{port,keyboards,quantum}`
- CHANGELOG_NOTATION: annotate edits with `'VYYMMDDRn'` comments; increment `Rn` for multiple revisions per day
- FIRMWARE_VERSION_POLICY: sync `_DEF_FIRMWARE_VERSION` in `src/hw/hw_def.h` to `VYYMMDDRn`
- HIGH_PRIORITY_REVIEW_FILES: `src/ap/modules/qmk/port/sys_port.*`, `src/hw/driver/`

BUILD_PIPELINE
- NOTE: UF2 conversion handled inside CMake targets
- COMMANDS:
  - `cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/sirind/brick60'`
  - `cmake --build build -j10`

DIRECTORY_MAP
- ROOT
  - PATH=CMakeLists.txt :: ROLE=global CMake configuration
  - PATH=LICENSE :: ROLE=project license record
  - PATH=README.md :: ROLE=Codex quick reference and workflow rules
  - PATH=prj/ :: ROLE=IDE environment configurations
  - PATH=src/ :: ROLE=all firmware sources and libraries
  - PATH=tools/ :: ROLE=toolchain helpers and UF2 utilities
- PRJ
  - PATH=prj/vscode/baram-45k.code-workspace :: ROLE=VS Code workspace for Baram 45K board
  - PATH=prj/vscode/baram-60mx-6.25u.code-workspace :: ROLE=VS Code workspace for Baram 60MX-6.25U board
- SRC
  - PATH=src/main.c :: ROLE=system entry (`main`), handles init + app launch
  - PATH=src/main.h :: ROLE=shared declarations for main module
  - PATH=src/ap/ :: ROLE=application layer + QMK port integration
  - PATH=src/bsp/ :: ROLE=board support package and startup code
  - PATH=src/common/ :: ROLE=shared definitions, errors, hardware helpers
  - PATH=src/hw/ :: ROLE=hardware abstraction layer + firmware version definitions
  - PATH=src/lib/ :: ROLE=external libraries (ST HAL, utilities)
- SRC/AP
  - PATH=src/ap/ap.c :: ROLE=main loop + QMK task scheduling
  - PATH=src/ap/ap.h :: ROLE=application interface declarations
  - PATH=src/ap/ap_def.h :: ROLE=application-level constants/structs
  - PATH=src/ap/modules/ :: ROLE=functional modules including QMK port
- SRC/AP/MODULES/QMK
  - PATH=src/ap/modules/qmk/CMakeLists.txt :: ROLE=QMK module build target config
  - PATH=src/ap/modules/qmk/keyboards/ :: ROLE=keyboard-specific layouts, ports, metadata
  - PATH=src/ap/modules/qmk/port/ :: ROLE=STM32H7S hardware abstraction for QMK
  - PATH=src/ap/modules/qmk/quantum/ :: ROLE=QMK core logic and shared modules
  - PATH=src/ap/modules/qmk/qmk.c :: ROLE=firmware↔QMK init bridge
  - PATH=src/ap/modules/qmk/qmk.h :: ROLE=QMK module external interface
- KEYBOARDS_SUBSTRUCTURE
  - CONTENTS=config.cmake (CMake options), config.h (USB IDs, matrix), info.json (metadata), json/*.json (VIA/Vial), keymap.c (keymap logic), port/* (per-board features)
  - MAJOR_BOARDS=baram/45k, baram/60MX-6.25U, era/sirind/brick60, era/sirind/brick60h
- PORT_SUBSTRUCTURE
  - ELEMENTS=kill_switch.*, kkuk.*, matrix.{c,h}, override.{c,h}, platforms/*, protocol/*, sys_port.*, ver_port.*, via_hid.*, version.h
  - FUNCTION=override base QMK behavior for STM32H7S timers/GPIO/USB, implement safety + communication
- QUANTUM_CORE
  - INPUT_PROCESSING=action*, keycode*, keymap_*, process_keycode*
  - DEVICE_FEATURES=mousekey*, joystick*, painter, pointing_device*, digitizer*
  - FEEDBACK_MODULES=rgb_matrix, rgblight, led*, haptic*, audio, backlight
  - STATE_MANAGEMENT=eeconfig*, dynamic_keymap*, wear_leveling, sync_timer*
  - COMMUNICATION=via*, virtser, raw_hid, usb*
  - UTILITIES=util.h, wait.*, timer.*, bootmagic, command*
- SRC/BSP
  - PATH=src/bsp/bsp.c :: ROLE=board init + hardware handler registration
  - PATH=src/bsp/bsp.h :: ROLE=BSP interface declarations
  - PATH=src/bsp/device/ :: ROLE=clock + pinmap low-level configs
  - PATH=src/bsp/ldscript/ :: ROLE=linker scripts and memory layout
  - PATH=src/bsp/startup/ :: ROLE=startup assembly and init
- SRC/COMMON
  - PATH=src/common/core/ :: ROLE=core utilities
  - PATH=src/common/def.h :: ROLE=global macros
  - PATH=src/common/err_code.h :: ROLE=error code tables
  - PATH=src/common/hw/ :: ROLE=shared hardware utilities
- SRC/HW
  - PATH=src/hw/hw.c :: ROLE=hardware layer init + module registration
  - PATH=src/hw/hw.h :: ROLE=hardware interfaces
  - PATH=src/hw/hw_def.h :: ROLE=board definitions + `_DEF_FIRMWARE_VERSION`
  - PATH=src/hw/driver/ :: ROLE=USB/GPIO/I2C driver implementations
- SRC/LIB
  - PATH=src/lib/ST/ :: ROLE=ST HAL + CMSIS
  - PATH=src/lib/lib8tion/ :: ROLE=lib8tion utilities used by QMK
- TOOLS
  - PATH=tools/W25Q16JV_BARAM-QMK-H7S.stldr :: ROLE=ST-LINK loader config
  - PATH=tools/arm-none-eabi-gcc.cmake :: ROLE=toolchain path/options definition
  - PATH=tools/uf2/uf2conv.py :: ROLE=UF2 converter script
  - PATH=tools/uf2/uf2families.json :: ROLE=UF2 board identifiers

ADDITIONAL_NOTES
- USB_MONITOR_BEHAVIOUR:
  - `USB_SOF_MONITOR_CONFIG_HOLDOFF_MS` = 750ms setup grace; scoring starts only after configuration+resume completes and warm-up succeeds
  - Warm-up requires 2048 HS microframes (128 FS frames) or a 2.75s timeout before instability affects downgrade scoring
  - Score threshold: HS=12, FS=6 with per-event cap; downgrade requests arm only after confirmation delay to filter transient spikes
- USB_PHY_POLICY: High-speed only; Full-speed or external PHY code remains disabled unless tied to the new 1000Hz support work
- TIMING_SENSITIVITY: preserve 8000Hz scheduling; review `src/ap/modules/qmk/port/sys_port.*` and `src/hw/driver/` when touching timers or DMA
- QMK_UPSTREAM_SYNC: compare `quantum/` first, adjust platform differences inside `port/`
