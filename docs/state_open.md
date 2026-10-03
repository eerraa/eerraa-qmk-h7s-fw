# Open items

Genre: state
Canonical for: unresolved decisions, decided but unimplemented changes,
unmeasured hardware or external checks, and next entry conditions

Only current unknowns live here. Delete an item when it closes. Persistent
product, wire, storage, safety requirements and prohibitions stay in their
owning contracts; verification commands and proof limits stay in
`docs/manual_verify.md`.

## 1. Decisions and decided work not yet implemented

### D-9. Smaller cross-product differences

- Default indicator colour: H7S green on overlay boards and red on dedicated-LED
  boards; EERRAA white. A new default reaches devices only through a reset.
- Dynamic macro inter-key delay: H7S 10 ms (`DYNAMIC_KEYMAP_MACRO_DELAY`),
  EERRAA 0 ms.
- Bootmagic: H7S has none although board `info.json` lists it; EERRAA erases
  storage and enters the bootloader.
- RGBLight Brightness / Color / Velocikey visibility (`showIf`) differs between
  the two products' official JSON; H7S has no Indicator-Only option.
- App side (peer owner, `docs/MAP.md` §7): app definitions do not share the
  official JSON's Permissive Hold visibility rule.

**Start condition**: a product decision per line; app-side lines start in the
app repository.

### D-11. Debounced input loss and earliest-service follow-up

The requested preservation of debounced logical key transitions is not yet
satisfied at every finite buffering boundary. The tapping ring has been enlarged,
and the reproduced LT plus eight events / four short taps cases now preserve
their events and original LT/MT decisions in host tests. Its final overflow
recovery still clears the waiting prefix and key state: a full ring followed by
the tap key's release can cross that boundary. This is a bounded mitigation,
not an end-to-end lossless admission design. The transport's existing finite
overflow can likewise coalesce a complete tap, and the 20-key/Boot report limits
remain those of the USB contract. None establishes the cause of the observed
BRICK60 stalls.

Nonwaiting same-scan logical merging and opt-in keyboard first-packet FIFO fill
are implemented. Isolated production-source tests cover immediate single-key
arming, completion during a scan, immutable active/pending reports, short taps,
Boot exclusion, FIFO-space fallback and lifecycle boundaries. They establish
software behavior, not silicon readiness or actual next-host-IN delivery.

**Decision (2026-10-01)**: retain the existing deterministic regressions and
continue ordinary-use observation. A new on-device replay/capture system and
a broad overflow-admission redesign are deferred until their additional value
is demonstrated. Unbounded input during an unbounded host stall is not a
losslessness requirement. The supported input envelope should preserve input;
overload recovery should remain memory-safe, release transient held state and
allow subsequent input. Full tapping/action/layer recovery after saturation
has not been established by the current fixtures.

**Reopens**: a reproducible missed/stuck input, measured queue pressure within
the supported configuration, or a need to quantify scan-to-host latency. Add
a focused regression or measurement for that evidence first. Any future
admission change must cover the original input, tapping, action side effects,
report caches and transport together; retrying a partly executed action is
unsafe. Preserve original timestamps/TD ownership, LT/MT decisions, Caps
intervals, wake-key and SOCD behavior.

Report-latency instrumentation is retired. Hardware timing/scan-throughput
verification remains in section 2 and requires separate authorization.

### USB polling TEXT: peer application migration

Firmware exposes the read-only setting and retires selector `0x07`; the custom
app still needs diagnostics UI removal and a generation-scoped observation owner
outside CONFIG caches. Start in the peer repository under `docs/MAP.md` §7,
using `docs/contract_via.md` §6. Preserve existing localStorage history; do not
re-expose diagnostics for old firmware. The H7S-only change does not implement
or validate this app migration.

## 2. Hardware or external verification still open

| Item | Next verification / entry condition |
| --- | --- |
| Caps host compatibility | On supported host OSes at FS/HS, verify TD, LT/MT and generic Caps taps, fast repeats, a following letter, host Caps filtering, and indicator feedback. |
| Input/USB architecture | Timing measurements are deferred under D-11. When reopened and separately authorized for hardware, correlate debounce/action completion, report admission, endpoint arm, TX FIFO fill and actual host IN/ACK under FS/HS with RGB, VIA and writes. Measure interrupt-masked duration and missed eligible host service opportunities; SOF timing alone does not prove scan-to-host latency. Existing software regressions cover the modeled batching, lifecycle and finite-overflow policies; they do not establish full tapping/action/layer recovery or actual endpoint quiescence. |
| BRICK60 intermittent HID loss | Reported twice at FS 1 kHz through the same USB hub, on V260929R1 and an earlier version: Pulse RGB reacted but keyboard input and VIA connection failed; reconnecting restored both. At that investigation stage the user returned to the previously reliable cable and observed it on unchanged firmware. The user subsequently reported no perceived problems during ordinary use; exact installed image, observation duration and recovery sequence have not been independently established. PCD clock/completion fixes pass deterministic tests as USB stability improvements; incident causality and resolution remain unconfirmed. Standard host tests/builds do not access hardware, and agent activity has not been established as a cause. Keep the hub as the test environment; hub hardware fault is outside this investigation. On recurrence, correlate firmware/cable, time, polling mode, prior Suspend/Resume or restart, input/VIA/RGB symptoms and USB events before choosing a targeted test. |
| USB controller failure paths | Software regressions cover failed class ownership, error propagation and retained callbacks/payloads across HID, optional CDC and composite lifecycles. The raw USBRST observer retires software admission before ENUMDNE without clearing hardware-owned buffers. Resume or completion cannot revive that session; SETUP arriving before stack Reset is dropped and requires a later host SETUP. The shared HAL Close/Abort path now follows RM0477 Rev 9 §62.15.6 for non-control endpoint teardown; focused MMIO regressions cover NAK, W1C EPDISD, early EPENA clear, explicit retry and queued slave-mode RX. These are software/register models, not measured silicon timing. Real DMA quiescence, FIFO waits with interrupts masked, reset-side autonomous register changes and host/speed combinations beyond the user-verified BRICK60 Windows 11/hub/FS 1 kHz setup remain hardware checks. The unchanged bus-reset register sequence and FIFO polling bounds are separate from this teardown repair. Existing HAL transfer-start/interface return handling does not establish detection of every controller failure. No automatic recovery or universal host-compatibility claim follows from the user's successful candidate test. |
| Async external EEPROM | Verify real I2C IT/ACK-probe sequencing, missing-IRQ timeout quiescence, absent/stuck-bus recovery, initial-read failure, and page durability. Power-loss atomicity across pages is not implemented (`docs/contract_eeprom.md` §3). |
| DMA/MPU/USB silicon guards | Confirm linked-list nodes in non-cache SRAM, unchanged row phase, GFXMMU Device/XN protection, and ES0596 IN-ZLP sequencing including EP0. |
| Legacy bootloader handoff | On the current candidate, repeat cold-boot/VIA-reset regression and UF2 auto-start checks on each supported board that still uses the legacy handoff. The requirement is `docs/contract_usb.md` §6. |
| Bootloader-side root fix | Verify only on later shipments written with ST-LINK; peer owner: `eerraa-qmk-h7s-boot/docs/uf2_auto_start.md`. |
| Official `usevia.app` MOUSE page | Verify all six controls read/write values and that setting Cursor Acceleration to Off changes the row as intended. |
| EEPROM CLEAN 10 s window | In SYSTEM → EEPROM, verify each confirmation bit can be cleared, stale partial confirmation expires after 10 s, and all three confirmations inside one window trigger the reset path. |
| Selected HS mode on an FS link | With the Custom polling observation, verify an FS-only connection reports `1000 Hz (FS)` despite the saved HS choice. This is a setting snapshot, not a warning or a speed measurement. |
| Internal-flash EEPROM emulation | No current board exercises `src/hw/driver/eeprom/emul.c`. Enter when a board/config or dedicated hardware fixture uses that backend; verify write/error cleanup. Any Unlock/Lock minimization must redesign cleanup state and rollback together. |
| Pulse 5 ms minimum | On each real WS2812B-2020 chain, verify speed 0 preserves at least 5 ms of unoccluded output, OFF/ON frames remain distinct, and fast repeated presses never leave the strip inverted. Capture DIN data/RESET and optical PWM separately; source/receiver-model passes do not close this hardware item. |
| Boot protocol report | Capture enumeration on the target BIOS/UEFI setup and KVMs: whether SET_PROTOCOL(Boot) arrives before boot-format reads, and that six held keys, a seventh, modifiers and Caps LED output work there. A host that reads boot packets without SET_PROTOCOL still gets 22 bytes (`docs/contract_usb.md` §3). |
| SOCD modes | With a key tester on each board: every mode while both keys of a pair are held, a Tap Dance hold or tap of one key against the opposing physical key, and a live mode change while held. |
| Wakeup-key rule | On FS and HS hosts that allow remote wake: a letter or Enter that wakes a sleeping host is not typed and the next press is; a key held at power-on or cable insertion (BIOS key) still reaches the host; a key held into Suspend and released there does not stick. |
| Mouse buttons 6–8, Magic, Space Cadet | Buttons 6–8 register on Windows and macOS; Magic GUI lock and Space Cadet keys work from the keymap and from TD actions; overlapping mouse-key directions keep their speed. |
| VIA RGB apply in Pulse effects | On hardware, verify brightness/colour SET is visible without a key press, leaving Pulse for a hue-driven effect does not inherit stale white output, a brightness change during Pulse Off Press (Hold) preserves the hold latch, RGB Sleep entered while a Hold key is down wakes to the base output and the waking key itself does not pulse, and Caps overlay release restores the committed base output. Include first LT/TD Caps activation at speed 0/15/255, near-simultaneous base/host frames, and Sleep of BRICK65/MAY65/SCULPTUREI physical indicators. |
| Maker USB identity | On each board, flash the current build and confirm the host enumerates the VID/PID and `KBD_MANUFACTURER` from its `<board>/config.h`; load its official JSON through `usevia.app` Design; reconnect the custom app after WebHID re-authorization and compare the identity with the app manifest (peer owner, `docs/MAP.md` §7). |
| Stock polling selection | After Apply/reboot, verify a fresh stock read displays the saved Boot Polling Mode. Stock JSON omits polling TEXT under the client support policy; use Custom for endpoint observations. Do not interpret the dropdown as negotiated FS/HS speed. |
| Official VIA reboot response mismatch | The user reported no VIA errors with BRICK60 B (40 ms before detach / 1000 ms detached) and C (500/100), and preferred C for smoother behavior. C is now the accepted default. The earlier CSV showed repeated request/response misalignment; the initial cause remains unproven. Reopen on recurrence and capture the transition, browser/host, disconnect/reconnect events and CSV. This observation does not establish all-host compatibility or Custom observation refresh/failure behavior. |
