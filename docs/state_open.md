# Open items

Genre: state
Canonical for: unresolved decisions, unmeasured hardware or external checks,
and next entry conditions

Only current unknowns live here. Delete an item when it closes. Persistent
product, wire, storage, safety requirements and prohibitions stay in their
owning contracts; verification commands and proof limits stay in
`docs/manual_verify.md`.

## 1. Pending decisions

### D-4. Exposing per-path transport loss on the wire

Whether selector `0x07` should expose per-path transport loss beyond the
existing aggregate drop semantics is unresolved. The current wire meaning
remains owned by `docs/contract_via.md` §6 and `docs/contract_usb.md` §3.

**Start condition**: a coordinated app/firmware envelope revision that defines
active, queued and coalesced event semantics and adds cross-repository wire
fixtures. Until then, do not reinterpret the existing aggregate fields or
consume reserved bytes on only one side.

### D-6. Tap Dance output that kill switch overrides

A kill-switch (SOCD) key that a TD hold also outputs stays reported while the
opposing key is held, because suppression removes only the ordinary bit. The
EERRAA tree behaves the same way.

**Start condition**: a report-level suppression rule for kill switch that every
owner respects, applied in both trees.

## 2. Hardware or external verification still open

| Item | Next verification / entry condition |
| --- | --- |
| Caps host compatibility | On supported host OSes at FS/HS, verify TD, LT/MT and generic Caps taps, fast repeats, a following letter, host Caps filtering, and indicator feedback. |
| Input/USB architecture | Measure real scan-to-host percentiles under FS/HS with RGB, VIA and writes; exercise overflow/release convergence, short taps across wake, configuration churn, endpoint quiescence, and optional CDC composite mode. |
| Async external EEPROM | Verify real I2C IT/ACK-probe sequencing, missing-IRQ timeout quiescence, absent/stuck-bus recovery, initial-read failure, and page durability. Power-loss atomicity across pages is not implemented (`docs/contract_eeprom.md` §3). |
| DMA/MPU/USB silicon guards | Confirm linked-list nodes in non-cache SRAM, unchanged row phase, GFXMMU Device/XN protection, and ES0596 IN-ZLP sequencing including EP0. |
| Legacy bootloader handoff | On the current candidate, repeat cold-boot/VIA-reset regression and UF2 auto-start checks on each supported board that still uses the legacy handoff. The requirement is `docs/contract_usb.md` §6. |
| Bootloader-side root fix | Verify only on later shipments written with ST-LINK; peer owner: `eerraa-qmk-h7s-boot/docs/uf2_auto_start.md`. |
| Official `usevia.app` MOUSE page | Verify all six controls read/write values and that setting Cursor Acceleration to Off changes the row as intended. |
| EEPROM CLEAN 10 s window | In SYSTEM → EEPROM, verify each confirmation bit can be cleared, stale partial confirmation expires after 10 s, and all three confirmations inside one window trigger the reset path. |
| Diagnostic session-loss path | Reproduce Suspend during an active diagnostic session in isolation, reconnect, start a new session, and determine whether the missing observation is firmware session accounting or host collection. Wire/session semantics remain `docs/contract_via.md` §6. |
| Mode↔negotiated-speed mismatch warning | Verify the positive warning path with an FS-only port or hub; the positive path remains unmeasured. |
| Internal-flash EEPROM emulation | No current board exercises `src/hw/driver/eeprom/emul.c`. Enter when a board/config or dedicated hardware fixture uses that backend; verify write/error cleanup. Any Unlock/Lock minimization must redesign cleanup state and rollback together. |
| Pulse 5 ms minimum | On each real WS2812B-2020 chain, verify speed 0 preserves at least 5 ms of unoccluded output, OFF/ON frames remain distinct, and fast repeated presses never leave the strip inverted. Capture DIN data/RESET and optical PWM separately; source/receiver-model passes do not close this hardware item. |
| VIA RGB apply in Pulse effects | On hardware, verify brightness/colour SET is visible without a key press, leaving Pulse for a hue-driven effect does not inherit stale white output, a brightness change during Pulse Off Press (Hold) preserves the hold latch, and Caps overlay release restores the committed base output. Include first LT/TD Caps activation at speed 0/15/255, near-simultaneous base/host frames, and Sleep of BRICK65/MAY65/SCULPTUREI physical indicators. |
