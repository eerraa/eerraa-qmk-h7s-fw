# Verification manual

Genre: manual
Canonical for: checks that run without a board and their commands,
what each check bites, what a change owes, toolchain premises,
what only hardware can decide and how to read it, and symptom order

## 1. Commands and what a change owes

| Command | Bites |
| --- | --- |
| `PYTHONUTF8=1 python tools/era_doc_refs.py` | document–code match, nine checks (`docs/MAP.md` §8) |
| `pwsh -NoProfile -File tools/era_via_host_tests/run.ps1` | VIA value layer, `0x07` diagnostics, RGB SLEEP, EEPROM CLEAN, VERSION GET, single raw-HID TX producer |
| `python tools/era_via_host_tests/run.py` | same host tests when `gcc` is on PATH |
| cmake (§4) | compile, link, `_Static_assert`, size |
| `PYTHONUTF8=1 python tools/era_doc_refs_selftest.py` | the checker is not empty |

| Change | Owes |
| --- | --- |
| `docs/` only, not the checker | `PYTHONUTF8=1 python tools/era_doc_refs.py`. No ARM build. |
| official VIA JSON | that checker (`menu`) |
| `tools/era_doc_refs.py` or `tools/era_doc_refs_selftest.py` | checker and selftest |
| `tools/era_via_host_tests/` or a firmware file that `tools/era_via_host_tests/run.ps1` compiles or that `tools/era_via_host_tests/check_single_producer.py` reads | the host-test command |
| firmware `src/` | checker and §4. Also the host-test command when the previous row applies |

**A change that does not touch firmware source does not owe an ARM
build.** That sentence is the verification statement.

`hooks/pre-commit` runs the checker with `PYTHONUTF8=1` after one
`git config core.hooksPath hooks` per clone. There is no CI workflow.

## 2. Toolchain premises

The checker needs Python 3. `PYTHONUTF8=1` is required when the host
Python default is not UTF-8; `hooks/pre-commit` already exports it. It
does not need ARM gcc. Host tests are a separate command because
`tools/era_via_host_tests/run.ps1` hard-codes an absolute mingw gcc
path.

ARM gcc is not on PATH. On the Windows machine, Git Bash prepends:

```bash
export PATH="/d/baram-fw-tools_exe/arm_toolchain/arm_gcc/gcc-arm-none-eabi-10.3-2021.10/bin:$PATH"
export PATH="/d/baram-fw-tools_exe/arm_toolchain/make/xpack-windows-build-tools-4.4.0-1/bin:$PATH"
```

`CMakeLists.txt` requires CMake 3.13 and Python 3 (UF2 post-build).
`tools/arm-none-eabi-gcc.cmake` requires `ARM_TOOLCHAIN_DIR` on
Windows; if it is unset on non-Windows, the prefix is `arm-none-eabi-`
for PATH. The named firmware command uses `-G "MinGW Makefiles"`.

Git Bash rewrites `-DKEYBOARD_PATH='/keyboards/...'` unless
`MSYS_NO_PATHCONV=1`. That variable also blocks conversion of
`ARM_TOOLCHAIN_DIR`, so that value is Windows form (`D:/...`). An MSYS
form fails configure: "is not a full path to an existing compiler
tool".

## 3. Host tests

`tools/era_via_host_tests/run.ps1` builds the host binaries with that
mingw gcc, runs them, then
`tools/era_via_host_tests/check_single_producer.py`. It does not use
the ARM toolchain. Which sources it compiles is the script.

| Source | Contract |
| --- | --- |
| `tools/era_via_host_tests/test_era_via_exact_ms.c` | `docs/contract_via.md` §3 exact-ms, §5 `0x06`, §7 MOUSE |
| `tools/era_via_host_tests/test_usb_diagnostics.c` | `docs/contract_via.md` §6 |
| `tools/era_via_host_tests/test_rgb_sleep.c` | RGB SLEEP GET/SET/SAVE, default 600 s / 10 min, CLEAN, idle/suspend/host-gone, official GET projection |
| `tools/era_via_host_tests/test_sys_eeprom_clean.c` | SYSTEM CLEAN three-toggle GET/SET, 10 s window, Jump to Boot SET 0 |
| `tools/era_via_host_tests/test_version.c` | VERSION GET ASCII `YYMMDDRn` plus NUL at value 5, with legacy Year/Month/Day/Rev GET values 1–4 retained (`V260901R1`) |
| `tools/era_via_host_tests/check_single_producer.py` | `docs/contract_via.md` §1 |

`tools/era_via_host_tests/check_single_producer.py` reads
`src/ap/modules/qmk/port/via_hid.c` and
`src/ap/modules/qmk/quantum/via.c`.

## 4. ARM build

```powershell
cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/keynetix/may65' -G "MinGW Makefiles"
cmake --build build -j10
```

`KEYBOARD_PATH` is one of the five in `docs/MAP.md` §3. UF2 is a
`CMakeLists.txt` POST_BUILD: `tools/uf2/uf2conv.py`, family
`0xFFFF0002`. Five boards share source and differ by
`-DKEYBOARD_PATH`. A change under `<board>/config.h` or `<board>/port/`
is built with that board.

**Size compare only the same board and the same toolchain.** Name the
baseline firmware version with any RAM/FLASH delta.

## 5. Hardware-only

What is still open is `docs/state_open.md`. This section is how to
read a result.

Comparable diagnostic axes are `docs/contract_via.md` §6-1. Absolute
microseconds include a phase redrawn every enumeration and are not a
run-to-run axis. Do not mix firmware versions in one set.

Boot counters are RAM saturating `uint32`, copied into a snapshot.
They are not a live page. CLEAR does not zero them
(`docs/contract_usb.md` §4). A later GET of the same snapshot does not
refresh them. To see whether an event was counted: snapshot after the
event on the same boot (boot counters), or a session that contained
the event (session fields). Apply stores the mode and resets the MCU
(`docs/contract_usb.md` §5), so those counters start at 0 on that
boot.

Apply tears USB. An in-flight diagnostic session cannot continue on
the same enumeration. Reconnect and start a new session.

## 6. Symptom order

| Symptom | Check |
| --- | --- |
| UF2 uploaded, firmware does not start by itself | Cable reinsert starts it → handoff detach. `docs/contract_usb.md` §6. Reproduce on a board still running the old bootloader. |
| No keys in BIOS/UEFI | Set USB POLLING to 1 kHz (FS). If still dead, the two boot-protocol deviations in `docs/contract_usb.md` §3. |
| 21st key does not register | Specified. `docs/contract_usb.md` §2. |
| Cursor never moves | Keymap needs a mouse keycode (`KC_MS_UP` and the rest). Channel 17 converts speed of a report that is already sending; it does not place the keycode. |
| Accel Off is too fast | `mousekey_config_apply_runtime()` writes `mk_max_speed` 1 when `time_to_max == 0`. `docs/contract_via.md` §7-3. |
| Accel 2.0 s reads back 1.3 s | 200 /s caps the ramp at 1.275 s; GET is the stored shorter value. `docs/contract_via.md` §7-2. |
| Ramp duration moves when interval SET | Interval SET must re-derive events from the held duration. `docs/contract_via.md` §7-2. Host test. |
| Official dropdown sent 130 ms, store is 120 ms | Legacy SET floors onto the 20 ms grid. `docs/contract_via.md` §3. |
| Value gone after reboot | VIA SAVE is the flush. `docs/contract_eeprom.md` §1. |
| VIA reply missing or is another request's reply | Two TX producers. `tools/era_via_host_tests/check_single_producer.py`. `docs/contract_via.md` §1. |
| Firmware feature missing from VIA | Official JSON has no menu for that channel. Checker's `menu`. |
| Boot stuck on LED blink | EEPROM auto-init failed three times. `docs/contract_eeprom.md` §2. |
| EEPROM writes feel slow or lost | CLI `eeprom info`: queue max / overflow (`eeprom_get_write_pending_max`, `eeprom_get_write_overflow_count`). |
| Stored polling mode disagrees with the boot log | CLI `boot info` prints `usbBootModeGet()`. No automatic revert. `docs/contract_usb.md` §4. |
