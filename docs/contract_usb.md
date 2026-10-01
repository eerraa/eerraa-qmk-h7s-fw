# USB host contract

Genre: contract
Canonical for: host-visible HID shape, the 20-key and boot-protocol compatibility
deviations, report/lifecycle ownership, user-owned polling mode, the retired
automatic USB recovery boundary, legacy bootloader handoff compatibility, and
main-loop periodic/reactive RGB ownership

Current implementation details are source-owned. Start at
`src/hw/driver/usb/usb_hid/usbd_hid.c` for descriptors/report transport,
`src/hw/driver/usb/usb.c` and `src/ap/modules/qmk/port/bootmode.c` for polling
apply/reset, and `src/ap/modules/qmk/port/` for QMK-facing behavior. VIA wire
bytes are owned by `docs/contract_via.md`; EEPROM layout and persistence are
owned by `docs/contract_eeprom.md`. The peer bootloader contract is
`eerraa-qmk-h7s-boot/docs/uf2_auto_start.md`. This file owns the compatibility
requirements and the reasons not to restore rejected behavior, not a duplicate
call graph or constant inventory.

## 1. Interface and endpoint layout

The shipped HID shape is a host-compatibility contract:

| Interface | Endpoint | Maximum packet | subclass / protocol | Host-visible reports |
| --- | --- | ---: | --- | --- |
| 0 keyboard | IN `0x81` | 64 B | BOOT / Keyboard | 22 B keyboard IN; LED Output is a one-byte SET_REPORT on EP0 |
| 1 VIA raw HID | IN `0x84`, OUT `0x04` | 32 B | none / none | 32 B request/reply |
| 2 EXK | IN `0x85` | 8 B | BOOT / none | SYSTEM and CONSUMER 3 B, MOUSE 6 B |

The descriptor and QMK report types are compile-time checked in
`src/hw/driver/usb/usb_hid/usbd_hid.c`. Full-Speed mode advertises a 1 ms
interval on all four HID endpoints. High-Speed modes apply the selected HS
interval to those endpoints. FS 1 kHz enumerates as Full Speed; the 2/4/8 kHz
modes enumerate as High Speed.

Optional VCOM/composite builds must preserve the same three HID functions.
Current packet/descriptor details are source-owned by
`src/hw/driver/usb/usb_cmp/usbd_cmp.c`. Do not prune `usb_cdc` or `usb_cmp`
from the build merely because shipped HID-only boards have composite mode off:
HID-only still links the CDC interface layer and VCOM needs the composite
builder. Reconsider source selection only with separate selections that build
the shipped HID configurations and a VCOM composite configuration.

## 2. Simultaneous keys are 20, not 6KRO or NKRO

The shipped keyboard report is mods + reserved + 20 key slots: 22 bytes.
`HW_KEYS_PRESS_MAX` in `src/hw/hw_caps_keys.h` owns the current slot count and
the report descriptor derives from it. A host that follows the descriptor sees
20 simultaneous key slots. When all slots are occupied, an additional key is
not inserted and no ErrorRollOver usage is substituted.

This array-report shape is compatibility behavior, not an invitation to enable
QMK NKRO as a transparent refactor. Changing the report model requires an
explicit host-compatibility change, including BIOS/UEFI and supported-OS
validation.

## 3. Report ownership, ordering, boot protocol, and USB lifecycle

### Report ordering and nonblocking intervals

Each IN path has one ordered transport owner. Once a report is accepted, an
older accepted report cannot be bypassed; the active packet remains immutable
until its matching DataIn completion, and a failed transmit arm does not consume
the pending head. Queue capacity and data structures are source-owned by
`src/hw/driver/usb/usb_hid/hid_tx_queue.c`.

Finite overflow is explicit. Preserve the already accepted prefix, then converge
keyboard/button/system/consumer state to the newest state after the prefix
drains. Intermediate events may be coalesced. Relative mouse motion/wheel deltas
must not be replayed during reconciliation. The aggregate wire drop counter and
the local transport counters keep their existing meanings; changing the
diagnostic wire envelope is a `docs/contract_via.md` change.

Keyboard tap timing is transport-owned and nonblocking. Requested keyboard
intervals are measured from completion of the latest accepted keyboard snapshot,
not from logical registration/enqueue; repeated requests keep the larger
minimum. While that interval is active, only later keyboard reports wait.
Matrix processing, QMK action resolution, RGB, EXK and VIA service continue.
Suspend keeps same-generation accepted reports and their interval; a new
transport generation discards the old backlog and interval. Do not replace this
with a busy wait, a report-service timer ISR, or delayed synthetic key-up
callbacks. The current Caps/tap timing cases are executable regression behavior
under `tools/firmware_regression_tests/`. The configured Caps interval remains a
separate host-compatibility requirement; FIFO ordering does not justify removing
it. Keep the USB pump free of dynamic allocation and keycode scanning.

### Boot-protocol deviations

Interface 0 advertises BOOT/Keyboard, but the current class request state is not
an alternate report formatter:

1. SET_PROTOCOL changes the protocol state returned by GET_PROTOCOL; it does not
   switch the keyboard sender to a separate boot-report implementation.
2. Consequently Boot Protocol still uses the shipped 22-byte, 20-slot keyboard
   report rather than the conventional 8-byte, six-key boot report.

These deviations are intentional compatibility facts. Do not "fix" them as a
local cleanup; a change requires explicit BIOS/UEFI and supported-host
acceptance.

### Suspend, Remote Wake, and Resume ownership

A configured transport remains the same generation through Suspend. Accepted
press/release reports are retained, but physical IN arming waits for Resume.
Remote Wake is requested by a debounced physical press before keycode/action
filtering, so layer-only or consumed keys can wake the host. Each new physical
press may retry while the same transport generation remains suspended. Report
submission itself is not a wake request.

A key pressed while the host sleeps — a host that had configured the device
suspended it (`usbHidHostSleeping()`), or the press requested Remote Wake — wakes
it but is not typed, as in QMK: `keypress_is_wakeup_key()`
(`src/ap/modules/qmk/port/platforms/suspend.c`) drops that key's press and its
release, so a lock screen or text field never receives the key that woke it. The
key types again on its next press. Bus Suspend seen before enumeration is not a
sleeping host, so a key held at power-on still reaches the host once it
configures the device. Retained reports are those accepted before Suspend, such
as the release of a key held into it.

Remote Wake must remain fail-closed around real bus state: revalidate software
Suspend, host permission, the current transport/suspend epoch and hardware
`DSTS.SUSPSTS`; wait at least 5 ms after Suspend; isolate only
`GINTMSK.WUIM`; use the ST HAL STOPCLK ungate; assert, verify and explicitly
deassert RWUSIG after the 10 ms signal. Do not add a GATECLK write or an unrelated
recovery sequence. The WUIM isolation is required because H7RS can raise
device-driven WKUINT while hardware still reports Suspend. At pulse end, discard
a pending WKUINT only if hardware still reports Suspend; if hardware has resumed,
preserve it for normal HAL processing. Restore WUIM after the attempt.

Physical bus Suspend and ST USBD logical Suspend have separate owners. The PCD
bridge owns the cached physical bus state; Reset, a genuinely active SOF, or a
genuine Resume clears it independently of HID Remote-Wake state. Logical
fresh-SOF fallback is allowed only for an outstanding wake attempt after
hardware is active, and it may complete `USBD_LL_Resume()` exactly once.
Pre-signal/stale SOF, SOF while `SUSPSTS` remains set, and a late WKUINT after
SOF recovery must not create a false or duplicate logical Resume.

### Generation, control, and hardware guards

Configuration/reset starts a new transport generation. Old queued work,
responses and report-delay state do not cross that boundary; current stable
key/button/usage state may be reconciled, but disconnected typing is not replayed
as event history. Endpoint/class teardown must quiesce old ownership before
reuse. Class storage must be bounded and reusable across repeated configurations;
configuration churn must not consume cumulative allocation. Class teardown must
not flush the shared RX FIFO, and control endpoint lifecycle remains core-owned.

VIA OUT uses backpressure: when receive capacity is full, stop rearming so the
host sees NAK rather than ACK-and-drop. Dispatch requires response capacity.
Reset may discard old-generation queued work and responses, but generation
checks are not rollback for side effects of a command already admitted to
dispatch. `docs/contract_via.md` owns the wire request/reply rules.

Keyboard SET_REPORT accepts only the keyboard interface's report-id-zero,
one-byte LED Output report. Validate request shape before arming EP0 receive, and
do not apply an actual payload of another length. The receive buffer must still
cover a full EP0 packet because HAL may round the physical receive size; a later
SETUP invalidates any pending LED receive.

USB FIFO allocation stays within the hardware bound enforced by source and its
compile-time guard; optional CDC capacity remains accounted for there rather
than duplicated here. ST ES0596 Rev 10 sections 2.2.17 and 2.21.3 remain the
source requirements for the local GFXMMU Device/XN guard in `src/bsp/bsp.c`
and the IN zero-length-packet sequencing in
`src/lib/ST/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_ll_usb.c`, including EP0.
These mitigations still require real-silicon validation; they are not evidence
that either erratum caused a historical symptom.

## 4. Automatic USB recovery is retired — do not restore it

The instability monitor and automatic polling downgrade are retired product
behavior, not missing work. `tools/era_doc_refs.py` owns the executable
`retired` symbol guard rather than this contract duplicating its symbol list.

Two independent reasons prohibit restoration:

- **User authority:** firmware may expose observation, but it must not produce a
  synthetic stability verdict or change polling mode on its own. Mode selection
  and Apply remain explicit user actions under §5.
- **Unresolved safety risk:** the former monitor-enabled path produced an
  unexplained whole-keyboard hang even when its runtime toggle was off. The
  cause was never identified, so restoring that path would reintroduce an
  unresolved failure mechanism.

Channel 13 value id 3 from the retired monitor remains reserved and must not be
reused. Its EEPROM slot remains `EECONFIG_USER_RESERVED_32` so later USER slot
addresses do not move; nothing should read or write that reserved value. Storage
ownership is `docs/contract_eeprom.md` §1.

Selector `0x07` is observation-only. Its wire envelope is
`docs/contract_via.md` §6. It must not apply/reset polling mode, write
diagnostic history to EEPROM, become State Sync recovery, or emit a synthetic
stability score. Session/counter storage is RAM-only; CLEAR zeros session state
only and must not clear boot counters. RGB Sleep may use
SOF activity to detect host loss, but that observation is not a stability score
and must not invoke polling apply/reset.

If more observation is required, extend the coordinated read-only diagnostics
protocol. Do not restore automatic benchmarking, scoring, downgrade, or recovery
control.

## 5. Polling mode is user-owned

The user-visible mapping is compatibility behavior:

| VIA selection | Mode | Link / interval |
| ---: | --- | --- |
| 3 | FS 1 kHz | Full Speed, 1 ms |
| 2 | HS 2 kHz | High Speed, `bInterval=3` |
| 1 | HS 4 kHz | High Speed, `bInterval=2` |
| 0 | HS 8 kHz | High Speed, `bInterval=1` |

The default is FS 1 kHz. Enum/storage details are source-owned; channel/value ids
are wire ABI under `docs/contract_via.md`.

Selecting a mode changes only the pending user choice. Apply is a distinct
explicit action: it persists the selected mode and schedules USB teardown/MCU
reset after the response and pending EEPROM work are allowed to complete.
Applying the already active mode still requests that reset. Custom SAVE on this
control is a no-op; persistence belongs to Apply. The CLI boot-mode setter is
also an explicit user path with the same persist-and-reset boundary.

No diagnostic, monitor, State Sync or ordinary USB service path may call polling
apply/reset on the user's behalf. Apply/reboot ends the current enumeration, so
an in-flight diagnostic session cannot continue on it; host behavior across that
boundary is owned by `docs/contract_via.md` §6 and
`the-via-eerraa/docs/adr/0002-h7s-usb-diagnostics.md`.

## 6. Bootloader-to-firmware handoff

The current peer bootloader contract uses MCU reset after UF2 completion/eject so
the firmware normally receives reset USB blocks; see
`eerraa-qmk-h7s-boot/docs/uf2_auto_start.md` §3. Firmware must nevertheless
retain compatibility with already shipped legacy bootloaders that can jump with
TinyUSB's OTG core/USBPHYC state still inherited.

For that legacy handoff, recovery must occur before normal HAL USB
initialization: when inherited TinyUSB ownership is detected,
`src/hw/driver/usb/usbd_conf.c` / `HAL_PCD_MspInit()` restores the OTG core
and USBPHYC to a clean reset state, clears stale interrupt ownership and provides
the host-visible detach interval before normal initialization continues. Current
detection and timing values are source-owned.

The current reset-handoff bootloader and the firmware compatibility branch must
coexist: reset-state handoff must pass normally, while a legacy armed handoff
must be normalized by firmware. Do not restore the rejected post-`HAL_PCD_Init()`
boot-time detach hold as the compatibility fix; the legacy failure occurs inside
`HAL_PCD_Init()`, before such a hold could run.

Remaining board-level acceptance is tracked only in `docs/state_open.md` §2.
Document/static review is not a substitute for unrun HIL.

## 7. Main-loop periodic work must not depend on a stale 16-bit expiry cache

USB service, QMK/RGB work and persistence share the main loop. A periodic task
that may sit idle beyond the 16-bit timer comparison window must not skip its
state/effect lookup solely because a cached 16-bit deadline has not "expired".
That pattern can leave stale effect/interval state and freeze rendering.

`rgblight_timer_task()` in
`src/ap/modules/qmk/quantum/rgblight/rgblight.c` must therefore keep current
state lookup outside that stale-cache gate. A future cache is acceptable only
with wrap-safe 32-bit expiry and complete invalidation for the state that affects
the deadline. The long-idle regression in `tools/firmware_regression_tests/`
owns the executable guard.

Diagnostic deadlines use their separate 32-bit wrap-safe rule; selector
semantics remain `docs/contract_via.md` §6.

## 8. Reactive RGB input belongs to physical switch transitions

Reactive pulse/Velocikey input is owned by debounced, ghost-filtered physical
matrix transitions in `src/ap/modules/qmk/quantum/keyboard.c`, before QMK
action filtering or tapping replay. Logical TD/LT/action replay must not create a
second physical RGB press/release.

The input path records bounded state/timestamps only. Colour calculation, LED
buffer traversal and frame submission belong to the RGB task. A pending
physical/configuration/indicator evaluation may make that task run promptly, but
it must not add a blocking wait.

Configuration producers must commit `rgblight_config` before requesting pulse
reevaluation. Do not compute/render pulse output inside
`rgblight_sethsv_eeprom_helper()` or another pre-commit setter; doing so can
render the previous request's values. Colour-only changes preserve the current
physical latch and held keys; a base-mode change may reset them. The host Caps indicator is
an independent overlay and its release must restore the committed base output.

Hold variants last while any physical key pressed in that mode is still down,
tracked per matrix position; the release of a key pressed before a mode change
or reset must not end a newer hold. Selecting a mode (including the same mode
again, and RGB being re-enabled), RGB OFF, and output Sleep entry or exit each
retire the latch and tracked keys, and a press while output is suspended is not
admitted. EERRAA draws the same boundaries, so the two products pulse alike.
Pulse expiry is owned by the RGB task's periodic gate rather than
animation-interval quantization. The integrated
physical-RGB fixture under `tools/firmware_regression_tests/` owns the
executable TD/LT, commit-order, hold, overlay and wrap regression cases. This
physical-input rule does not collapse TD/LT logical semantics. Host LED timing
and frame coalescing may affect visibility; no minimum visible frame count is
guaranteed.