# USB host contract

Genre: contract
Canonical for: what the host is shown and in what shape — interface and
endpoint layout, the 20-key report and boot-protocol size deviation, why NKRO
is not shipped, polling-mode ownership, the retired automatic USB recovery
path and why it must not be restored, bootloader-handoff detach, and the
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
- FS (`USBD_HID_GetFSCfgDesc`): only keyboard IN is patched to
  `HID_FS_BINTERVAL` (1). VIA IN/OUT keep the static descriptor value 4;
  EXK keeps the static `HID_HS_BINTERVAL` (1).

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

## 3. Boot-protocol size deviation is known and kept

| | This firmware (interface 0) | HID Boot Keyboard |
| --- | --- | --- |
| IN report | 22 B — mods(1) + reserved(1) + keys[20] | 8 B — mods(1) + reserved(1) + keys[6] |
| Output report | 1 B (5 LED bits + 3 bits padding) | Same |
| Interface | subclass 1 (BOOT), protocol 1 (Keyboard) | Same |

The first 8 B of the 22 B IN report are the boot layout byte-for-byte. A
host that reads only 8 B sees the same modifiers, reserved byte, and first
six keys a boot keyboard would have sent.

Two deviations from boot protocol:

1. `USBD_HID_REQ_SET_PROTOCOL` stores `hhid->Protocol` and does not shrink
   the IN report. `usbHidSendReport()` always transmits
   `HID_KEYBOARD_REPORT_SIZE`. That field is not wired to QMK
   `keyboard_protocol`.
2. Interface 0 has no `USBD_HID_REQ_GET_REPORT` handler; the class SETUP
   default STALLs it (`USBD_CtlError`).

The remaining risk is transfer size, not content. `wMaxPacketSize` is 64
(`HID_EPIN_SIZE`). A host that arms an 8 B IN transfer against a 22 B
packet can babble-halt the endpoint.

BIOS/UEFI compatibility is therefore the 8 B prefix match, not a spec
guarantee. Do not blur that in this file or in release notes.

> **REFUSED:** shrinking the keyboard IN report to 8 B on boot protocol.
> **WHY:** the 8 kHz send path (retry queue and SOF drain) would have to
> become protocol-state variable length, and shipping images already run
> this 22 B report with no field failure report.
> **REOPENS:** a specific BIOS/KVM that fails; start with deviation 1.

> **REFUSED:** shipping NKRO (bitmap report, separate report ID).
> **WHY:** the default report already holds 20 keys with no toggle, while a
> 32 B NKRO report (`NKRO_REPORT_BITS` 30 plus ID and mods) would force EXK
> from 8 B to 32 B and grow the EXK retry queue for every user, including
> those who never enable it. A 6KRO↔NKRO toggle is a boot-compat versus
> rollover trade-off this layout does not have.
> **REOPENS:** field evidence that 20 keys is not enough. Reintroduction
> still means an extra report ID on EXK and tying `keyboard_protocol` to
> SET_PROTOCOL with `wIndex` 0.

NKRO was never offered. `NKRO_ENABLE` is not a compile definition
(`src/ap/modules/qmk/CMakeLists.txt`), so `keymap_config.nkro` has no
effect. Keeping 20-key reports is the status quo, not a regression.

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
for the VIA response, then `usbProcessDeferredReset()` detaches
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

## 6. Bootloader-to-firmware handoff needs a 100 ms detach hold

UF2 upload itself succeeds. Jump-entry from the bootloader can leave
the host with a device that is electrically still attached: the
bootloader clock-gates USB and does not set `DCTL.SDIS`, so D+ pull-up
and HS termination stay. Firmware that then enumerates never gets a
new address.

Firmware already disconnects. `HAL_PCD_Init()` ends with
`USB_DevDisconnect()` (`DCTL.SDIS` = 1). `HAL_PCD_Start()` immediately
calls `USB_DevConnect()` (`SDIS` = 0). That window is hundreds of
microseconds — shorter than host/hub debounce (~100 ms).

`USBD_LL_Start()` in `src/hw/driver/usb/usbd_conf.c` holds
`USBD_BOOT_DETACH_HOLD_MS` (100) after that init disconnect, then
starts PCD. It is a longer hold of an existing electrical detach, not
a new USB behavior. `usbBegin()` runs once per boot (`src/hw/hw.c`),
so the blocking delay is on the boot path only.

The VIA reset path uses a different constant:
`USB_RESET_DETACH_DELAY_MS` (100) in `src/hw/driver/usb/usb.c`, after
`USBD_Stop`/`USBD_DeInit` and before `resetToReset()`. Pre-reset
detach grace and boot-time detach hold stay independently tunable.

Firmware does not detect jump-entry. Every boot takes the 100 ms hold.
PCD init clocks the core and clears `DCFG.DAD` in the same call, so a
detector would need a second init.

This workaround is for boards already shipped. The bootloader-side
root fix (detach, then system reset instead of a jump) is ST-LINK
only and applies to later shipments. Firmware cannot intervene while
the bootloader's UF2 copy has stalled USB. Field confirmation of
auto-start stays in `docs/state_open.md`; this section does not close
it.

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
