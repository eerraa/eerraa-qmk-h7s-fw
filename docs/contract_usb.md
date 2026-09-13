# USB host contract

Genre: contract
Canonical for: what the host is shown and in what shape — interface and
endpoint layout, the 20-key report and boot-protocol size deviation, why NKRO
is not shipped, polling-mode ownership, the retired automatic USB recovery
path and why it must not be restored, the bootloader handoff, and the
main-loop periodic-work timer rule

## 1. Interface and endpoint layout

Shipped boards start `USB_HID_MODE` (`HW_USB_CMP` is 0 unless `_USE_HW_VCOM`
is defined). The HID configuration descriptor in
`src/hw/driver/usb/usb_hid/usbd_hid.c` declares three interfaces:

| Interface | Endpoint | wMaxPacketSize | subclass / protocol | Reports |
| --- | --- | --- | --- | --- |
| 0 | `HID_EPIN_ADDR` `0x81` IN | `HID_EPIN_SIZE` 64 B | 1 (BOOT) / 1 (Keyboard) | Keyboard IN, 22 B. No OUT endpoint; LED output is the 1 B HID SET_REPORT on EP0 |
| 1 | `HID_VIA_EP_IN` `0x84` IN / `HID_VIA_EP_OUT` `0x04` OUT | `HID_VIA_EP_SIZE` 32 B | 0 / 0 | VIA raw HID, 32 B each way |
| 2 | `HID_EXK_EP_IN` `0x85` IN | `HID_EXK_EP_SIZE` 8 B | 1 (BOOT) / 0 (none) | SYSTEM (`REPORT_ID_SYSTEM` 3, 3 B) / CONSUMER (`REPORT_ID_CONSUMER` 4, 3 B) / MOUSE (`REPORT_ID_MOUSE` 2, 6 B) |

The mouse report is 6 B (`report_mouse_t` with `MOUSE_SHARED_EP`) and fits
the existing 8 B EXK endpoint. `_Static_assert` in
`src/hw/driver/usb/usb_hid/usbd_hid.c` locks the report-descriptor sizes
to the QMK structs.

Advertised `bInterval` on that HID-only descriptor:

- HS (`USBD_HID_GetHSCfgDesc`): keyboard IN, VIA IN, VIA OUT, and EXK IN
  all take `usbBootModeGetHsInterval()`.
- FS: keyboard IN, VIA IN, VIA OUT, and EXK IN all advertise a 1 ms interval.
  VIA IN/OUT use `HID_FS_BINTERVAL` directly in the static descriptor; the
  keyboard getter patches its endpoint to the same value.

`FS 1K` enumerates as Full Speed (`PCD_SPEED_HIGH_IN_FULL`). The HS 2/4/8K
modes enumerate as High Speed (`PCD_SPEED_HIGH`).

When `_USE_HW_VCOM` is on, `src/hw/driver/usb/usb_cmp/usbd_cmp.c` builds the
same three HID interfaces from the same size and report-descriptor-length
constants. HS keyboard `wMaxPacketSize` then ORs `(2U << 11)` (three
transactions per microframe); the HID-only descriptor does not.

The root `CMakeLists.txt` deliberately keeps both `usb_cdc` and `usb_cmp` in
the recursive `src/hw/*.c` source set. `_USE_HW_CDC` is always defined in
`src/hw/hw_caps_usb.h`, and `cdcInit()` in `src/hw/driver/cdc.c` calls
`cdcIfInit()` from `src/hw/driver/usb/usb_cdc/usbd_cdc_if.c` even on shipped
`HW_USB_CMP == 0` images. The `HW_USB_CMP == 1` VCOM path uses the same source
set for the composite builder.

> **REFUSED:** removing `usb_cdc` or `usb_cmp` from the root source glob based
> only on shipped boards having `HW_USB_CMP == 0`.
> **WHY:** HID-only images still link `cdcIfInit()`, while the VCOM composite
> path needs `usb_cmp`; an unconditional exclusion breaks one of those modes.
> **REOPENS:** separate CMake source selections that link both modes, with all
> five shipped HID images and a VCOM composite image built from that design.

## 2. Simultaneous keys are 20, not 6KRO

`HW_KEYS_PRESS_MAX` is 20 (`src/hw/hw_caps_keys.h`). No board overrides it.
Interface 0 has no `KEYBOARD_SHARED_EP`, so `report_keyboard_t` is mods(1) +
reserved(1) + `keys[20]` = `HID_KEYBOARD_REPORT_SIZE` / `KEYBOARD_REPORT_SIZE`
**22 B**. The report descriptor's `REPORT_COUNT` is `HW_KEYS_PRESS_MAX`.

A host that parses the report descriptor sees 20 key slots. A 21st key is
dropped: `add_key_byte()` in `src/ap/modules/qmk/port/protocol/report.c`
leaves the report unchanged when no empty slot remains. It does not send an
ErrorRollOver code.

## 3. Report ownership, ordering, and USB lifecycle

`src/hw/driver/usb/usb_hid/hid_tx_queue.c` owns a fixed FIFO per IN endpoint.
Each FIFO has 128 pending slots and one separate active packet. Only the active
packet is passed to `USBD_LL_Transmit`; it remains immutable until the matching
DataIn completion. All queue and PCD-register operations use the same saved
PRIMASK critical section. A failed arm does not consume the FIFO head. A new
report cannot bypass older pending reports.

Caps taps keep the upstream `TAP_HOLD_CAPS_DELAY` default of 80 ms.
[QMK's configuration documentation](https://docs.qmk.fm/config_options) identifies
it as a macOS compatibility setting and notes that some macOS configurations may
need 200 ms or more. USB FIFO ordering and host-side short-Caps filtering are
separate requirements; a lossless FIFO does not justify removing the hold.

`tap_code_wait()` in `src/ap/modules/qmk/quantum/action.c` routes keyboard tap
intervals through the host port to `usbHidDelayKeyboardReport()`. TD, LT/MT and
generic keyboard tap helpers use this same path. A Tap Dance synthesized tap requests
the width the generic helper would, `TAP_HOLD_CAPS_DELAY` for Caps Lock and
`TAP_CODE_DELAY` otherwise, and a hold's release requests none
(`src/ap/modules/qmk/port/tapdance.c`). QMK completes its logical release
synchronously, and the existing FIFO owns immutable report snapshots. There is
no future `unregister_code()` callback that could release a later key or disturb
its modifiers. Non-keyboard tap delays keep their existing synchronous path.
The wait port in `src/ap/modules/qmk/port/platforms/wait.c` returns immediately
for 0 ms instead of inheriting `HAL_Delay(0)`'s minimum tick wait.

The transport attaches a minimum interval after the latest accepted keyboard
snapshot, using pending-packet metadata, separate active-transfer metadata, or
the last completed report's timestamp. Repeated requests take the larger minimum.
Time is measured from DataIn completion with the existing microsecond counter,
not from logical registration or enqueue. Time already elapsed since that
completion counts toward the interval. Pending/active snapshots remain ordered;
failed arms do not consume the interval or the FIFO head. A new transport
generation discards old intervals along with the old backlog. Suspend preserves
them, so a tap queued while asleep gets its hold after its press is delivered.

During an interval, the next keyboard report and its successors wait in the FIFO.
This preserves Caps-release/next-letter and modifier ordering; it is an intentional
host-output delay. Matrix processing, tap/hold resolution, RGB rendering, EXK and
VIA continue. No busy wait, new periodic ISR, dynamic allocation, or keycode scan
is added to the USB pump. The ordinary pump bypasses the clock read when no
interval is active; each keyboard completion records one timestamp. Finite queue
limits below still apply: overflow can lose taps, while the accepted prefix's
intervals and convergence to the latest release state remain intact.

`USBD_HID_DataIn()` completes the endpoint and immediately pumps its next head.
`USBD_HID_SOF()` supplies a bounded fallback retry, not a wall-clock throttle.
There is no TIM2 report-service ISR and no USB work in a generic timer/PWM
callback. This removes an additional scheduling phase; it does not change the
host's polling interval or guarantee a measured end-to-end latency.

Keyboard/EXK overflow is explicit: retain the accepted FIFO prefix, then append
the newest state after that prefix drains. Intermediate events beyond finite
capacity may be coalesced. Keyboard release, mouse buttons, system and consumer
usages converge to the latest state. Relative mouse motion/wheel deltas are not
replayed during reconciliation. This is not an unlimited lossless input log.
The existing wire drop counter still aggregates keyboard and EXK saturation.
`usbHidGetTransportStats()` additionally exposes local RAM-only arm failures,
per-path coalescing, invalid packets and discarded session backlog; it does not
change selector 0x07 or its reserved bytes.

A configured session remains the same session during Suspend. Its accepted
press/release FIFO is retained, including a short tap during wake latency.
Physical IN arming waits for Resume. Remote wake requires the host-enable bit,
a sufficient Suspend interval and a debounced physical press.
A debounced physical press requests wake before keycode/action filtering, so
layer-only or otherwise consumed keys do not depend on a HID report to wake the
host. Each new physical press may retry if an earlier pulse did not resume the
same suspended session. Report submission itself does not request wake and the
ordinary SysTick path contains no Remote-Wake work.

The Remote-Wake state is `IDLE -> SIGNALING -> WAIT_RESUME -> IDLE`. The request
path first revalidates software Suspend, host permission, the PCD, the suspend
epoch and transport generation, then requires hardware `DSTS.SUSPSTS=1`. It
waits until at least 5 ms after Suspend, masks only `GINTMSK.WUIM`, uses the ST
HAL STOPCLK ungate, asserts RWUSIG, verifies that RWUSIG was actually asserted,
holds it for 10 ms, and explicitly deasserts it. No GATECLK write or additional
PHY/recovery sequence is part of this contract.

The WUIM mask is required by BRICK60 hardware: H7RS may raise device-driven
WKUINT immediately after RWUSIG assertion while `DSTS.SUSPSTS` is still set,
and the vendor IRQ handler otherwise clears RWUSIG before its Resume callback.
At pulse end, such a pending WKUINT is discarded only if hardware still reports
Suspend; WUIM is then restored. If hardware has resumed, WKUINT is preserved for
normal HAL processing.

Physical bus Suspend and ST USBD logical Suspend are separate owners. The PCD
bridge alone owns the cached physical `bus_suspended` state: Suspend sets it;
USB Reset clears it because Reset is bus activity; a Resume callback or SOF with
`DSTS.SUSPSTS=0` also clears it. Remote-Wake state never gates that physical
transition. QMK suspend hooks and RGB Sleep consume this physical bus state, so
an enumeration Reset/fresh SOF cannot leave RGB dark because of a stale Suspend
callback. Logical SOF recovery is instead gated by `pdev->dev_state ==
USBD_STATE_SUSPENDED`, so clearing physical Suspend cannot suppress the next
fresh SOF needed to complete ST USBD recovery.

A Resume callback changes ST USBD state only when `DSTS.SUSPSTS=0`. Because a
successful host Resume need not produce a second usable WKUINT, an outstanding
Remote-Wake attempt also accepts the first fresh SOF for which `SUSPSTS=0` and
calls `USBD_LL_Resume()` exactly once. An SOF already pending before signaling,
an SOF while `SUSPSTS=1`, and a late WKUINT after SOF recovery cannot produce a
false or duplicate logical Resume. BRICK60 hardware established the two
underlying acceptance facts: a 10 ms WUIM-isolated signal wakes the PC from S3,
and fresh-SOF logical recovery restores post-wake keyboard/VIA raw-HID service.

Configuration/reset is a new transport generation. Init fully initializes the
class state; DeInit closes every owned endpoint and clears aliases. The PCD
adapter quiesces non-control endpoints, masks stale TXFE, clears completion
flags and flushes private IN FIFOs before reuse. It does not flush the shared
RX FIFO during a class close. Control endpoint lifecycle remains core-owned.
`src/hw/driver/usb/usb_class_pool.c` provides bounded, reusable class slots rather
than cumulative bump allocation. Latest key/button/usage state is reconciled
in the new generation, but disconnected typing is not replayed as event history.
Hardware stress must still verify callback/FIFO ordering across reset and detach.

VIA OUT has 16 queued 32-byte frames. A full RX queue stops rearming OUT, so the
host receives NAK rather than an ACK followed by silent command loss. Main-loop
processing is limited to one command after keyboard processing. A response slot
must be available before dispatch, and the single response producer attaches the
request's generation. Reset discards queued old commands and rejects old-generation
responses. A command already admitted to dispatch may execute/finish its side effects; generation
checks are not transactional rollback. `docs/contract_via.md` owns wire bytes.

SET_REPORT accepts only the keyboard interface's report-ID-zero, one-byte LED
Output report. Length, recipient, direction, type and ID are checked before the
control receive is armed. The RX buffer nevertheless covers a full EP0 packet
because HAL rounds the physical receive size up to the endpoint maximum. A
non-one-byte actual payload is not applied as an LED value. A later SETUP
invalidates the pending LED receive.

### Hardware allocation and errata

The HS FIFO allocation is RX 512 words plus TX 32/32/128/16/16/16 words: 752 of
1024 words, with a compile-time bound. This also reserves the optional CDC bulk
endpoint's maximum packet. FS descriptor requests restore the interval of all
four HID endpoints after any HS descriptor request.

ST ES0596 Rev 10, sections 2.2.17 and 2.21.3, govern two local mitigations:
`src/bsp/bsp.c` maps the unimplemented GFXMMU aperture as inaccessible Device/XN;
`src/lib/ST/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_ll_usb.c` applies the documented
NAK/enable sequencing only to IN zero-length packets, including EP0. Device-register
reads supply minimum AHB-cycle gaps without an assumed CPU/HCLK ratio. Normal
nonzero HID packets do not take that delay path. These mitigations require real
silicon/host validation; they do not establish a cause for any historical event.

## 4. Automatic USB recovery is retired — do not restore it

Gone from `src/` (0 hits). `tools/era_doc_refs.py` `retired`
fails if any of these return: `usbMonitor`, `usbInstability`,
`usbHidMonitor`, `usbRequestBootModeDowngrade`,
`usbd_hid_instrumentation`, `USB_MONITOR_ENABLE`, `auto_downgrade`.
What they implemented is also gone: SOF-interval scoring and
warmup/timeout, enumeration/speed/suspend scoring, the automatic
8k → 4k → 2k → 1k downgrade queue, the monitor EEPROM toggle, that
toggle's VIA channel 13 value id 3, and the compile-time
instrumentation unit. Channel 13 value id 3 is not in
`src/ap/modules/qmk/quantum/via.h`; do not reuse it. Official JSON
exposes value ids 1 and 2 only.

This is retired, not missing. Two reasons stand together.

1. **Product contract.** Firmware does not emit a stability score or a
   stable/unstable verdict, and it does not revert polling mode on its
   own. Mode choice is always user-owned (§5). The other side is
   `the-via-eerraa/docs/adr/0002-h7s-usb-diagnostics.md`. Observation
   is the read-only `0x07` session; the byte envelope lives in
   `docs/contract_via.md` §6, not here.
2. **That code hung the keyboard for an unexplained reason.** A build
   with `USB_MONITOR_ENABLE` defined froze after about 620 s from
   boot, including the LED toggle. It reproduced with the runtime
   toggle OFF. It did not reproduce when the macro was removed from
   the build. Adding instrumentation made it vanish. The cause was
   never identified. Restoring the path reimports that unresolved
   risk.

> **REFUSED:** restoring the instability monitor or automatic polling
> downgrade.
> **WHY:** both reasons stand together — it would break the product
> contract with the app, and it would reimport a hang path whose
> cause was never identified.
> **REOPENS:** none. If more observation is needed, widen the
> selector `0x07` session.

The EEPROM monitor slot was not deleted. It remains
`EECONFIG_USER_RESERVED_32` so later slot addresses do not move
(`docs/contract_eeprom.md` §1). Nothing reads or writes it.

RGB SLEEP may count SOF callbacks to notice a host that vanished while
VBUS stayed up. That counter is not a stability score and must not call
polling-mode apply/reset. The owner is `src/ap/modules/qmk/port/rgb_sleep.c`.

### 0x07 product boundary

Selector `0x07` (`ERA_USB_DIAGNOSTICS_KEYBOARD_VALUE`) is observation
only. It must not couple to polling-mode apply/reset or to State Sync
recovery. That is the same product boundary as ADR 0002 (auto
downgrade, auto mode benchmark, EEPROM diagnostic history, synthetic
stability score, coupling `0x07` to polling mode or State Sync
recovery). Firmware:

- `src/ap/modules/qmk/port/era_usb_diagnostics.c` does not call
  `usbBootModeScheduleApply`, `usbBootModeSaveAndReset`, or
  `usbScheduleGraceReset`. START reads `usbBootModeGet()` only to
  compute the histogram's expected interval.
- That file does not include `src/ap/modules/qmk/port/era_state_sync.h`.
  Channel 13 select still bumps CONFIG revision; `0x07` does not.
- Session state and always-on counters live in RAM
  (`src/hw/driver/usb/usb_hid/usb_diagnostics.c`). CLEAR zeros the
  session; it does not write EEPROM and does not clear boot counters.
- No synthetic score is computed.

> **REFUSED:** coupling selector `0x07` to polling-mode apply/reset or
> State Sync recovery, writing diagnostic history to EEPROM, or
> emitting a synthetic stability score.
> **WHY:** mode choice is always the user's, and observation that
> changes the control plane, EEPROM, or recovery contaminates what it
> measures.
> **REOPENS:** none. If more observation is needed, widen the
> read-only `0x07` session.

Always-on counters (saturating `uint32`, event-driven): keyboard/EXK
report-queue drop, USB reset, HID configuration, suspend, speed
change. RAM only.

Matrix development instrumentation
(`src/ap/modules/qmk/port/matrix_instrumentation.c`) is a separate
store and a separate compile flag (`_DEF_ENABLE_MATRIX_TIMING_PROBE`,
0 on every shipped board). Do not add the two sets of numbers
together — they measure different intervals.

## 5. Polling mode is user-owned

| enum | Link | HS `bInterval` (`usbBootModeGetHsInterval`) | VIA dropdown |
| --- | --- | --- | --- |
| `USB_BOOT_MODE_FS_1K` | FS 1 kHz | 1 (FS uses `HID_FS_BINTERVAL`) | 3 |
| `USB_BOOT_MODE_HS_2K` | HS 2 kHz | 3 | 2 |
| `USB_BOOT_MODE_HS_4K` | HS 4 kHz | 2 | 1 |
| `USB_BOOT_MODE_HS_8K` | HS 8 kHz | 1 | 0 |

Default is **FS 1 kHz**. `USB_BOOT_MODE_DEFAULT_VALUE` is
`USB_BOOT_MODE_FS_1K`; no board overrides it. All five boards set
`BOOTMODE_ENABLE`.

Channel 13 (`id_qmk_usb_polling`) in `src/ap/modules/qmk/port/bootmode.c`:
`id_qmk_usb_bootmode_select` (value 1) SET updates `pending_boot_mode`
only. `id_qmk_usb_bootmode_apply` (value 2) SET of a non-zero byte calls
`usbBootModeScheduleApply()`. `id_custom_save` is a no-op; persist is
Apply (`docs/contract_eeprom.md` §1).

The main loop (`src/ap/ap.c`) runs `usbProcess()`, which drains that queue
through `usbProcessBootModeApply()` → `usbBootModeSaveAndReset()`: write
`EECONFIG_USER_BOOTMODE`, wait at least `USB_BOOTMODE_APPLY_GRACE_MS` (40)
for the VIA response. `usbProcessDeferredReset()` additionally waits until
EEPROM has no unacknowledged writes and VIA has no queued/active responses,
while the normal input loop continues. It then detaches
(`USB_RESET_DETACH_DELAY_MS` 100) and resets the MCU. Apply of the already
active mode still queues that reset.

CLI `boot info` / `boot set {1k|2k|4k|8k}` calls `usbBootModeSaveAndReset()`
directly — same persist-and-reset, no pending queue.

Only those user paths call the apply/reset APIs.
`src/ap/modules/qmk/port/era_usb_diagnostics.c` does not. Any automatic
step in `usbProcess()` besides user apply/reset violates §4.

Apply/reset tears down USB and reboots, so an in-flight diagnostic session
cannot continue on the same enumeration. How the host treats that boundary
is `the-via-eerraa/docs/adr/0002-h7s-usb-diagnostics.md` and
`docs/contract_via.md` §6.

## 6. Bootloader-to-firmware handoff resets the inherited USB blocks

Every firmware start is a jump from the bootloader. Its boot-up routine
(`eerraa-qmk-h7s-boot/src/ap/ap.c`) copies FIRM into SRAM and jumps, on
a cold boot and after a UF2 upload alike. The bootloader on current
boards jumps after a UF2 upload with USB still enabled: it clock-gates
the OTG core and USBPHYC without resetting them. Handed over that way,
the core does not complete the soft reset that `HAL_PCD_Init()` issues;
the HAL loop gives up after `HAL_USB_TIMEOUT`, about ten seconds, and
`USBD_LL_Init()` in `src/hw/driver/usb/usbd_conf.c` stops in
`Error_Handler()`.

`HAL_PCD_MspInit()` in the same file therefore inspects the inherited
core after enabling its clocks. `USB_OTG_GAHBCFG_GINT` set means the
bootloader's TinyUSB armed it; the firmware then force-resets
`USB_OTG_HS` and USBPHYC through RCC, clears the pending `OTG_HS_IRQn`,
and holds `USBD_HANDOFF_RESET_HOLD_MS` (100) with the pull-up gone
before the normal init continues. A cold boot, a VIA reset and the
reset-style bootloader (`eerraa-qmk-h7s-boot` 19e0487) all hand over a
core with that bit clear, so the branch costs nothing there, and the two
fixes coexist.

> **REFUSED:** a boot-time detach hold — keeping the `DCTL.SDIS` that
> `HAL_PCD_Init()` sets for 100 ms before `HAL_PCD_Start()` — as the
> firmware-side fix (the V260824R2 approach).
> **WHY:** on the old bootloader the failure is inside `HAL_PCD_Init()`,
> before that hold could run; it changed nothing and cost 100 ms on
> every boot.
> **REOPENS:** none. The failing call is measured, and a hold placed
> after it cannot reach it.

The VIA reset path is unchanged: `usbProcessDeferredReset()` in
`src/hw/driver/usb/usb.c` stops USB, waits `USB_RESET_DETACH_DELAY_MS`
(100), then resets the MCU. What is still owed on hardware is
`docs/state_open.md` §2.

## 7. Do not skip rgblight lookup behind a 16-bit expiry cache

USB polling, RGB animation, EEPROM drain, and key scan share the main
loop (`src/ap/ap.c`: `usbProcess()` then `qmkUpdate()`).
`rgblight_timer_task()` in
`src/ap/modules/qmk/quantum/rgblight/rgblight.c` expires with 16-bit
`sync_timer_read()` / `timer_expired()`. That compare window is
`UINT16_MAX / 2` milliseconds, about 32 s.

Every call recomputes `effect_func` and `interval_time` before the
expiry check. `next_timer_due` is still the 16-bit next deadline.
Pulse-on-press paths already expire with a 32-bit signed compare on
`sync_timer_read32()`.

> **REFUSED:** putting back an rgblight early branch that caches
> `effect_func` and interval and skips that lookup until a 16-bit
> `next_timer_due` expires.
> **WHY:** when `next_timer_due` is pushed outside the 16-bit compare
> window, or Velocikey / inactive state leaves the cache stale,
> animation and render stop silently. That freeze reproduced around
> ten minutes; it stopped only after the lookup-skipping cache was
> removed.
> **REOPENS:** an expiry design that uses a 32-bit clock and unsigned
> wrap (the pulse paths already do). Cache itself is not forbidden; a
> cache sitting on the 16-bit window is.

Diagnostic sessions follow the 32-bit rule: `usbDiagnosticsTask()`
completes when `(int32_t)(now_us - deadline_us) >= 0`. Counters
saturate at `UINT32_MAX`. TIM5 wrap is `docs/contract_via.md` §6-2.

## 8. Reactive RGB input belongs to physical switch transitions

Pulse effects and Velocikey consume debounced, ghost-filtered matrix transitions
in `src/ap/modules/qmk/quantum/keyboard.c`, before QMK action filtering or tapping
buffering. They use the scan's shared 32-bit timestamp. The input handler in
`src/ap/modules/qmk/quantum/rgblight/rgblight.c` updates bounded RAM state only;
color calculation, LED-buffer traversal and frame submission belong to the RGB
task. A pending pulse evaluation (physical input, configuration commit, indicator
overlay release) bypasses the periodic task gate without adding a wait.
While a pulse is latched, its expiry is judged on the RGB task's 1 ms periodic
gate, not by the animation timer interval, so that interval does not quantize
the pulse width.
Logical action replay, TD-generated actions and synthetic records do not generate
another physical press or release for RGB.

Configuration producers follow the same ownership. A VIA channel 2 SET or an RGB
keycode commits `rgblight_config` first and then only requests a pulse
evaluation; `rgblight_sethsv_eeprom_helper()` never computes pulse output itself.
The RGB task derives the pulse base output from the committed hue, saturation
and value, so a brightness or colour change in a Pulse effect shows the value
just sent rather than the previous one. A colour-only commit keeps the physical
latch and the tracked key; only a base-mode change resets them. An indicator
overlay release raises the same request, and the task drains the host LED queue
before that evaluation so the release and the restored base output land in one
frame.

> **REFUSED:** computing pulse output inside `rgblight_sethsv_eeprom_helper()`
> or any other configuration setter.
> **WHY:** the setter ran before its own commit, so every VIA brightness or
> colour change in a Pulse effect rendered the previous request's values; the
> frame only caught up on the next key press or the next change.
> **REOPENS:** a setter whose commit and evaluation are one step and whose
> output `python tools/firmware_regression_tests/run.py --only rgb` verifies.

Pulse duration starts at physical press. Hold variants extend that pulse while
the most recently pressed key remains down; releasing an older key cannot clear
the newer key's hold. This retains last-pressed-key tracking rather than changing
the effect to track all simultaneously held keys. The Caps indicator remains an
overlay of host LED Output state, independent of the physical pulse and local
tap/hold decision. Host response time and frame coalescing may affect visibility;
there is no guaranteed minimum number of visible pulse frames.

An isolated short TD tap with only tap and hold configured resolves at release,
as does the first LT tap. This does not make their full semantics identical: TD
interruption and configured double actions, and LT tapping policies and quick-tap
repetition still determine different logical action sequences. RGB must not gain
those differences merely because one resolver buffers a physical event longer.
