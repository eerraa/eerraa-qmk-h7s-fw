# H7S firmware — data map

Genre: map
Canonical for: repository-local ownership anchors, active-document reachability,
and peer-interface pointers

This map points to owners; it does not copy source-owned inventories, offsets,
channel tables, or current Git state. Product requirements and compatibility
constraints live in `docs/contract_via.md`, `docs/contract_usb.md`, and
`docs/contract_eeprom.md`. Open items live in `docs/state_open.md`.

## 1. Which side wins

| Concern | Requirement / decision owner | First implementation owner | Verify |
| --- | --- | --- | --- |
| Firmware version and reset coupling | `docs/contract_eeprom.md` §2 | `_DEF_FIRMWARE_VERSION` in `src/hw/hw_def.h` | document/version checks plus affected firmware tests |
| VIA wire/value compatibility | `docs/contract_via.md` | `src/ap/modules/qmk/quantum/via.h`, `<board>/port/via_port.c`, official JSON | local document/menu checks and VIA host tests |
| EEPROM USER compatibility | `docs/contract_eeprom.md` | `src/ap/modules/qmk/port/port.h`, board `config.h` | affected storage/firmware tests |
| USB host/boot behavior | `docs/contract_usb.md` | `src/hw/driver/usb/`, `src/ap/modules/qmk/port/` | affected host/build checks |
| User-facing distribution copy | `docs/readme.txt`, `docs/usevia.txt`, `docs/via_keycodes.txt` | those files | document/version checks and distribution review |
| Code location and call graph | source | source search (`git grep -n`, `rg`) | compiler/tests when changed |

A copied implementation fact yields to current source. A requirement, wire/storage
compatibility rule, or safety boundary does not: source/contract disagreement is a
conflict to resolve, not permission to weaken the contract.

## 2. Document index

| Document | Genre | Owns |
| --- | --- | --- |
| [contract_via.md](contract_via.md) | contract | VIA/app wire contract. Channel addresses, exact-ms/exact-sec, `0x06`/`0x07` envelopes, single TX producer, MOUSE unit conversion |
| [contract_usb.md](contract_usb.md) | contract | USB host contract. Interface/report layout, boot-protocol deviation, polling-mode ownership, retired automatic recovery, report ownership and lifecycle |
| [contract_eeprom.md](contract_eeprom.md) | contract | Persistent-state contract. USER slot ownership, version cookie and factory reset, asynchronous persistence and durability |
| [manual_verify.md](manual_verify.md) | manual | Checks that run without a board and their commands, what only hardware can decide, symptom order |
| [state_open.md](state_open.md) | state | Undecided items and start conditions. The only document that goes away with time |
| [readme.txt](readme.txt) | (user document) | Short ZIP-root flash and configuration guide. Exception to the agent-doc spec — §8 |
| [usevia.txt](usevia.txt) | (user document) | Detailed official-VIA guide placed under the distribution's usevia.app folder. Exception to the agent-doc spec — §8 |
| [via_keycodes.txt](via_keycodes.txt) | (user document) | Board-neutral TAPDANCE keycode syntax and examples placed with the official-VIA guide. Exception to the agent-doc spec — §8 |

`AGENTS.md` owns task routing and `CLAUDE.md` only adapts that entry. This
section keeps active documents reachable; it is not a mandatory pre-read list.

## 3. Boards

Current board inventory, product ids, and official VIA JSON names are source-owned
under `src/ap/modules/qmk/keyboards/era/`. Locate them from the tree/JSON when a
change needs them; this map does not keep a regenerated inventory.

## 4. VIA channels

Current channel numbers are owned by `src/ap/modules/qmk/quantum/via.h`. Board
routing is in `<board>/port/via_port.c`; official JSON owns UI exposure.
`docs/contract_via.md` §2 owns shipped/reserved-number semantics, and the local
`menu` check verifies that firmware-routed channels remain reachable in official
JSON. Contract-owned wire value ids remain in `docs/contract_via.md`.

## 5. EEPROM USER slots

Current offsets/sizes are owned by `src/ap/modules/qmk/port/port.h`; board USER
block sizing is in each board `config.h`. `docs/contract_eeprom.md` owns shipped
slot immutability and version-cookie reset policy. This map does not copy the
current layout.

## 6. Structure questions are answered by source

Use source search (`git grep -n`, `rg`) for implementation structure and current
values. Use the owning contract for why a shape is required.

| Question | Owner / entry |
| --- | --- |
| Where code lives and what it calls | source search |
| Current offsets, channel numbers, constants | current source/config in §3–§5 |
| Required wire/storage/USB behavior | `docs/contract_via.md`, `docs/contract_eeprom.md`, `docs/contract_usb.md` |
| Retired USB subsystem and why | `docs/contract_usb.md` §4 |
| Cross-repository interface | §7 |
| Validation command and proof limit | `docs/manual_verify.md` |
| Unresolved item | `docs/state_open.md`, only when the task touches that item |

## 7. Peer repositories

Peer paths are pointers, not authority to modify another repository. Read that
repository's own `AGENTS.md` before any peer-side work.

| Interface / fact owner | Peer consumer | Compatibility check |
| --- | --- | --- |
| `docs/contract_via.md` and `src/ap/modules/qmk/quantum/via.h` | `the-via-eerraa/docs/adr/0001-state-sync-protocol.md`, `the-via-eerraa/docs/adr/0002-h7s-usb-diagnostics.md`, app definitions | compare the exact revisions when changing the interface; local host tests prove only this side |
| User-facing menu semantics in `docs/contract_via.md` | `the-via-eerraa/docs/adr/0003-era-menu-help-ui.md`, app definitions | compare labels/value ids and official JSON for the changed feature |
| Boot handoff in `docs/contract_usb.md` §6 | `eerraa-qmk-h7s-boot` | compare the handoff contract; hardware validation remains separate |
| `docs/via_keycodes.txt` distribution copy | corresponding EERRAA QMK distribution copy | keep content identical before distribution |

## 8. Local document validation

The convention pin is owned by `AGENTS.md`; this section does not restate the
central convention. Commands, toolchain premises, and what each change owes are
owned by `docs/manual_verify.md`.

`python -X utf8 tools/era_doc_refs.py` validates local document reachability and
paths, source-comment document pointers, retired-USB non-restoration, official
VIA JSON reachability, version consistency, and the contract-owned wire-value
marker in `docs/contract_via.md`. It does not prove sentence meaning or peer-repo
compatibility.

`python -X utf8 tools/era_doc_refs_selftest.py` first requires a clean positive
checker result, then plants negative fixtures for the active checks and restores
the tree.
