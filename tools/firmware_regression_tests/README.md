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
