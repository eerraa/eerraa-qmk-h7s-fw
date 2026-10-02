# Verification manual

Genre: manual
Canonical for: change-to-check routing, toolchain premises, and proof limits

## 1. Change-to-check routing

Run only the checks whose inputs or asserted behavior the change can affect.

| Change | Required check | Evidence boundary |
| --- | --- | --- |
| `docs/` only, checker unchanged | `python -X utf8 tools/era_doc_refs.py` | Local paths, pointers, reachability, menu exposure, retired-USB guard, distribution version consistency, and the storage-format record; not sentence meaning, peer state, or hardware. |
| Official `*-VIA.JSON` | `python -X utf8 tools/era_doc_refs.py` | The `menu` check proves firmware-routed channels are reachable in local official JSON only. |
| `tools/era_doc_refs.py` or `tools/era_doc_refs_selftest.py` | checker plus `python -X utf8 tools/era_doc_refs_selftest.py` | Positive baseline plus planted negative fixtures for the document checker; not product behavior. |
| `hooks/pre-commit`, `.gitattributes`, or `hooks/test_pre_commit.py` | checker plus `python hooks/test_pre_commit.py` | Hook wiring, staged-snapshot execution, interpreter fallback, and fail-closed launcher behavior. |
| `tools/era_via_host_tests/` or firmware source covered by those fixtures | `pwsh -NoProfile -File tools/era_via_host_tests/run.ps1`; when host `gcc` is already on PATH, `python tools/era_via_host_tests/run.py` is the equivalent entry | Compiled host fixtures and source guards only; no ARM target or physical USB device. |
| `tools/firmware_regression_tests/` or firmware source covered by a regression group | `python tools/firmware_regression_tests/run.py`, or an affected `--only` group from its README | Deterministic host/source-region regression coverage; no electrical, silicon, or real-host timing proof. |
| Other firmware `src/` changes | document checker, affected host/regression checks, and an ARM build for each affected board configuration | Compile/link/static assertions and the selected executable fixtures. Add hardware only when the requirement is hardware-only. |
| Stored format, `ERA_EEPROM_RESET_KEY`, or release/distribution preparation | Read `docs/contract_eeprom.md` §2; the document checker's `storage` check must pass against `tools/eeprom_reset_key.json`; run the checks implied by changed firmware/source | The record proves a reset decision was written down, not that old stored bytes read correctly. A successful build or document check is not permission to flash, install, publish, or deploy. |

A documentation-only change does not owe an ARM build or HIL. A skipped,
unknown, not-run, or hardware-unmeasured item remains unverified.

## 2. Toolchain premises and commands

The document checker and selftest require Python 3. Use UTF-8 mode; the
versioned pre-commit hook exports `PYTHONUTF8=1`. The hook validates a temporary
checkout of the staged index, so unrelated unstaged user changes are not used
as commit evidence.

`tools/era_via_host_tests/run.ps1` owns its Windows host-compiler selection.
The Python host-test entry requires `gcc` on PATH. The firmware-regression
runner uses `HOST_CC` when set and otherwise its documented host-GCC discovery;
`tools/firmware_regression_tests/README.md` owns current groups and fixture
coverage. Do not copy a machine-specific compiler path into this manual.

For an ARM build, `CMakeLists.txt` requires CMake 3.13 and Python 3.
`tools/arm-none-eabi-gcc.cmake` owns toolchain discovery; on Windows,
`ARM_TOOLCHAIN_DIR` must resolve to the ARM toolchain. A representative build is:

```powershell
cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/keynetix/may65' -G "MinGW Makefiles"
cmake --build build -j10
```

Generate official VIA polling-TEXT candidates separately from shipped definitions:

```powershell
python -X utf8 tools/prepare_polling_test_json.py build-usb-polling-delivery/official-via-test
```

The generator validates that each of the five definitions gains only the gated
read-only label. Release packages may include these definitions paired with their
matching support-revision-1 firmware; preserve the source definitions as the old
firmware compatibility path. Generation does not verify an official browser
session. Apply/reboot refresh and legacy-firmware acceptance remain
`docs/state_open.md` items; user guidance must describe the last-read snapshot
and F5 refresh rather than promise automatic refresh.

Build the board whose `config.h` or `<board>/port/` is affected. In Git Bash,
`MSYS_NO_PATHCONV=1` may be needed for the slash-prefixed `KEYBOARD_PATH`;
keep `ARM_TOOLCHAIN_DIR` in the form expected by the CMake toolchain file.
Compare size only for the same board and toolchain.

## 3. Proof limits

- `tools/era_doc_refs.py` checks this repository only. It does not establish
  peer-app compatibility, document semantics, or physical behavior.
- `tools/era_doc_refs_selftest.py` proves that active document-checker failure
  classes are caught by its positive/negative fixtures; it does not widen the
  checker's product coverage.
- `hooks/test_pre_commit.py` proves launcher and staged-index behavior. It does
  not prove the checker itself beyond the checker process result.
- VIA host tests and firmware regression tests execute selected production
  source against host fixtures/stubs. Their source/README files own exact
  coverage. They do not emulate the Cortex-M memory system, electrical timing,
  real host scheduling, silicon errata, power interruption, or a physical
  WS2812/USB/I2C path.
- An ARM build proves configure/compile/link/UF2 generation for the selected
  board and toolchain. It does not prove boot, USB enumeration, persistence,
  host compatibility, or latency on a device.
- Cross-repository compatibility is separate evidence and must name the peer
  revision actually compared or tested.
- Hardware/external questions still open are listed only in
  `docs/state_open.md`. Run them only when the task is authorized to use the
  required device/environment; otherwise report them as unverified.
