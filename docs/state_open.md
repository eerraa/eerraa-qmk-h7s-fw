# Open items

Genre: state
Canonical for: what is still undecided and what the start condition is,
hardware-only unverified items, and what must not be restored

**This is the only document in this repository that goes away with
time.** Close an item by deleting it in place. Rules that still apply
move to the contract that owns them. When every item is closed, delete
this file — do not leave closed items marked done. Those lines still
hit the index and still read as current. The file is not archived.

Identifiers (D-2 and the rest) stay. The app repository and hardware
replies use the same numbers.

## 1. Waiting on a decision — data decides

### D-4. Exposing per-path transport loss on the wire

The existing `report_drops` field remains the aggregate of keyboard/EXK FIFO
saturation. `usbHidGetTransportStats()` has separate local counters for each path,
arm failures and discarded session backlog. Suspend preserves same-session
transitions, and reset discards old-generation work explicitly. These local
counters are not added to reserved selector 0x07 bytes.

**Start condition**: a coordinated app/firmware envelope revision and a defined
meaning for active, queued and coalesced events. Do not silently reinterpret the
existing aggregate or consume reserved bytes on only one side.

### D-5. Tap Dance synthetic-key ownership

The nonblocking report-interval policy is `docs/contract_usb.md` §3. It delays
USB snapshots, not synthetic key-up callbacks. QMK's shared HID usage state still does not distinguish
a TD-generated usage from a separately held physical key with that same usage.
This pre-existing overlap issue is not solved by changing a delay constant.

**Start condition**: define synthetic-versus-physical usage ownership and add
host-visible overlap fixtures for "synthetic down + physical same-usage down/up".
An implementation that later schedules logical key-up must define that ownership,
including reconfiguration/cancellation and same-usage overlap. The report-interval
implementation does not change the existing simultaneous logical-usage behavior.

## 2. Hardware-unverified

| Item | What to look at |
| --- | --- |
| Caps host compatibility | Confirm TD, LT/MT and generic Caps taps on the supported host OSes at FS/HS, including fast repeated taps and a following letter. Host Caps filtering and indicator feedback require real host validation. |
| Input/USB architecture | Verify real scan-to-host percentiles under FS/HS, RGB, VIA and writes; overflow/release convergence; short taps across wake; configuration churn, endpoint quiescence and optional CDC composite mode. |
| Async external EEPROM | Verify real I2C IT/ACK-probe sequencing, missing-IRQ timeout quiescence, absent/stuck bus recovery, initial-read failure and page durability. Multi-page power-loss atomicity is not implemented. |
| DMA/MPU/USB silicon guards | Confirm linked-list nodes in non-cache SRAM, unchanged row phase, GFXMMU Device/XN protection and ES0596 IN-ZLP sequencing including EP0. |
| Old-bootloader UF2 auto-start | The inherited-block reset in `HAL_PCD_MspInit()` (`docs/contract_usb.md` §6) is confirmed on BRICK60 with the old bootloader. Owed on the same build: a few cold boots and VIA resets for regression, and the same upload check on the other four boards. |
| Bootloader-side root fix | Confirmable only on later shipments written with ST-LINK: `eerraa-qmk-h7s-boot/docs/uf2_auto_start.md`. |
| Official `usevia.app` MOUSE page | Whether the six controls read and write values, and whether setting `Cursor Acceleration` to Off actually swaps the row. |
| RGB SLEEP / PC wake | Idle RGB Sleep → RGB off while VIA RGB Effect remains the selected effect; key wake restores physical output without rewriting the logical RGB setting. Hardware established that a premature device-driven WKUINT must be isolated, that masking only WUIM while holding RWUSIG for 10 ms wakes BRICK60's PC from S3, and that fresh SOF + `SUSPSTS=0` completes a missing logical USBD Resume and restores VIA/raw-HID. PCD physical bus Suspend and ST USBD logical Resume have separate owners: Reset/active SOF/genuine Resume clear physical bus Suspend independently, while stale-SOF and exactly-once logical Resume remain Remote-Wake state. BRICK60 hardware acceptance reports the sleep→physical-key wake routine normal. Master OFF preserves the timeout and prevents RGB sleep for idle, OS USB suspend, and powered hub + PC off (~300 ms SOF stale); master ON restores all three reasons; charger-only never enumerated stays lit; usevia.app SYSTEM → SLEEP GET/SET/SAVE and reboot; EEPROM CLEAN → ON / 10 min. |
| EEPROM CLEAN 10 s window | usevia.app SYSTEM → EEPROM: three toggles GET their bits, SET 0 clears, leftover confirms fall off after 10 s, all three inside the window wipe. |
| Diagnostic session-loss path | A completed session stays in RAM until CLEAR or the next START. There have been runs interrupted by suspend during the session that did not appear in the dump. Reproduce the suspend scenario alone, then read counters on a **new session** (`docs/manual_verify.md` §5). |
| Mode↔negotiated-speed mismatch warning | The negative path (correctly not shown) is confirmed on all four modes. **The positive path is unverified.** Needs an FS-only port/hub. |
| Internal-flash EEPROM emulation | Code-level refactoring is done; hardware verification is not. Current boards use external I2C only. Minimizing Unlock/Lock around multi-byte writes needs a redesign of the clean-up state machine and error rollback together, so it is not started. |
| Pulse 5 ms minimum | Effect Speed 0 now gives a 5 ms pulse (255 gives 260 ms) and expiry is judged on the 1 ms RGB task gate. On a real WS2812B-2020 chain confirm the OFF/ON pulse is visible at speed 0, that the two frames never merge (frame time is 0.7~1.1 ms per board), and that fast repeated presses never leave the strip in the inverted state. |
| VIA RGB apply in Pulse effects | On BRICK60 with a Pulse effect selected: click (not drag) the brightness slider between two distant values and change the colour; each SET must show the value just sent, with no key press. From white, change the effect Pulse → Rainbow Mood 1: the rainbow must start without touching the colour. A brightness change while a key is held in Pulse Off Press (Hold) must keep the strip off until release, then light at the new value. Caps ON/OFF during a Pulse effect must still restore the base output in the same frame as the overlay release. |

## 3. Must not restore

**The 537-line persistence_burst_design.md that used to sit under
`docs/` is retired and has no copy.** It covered VIA consecutive
settings and EEPROM burst-safe design. That commit was deleted on both
local and remote.

**Why:** this issue is designed from scratch. Leaving the old document
makes a retired design look current. **A session looking for this
document does not restore it — start from a blank page.** Do not treat
reflog or cherry-pick restore as a "structure" action.

Why the instability monitor and automatic polling downgrade must not
be restored is a different kind of fact; `docs/contract_usb.md` §4
holds it as contract.

## 4. Hand off to the peer repository

These two lines in `the-via-eerraa` still point at this repository's
gone worktrees. A session that opens that repository fixes them —
**do not move cwd and do not fix them here** (`docs/MAP.md` §7).

- `the-via-eerraa/docs/MAP.md` §8 lists `eerraa-qmk-h7s-fw-via` and
  `-via2` as "H7S working worktrees". Both are retired; there is one
  worktree.
- `the-via-eerraa/docs/adr/0003-era-menu-help-ui.md` cites an
  `eerraa-qmk-h7s-fw-via2/src/...` path as evidence. The same file is
  at `eerraa-qmk-h7s-fw/src/...`.
