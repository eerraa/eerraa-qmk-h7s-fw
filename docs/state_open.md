# Open items

Genre: state
Canonical for: unresolved decisions, decided but unimplemented changes,
unmeasured hardware or external checks, and next entry conditions

Only current unknowns live here. Delete an item when it closes. Persistent
product, wire, storage, safety requirements and prohibitions stay in their
owning contracts; verification commands and proof limits stay in
`docs/manual_verify.md`.

## 1. Decisions and decided work not yet implemented

### D-4. Exposing per-path transport loss on the wire

Whether selector `0x07` should expose per-path transport loss beyond the
existing aggregate drop semantics is unresolved. The current wire meaning
remains owned by `docs/contract_via.md` §6 and `docs/contract_usb.md` §3.

**Start condition**: a coordinated app/firmware envelope revision that defines
active, queued and coalesced event semantics and adds cross-repository wire
fixtures. Until then, do not reinterpret the existing aggregate fields or
consume reserved bytes on only one side.

### D-9. Smaller cross-product differences

- Default indicator colour: H7S green on overlay boards and red on dedicated-LED
  boards; EERRAA white. A new default reaches devices only through a reset.
- Dynamic macro inter-key delay: H7S 10 ms (`DYNAMIC_KEYMAP_MACRO_DELAY`),
  EERRAA 0 ms.
- Bootmagic: H7S has none although board `info.json` lists it; EERRAA erases
  storage and enters the bootloader.
- RGBLight Brightness / Color / Velocikey visibility (`showIf`) differs between
  the two products' official JSON; H7S has no Indicator-Only option.
- App side (peer owner, `docs/MAP.md` §7): the usekb.cc diagnostics panel shows
  the firmware version with a `V` prefix, and the app definitions do not share
  the official JSON's Permissive Hold visibility rule.

**Start condition**: a product decision per line; app-side lines start in the
app repository.

### D-11. Debounced input loss and earliest-service follow-up

The requested preservation of debounced logical key transitions is not yet
satisfied at every buffering boundary. QMK tapping-buffer overflow discards
waiting events before USB admission; the existing finite transport-overflow
policy can also coalesce a complete tap. These are source/fixture-confirmed
limits, not evidence that either caused the reported BRICK60 stalls. Normal
plain-key submission and completion-driven rearming pass isolated tests, but
same-scan keys still generate successive snapshots, and immediate endpoint
arming does not establish when the non-DMA TX FIFO becomes ready.

**Next work**: define and implement tapping-overflow handling and bounded USB
admission that preserve required transitions without silently changing LT/MT,
Caps timing, wake-key, SOCD or report-format behavior. Compare safe same-scan
report batching before USB admission; preserve synthetic taps and already
accepted report order. A larger queue alone does not prove lossless operation
under unbounded host stalls. Current report-latency diagnostics start at HID
submission, so they do not cover debounce-to-report action delays or upstream
drops. Keep wire meanings unchanged; establish the full timing/observation
boundary before claiming earliest-host-service completion. Hardware timing
verification remains in section 2 and requires separate authorization.

## 2. Hardware or external verification still open

| Item | Next verification / entry condition |
| --- | --- |
| Caps host compatibility | On supported host OSes at FS/HS, verify TD, LT/MT and generic Caps taps, fast repeats, a following letter, host Caps filtering, and indicator feedback. |
| Input/USB architecture | After separate hardware authorization, correlate debounce/action completion, report admission, endpoint arm, TX FIFO fill and actual host IN/ACK under FS/HS with RGB, VIA and writes. Measure interrupt-masked duration and missed eligible host service opportunities; SOF timing or the current report-to-completion diagnostic alone does not prove scan-to-host latency. Include the loss/batching work in D-11, overflow/release convergence, short taps across wake, configuration churn, endpoint quiescence and optional CDC composite mode. |
| BRICK60 intermittent HID loss | Reported twice at FS 1 kHz through the same USB hub, on V260929R1 and an earlier version: Pulse RGB reacted but keyboard input and VIA connection failed; reconnecting restored both. A recently introduced cable has now been replaced with the previously reliable cable; observe that change on unchanged firmware first. PCD clock/completion fixes pass deterministic tests as USB stability improvements; incident causality remains unconfirmed. Agents were probably active; exact times are unavailable. Standard host tests/builds do not access hardware, while ad hoc agent/device interactions still need correlation. Keep the hub as the test environment; hub hardware fault is outside the current investigation per the user's comparison with months of unaffected QMK use. Later verify host Suspend/Resume, typing/VIA traffic and extended use on the candidate, recording the cable, firmware, agent commands and USB events on recurrence. |
| BRICK60 host-restart enumeration failure | The user reports no physical/environment change during the Windows-update restart; BRICK60 remains connected and failed, with no RGB/input. Reconnect has deliberately not been attempted, so recovery is expected but unverified; continuous USB power is unmeasured. Read-only Windows PnP inspection on 2026-10-01 03:53 KST confirms the present BRICK60 VID/PID `4501:000C` and serial: USB Composite Device, service `usbccgp`, Code 10 (CM_PROB_FAILED_START), problem status `0xC0000001`, no present HID children, last arrival 03:39:20 after OS start 03:39:06. This establishes parent-device start failure, not the failing USB request or a firmware root cause. EP0 request loss and SET_CONFIGURATION failure (for example, endpoint open/rearm failure) remain distinct unmeasured paths. An earlier 03:19:46 Code 43 / `USB\DEVICE_DESCRIPTOR_FAILURE` record has a different failed devnode and is not the current error; do not merge those events or infer an agent-command trigger. RGB Sleep can blank output on Suspend/SOF loss without an MCU hang. The V261001R2 candidate restores the prior Suspend STOPCLK gate and retains Resume/completion fixes plus ungating on an observed bus Reset. The preventive always-running PHY change is excluded because its actual reset benefit and suspend current/thermal effects are unmeasured. Whether a gated controller detects the relevant physical reset remains unverified; software ungating after observation cannot prove it. After separate authorization, verify host restart/Suspend/reset/re-enumeration and correlate power continuity, cable, USB requests and errors. Preserve the current failed state; hardware causality and reconnect recovery remain open, and hub hardware fault remains excluded. |
| USB controller failure paths | The wider BRICK60 audit covers HID/EXK/VIA ownership, backpressure, EP0, Suspend/Resume, reset/configuration, main-loop work and optional CDC class lifetime; it is not hardware acceptance. Failed close ownership, configuration error propagation and retained payloads now have deterministic regressions, including CDC/interface queues and composite core rollback. Arm errors are injected at the USBD LL boundary: current HAL start-result handling and ignored CDC interface returns do not establish detection of every controller failure or lossless recovery. Actual endpoint quiescence, late RX FIFO writes after a successful close/reopen, and FIFO wait duration with interrupts masked still need H7RS-specific register/timing evidence. The R2 software barrier closes the modeled old-session admission gap after observed raw USBRST and before ENUMDNE, including hardware-active Resume and deferred early-WKUINT paths. It retires software admission without wiping retained buffers; production HAL reset/abort ordering and FIFO polling bounds stay unchanged. HAL FIFO refill, receive-buffer writes before bridge callbacks, physical reset side effects and the legality of the injected event combinations on silicon remain outside that barrier. SETUP arriving before the stack Reset is dropped, even if ENUMDNE is pending in the same IRQ; the host must issue a later SETUP. Controller/host traces are needed to assess that boundary on hardware. Obtain the current RM0477 endpoint-disable/reset programming details before changing reset/abort ordering, and measure the unchanged finite FIFO polling on target only after separate authorization. Keep the current failed device untouched; no automatic recovery or field-incident causality follows from these observations. |
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
| Boot protocol report | Capture enumeration on the target BIOS/UEFI setup and KVMs: whether SET_PROTOCOL(Boot) arrives before boot-format reads, and that six held keys, a seventh, modifiers and Caps LED output work there. A host that reads boot packets without SET_PROTOCOL still gets 22 bytes (`docs/contract_usb.md` §3). |
| SOCD modes | With a key tester on each board: every mode while both keys of a pair are held, a Tap Dance hold or tap of one key against the opposing physical key, and a live mode change while held. |
| Wakeup-key rule | On FS and HS hosts that allow remote wake: a letter or Enter that wakes a sleeping host is not typed and the next press is; a key held at power-on or cable insertion (BIOS key) still reaches the host; a key held into Suspend and released there does not stick. |
| Mouse buttons 6–8, Magic, Space Cadet | Buttons 6–8 register on Windows and macOS; Magic GUI lock and Space Cadet keys work from the keymap and from TD actions; overlapping mouse-key directions keep their speed. |
| VIA RGB apply in Pulse effects | On hardware, verify brightness/colour SET is visible without a key press, leaving Pulse for a hue-driven effect does not inherit stale white output, a brightness change during Pulse Off Press (Hold) preserves the hold latch, RGB Sleep entered while a Hold key is down wakes to the base output and the waking key itself does not pulse, and Caps overlay release restores the committed base output. Include first LT/TD Caps activation at speed 0/15/255, near-simultaneous base/host frames, and Sleep of BRICK65/MAY65/SCULPTUREI physical indicators. |
| Maker USB identity | On each board, flash the current build and confirm the host enumerates the VID/PID and `KBD_MANUFACTURER` from its `<board>/config.h`; load its official JSON through `usevia.app` Design; reconnect the custom app after WebHID re-authorization and compare the identity with the app manifest (peer owner, `docs/MAP.md` §7). |
