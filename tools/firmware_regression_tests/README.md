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
with `--only queue`, `debounce`, `eeprom`, `usb`, `qbuffer` or `guards`.
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
