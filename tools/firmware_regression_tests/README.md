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

## What is executed

- The production HID FIFO runs 100000 deterministic operations against an independent
  order model. Failed arms retain the head, and delayed pointer reads verify the
  active payload remains unchanged.
- The production HID class, diagnostics implementation and fixed class pool run
  against an endpoint mock that retains pointers, provides RX NAK/backpressure,
  injects arm failures and executes the actual class callbacks. Tests cover overflow,
  response credit and generations, EP0 input sizes, 2048 configurations, partial
  open failure, short taps during wake, first relative mouse input and FS/HS descriptors.
  Remote-Wake cases inject the H7RS early-WKUINT behavior, enforce the 10 ms RWUSIG
  window, reject stale/SUSPSTS SOF, de-duplicate late WKUINT and consume VIA after
  fresh-SOF logical Resume.
- Source guards keep PCD physical bus-Suspend ownership separate from HID logical
  Resume: Reset and hardware-active SOF/Resume release the bus state independently,
  while logical SOF fallback remains gated by ST USBD Suspend plus wake freshness.
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

### Physical RGB input coverage

`--only rgb` connects the unedited production matrix dispatch, QMK tapping buffer,
TD callbacks, action execution, pulse state machine and RGB task in one host
fixture. It checks TD/LT Caps taps and holds, delayed replay, filtered releases,
all four pulse modes, last-pressed-key tracking, host indicator precedence,
output sleep, RGB OFF, Velocikey and 32-bit wrap. It also drives the actual
`rgblight_sethsv_eeprom_helper()` commit: a brightness commit in a Pulse effect
renders the committed value on the next task pass, a commit during a physical
hold keeps the latch, and only a base-mode change drops it. It also retains the distinct
TD interruption/double-action and LT quick-tap behavior. Matrix input, keymap
lookup, host LED feedback, indicator composition and LED hardware are test
adapters; USB transport and real WS2812 frames are not measured by this fixture.
Oneshot and optional tapping policies are outside this fixture's configuration.
MinGW uses GCC bitfield layout, checked against the production two-byte action.
The fixture also includes the actual QMK wait/host ports and generic tap helpers.
It requires the upstream 80 ms Caps interval request with zero calls to the delay
backend, and checks ordinary explicit waits separately. The actual USB class
fixture checks 80/200 ms intervals measured from completion, including completion
at a fractional-ms boundary; EXK/VIA continue during the interval. It covers fast
Caps/letter sequences, later same-usage snapshots, failed arms, overflow, Suspend,
reset, and clock wrap under FS/HS and boot/report protocol. Existing zero-interval
FIFO tests cover ordinary input. No host Caps-activation filter is emulated.

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

`test_input_features.c` compiles the production `kkuk.c` and `kill_switch.c` against a deterministic report/timer stub. It covers KKUK activation/repeat timing, live value normalization, fresh-epoch reconfiguration and count saturation; SOCD basic/modifier report ownership, invalid 16-bit keycode inertness and live-remap stale-state release.

The generated `test_rgb_task_gate.c` compiles the exact production `rgblight_task_periodic_due()` helper and checks inactivity disarm, resume after more than the 16-bit half-range, urgent-event rearm and 32-bit wrap.

The generated `test_rgb_mode_transition.c` compiles the exact production saturation-restore rule (`rgblight_mode_transition_sat()`) and checks that it keys on the destination effect (Rainbow Mood/Swirl, Gradient, Christmas) from any different base mode rather than on leaving Solid Color. The same generator asserts that `rgblight_sethsv_eeprom_helper()` commits before it asks for a pulse evaluation and never evaluates pulse output itself.
