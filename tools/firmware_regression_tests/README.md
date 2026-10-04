# Firmware architecture regression tests

Run from the repository root:

```text
python tools/firmware_regression_tests/run.py
python tools/era_via_host_tests/run.py
python tools/era_doc_refs.py
```

The runner uses HOST_CC when set, otherwise the installed Windows host GCC or
`gcc` from PATH. No ARM emulator, board, USB device or network is required.
Executables and source-isolation copies are written below the ignored repository
`build-firmware-regression-tests` directory. Individual groups can be selected
with `--only queue`, `debounce`, `eeprom`, `usb`, `qbuffer`, `input`, `rgb` or `guards`.
All test binaries compile with warnings as errors. Stubs are test-only and are
not included in firmware builds.

The RGB/input fixture also includes `test_td_direct.h` and `test_td_modes.h`: immediate first TD edges,
second-press tap/hold, interruption, settings snapshots, shared key ownership,
cancellation, all eight slots and uint16-term/32-bit-wrap boundaries. Host VIA
fixtures cover all eight gesture combinations, explicit silence/inheritance,
the mode byte/support marker, unchanged legacy storage and
SAVE/reload. These prove logical transitions, not game/USB latency.

## What is executed

- The production HID FIFO runs 100000 deterministic operations against an independent
  order model. Failed arms retain the head, and delayed pointer reads verify the
  active payload remains unchanged.
- The production HID class and fixed class pool run
  against an endpoint mock that retains pointers, provides RX NAK/backpressure,
  injects arm failures and executes the actual class callbacks. Tests cover overflow,
  response credit and generations, EP0 input sizes, 2048 configurations, partial
  open failure, short taps during wake, first relative mouse input and FS/HS descriptors.
  The interface GET_STATUS case reads the retained response after Setup has
  returned and unrelated code has reused the stack, checking response lifetime.
  Boot protocol cases check the 8-byte form, the first six non-empty slots, the
  resync and unchanged armed packet at a protocol change, and interface/request checks.
  HID-descriptor reads for keyboard, VIA and EXK match their FS/HS configuration
  and actual report lengths, retain aligned response storage, clamp short reads,
  and reject all other interface indices.
  Remote-Wake cases inject the H7RS early-WKUINT behavior, enforce the 10 ms RWUSIG
  window, reject stale/SUSPSTS SOF, de-duplicate late WKUINT and consume VIA after
  fresh-SOF logical Resume.
- `test_usb_polling.h` checks the active keyboard IN setting across pending/saved
  mode changes, Other-Speed reads, FS/HS, Suspend, failed initialization and raw
  Reset. Failed-teardown tests also reject retained endpoint metadata.
  `polling_cases.py` compiles production BootMode, five board routers and the
  keyboard-value switches for TEXT boundaries and reserved-selector echo.
- `test_keyboard_merge.h`, included in the actual HID transport fixture, covers
  immediate single-key submission, completion before scan end, twenty same-scan
  plain presses with an immutable first packet and one merged suffix, direction
  and scan/action barriers, malformed/filtered deltas, Boot's seven-key release
  projection, protocol changes,
  arm failures, Caps intervals, Suspend and raw-reset retirement. A deterministic
  3200-transition run compares externally delivered per-usage edges with input
  edges while completions and ordinary reports interleave with scans.
  These are logical/LL-admission tests; they do not measure host polling times.
- `usb_fifo_ready_cases.py` joins the actual opt-in HAL helper, bridge selection,
  LL start/write bodies, FIFO refill helper, IRQ handler and immutable TX queue.
  Both callback configurations cover FS/HS and nested PRIMASK, 8/22-byte first
  fill, insufficient-space TXFE fallback, completion chains, stale TXFE,
  pending ownership and excluded EP0/ZLP/DMA/CDC/EXK/VIA paths. Host adapters
  model register reads/W1C and FIFO stores; USB tokens, PHY, real bus reset
  quiescence, interrupt-masked duration and target latency are unmeasured.
- `usb_pcd_cases.py` compiles unmodified production PCD callbacks and USBD
  lifecycle/completion functions together with the real HID transport, standard
  request dispatcher and EP0 IO code. Cases check host Resume undoing STOPCLK,
  early WKUINT followed by hardware-active SOF, and independent IN, OUT and SETUP
  callbacks arriving before WKUINT. They verify keyboard release delivery and
  VIA request/reply progress afterward. Suspend sets STOPCLK with low power
  disabled or enabled; Resume and observed Reset release that gate. This only
  checks register writes, not reset detection while the physical PHY is gated.
  Sixty-four FS Suspend/reset cycles execute actual core Reset, descriptor
  requests, SET_ADDRESS and SET_CONFIGURATION, then restart keyboard and VIA
  without injecting WKUINT. Device-descriptor bytes are fixture data; the HID
  configuration descriptor is production data, checked as a 9-byte header and
  a complete 91-byte payload split into 64+27-byte EP0 packets.
  Thirty-six configured/Suspended reset snapshots combine raw-pending,
  consumed-bit gap and ENUMDNE-pending states with IN/OUT/SETUP/SOF/WKUINT/Suspend.
  They check old admission rejection, unchanged retained payload/descriptors,
  failed teardown and later descriptor/address/configuration/input/VIA progress.
  Completion during a real HID Remote-Wake call also preserves the 10 ms signal,
  WUIM restoration and non-duplicated queue progress.
  Standard endpoint requests test all 65536 wIndex values for GET_STATUS and
  endpoint halt/clear, rejecting invalid addresses before non-control endpoint
  access while preserving valid endpoints and class/vendor dispatch. Lifecycle
  callbacks fail the fixture if they enter the shared application logger.
  Pending host wake rejects an already-pending SOF and expires at Suspend, Reset
  or Disconnect; unsolicited SOF cannot create a logical Resume. Disconnect
  downstream stack actions remain stubbed. Controller registers, EP0 packet
  limits/buffer advancement, endpoint operations and IRQ ordering are adapters;
  HAL USBRST/ENUMDNE handling, real endpoint quiescence, MCU STOP/wake and silicon
  timing are not executed. These passes do not establish the cause of a field
  incident or that a physical device can enumerate on Windows.
- `usb_irq_cases.py --bridge`, also in the normal USB group, joins the production
  IRQ wrapper, HAL handler, PCD bridge and pending/connection queries. Both HAL
  callback-registration modes and FS/HS run against explicit register adapters.
  The boundary starts when software observes raw Reset and persists after the
  flag clears until enumeration completion runs stack Reset. The production HID
  Remote-Wake function body also runs across a normal completion and a reset/new
  epoch during its 10 ms signal. USBD/HID forwarding and admission are recording
  adapters here; the separate PCD fixture above runs the actual core/HID teardown
  and queues. IRQ-time injection distinguishes the wrapper observation
  from the HAL raw-reset notification. A SETUP dispatched before stack Reset is
  discarded and is not replayed; later host SETUP is required. The barrier does
  not stop all HAL FIFO refill/RX buffer writes or prove endpoint quiescence.
- `usb_flush_cases.py` compiles the unmodified production endpoint FIFO flush
  wrapper against low-level status adapters. It checks IN/OUT result propagation,
  lock release after success/failure and the already-locked busy path. It does
  not execute FIFO registers, polling duration or class-teardown recovery.
- `teardown_cases.py` connects the production HID class and core teardown to
  endpoints that can retain ownership after a failed close. It verifies immutable
  keyboard/EXK/VIA IN and Boot payloads, a late VIA OUT write/completion, retained
  bounded class storage, rejected new configuration and no automatic close retry.
  Failed teardown invalidates the old control/wake epoch without clearing active
  payloads; an interrupted Remote-Wake attempt cannot later write the new epoch's
  signal. Core Stop/DeInit preserve failures while retaining their call order.
  The PCD fixture additionally checks actual SET_CONFIGURATION dispatch: failed
  teardown stalls without a success status packet, failed Init leaves configuration
  zero, and a subsequent explicit modeled Reset can start a clean session.
- `usb_reset_barrier_cases.py` connects the production HID queues, core teardown
  and public user-reset scheduling/service bodies. Failed keyboard, VIA IN and
  VIA OUT closes retain payloads and the bounded class handle, but retired replies
  no longer block an already requested reset. Queued-only replies, late callbacks,
  32-bit grace wrap, live/Suspended same-generation FIFO drain, EEPROM durability
  and the absence of an unrequested reset are checked together. Separate cases
  exercise HID's pending-query guard and session retirement during both the
  pre-signal wait and the 10 ms Remote-Wake window, including WUIM restoration.
  The pending query, EEPROM, endpoint ownership and MCU reset are adapters here;
  this fixture does not execute the raw IRQ observer or reset hardware.
- `usb_cdc_control_cases.py` executes the unedited optional CDC SETUP handler.
  GET_STATUS and GET_INTERFACE responses are consumed after stack reuse, checking
  storage lifetime, alignment and the unchanged response lengths. This does not
  establish CDC data-path or composite-host acceptance.
- `usb_cdc_lifecycle_cases.py` connects the production CDC class, actual CDC
  interface, qbuffer and class pool in standalone and composite builds. FS/HS
  bulk/ZLP completion, busy buffers, Init/arm/Close failures, late callbacks and
  the actual SOF pump check that stopped owners keep their buffers and reject
  reuse. Interface teardown runs only after endpoint closes succeed. Production
  core DeInit/Reset retain interface callbacks while a class owner remains, then
  permit explicit successful cleanup; normal cleanup still clears the alias.
  Close failure, interface failure, LL errors, owner-absent cleanup and interface
  registration checks run in both configurations. Repeated
  configuration checks bounded class storage. Rejected TX arms preserve the
  staged payload ahead of later bytes; SOF retries failed RX arms and bulk ZLPs.
  Reset/reconfigure cases execute without resetting application queues in the
  fixture and reject old RX/TX bytes, staged payloads and line-coding intent.
  A reset during a backpressured main-thread write must stop that write before
  the new session; Suspend preserves DTR, and nested PRIMASK is retained.
  Endpoint operations and composite endpoint lookup are adapters. Arm errors
  are injected at the USBD LL boundary; the current HAL transmit/receive
  wrappers discard `USB_EPStartXfer`'s result, so these tests do not establish
  detection of every controller failure. Arbitrary instruction-level IRQ
  interleavings and the duration of the bounded queue critical section remain
  outside these deterministic fixtures.
- `usb_composite_cases.py` executes the actual core Init/Clear/Stop/DeInit/Reset and
  SET_CONFIGURATION helper with three explicit class adapters. It checks first
  Init failure, rollback of earlier successes only, retained failed-cleanup
  handles, error propagation and remaining teardown calls. A fixture-only limit
  of two configurations covers the 1-to-2 switch: one old teardown, one partial
  cleanup/rollback, no duplicate Clear or success ACK after failure, and config
  zero. Explicit Reset/DeInit recovery also checks that a failed class retains
  its interface pointer without changing sibling-class aliases. The shipped
  configuration limit remains one. Class ownership and EP0
  ACK/STALL are adapters; this does not execute the HID/CDC classes together or
  the complete SETUP dispatcher, nor prove composite enumeration on a host.
- `usb_ll_cases.py` executes production H7RS FIFO flush, endpoint stop and
  deactivate routines against scripted registers. It checks AHB-idle/flush
  timeouts, FIFO selection, NAK and disable completion, and rejection of direct
  deactivation while enabled. The fixture overrides only the FIFO polling bound
  to 64 iterations; production FIFO bounds and bus-reset sequencing are unchanged.
- `usb_close_cases.py` connects the production USBD status adapter, HAL Close/Abort,
  common receive/abort helpers and LL routines. It covers delayed/missing/already-
  effective OUT NAK, IN NAK failure and natural completion, stale EPDISD,
  EPENA clearing before EPDISD, failure followed by explicit cleanup, queued
  SETUP/data with active or already-inactive OUT, and both initial PRIMASK states.
  Direct HAL callers get the same complete teardown as USBD callers; a locked
  handle, stop failure or FIFO timeout cannot deactivate the endpoint or retire
  descriptors. Abort retains descriptors after success, while Close retires IN.
  The MMIO adapter implements endpoint W1C stores and scripted NAK/disable
  progression. It does not emulate electrical timing, real host traffic or DMA.
  DMA cases skip slave FIFO reads; they do not establish real DMA quiescence.
- `usb_stop_cases.py` executes the actual HAL Stop, LL disconnect/global-interrupt
  disable and bridge status mapping with injected FIFO results. HAL returns the
  FIFO status and the bridge preserves its mapping; cleanup and unlock run after
  flush failure, and an already-locked
  handle makes no controller calls. PHY-selection/battery-charging branches are
  covered as register operations, without measuring electrical disconnect or time.
- Source guards keep PCD physical bus-Suspend ownership separate from HID logical
  Resume: Reset and hardware-active SOF/Resume release the bus state independently,
  while logical SOF completion requires a pending host/device wake and fresh SOF.
- The production runtime debounce and all three per-key algorithms cover all nine
  mode transitions, unchanged-config deadlines, pending press/release reconciliation,
  timer wrap, invalid configuration and row-buffer canaries.
- The production QMK EEPROM image, ZD24C128 state machine and I2C IT layer run as a
  chain against mock HAL registers/interrupts and EEPROM storage. A write-cycle ACK,
  not transmit completion, commits storage. Tests cover in-flight updates, repeated
  writes, all 4096 bytes, NACK, failed starts, missing IRQ, timeout quiescence, retry
  ownership, flush failure, wrap, initialization failure, bounds and PRIMASK.
  Runtime calls to synchronous I2C/ready waits or delay fail the no-blocking assertion.
- The production qbuffer tests empty-output canaries, its existing partial-prefix
  contract, 100000 wraps, full queues, peek/commit, index-only use and invalid geometry.
- `source_cases.py` compiles the unedited guard statements before VIA dispatch and
  the unedited USB reset-service function. It tests all 65536 command/size pairs,
  256 frame lengths, NULL/canaries, reset grace/wrap, persistence and response barriers.
  These are source-region tests; they do not execute the entire VIA dispatcher or
  the low-level USB reset hardware implementation.

Debounce runtime and the EEPROM image source are copied without statement edits
solely so host headers can replace target-local matrix/timer/bootloader headers.
The actual algorithm and state-machine implementations are not replaced by models.

### Separate raw-reset IRQ audit

`python -X utf8 tools/firmware_regression_tests/usb_irq_cases.py --observe`
is a conditional HAL-only source-path reproducer, separate from the normal
runner's bridge regression. It executes the production HAL IRQ, endpoint helpers
and selected LL routines for FS/HS and both callback-registration configurations.
The new Reset-begin notification uses its weak no-op default in this mode,
without the production PCD bridge guard. The host copy
widens register base pointers and redirects W1C, summary interrupts and RX-pop
accesses to explicit adapters. Stack callbacks and the all-TX flush are recording
adapters; generation labels advance at the Reset callback boundary rather than
executing the HID class. It observes callbacks before USBRST/ENUMDNE and FIFO
refill between raw reset and that Reset callback; these observations
are not a stability pass or evidence that the injected combinations occurred on
BRICK60. The model does not supply the controller's automatic bus-reset effects.

The explicit `--assert-reset-boundary` mode asserts that the observed paths are
absent and intentionally fails with the current HAL. It is not a required green
regression check. The production software observer/guard is tested by bridge
mode above; controller reset order and FIFO polling bounds remain unchanged.
The endpoint teardown follows RM0477 Rev 9 §62.15.6; hardware acceptance
of the consolidated patch remains tracked in `docs/state_open.md`.

### Physical RGB input coverage

`--only rgb` connects the unedited production matrix dispatch, QMK tapping buffer,
TD callbacks, action execution, pulse state machine and RGB task in one host
fixture. It checks TD/LT Caps taps and holds, delayed replay, filtered releases,
all four pulse modes, any-held-key Hold tracking, host indicator precedence,
output sleep, RGB OFF, Velocikey and 32-bit wrap. It also drives the actual
`rgblight_sethsv_eeprom_helper()` commit: a brightness commit in a Pulse effect
renders the committed value on the next task pass, a commit during a physical
hold keeps the latch, and only a base-mode change drops it. It also retains the distinct
TD interruption/double-action and LT quick-tap behavior. Matrix input, keymap
lookup, host LED feedback, indicator composition and LED hardware are test
adapters; USB transport and real WS2812 frames are not measured by this fixture.
Optional tapping policies beyond the selected fixture configuration remain outside its proof scope.
The production wakeup-key rule (`port/platforms/suspend.c`) runs in the same
dispatch: a key pressed while the host is suspended requests Remote Wake but
neither its press nor its release reaches QMK, and its next press types.
MinGW uses GCC bitfield layout, checked against the production two-byte action.
The fixture also includes the actual QMK wait/host ports and generic tap helpers.
It requires the upstream 80 ms Caps interval request with zero calls to the delay
backend, and checks ordinary explicit waits separately. The actual USB class
fixture checks 80/200 ms intervals measured from completion, including completion
at a fractional-ms boundary; EXK/VIA continue during the interval. It covers fast
Caps/letter sequences, later same-usage snapshots, failed arms, overflow, Suspend,
reset, and clock wrap under FS/HS and boot/report protocol. Existing zero-interval
FIFO tests cover ordinary input. No host Caps-activation filter is emulated.

### Physical input admission and report provenance

The input group executes `tapping_admission_cases.py` against the actual tapping
state machine and native event/record definitions. It reproduces the former
small-ring loss with LT plus eight events and four taps in 48 ms, checks tap
rather than forced-hold resolution, MT/term/wrap and selected per-key policies,
and verifies that stored/replayed events cannot opt into physical-scan merging.
The same group uses `merge_frontend_cases.py` to run actual matrix dispatch,
action/report production, host scope and SOCD filtering. It covers same-scan
provenance, different scans sharing a millisecond timestamp, duplicate-usage
up/down, consumed/no-report and layer barriers, modifiers/locking/SOCD/TD
exclusions, LT replay, and one-shot consumption of a scope before driver
reentry. Its USB calls record admission intent; the actual HID/FIFO behavior is
tested separately in the USB group.

Ring saturation, index wrap, counters and the retained clear-on-overflow recovery
are explicit tests. In particular a release arriving after a full waiting ring
still clears queued events. Increased capacity does not establish losslessness
for all input rates or host stalls, nor prevent downstream transport overflow.

### Dynamic macro bounds

`--only input` also compiles the unmodified production macro reader with
bounded in-memory EEPROM and string-output adapters. It checks a missing
macro after the final terminator, a full-buffer valid macro, empty macros,
tap/down/up/delay commands, truncated commands and the interrupted-write
sentinel. An out-of-range read fails before touching memory. This verifies
termination and read bounds, not real EEPROM or macro timing.

### Full-range tapping time

The physical-input fixture also selects the production dynamic uint16 tapping-term
input and drives LT/MT and all eight TD slots with 1..65535 ms boundary values.
It checks the original scan timestamp in queued records, delayed replay, skipped
16-bit expiry windows and 32-bit wrap. VIA host tests separately execute the
production exact setters, storage validators, SAVE/reload and read-only legacy
projection for every nonzero uint16 value. DT_* adjustment keycodes are not part
of this fixture. Neither fixture is physical hardware.

### RGB frame completion and receiver coverage

`--only rgb` also builds `test_rgb_frames_brick60` and `test_rgb_frames_brick65`.
`rgb_frame_cases.py` extracts unedited production Pulse/config/mailbox/composition
functions and connects the real board USB-LED entrypoints, board pixel adapter,
and the complete `src/hw/driver/ws2812.c`. The BRICK65 case executes its actual
physical indicator callback. Each board runs 168 deterministic host-response
phase combinations (three pulse durations, eight holds, seven response offsets).
Further cases cover a Pulse queued behind a busy frame, release before the first
RGB task, minimum 5 ms wire dwell, all four modes, input retrigger, same-mode HSV
commits, overlay cancellation/restoration, static RGB/RGB OFF without an animation
timer, microsecond/millisecond wrap, mailbox IRQ interleaving and PRIMASK restore.
BRICK65 also covers two-slot union coverage and blanking both physical indicators
under Sleep. Production base pixels must remain independent of the overlay.

`--only guards` builds the full production driver against `ws2812_include` for
19, 27, 30 and 32 LEDs. The HAL adapter retains the DMA pointer and checks every
read against the start snapshot. A two-stage CCR/preload model feeds the actual
GRB-encoded symbols to a digital first-N-pixels receiver with a RESET boundary.
Tests assert the reset suffix, completion generation, latest-frame coalescing,
start failures, DMA errors, missing IRQ polling, asynchronous abort ownership,
idle LOW and generation/clock wrap. A preempted-start case lets the DMA finish
before its start call returns; the AF pin must already be connected, with a zero
compare applied before DMA starts. Only completed data plus RESET may advance a
completion token. Accepted requests and successful wire completion are distinct.

The colour converter, EEPROM, non-Pulse animation timer, physical input and USB
arrival times remain explicit adapters in the frame fixture. The separate
physical-input fixture above continues to exercise actual QMK LT/TD decisions.
Neither fixture measures analogue signalling, actual Windows scheduling, LED
internal PWM or physical light output. No device sends a WS2812 reception ACK.

Run `python -X utf8 tools/firmware_regression_tests/rgb_frame_selftest.py` after
changing the frame generator, assertions or HAL adapter. It first checks the
positive baseline, then injects five negative controls only into ignored build
copies: dropped Pulse presentation fence, mailbox read/clear IRQ race, stale
layer repost, shortened RESET rejected by the production static assertion, and
the same short RESET rejected by the receiver when that static check is omitted.
Production files are never mutated by this selftest.

## What a pass does not establish

Mocks do not reproduce electrical timing, the Cortex-M memory system, GPDMA hardware,
AHB USB FIFO/register races, real remote-wake pulse width, actual host scheduling,
I2C bus recovery, silicon errata or power interruption. Firmware builds and link-map
inspection complement these tests; hardware acceptance is in `docs/manual_verify.md`.
The EEPROM layout is not a power-loss journal. Finite HID queues can coalesce events
under sustained overload; preserving final release state does not preserve every tap.
The tests do not prove a universal latency bound or close the input-feature issues
listed in `docs/state_open.md`.

### Input-feature and long-idle scheduler coverage

`test_input_features.c` compiles the production `kkuk.c` and `kill_switch.c` against a deterministic report/timer stub. It covers KKUK activation/repeat timing, live value normalization, fresh-epoch reconfiguration, count saturation and a count that survives a SOCD change while a key is held; SOCD modes on the report the stub sends through the production filter, restore on release, live changes reaching the host without input, invalid 16-bit keycode inertness, overlapping pairs and modifier pairs. The `guards` group checks that the production 6KRO sender filters a copy before its duplicate check and send.

The generated `test_rgb_task_gate.c` compiles the exact production `rgblight_task_periodic_due()` helper and checks inactivity disarm, resume after more than the 16-bit half-range, urgent-event rearm and 32-bit wrap.

The generated `test_rgb_mode_transition.c` compiles the exact production saturation-restore rule (`rgblight_mode_transition_sat()`) and checks that it keys on the destination effect (Rainbow Mood/Swirl, Gradient, Christmas) from any different base mode rather than on leaving Solid Color. The same generator asserts that `rgblight_sethsv_eeprom_helper()` commits before it asks for a pulse evaluation and never evaluates pulse output itself.

### Tap Dance ownership and lifetime

`--only rgb` also runs `test_td_ownership.h` and `test_td_lifetime.h` against
production TD, LT/MT, modifier/layer/usage ownership, report construction and
indicator selection: per-input ownership of one slot, outputs shared with other
inputs, and dance lifetime across keymap, layer and one-shot changes. The
assertions in those headers own the exact behaviour. Matrix/keymap, clocks,
EEPROM, RGB colour conversion and physical output remain adapters.

After changing those assertions or their generator, run
`python -X utf8 tools/firmware_regression_tests/td_ownership_selftest.py`. Each
control mutates only an ignored generated copy and counts only when it compiles
and its named runtime assertion then fails. Source files are never mutated, and
none of this measures hardware.

The Tap Dance input fixture also covers independent hold deadlines versus the
consecutive-press window, captured timing, early hold before layer lookup,
first-press immediacy and cached release after the layer turns off.

## EEPROM receipt and initialization fixtures

`--only eeprom` links the production RAM-image writer, ZD24C128 page state
machine and I2C IT owner to a modeled chip/bus. The macro GET/SET/RESET and
completion notification bodies are extracted intact from production. Tests cover
ACK-without-program, readback errors, same-value retry, immutable in-flight data,
failed invalidation/CLOSE, and revision publication only after completion.

The fixture also stops at each modeled programmed byte and bus-completion edge
of an upload. Recovery starts a new process using only the saved physical image;
it checks that a zero marker describes an old or complete new payload and that a
fresh full upload succeeds. These are simulated cuts, not electrical power tests.

`storage_cases.py` separately connects the actual factory -> quantum -> VIA
initialization owners, injecting failure at every flush barrier. Default providers
and flush receipts are stubs in that fixture; it checks ownership and fail-closed
propagation, not physical page timing. The chip/bus chain above provides the
independent physical receipt coverage. The unselected internal-flash emulation
backend is not exercised by shipped-board tests.
