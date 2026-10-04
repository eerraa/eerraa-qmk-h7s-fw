# USB host contract

Genre: contract
Canonical for: host-visible HID shape, the 20-key report and its Boot-protocol
form, report/lifecycle ownership, user-owned polling mode, the retired
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
| 0 keyboard | IN `0x81` | 64 B | BOOT / Keyboard | 22 B keyboard IN, 8 B in Boot protocol (§3); LED Output is a one-byte SET_REPORT on EP0 |
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

The shipped keyboard report is mods + reserved + 20 key slots: 22 bytes, sent
in Report protocol.
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

Physical plain-key updates have a separate, explicit opt-in admission path.
Within one matrix scan, compatible distinct presses or compatible distinct
releases may share a logical candidate only while the keyboard endpoint is
already busy and no immutable report is queued behind it. A single update on
an idle endpoint is submitted immediately. Completion may freeze and arm even
one candidate update in the middle of that scan; scan end, key count and SOF
must never delay its first eligible service opportunity. A frozen report joins
the ordinary immutable FIFO and cannot be edited or bypassed.

Provenance and action eligibility belong to the physical/QMK producer. Synthetic
or deferred tapping events, repeated usages, modifiers, locking keys, SOCD
pairs, direction changes, a different scan, report intervals and actions that
produce no report close the candidate. Builds with combo buffering disable this
opt-in path until deferred combo records have explicit provenance retirement.
Boot protocol is excluded because its
six-slot projection can expose an otherwise hidden key between two releases.
The transport also checks that the supplied snapshot is exactly the declared
single-slot update before combining it. A protocol change freezes the candidate;
Suspend keeps it in the same session and reset retires its admission with the
rest of that session. These logical updates do not count as dropped reports.

Preparing a packet must not wait for the next microframe or additional input.
The keyboard's non-DMA single-packet path fills available TX FIFO space in the
arming call; insufficient space keeps the existing TXFE refill path without
polling. Filling the FIFO does not complete a report or release its payload.
Host IN/ACK scheduling remains external to firmware; SOF alone is not a
delivery guarantee.

Finite overflow is explicit. Preserve the already accepted prefix, then converge
keyboard/button/system/consumer state to the newest state after the prefix
drains. Intermediate events may be coalesced. Relative mouse motion/wheel deltas
must not be replayed during reconciliation. Local transport counters retain
their meanings; the retired diagnostic wire aggregate is no longer exposed.

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
it. Keep the USB pump free of dynamic allocation and keycode interpretation;
its payload work is bounded snapshot copying, including freezing a candidate
and making the Boot-form copy when a packet is armed. It must not execute QMK
actions or wait for a scan to finish.

### Shared output contributions and feature lifetimes

Ordinary QMK output (including unscoped firmware output), Tap Dance seats and
macro lifetimes contribute through one common HID ownership layer. Selecting and
restoring a scope accepts the same token domain; clearing another owner preserves
the caller's scope. Ordinary keys/modifiers/mouse usages retain bit-state semantics,
not physical-switch reference counts. Tap Dance alone owns its callback/input
lifetime and momentary-layer policy; the common HID layer has no TD action types
or layer lifetime. Macro execution and cancellation own their publication policy.

System and consumer reports each carry one usage. A DOWN selects that owner's
latest usage. A matching UP removes only its contribution, keeps the selected
usage if any owner still holds it, otherwise selects the ordinary contribution,
then the lowest surviving owner token. Normal release, owner cleanup and session
reconciliation use this same projection. This cannot report two different usages
simultaneously or remember multiple usages within one owner/page.

Report-only key clearing does not cancel input ownership. Ordinary real-modifier
clear clears only ordinary real modifiers; weak-modifier clear clears all weak contributions,
as required by QMK action processing. Full keyboard clear cancels feature lifetimes
before resetting ordinary state. Successful QMK initialization also retires macro
execution before timers are reinitialized; failed EEPROM guards stop beforehand.
Session reconciliation must force an unchanged
surviving keyboard union through the ordinary report filter and deduplication cache.

### Nonblocking dynamic macros

Dynamic VIA macros execute serially from a bounded FIFO of macro IDs. A full
queue rejects the newest request and preserves accepted order. Input dispatch only
enqueues an ID. The task bounds snapshot activation separately from execution
phases, so consecutive empty or invalid macros cannot multiply the large copy and
selection cost within one main-loop pass. Each macro takes
an immutable byte snapshot when it starts; later queued IDs use the contents
current at their own start. Edits cannot change a running macro halfway through.
The cooperative executor returns to matrix, VIA, RGB and storage service between
steps and deadlines; it does not recurse into QMK or wait through a macro delay.
Command order, character timing and transport-owned keyboard intervals remain.

Synthetic output has separate ownership from ordinary and Tap Dance output.
Explicit DOWN survives normal macro completion until its UP; temporary character
modifiers are released on completion or malformed-input abort. Explicit DOWN also
survives malformed abort until its UP or feature cancellation. A release must
not remove the same usage or modifier held by another owner. Explicit keyboard
clear and a retired USB session cancel active and queued macros and release
macro contributions. Cancellation republishes mouse buttons with zero relative
movement and wheel deltas; it does not replay old motion or consume the next
ordinary mouse-task interval. Same-session Suspend pauses execution with remaining delay
preserved, and Resume continues it.

Reports containing macro contributions carry the admitted USB generation into
transport enqueue. Equal report bytes do not justify suppressing a change between
ordinary and generation-bound provenance. Host-side deduplication tracks the last
submitted context; it is not a transport acceptance receipt. The existing IRQ lock
checks the generation before queue, candidate or
latest-state mutation. A new session neutralizes cached synthetic snapshots;
the main loop cancels old macro ownership and republishes surviving physical/TD
state, including unchanged unions. This prevents old macro output from crossing
reconnect even when Reset occurs between executor observation and report submit.
No QMK action runs in the USB ISR, and no wire command is added.

### Boot protocol report

Interface 0 advertises BOOT/Keyboard, and SET_PROTOCOL on it selects the report
form with no user toggle. Report protocol, the state after every configuration,
sends the 22-byte report of §2. Boot protocol sends the 8-byte boot report of
HID 1.11 Appendix B.1: mods, reserved, then the first six non-empty key slots of
the 22-byte report in slot order. A seventh held key is left out rather than
turning the report into ErrorRollOver, and appears once one of the six is
released. The report descriptor does not change.

The form is chosen when a packet is armed. Reports still queued at a protocol
change leave in the new form, the armed packet stays unchanged until its
completion, and the current keyboard state is queued once more in the new form
because a host discards key state across the change. SET_PROTOCOL must be a
class/interface request with wLength 0 and value 0 or 1; anything else stalls.
On another interface it is acknowledged without effect and GET_PROTOCOL there
answers Report protocol: EXK has no boot form.

A host that reads boot-format packets without sending SET_PROTOCOL still
receives the 22-byte form. Changing that, or the six-key choice above, requires
BIOS/UEFI and supported-host acceptance.

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
genuine Resume clears it independently of HID Remote-Wake state. Host-driven
Resume must undo any STOPCLK gate before checking hardware activity; an observed
bus Reset also releases STOPCLK. Suspend clock policy alone does not establish
that a gated controller detects every host reset. Its electrical and reset
acceptance remain hardware checks in `docs/state_open.md`.
The PCD bridge must reconcile hardware-active Resume before forwarding SETUP
or IN/OUT completion: HAL may dispatch endpoint events before WKUINT, and the
USBD core otherwise discards class completions while logically suspended.
Logical fresh-SOF completion is allowed only for an outstanding device wake
attempt or a deferred PCD Resume indication, after hardware is active, and it
may complete `USBD_LL_Resume()` exactly once. Once raw bus Reset is observed,
these Resume paths and class callbacks must not revive the retired session while
enumeration completion has not yet delivered the stack Reset. A PCD Resume
indication observed while `SUSPSTS` is still set must survive until fresh active
bus activity;
Suspend, Reset and Disconnect invalidate that indication. This completes an
observed USB lifecycle event, without resetting transport or changing polling.
Pre-signal/stale SOF, SOF while `SUSPSTS` remains set, and a late WKUINT after
SOF completion must not create a false or duplicate logical Resume. Hardware-
active SOF alone, without either wake indication, must not resume the stack.

### Generation, control, and hardware guards

Configuration/reset starts a new transport generation. Old queued work,
responses and report-delay state do not cross that boundary; current stable
key/button/usage state may be reconciled, but disconnected typing is not replayed
as event history. Endpoint/class teardown must quiesce old ownership before
reuse. Class storage must be bounded and reusable across repeated configurations;
configuration churn must not consume cumulative allocation. A failed endpoint
stop/close/flush does not authorize transfer-descriptor or payload reuse. Retain
failed ownership, invalidate old software admission and wake/control epochs,
and reject a new class configuration until a later explicit lifecycle teardown
successfully releases it. Retain its interface callbacks as well as payloads so
that explicit cleanup remains possible. Do not silently retry teardown from
class Init: a SET_CONFIGURATION retry alone need not recover a retained owner.
A successful later Reset or Disconnect teardown can release it.
SET_CONFIGURATION must not acknowledge failed teardown as success or advertise
a configuration whose initialization failed; Stop/DeInit must preserve errors
without skipping their remaining teardown calls. Composite initialization must
stop at the first failed class and roll back only earlier successful classes;
the failed class owns its partial cleanup. Class teardown must not flush
the shared RX FIFO, and control endpoint lifecycle remains core-owned.
Hardware teardown is owned by HAL PCD Close/Abort, not by a class-specific
bridge sequence. Close must finish stop and any IN FIFO flush before endpoint
deactivation, interrupt retirement or IN descriptor clearing. Abort quiesces the
transfer but retains its descriptor for the caller. Both serialize the whole
operation with the PCD lock and restore the caller's interrupt mask.
Class Close rejects EP0; OUT EP0 cannot be disabled by hardware. Reset/control
handling continues to own that endpoint.

Follow [RM0477 Rev 9](https://www.stmcu.jp/wp/wp-content/uploads/2024/03/RM0477_Rev9.pdf)
§62.15.6, pp. 3196–3197 and 3205: establish Global OUT NAK before OUT disable;
stop IN FIFO refill, establish non-isochronous IN NAK, and wait for a fresh
endpoint-disabled indication. An early EPENA clear is not a substitute for an
outstanding disable's completion. Direct LL deactivation must reject an enabled
endpoint or a pending disable instead of initiating another stop.

In slave mode, OUT teardown processes queued receive statuses through the same
receive path as the IRQ, preserving SETUP and other-endpoint payloads. This also
applies when EPENA was already clear: the completed transfer's payload may still
be queued. NAK timeout must not proceed to disable or release ownership. Restore
interrupt masks and release only Global OUT NAK acquired by this operation.

Raw bus Reset and enumeration completion are separate boundaries. At the first
software observation of Reset, retire old HID admission and wake/control epochs
without clearing controller-owned payloads or transfer descriptors. Block stack
SETUP/completion/Resume forwarding until the normal enumeration-completion Reset
has run. Observe Reset both before HAL dispatch and at its raw-reset branch so a
flag consumed within the same IRQ cannot bypass this boundary. This software
barrier does not establish hardware quiescence or authorize changing controller
reset order. A SETUP discarded before the stack Reset is not replayed afterward.

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

Standard endpoint requests must reject reserved address bits and unused
endpoint directions before indexing endpoint state or touching the controller.
Control IN response storage must outlive SETUP handling until the controller
has consumed it. USB lifecycle IRQ callbacks must not enter the shared,
non-reentrant application logger. Endpoint and device-stop FIFO flush failures must
remain visible to callers instead of being reported as success.

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

User diagnostics and their dedicated instrumentation are also retired. Selector
`0x07` stays reserved; the replacement is the last-read keyboard IN setting TEXT
under `docs/contract_via.md` §6. Keep real report spacing, ownership/generation
barriers and internal transport counters. Do not retain unused session collectors
or timers as a hidden replacement. RGB Sleep may observe SOF for host loss, but
must not score stability or invoke polling apply/reset.

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
BootMode Apply keeps the main loop and USB service running for at least 500 ms
before teardown, allowing host follow-up requests to finish. EEPROM durability
and live response drain remain required after that deadline. Keep the detached
wait at 100 ms before MCU reset; other reset callers retain their own grace.
This is a host-compatibility grace, not acknowledgement that the browser has
finished processing every request.
Only replies in a live transport generation count toward response drain; buffers
retained after that generation retires must not indefinitely block an already
requested reset. Keep the EEPROM durability barrier and do not schedule a reset
merely because ownership retired.
Applying the already active mode still requests that reset. Custom SAVE on this
control is a no-op; persistence belongs to Apply. The CLI boot-mode setter is
also an explicit user path with the same persist-and-reset boundary.

No monitor, State Sync or ordinary USB service path may call polling apply/reset
on the user's behalf. Apply/reboot ends the current enumeration. A new TEXT
value requires a successful read in the successor connection; the old snapshot
is not proof of the new setting. Host refresh ownership is `docs/contract_via.md` §6.

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
