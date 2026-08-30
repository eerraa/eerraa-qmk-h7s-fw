# Dead code and retired architecture

Genre: map
Canonical for: where unused, retired, or paired-stop code sits in this
tree, and the class of each claim. Not why USB automatic recovery is
refused (`docs/contract_usb.md` §4). Not open-issue start conditions
(`docs/state_open.md`).

Measured at `101c021edbd104106c271e01b489989bc1f20589`. Firmware version
in `src/hw/hw_def.h` is `V260824R2`. Peer trees were read only:
`the-via-eerraa` `a37dfaa768792d8a480621a120b80affb0fd13cd`,
`qmk_firmware_eerraa` `dc2ebf485b748bf8c74fe5eee782b8be70606784`.

This file does not delete firmware. Remaining DELETE rows are later
sessions, one claim-cluster at a time.

## Classes

| Class | Meaning |
| --- | --- |
| DELETE | Unused in this tree; a later session may remove it |
| removed | Gone after a deletion session. Date and PR in the row. Not pending DELETE |
| RETIRED-ID | Name or slot must stay reserved / must not return in `src/` |
| STALE-COMMENT | Text or path is leftover; behavior is live or already gone |
| PAIRED-STOP | Cross-repo mismatch. Record both sides. Do not pick a winner here |
| KEEP | Present, used, gated-on-purpose, or required by a contract |

## 1. Graphify (#266)

Commit `5381fb2` removed graphify-out, tools/graphify, .graphifyignore,
Claude settings SessionStart wiring, and graphify-first prose.
Whole-tree search for graphify, GRAPHIFY, graphify-out, .graphifyignore,
and SessionStart: **0 hits**.

`AGENTS.md` Navigation still refuses an obligatory knowledge graph and
session-start search hook. That is the restoration ban, not leftover
tooling.

| Item | Class | Proof |
| --- | --- | --- |
| Graphify files, hooks, comments, paths | RETIRED-ID | Absent after `5381fb2`. Do not restore |
| Forbidden `Status:` / `Read when:` headers | KEEP | Checker tuple in `tools/era_doc_refs.py:37`. Not a Graphify leftover |

## 2. USB automatic recovery / monitor

`src/` has **0 hits** for `usbMonitor`, `usbInstability`, `usbHidMonitor`,
`usbRequestBootModeDowngrade`, `usbd_hid_instrumentation`,
`USB_MONITOR_ENABLE`, `auto_downgrade`. The C is not compiled because it
is not in the tree. Why it must not return: `docs/contract_usb.md` §4.
The checker list is `tools/era_doc_refs.py:50-58`.

| Item | Class | Proof |
| --- | --- | --- |
| Monitor / autodowngrade C | RETIRED-ID | Already gone from `src/`. `retired` fails if the names return |
| Channel 13 value id 3 | RETIRED-ID | Comment only, `src/ap/modules/qmk/quantum/via.h:121`. Enum is 1 and 2, `via.h:160-163`. Official JSON exposes 13/1 and 13/2 (`src/ap/modules/qmk/keyboards/era/keynetix/may65/json/MAY65-H7S-VIA.JSON:683` and `:694`). Other value ids fall through to `id_unhandled` (`src/ap/modules/qmk/keyboards/era/keynetix/may65/port/via_port.c:147-157`). Same handler shape on all five boards |
| `EECONFIG_USER_RESERVED_32` | RETIRED-ID | Define only, `src/ap/modules/qmk/port/port.h:23`. No read/write in `src/**/*.c`. Init comment: `src/ap/modules/qmk/port/eeconfig_port.c:17`. Do not compact (`docs/contract_eeprom.md` §1) |
| `usbd_hid.h:166` "legacy HID rate/monitor API" on `usbHidSetStatusLed` | STALE-COMMENT | The function remains; the monitor API does not |
| `via_handle_usb_polling_channel` name | STALE-COMMENT | Channel 13 is BootMode only (`via_port.c:31-33`) |

Selector `0x07` on this tree is observation-only.
`src/ap/modules/qmk/port/era_usb_diagnostics.c` does not call
`usbBootModeScheduleApply`, `usbBootModeSaveAndReset`, or
`usbScheduleGraceReset`. It does not include
`src/ap/modules/qmk/port/era_state_sync.h`. Product-boundary REFUSED
list: `docs/contract_usb.md` §4 (0x07 product boundary). Peer ADR:
`the-via-eerraa/docs/adr/0002-h7s-usb-diagnostics.md`. **No leftover
0x07 control-plane coupling in this firmware.** Live path: KEEP.

## 3. Selector `0x06` short packet — PAIRED-STOP

Do not pick a winner.

| Side | What a length other than 32 does | Proof |
| --- | --- | --- |
| H7S | `era_state_sync_via_command` returns false when `length < 32`. It does not write `ERA_STATE_SYNC_STATUS_INVALID`. `via.c` sets `id_unhandled` (`0xFF`) | `src/ap/modules/qmk/port/era_state_sync.c:70-74`; `src/ap/modules/qmk/quantum/via.c:344-346`; host test 31-byte false return in `tools/era_via_host_tests/test_era_via_exact_ms.c` |
| H7S contract | Short is not INVALID; peer parse returns null | `docs/contract_via.md` §5 |
| QMK `work/era-nvm` | Same gate: `length < 32` returns false, no INVALID fill | `qmk_firmware_eerraa/keyboards/era/common/system/era_state_sync.c:196-201`. Capable path then calls `raw_hid_send` (H7S TX producer is different; that is `docs/contract_via.md` §1, not this row) |
| VIA app | parseStateSyncEnvelope: length not 32 → null. Byte0 `0xFF` on a 32-byte report is a separate null | `the-via-eerraa/src/utils/era-state-sync.ts:68-73` |
| VIA ADR 0001 | Wire is a 32-byte payload. Status list includes INVALID `0x02`. The ADR does not bind a short HID length to INVALID or to `0xFF` | `the-via-eerraa/docs/adr/0001-state-sync-protocol.md` (Accepted 32-byte wire contract) |

`0x07` uses the same short-length false → `id_unhandled` path
(`src/ap/modules/qmk/port/era_usb_diagnostics.c:217-219`,
`via.c:350-352`). Endpoint TX is still 32 B.

`raw_hid_send` in `src/ap/modules/qmk/port/via_hid.c:64-68` is an empty
stub on purpose. KEEP / PAIRED-STOP with `docs/contract_via.md` §1.

## 4. Channels 13 and 17 vs RP2040 QMK — PAIRED-STOP

The hunch that this firmware does not answer mouse or USB polling is
false. H7S answers **both**. The numbers differ from QMK.

| Control | H7S | QMK `work/era-nvm` |
| --- | --- | --- |
| USB BootMode | Channel 13, values 1–2 | No BootMode channel |
| MOUSE six controls | Channel 17, values 1–6. JSON 17,* e.g. MAY65 line 380 | Channel 13 (ERA_VIA_MOUSEKEY_CHANNEL in `qmk_firmware_eerraa/keyboards/era/common/storage/era_eeprom_layout.h:191`) |
| VIA reserved 1 / 3 / 4 / 5 | Enum only. `via.c` handlers sit behind `BACKLIGHT_ENABLE` / `RGB_MATRIX_ENABLE` / `AUDIO_ENABLE` / `LED_MATRIX_ENABLE`, which this CMake does not set | Stock VIA reserved |

H7S-only live channels 8 (version) and 16 (Tap Dance) are KEEP.

QMK SOCD/KKUK value id 4 (mode) has no H7S VIA id. H7S
`kill_switch_config.mode` and `kkuk_config.mode` are forced to `1` at
init (`src/ap/modules/qmk/port/kill_switch.c:59-75`). Layout KEEP.
NKRO: not a compile definition (`src/ap/modules/qmk/CMakeLists.txt:129-130`).
`keymap_config.nkro` has no effect. KEEP / PAIRED-STOP to restore
(`docs/contract_usb.md` §3).

`docs/state_open.md` §4 still says RP2040 JSON uses the name
Anti-Ghosting. At QMK `dc2ebf48` the HID contract states the 25 files
label the submenu KKUK. That sentence in `docs/state_open.md` is
STALE-COMMENT. This campaign does not edit `docs/state_open.md`.

Peer leftover (not this `src/`): `the-via-eerraa/docs/MAP.md:197` still
lists `eerraa-qmk-h7s-fw-via` and `-via2` as H7S working worktrees.
Recorded in `docs/state_open.md` §4. Do not fix from this repository.

## 5. EEPROM fields this firmware does not speak on the wire

| Field | Class | Proof |
| --- | --- | --- |
| `EECONFIG_USER_RESERVED_32` | RETIRED-ID | §2. Address KEEP |
| MOUSE raw 10 vs VIA 6 | KEEP | `mousekey_config_storage_t` is 16 B / 10 engine fields (`src/ap/modules/qmk/port/mousekey_config.c:49-66`). VIA ids 1–6 only (`via.h:244-250`). Contract: store the rest (`docs/contract_eeprom.md` §1) |
| kill_switch / kkuk `mode` byte | KEEP | Stored; forced to 1; no VIA value id (`kill_switch.c:18-22`) |

Internal-flash EEPROM emulation (`src/hw/driver/eeprom/emul.c:8`) is
compiled only when `EEPROM_CHIP_EMUL` is defined. Product define is
`EEPROM_CHIP_ZD24C128` (`src/hw/hw_caps_eeprom.h:14-16`). Gated KEEP.
Hardware-unverified note stays in `docs/state_open.md`; this file does
not close it.

EEPROM burst thresholds in `src/hw/driver/eeprom/` are live
(`eeprom_get_burst_extra_calls` used from the QMK loop). KEEP. The
deleted persistence_burst_design document must not be restored
(`docs/state_open.md` §3).

## 6. `#if 0` and orphan probes

| Item | Class | Proof |
| --- | --- | --- |
| `HID_MOUSE_ReportDesc` | DELETE | `src/hw/driver/usb/usb_hid/usbd_hid.c:370-412` `#if 0`. Live mouse is EXK report ID 2, not this descriptor. `HID_MOUSE_REPORT_DESC_SIZE` remains `src/hw/driver/usb/usb_hid/usbd_hid.h:59` |
| `USBD_CMPSIT_HIDMouseDesc` | DELETE | `src/hw/driver/usb/usb_cmp/usbd_cmp.c:941-980` `#if 0`, and the TU is also behind `USE_USBD_COMPOSITE` which is defined only when `HW_USB_CMP == 1` (`src/hw/driver/usb/usbd_conf.h:56-58`). Product: `HW_USB_CMP` 0 (`src/hw/hw_caps_usb.h:38-39`) |
| `lib8tion.c` `#if 0` Arduino test | KEEP | Vendor FastLED test. Not this product's HID path |
| `_DEF_ENABLE_USB_HID_TIMING_PROBE` | DELETE | Defined `src/hw/hw_def.h:68-69`. No other `src/` reference. Name matches the retired instrumentation unit |
| `_DEF_ENABLE_MATRIX_TIMING_PROBE` | KEEP | Default 0 on all five `config.h` files. Callers exist in `src/ap/modules/qmk/port/matrix.c` and `src/ap/modules/qmk/port/matrix_instrumentation.c` |
| `loader jump` body commented out | DELETE | `src/hw/driver/loader.c:224-242`, already inside `_USE_HW_LOADER` which is unset |

## 7. Symbols with 0 callers (port / `hw/driver`)

Definitions exist; no other `src/` call.

| Symbol | Class | Proof |
| --- | --- | --- |
| `debounce_profile_restore_defaults` | DELETE | `src/ap/modules/qmk/port/debounce_profile.c:229`. Factory uses `debounce_profile_storage_apply_defaults` (`eeconfig_port.c:18`) |
| `eeprom_write_block` | DELETE | `src/ap/modules/qmk/port/platforms/eeprom.c:338` |
| `usbDiagnosticsGetCurrentSpeed` | DELETE | `src/hw/driver/usb/usb_hid/usb_diagnostics.c:166` |
| `host_last_system_usage` / `host_last_consumer_usage` | DELETE | `src/ap/modules/qmk/port/protocol/host.c:320-326` |
| `get_first_key` | DELETE | `src/ap/modules/qmk/port/protocol/report.c:59` |
| `timer_clear` | DELETE | `src/ap/modules/qmk/port/platforms/timer.c:11` empty body |
| `suspend_wakeup_condition` | DELETE | `src/ap/modules/qmk/port/platforms/suspend.c` |
| `cdcIsInit` | DELETE | Defined `src/hw/driver/cdc.c:34`. `cdcInit` is called (`src/hw/hw.c:135`) |

`raw_hid_send` is called from `via.c` and is a no-op. KEEP (§3).

## 8. Compiled but product-off / empty-table

Root `CMakeLists.txt:39-49` GLOB-recurses `src/hw/*.c` with empty
`EXCLUDE_PATHS`. `--gc-sections` is on (`CMakeLists.txt:135`).

| Item | Class | Proof |
| --- | --- | --- |
| `src/hw/driver/usb/usb_cdc/` and `src/hw/driver/usb/usb_cmp/` TUs | DELETE (compile inclusion) | Always GLOB'd. Product `usbBegin(USB_HID_MODE)` (`src/hw/hw.c:137`). `_USE_HW_VCOM` commented (`src/hw/hw_caps_core.h:23`). `USE_USBD_COMPOSITE` off. Files are the VCOM toggle path, not a second product |
| `_USE_HW_CDC` always defined | KEEP / review | `src/hw/hw_caps_usb.h:14-16` vs `HW_USB_CDC` 0. `cdcInit()` still runs |
| `button.c` / `spi.c` / `qspi.c` / `loader.c` | KEEP (gated empty TU) | `_USE_HW_BUTTON`, `_USE_HW_SPI`, `_USE_HW_LOADER` unset; `_USE_HW_QSPI` commented |
| `HAL_I3C_MODULE_ENABLED` | DELETE (the enable) | `src/bsp/device/stm32h7rsxx_hal_conf.h:56`. No `src/hw` / `src/ap` I3C call. HAL `.c` is GLOB'd |
| `src/ap/modules/qmk/quantum/sequencer/*.c` | DELETE (CMake GLOB) | `src/ap/modules/qmk/CMakeLists.txt:78`. `SEQUENCER_ENABLE` is not a compile definition. sequencer.c has no feature guard around its globals |
| `KEY_OVERRIDE_ENABLE` | DELETE (the define) | Set `src/ap/modules/qmk/CMakeLists.txt:124`. `process_key_override.c` is in the file list. Weak `key_overrides = NULL` (`src/ap/modules/qmk/quantum/process_keycode/process_key_override.c:87`). No board keymap defines a table |
| Unlinked QMK modules (backlight, audio, rgb_matrix, led_matrix, unicode, split_common, bootmagic, wear_leveling, most of `src/ap/modules/qmk/quantum/process_keycode/*.c`) | KEEP | On disk, not in the CMake file list. `AGENTS.md` §3 merge procedure compares `src/ap/modules/qmk/quantum/` then re-applies `src/ap/modules/qmk/port/` |
| `GRAVE_ESC_ENABLE` / `send_string` via dynamic macros / `quantum/logging` | KEEP | All five boards define `GRAVE_ESC_ENABLE`. Macros call `send_string_with_delay`. `keyboard.c` binds `sendchar` |

## 9. Headers never included

No `#include` of these kit headers anywhere in the tree:

`src/common/hw/include/buzzer.h`, `cmd.h`, `es8156.h`, `fatfs.h`,
`fault.h`, `files.h`, `mixer.h`, `nvs.h`, `pdm.h`, `pwm.h`,
`resize.h`, `swtimer.h`, `touch.h`, `w5300.h`.

Class: DELETE. They are BARAM-kit leftovers. `hw.h` does include
`button.h`, `spi.h`, `qspi.h`, `cdc.h` (gated shells).

## 10. Tools, tests, IDE — nothing CI-runs

There is no `.github/workflows` tree.

| Artifact | Who runs it | Class |
| --- | --- | --- |
| `tools/era_doc_refs.py` | `hooks/pre-commit`, `docs/manual_verify.md` | KEEP |
| `tools/era_doc_refs_selftest.py` | Manual, when the checker changes | KEEP |
| `tools/era_via_host_tests/run.ps1` | Manual. Hard-coded mingw `gcc` path | KEEP |
| `tools/uf2/uf2conv.py` | CMake POST_BUILD | KEEP |
| `tools/W25Q16JV_BARAM-QMK-H7S.stldr` | `.vscode/launch.json` `--extload` | KEEP (flash helper) |
| `.vscode/launch.json` OpenOCD `target/stm32u5x.cfg` | MCU family is H7S, not U5 | STALE-COMMENT |
| baram-45k and baram-60mx-6.25u VS Code workspaces under prj/vscode | Absent after 2026-08-30, commit `c3462d9`, PR #277. Named keyboards/baram paths that are not in this tree. CMake GLOB and include dirs are `src/` only. `src/ap/modules/qmk/keyboards/` has era boards only | removed |
| `.vscode/tasks.json` uf2-make-uf2 task | Absent after 2026-08-30, commit `c3462d9`, PR #277. Input was build/baram-45k-h7s.bin. CMake `PRJ_NAME` is baram-qmk-h7s; POST_BUILD still runs `tools/uf2/uf2conv.py` on that bin | removed |
| empty .codex file | Absent after 2026-08-30, commit `c3462d9`, PR #277. Zero bytes, 0 readers | removed |

## 11. Open items that are not unused code

Do not close these in this campaign.

| Item | Class | Why it is in this file |
| --- | --- | --- |
| D-2 / D-3 / D-4 | KEEP | Live paths. Start conditions in `docs/state_open.md` §1 |
| `V260824R2` bootloader handoff 100 ms detach | KEEP | Live, hardware-unverified (`docs/state_open.md` §2, `docs/contract_usb.md` §6) |
| Internal-flash EEPROM emulation | KEEP | Gated off on shipping boards (§5) |

## 12. Proposed deletion-session order

One claim-cluster per later session. Do not combine with a source
version bump unless that session edits `src/`.

1. IDE / workspace stale paths — removed 2026-08-30, commit `c3462d9`,
   PR #277. No firmware image change. Remaining `.vscode/tasks.json`
   cmake tasks and `.vscode/launch.json` stay.
2. `#if 0` HID mouse descriptor blocks (`usbd_hid.c`, `usbd_cmp.c`) plus
   unused `HID_MOUSE_REPORT_DESC_SIZE` if nothing else remains.
3. Orphan `_DEF_ENABLE_USB_HID_TIMING_PROBE`.
4. Zero-caller helpers in §7 (one session; split if review wants a
   smaller diff).
5. Never-included kit headers in §9.
6. `KEY_OVERRIDE_ENABLE` and drop `process_key_override.c` from CMake
   if the table stays NULL.
7. Remove `src/ap/modules/qmk/quantum/sequencer/*.c` from the QMK CMake GLOB.
8. Undefine `HAL_I3C_MODULE_ENABLED`.
9. Stop GLOB-compiling `src/hw/driver/usb/usb_cdc/` and
   `src/hw/driver/usb/usb_cmp/` when `HW_USB_CMP` is 0
   (highest USB risk in this list; VCOM toggle must still work).

Not a deletion session unless Hyojin says so: `EECONFIG_USER_RESERVED_32`,
the retired-symbol list, `raw_hid_send` stub, EEPROM burst code,
channel 13/17 layout, `0x06`/`0x07` handlers, thinning unlinked QMK
modules under `src/ap/modules/qmk/quantum/`, Graphify restore, D-2/D-3/D-4,
bootloader handoff, closing `docs/state_open.md` items.

## 13. Product firmware identifiers renamed (this PR)

Not a deletion. No `src/` edit. Firmware version stays `V260824R2`.

`CMakeLists.txt` `PRJ_NAME` baram-qmk-h7s → eerraa-qmk-h7s. That stem is the elf, map, and POST_BUILD `${PROJECT_NAME}.bin`. `.vscode/launch.json` three `executable` paths follow `./build/eerraa-qmk-h7s.elf`. `tools/W25Q16JV_BARAM-QMK-H7S.stldr` → `tools/W25Q16JV_EERRAA-QMK-H7S.stldr` by git mv (same SHA-256). Two `--extload` paths follow.

§10 still names the old stldr as measured at `101c021`. Do not restore the old `PRJ_NAME` value. The loader ELF still contains the C string `W25Q16JV_BARAM-QMK-H7S`; the blob was not patched.
