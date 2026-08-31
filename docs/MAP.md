# H7S firmware — data map

Genre: map
Canonical for: where each fact lives in this repository, which side wins
on a mismatch, and what bites that mismatch — the document index,
generated board / channel / EEPROM tables, peer-repository pairing, and
this repository's remaining document rules

Why a fact has its present shape is `docs/contract_via.md`,
`docs/contract_usb.md`, `docs/contract_eeprom.md`. Open items are
`docs/state_open.md`. History is `git log`. Dates, session narrative,
and progress do not live here.

## 1. Which side wins

| Fact | Canonical | What bites a mismatch |
| --- | --- | --- |
| Current firmware version | `_DEF_FIRMWARE_VERSION` in `src/hw/hw_def.h` | `version` — a document literal above current fails |
| VIA channel and value id | `src/ap/modules/qmk/quantum/via.h` | `table`, `menu` |
| EEPROM USER layout | `src/ap/modules/qmk/port/port.h` | `table` |
| Board list and PID | tree under `src/ap/modules/qmk/keyboards/era/` | `table` |
| VIA wire envelopes (`0x06` / `0x07`) and value layer | source; the other side is the app ADRs (§7) | `tools/era_via_host_tests/run.ps1` |
| raw-HID TX producer | `src/ap/modules/qmk/port/via_hid.c` | `tools/era_via_host_tests/check_single_producer.py` |
| User-facing copy | `docs/readme.txt` | `version` (release filenames) |
| Where code lives and what it calls | source. Look up with source search | none — a derived index is not canonical |

On a document/code mismatch, **code wins.** Edit the document and record
why in the commit message. When code violates a contract, the contract
wins — that contract pairs with the app repository in §7, so editing
only this side breaks the app.

## 2. Document index

| Document | Genre | Owns |
| --- | --- | --- |
| [contract_via.md](contract_via.md) | contract | VIA/app wire contract. Channel addresses, exact-ms, `0x06`/`0x07` envelopes, single TX producer, MOUSE unit conversion |
| [contract_usb.md](contract_usb.md) | contract | USB host contract. Interface/report layout, boot-protocol deviation, polling-mode ownership, retired automatic recovery, periodic-work timer rule |
| [contract_eeprom.md](contract_eeprom.md) | contract | Persistent-state contract. USER slot ownership, version cookie and factory reset, 8 kHz write budget |
| [manual_verify.md](manual_verify.md) | manual | Checks that run without a board and their commands, what only hardware can decide, symptom order |
| [state_open.md](state_open.md) | state | Undecided items and start conditions. The only document that goes away with time |
| [readme.txt](readme.txt) | (user document) | User copy shipped with a release. Exception to the agent-doc spec — §8 |

`AGENTS.md` and `CLAUDE.md` are the entry chain, so they omit the header
pair. Do not keep a second document list between those files and this
index — `AGENTS.md` routes by Change / Locate / Verify; this section
routes by **what is original**.

## 3. Boards

`python tools/era_doc_refs.py --tables` reprints the `boards`,
`via-channels`, `eeprom-slots`, and `wire-values` tables from source.
Do not hand-edit the marker blocks — `table` compares them to source.
`wire-values` lives in `docs/contract_via.md`. Byte envelopes stay in
the contracts; this file does not copy them.

<!-- era-doc-refs: boards -->
| Board | Source | Official VIA JSON (under json/) | PID |
| --- | --- | --- | --- |
| BRICK60 | `src/ap/modules/qmk/keyboards/era/sirind/brick60/` | `BRICK60-H7S-VIA.JSON` | `0x0022` |
| BRICK65 | `src/ap/modules/qmk/keyboards/era/sirind/brick65/` | `BRICK65-H7S-VIA.JSON` | `0x0023` |
| INTIGRITY80 | `src/ap/modules/qmk/keyboards/era/intigrity80/` | `INTIGRITY80-VIA.JSON` | `0x0028` |
| MAY65 | `src/ap/modules/qmk/keyboards/era/keynetix/may65/` | `MAY65-H7S-VIA.JSON` | `0x0030` |
| SCULPTUREI | `src/ap/modules/qmk/keyboards/era/sirind/sculpturei/` | `SCULPTUREI-VIA.JSON` | `0x0034` |
<!-- era-doc-refs: end -->

vendorId is `0x4552` on all five. The five boards share one source tree
and differ only by `-DKEYBOARD_PATH`.

## 4. VIA channels

Column meanings, channel 2 (VIA core, no board routing), and reserved
numbers 1 / 3 / 4 / 5: `docs/contract_via.md` §2. Value-id rows live in
that file (`wire-values`).

<!-- era-doc-refs: via-channels -->
| Channel | Name | Firmware routing | Board JSON exposure |
| --- | --- | --- | --- |
| 0 | `id_custom_channel` | O | O |
| 1 | `id_qmk_backlight_channel` | - | - |
| 2 | `id_qmk_rgblight_channel` | - | O |
| 3 | `id_qmk_rgb_matrix_channel` | - | - |
| 4 | `id_qmk_audio_channel` | - | - |
| 5 | `id_qmk_led_matrix_channel` | - | - |
| 8 | `id_qmk_version` | O | O |
| 9 | `id_qmk_system` | O | O |
| 10 | `id_qmk_kill_switch_lr` | O | O |
| 11 | `id_qmk_kill_switch_ud` | O | O |
| 12 | `id_qmk_kkuk` | O | O |
| 13 | `id_qmk_usb_polling` | O | O |
| 14 | `id_qmk_key_response` | O | O |
| 15 | `id_qmk_tapping` | O | O |
| 16 | `id_qmk_tapdance` | O | O |
| 17 | `id_qmk_mousekey` | O | O |
| 18 | `id_qmk_rgb_sleep` | O | O |
<!-- era-doc-refs: end -->

## 5. EEPROM USER slots

Offsets from `EECONFIG_USER_DATABLOCK`. `EECONFIG_USER_DATA_SIZE` is
512 B on every `<board>/config.h`. Slot addresses do not move after
they have shipped (`docs/contract_eeprom.md` §1).

<!-- era-doc-refs: eeprom-slots -->
| Offset | Size | Symbol |
| --- | --- | --- |
| +0 | 8B | `EECONFIG_USER_INDICATOR` |
| +8 | 8B | `EECONFIG_USER_KILL_SWITCH_LR` |
| +16 | 8B | `EECONFIG_USER_KILL_SWITCH_UD` |
| +24 | 4B | `EECONFIG_USER_KKUK` |
| +28 | 4B | `EECONFIG_USER_BOOTMODE` |
| +32 | 4B | `EECONFIG_USER_RESERVED_32` |
| +36 | 4B | `EECONFIG_USER_EEPROM_CLEAR_FLAG` |
| +40 | 4B | `EECONFIG_USER_EEPROM_CLEAR_COOKIE` |
| +44 | 8B | `EECONFIG_USER_DEBOUNCE` |
| +52 | 12B | `EECONFIG_USER_TAPPING_TERM` |
| +64 | 88B | `EECONFIG_USER_TAPDANCE` |
| +152 | 16B | `EECONFIG_USER_MOUSEKEY` |
| +168 | 4B | `EECONFIG_USER_RGB_SLEEP` |
<!-- era-doc-refs: end -->

## 6. Structure questions are answered by source

A structure question is answered by source search (`git grep -n`,
`rg`). A derived index is not canonical. This file only points at
values and contracts.

| Question | Where it is answered |
| --- | --- |
| Where does this function live and what does it call | source search (`git grep -n`, `rg`) |
| Values — offsets, channel numbers, constants | this file §3–§5 (recomputed from source). Value ids: `docs/contract_via.md` (`wire-values`) |
| Why it is this shape | `docs/contract_via.md`, `docs/contract_usb.md`, `docs/contract_eeprom.md` |
| **Absent** — retired subsystems and why | `docs/contract_usb.md` §4 |
| The other side of a cross-repo contract | §7 and the app ADRs |
| What to run and how | `docs/manual_verify.md` |

## 7. Peer repositories

All paths below exist only on this PC. **Keep cwd in this repository**
(`AGENTS.md` §2). To edit the other side, start in that repository from
its `AGENTS.md`.

| Repository | Pairs with |
| --- | --- |
| `the-via-eerraa/docs/adr/0001-state-sync-protocol.md` | selector `0x06` envelope, 3-domain revision, exact-ms rule |
| `the-via-eerraa/docs/adr/0002-h7s-usb-diagnostics.md` | selector `0x07` wire, measurement boundary, comparison validity |
| `the-via-eerraa/docs/adr/0003-era-menu-help-ui.md` | menu labels (KKUK and others) and help copy |
| `the-via-eerraa/docs/MAP.md` §3 | channel / value-id table (H7S TD is channel 16 / 41–48, MOUSE is 17) |
| `qmk_firmware_eerraa/keyboards/era/` | reference implementation. Channel numbers differ (§4 · `docs/contract_via.md` §2) |
| `eerraa-qmk-h7s-boot` | UF2 bootloader. Handoff is `docs/contract_usb.md` §5 |

**A new feature goes into the app custom definition and this
repository's official JSON together.** A path that only the custom app
can speak is an error. Each side has shipped a one-sided change: this
side routed MOUSE channel 17 with no menu in the five official JSON
files; the RP2040 side shipped one custom definition missing three
menus for its whole life. Both happened **while both repositories
already had documents.** Cross-repo match is still unchecked (§8).

## 8. This repository's remaining document rules

Header pair, five genres, three-line refusal, retirement, and the
minimum check set are specified by tag **v1** of
`eerraa-agent-docs/AGENT_DOCS_CONVENTION.md`. This section records only
what that spec leaves each repository to choose. It does not copy the
spec.

- Six agent documents, **flat** under `docs/`. No genre directories and
  no second index. `AGENTS.md` routes Change / Locate / Verify; §2
  indexes originals.
- Repository paths in backticks are the **full repository-relative
  path** (`src/ap/modules/qmk/port/via_hid.c`). Peer-repo and spec-repo
  files take the repository name as prefix (`the-via-eerraa/docs/MAP.md`,
  `eerraa-agent-docs/AGENT_DOCS_CONVENTION.md`). `path` resolves them. A
  `file:line` citation also checks that the line is inside the file.
- Board, channel, EEPROM, and wire tables sit in
  `<!-- era-doc-refs: … -->` markers.
  `python tools/era_doc_refs.py --tables` recomputes them from source.
  Do not hand-edit — `table` compares them to source.
- This repository has no value-changing ADRs, so it does not use
  `Status:` or `Read when:`. `header` checks their absence.

The checker is `python tools/era_doc_refs.py` and implements nine:
`path` `comment` `header` `index` `symbol` `retired` `table` `menu`
`version`. `citation` is not a separate check name; `path` already
accepts `:line`. `comment` looks the other way — a `docs/` path in a
source comment must exist. Checking documents alone leaves drift when a
document is deleted and a comment still points at it.
`python tools/era_doc_refs_selftest.py` plants a fault per check to
prove the checker is not empty. Per-commit: once per clone,
`git config core.hooksPath hooks` — `hooks/pre-commit` runs the
checker. There is no CI workflow.

**Still unchecked by a machine:** whether a sentence matches its
declared genre, whether `Canonical for` is true (only non-empty is
checked), and match with a peer repository. The two incidents in §7
came from that last hole.
