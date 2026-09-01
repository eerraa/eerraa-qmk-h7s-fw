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

### D-2. VIA response-throttle reference point

`usbHidEnqueueViaResponse()` refreshes the delay timer on **every
enqueue**. SOF drains only after 20 ms from the last enqueue. The
reference is last enqueue, not last transmit, so a request stream
inside 20 ms keeps postponing drain. That is not a problem today
because the host is serial.

The practical effect is a 20 ms floor per VIA round-trip. One
12-chunk snapshot takes about 240 ms, so a 1 Hz diagnostic session
puts VIA traffic at about 25 % of wall-clock.

**Start condition**: first confirm whether that 20 ms was to dodge a
past host-side race. If there is no evidence, move the reference to
**transmit time** (a real rate limit) or lower the value. If there is
evidence, record the reason in `docs/contract_via.md` and close this
item.

### D-3. Loss-path count scope

There are three report-loss paths and only one is counted — retry-queue
saturation (counted), discard while suspended (silent), discard while
unconfigured during TIM2 drain (silent). The current UI name is
"Report queue drops", which is scoped exactly to the first, so **it is
not false.**

**Start condition**: if hardware shows the other two, widen the count
and rename with it. If they are not observed, keep the current scope
and close this item. Suspend and unconfigured already surface as hard
events, so a user can care.

### D-4. keyboard / EXK drop split

`report_drops` sums both queues. Structurally they are separate queues
with separate outcomes, so a split is right, but splitting needs a
field in the snapshot payload and the app checks that reserved region
as 0, so it is a **simultaneous change on both sides.**

**Start condition**: only when hardware actually produces EXK drops.
A mouse-key-heavy session has already returned drop 0, so do not add a
metric without evidence.

## 2. Hardware-unverified

| Item | What to look at |
| --- | --- |
| `V260824R2` bootloader-handoff complement | On a board still on the old bootloader, UF2 upload then auto-start succeeds 10 times in a row. Cold-boot enumeration delay is not perceptible. Re-enumeration after VIA reset and mode change. Via a hub and on a different PC. |
| Bootloader-side root fix | Confirmable only on later shipments written with ST-LINK. `docs/contract_usb.md` §6. |
| Official `usevia.app` MOUSE page | Whether the six controls read and write values, and whether setting `Cursor Acceleration` to Off actually swaps the row. |
| RGB SLEEP | Real 10-minute idle → RGB off; key wake; master OFF preserves the timeout and prevents RGB sleep for idle, OS USB suspend, and powered hub + PC off (~300 ms SOF stale); master ON restores all three reasons; charger-only never enumerated stays lit; usevia.app SYSTEM → SLEEP GET/SET/SAVE and reboot; EEPROM CLEAN → ON / 10 min. |
| EEPROM CLEAN 10 s window | usevia.app SYSTEM → EEPROM: three toggles GET their bits, SET 0 clears, leftover confirms fall off after 10 s, all three inside the window wipe. |
| Diagnostic session-loss path | A completed session stays in RAM until CLEAR or the next START. There have been runs interrupted by suspend during the session that did not appear in the dump. Reproduce the suspend scenario alone, then read counters on a **new session** (`docs/manual_verify.md` §5). |
| Mode↔negotiated-speed mismatch warning | The negative path (correctly not shown) is confirmed on all four modes. **The positive path is unverified.** Needs an FS-only port/hub. |
| Internal-flash EEPROM emulation | Code-level refactoring is done; hardware verification is not. Current boards use external I2C only. Minimizing Unlock/Lock around multi-byte writes needs a redesign of the clean-up state machine and error rollback together, so it is not started. |

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
