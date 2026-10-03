# VIA wire contract

Genre: contract
Canonical for: official VIA compatibility, exact-value encoding, State Sync revision semantics, the single raw-HID TX producer, selector `0x06`/`0x07` wire rules, and firmware/app responsibility boundaries

Implementation-owned inventories are not repeated here. Current command/channel/value ids are in `src/ap/modules/qmk/quantum/via.h`; dispatch is in `src/ap/modules/qmk/quantum/via.c` and `<board>/port/via_port.c`; official VIA definitions are `src/ap/modules/qmk/keyboards/era/**/json/*-VIA.JSON`; the firmware version is `_DEF_FIRMWARE_VERSION` in `src/hw/hw_def.h`.

The app-side owners are `the-via-eerraa/docs/adr/0001-state-sync-protocol.md` for State Sync/exact-ms and `the-via-eerraa/docs/adr/0002-h7s-usb-diagnostics.md` for the retired diagnostics migration. Local document checks prove only this repository; a peer or remote revision is not verified merely because these pointers exist.

## 1. Official VIA compatibility and shipped ids

Stock VIA with the firmware-local definitions keeps basic configuration usable. Features it cannot support correctly, or that need complicated explanations or awkward UI, are reduced in official JSON and supported in the Custom app at usekb.cc. usevia.txt documents this boundary. Preserve existing wire meanings; equal UI exposure is not required.

Shipped channel/value meanings are wire ABI. Do not renumber or reuse a shipped id to compact a hole. VIA-reserved channels 1, 3, 4, and 5 remain reserved; retired channel 13 value 3 remains reserved by `docs/contract_usb.md` §4. New features take unused additive ids. Review firmware and both definition surfaces in the same compatibility change, using the stock usability policy above. Advanced Tap Dance editing (§3) and polling observations (§6) belong to the Custom app; official JSON retains the basic path without changing the Custom Value wire ABI. Use `via.h` and the JSON as the current id inventory rather than copying the full list here.

GET keyboard-value selectors `0x06` (§5) and `0x07` (§6) are ERA additions in an id range QMK extends in order. QMK VIA protocol 13 (QMK release 2026-05-31) gives `0x06` to QMK's id_keycodes_version, which official VIA reads from protocol-13 boards before remapping. Keep `VIA_PROTOCOL_VERSION` at `0x000C` until both selectors move to new ids together with the app (`the-via-eerraa/docs/adr/0001-state-sync-protocol.md`); shipped firmware keeps its ids.

The local `menu` check verifies that firmware-routed channels remain reachable from official JSON. The `tapdance` check verifies the official editing boundary for every slot and mode byte. Neither verifies the peer app overlay or a live official-browser session.

Official menu names are shared with EERRAA: lighting sits under `Lighting` in submenus named for where the LEDs are (`Underglow`, `RGB Row`, never an engine name), and lock indication under `Indicators` with `Indicator` / `Indicator N` dropdowns whose value 0 reads `RGB Effect` where the indicator shares the lighting LEDs (INTIGRITY80, BRICK60) and `Off` on a dedicated LED. Labels are not wire ABI, but a rename must reach every official JSON, `docs/usevia.txt` and the app definition together.

### VERSION

The shipped legacy Year/Month/Day/Revision values keep their zero-based meaning. The additive read-only VERSION string returns `_DEF_FIRMWARE_VERSION` without the leading `V`, followed by NUL (`YYMMDDRn\0`). SET and SAVE do not alter it. Firmware behavior is owned by `src/ap/modules/qmk/port/ver_port.c`; app presentation is owned by `the-via-eerraa/docs/adr/0003-era-menu-help-ui.md`.

### Non-obvious custom-control semantics

- RGB Sleep has one shared uint16-second timeout, default 600 seconds, and one shared master enable. Official VIA exposes channel 18 values 1 (minute presets) and 3 (master), not exact value 2; Custom VIA may use value 2. OFF preserves the timeout and SAVE owns persistence. The master gates input-idle sleep, explicit USB Suspend, and host-loss sleep after SOF has been stale for 300 ms once a host was seen. Sleep is a physical-output gate and must not rewrite the user's RGB enable, effect, hue, saturation, or value.
- Pulse controls retain the 5..260 ms setting range (5 ms + the stored one-byte speed) and the physical-press deadline and Hold semantics: a Hold effect lasts while any key pressed in that effect is still down. A Pulse onset which is not covered by an indicator must not be coalesced away before transmission: it remains protected until a complete output frame has been observed and at least 5 ms of wire dwell has elapsed. This minimum may extend a short logical deadline; it is not a promise to end exactly 5 ms after the physical press. Active indicators, output Sleep and RGB OFF take priority and cancel that protection; output Sleep and RGB OFF also retire the physical latch and tracked keys, and masked historical pulses are not replayed. A same-effect colour/brightness commit preserves the physical latch. Host LED bits are authoritative USB feedback; layer reevaluation must not republish an older sampled value. No new VIA id, value encoding or EEPROM layout is introduced by this output contract.
- Entering a hue-driven RGB effect from a different base effect while stored saturation is zero restores saturation to 255. Switching variants of the same effect or any nonzero saturation leaves the stored colour unchanged. `rgblight_mode_transition_sat()` owns that implementation.
- KKUK normalizes enable and clamps Delay to 5..30 ticks and Repeat to 5..20 ticks on load and live SET. A semantic live change starts a fresh tracking epoch; keys already held before the change are not retroactively counted, a release uncounts only a press that was counted at the same matrix position, and repeat elapsed time is measured from a current timestamp. Active, non-overlapping SOCD pairs are excluded both from that count and from the release pulse: their raw held usages stay in the report before the SOCD filter selects its winner. Disabled or invalid pairs have no exemption. Modifiers are never cleared; release and restore are sent in the same KKUK task.
- SOCD (kill switch) resolves on the report the host receives, whoever put each usage there (physical key, Tap Dance, macro): no sent report carries both usages of an eligible pair. The stored mode byte, value 4 on channels 10/11, is 1 Last Input, 2 Neutral, 3 First Input, 4 key-0 priority, 5 key-1 priority, the same values as EERRAA; an unknown mode leaves the setting unchanged. A pair is eligible only with distinct reportable basic/modifier usages; overlapping enabled pairs are inert. A semantic live change restarts the pair and sends the resolved report at once, so a usage the old setting hid returns without input; two keys already held with no known order are both withheld until one is pressed again. Unsupported retained 16-bit keycodes may round-trip through storage/UI but must never be truncated into reportable usages.

## 2. Single raw-HID TX producer

VIA request handling has one response producer. `raw_hid_receive()` fills the 32-byte response buffer; `raw_hid_send()` in `src/ap/modules/qmk/port/via_hid.c` remains a no-op; `via_hid_task()` alone enqueues the completed response through `usbHidEnqueueViaResponse()`.

A second producer would allow duplicate/mixed responses on the one VIA IN endpoint and break the host's serialized request/reply pairing. Do not make `raw_hid_send()` live while `via_hid_task()` owns enqueue.

Only complete 32-byte VIA OUT frames are admitted. Keymap/macro buffer commands reject a payload size greater than 28 before reading or writing the frame; invalid sizes become unhandled with no side effects. Main-loop dispatch handles at most one admitted request per iteration when response credit exists, and a full RX queue uses USB NAK instead of ACK-and-drop. Reset may discard old-generation queued work/responses but cannot roll back side effects of a command already admitted to dispatch. Queue/lifecycle mechanics are owned by `docs/contract_usb.md`.

`tools/era_via_host_tests/check_single_producer.py` checks the single-producer rule; `tools/era_via_host_tests/check_via_transport_latency.py` checks the transport scheduling invariant.

## 3. exact-ms / exact-sec

Exact Custom Value SET/GET uses the existing `id_custom_set_value` (`0x07`) / `id_custom_get_value` (`0x08`) commands. The exact value is a two-byte big-endian uint16 after command/channel/value-id.

### TAPPING and Tap Dance exact milliseconds

H7S exact-ms addresses are global TAPPING channel 15 value 5 and Tap Dance channel 16 values 41..48. SET accepts 1..65535 ms inclusive. Zero is invalid. Fewer than two value bytes (`length < 5`) or an out-of-range value is refused and leaves storage unchanged. Exact GET returns the stored uint16 without snapping.

Official `*-VIA.JSON` definitions keep the legacy one-byte ×10 ms controls. Legacy SET snaps onto the 100..500 / 20 ms grid; legacy GET projects the exact stored value onto that grid without writing storage. An official-VIA read therefore must not destroy a custom-app value such as 137 or 65535 ms. Tap Dance slot terms remain independent of global `TAPPING_TERM`.

`src/ap/modules/qmk/port/tapping_term_policy.h` owns the shared exact validity and legacy projection; `tapping_term.c` and `tapdance.c` own the handlers. Wire and EEPROM terms remain uint16 with unchanged IDs, offsets, signatures and versions. Runtime key events retain a 32-bit captured timestamp through the tapping queue; elapsed comparisons and Tap Dance deadlines must represent intervals greater than the maximum term. Legacy GET is read-only even for values outside its display range; only explicit Legacy SET normalizes the stored value. SAVE/reload must preserve every valid exact value. App encoding and definition bounds are owned by `the-via-eerraa/docs/adr/0001-state-sync-protocol.md`.

### Tap Dance input modes

The QMK and H7S support policy is shared with
`the-via-eerraa/docs/PROJECT_DIRECTION.md` **Tap Dance and exact-ms**: official
VIA edits basic Legacy slots; the custom app owns advanced modes and independent
timing under the stock usability policy in §1. Restricting advanced slots prevents a basic
editor from changing actions whose inheritance, explicit silence and timing it
cannot explain or edit completely.

H7S channel 16 values 49..56 select the input mode for TD0..TD7 through
Custom Value GET/SET/SAVE. Payload byte 0 is 0 legacy, 1 after-decision, or
2 on-press. Missing payloads and other values are refused without mutation.
GET/SET echo appends `0xD2` in byte 1 when the buffer has room. A definition
alone is not support evidence: older firmware's unmarked zero reply must keep
the old editor. The four action and term IDs keep their encodings.

Official V3 TAPDANCE menus edit only mode-0 (Legacy) slots: the four actions and
legacy Term presets. Modes 1/2 are edited in the custom app at `https://usekb.cc`;
official JSON shows only "Advanced settings: Visit usekb.cc"
without editable action or timing controls. Empty mode-0 slots remain editable.
Unknown mode values show the same guidance and no editable controls.
A hidden read-only label binding (`showIf: "0"`) retains each existing mode GET
as the display-condition source; it is not a rendered ASCII label or a new wire
command. Constant-false display conditions must not prune this GET. Static labels
consume no commands. Mode SET, advanced timing controls, and automatic conversion
are absent from the official UI; firmware support and stored settings remain.
Opening a definition must never reset a slot. The website guidance is plain text,
not a promise of a clickable link or an app-side Legacy conversion feature.

Mode 0 keeps the existing Tap Dance rules, including empty-action fallback.
In modes 1/2, On Tap is the base key. `KC_TRNS` in an additional action means
inherit; `KC_NO` means explicitly send nothing. Hold inherits the base key;
Double Tap inherits two base taps; Tap+Hold inherits a base tap followed by
Hold (or the base key). Each completed pair starts a fresh gesture.

Mode 1 waits only while a different gesture is possible. Without any additional
action it behaves as a normal physical base key. Mode 2 immediately emits and
releases the first base key, including a long first hold. Only the second press
waits: short release chooses Double Tap or one more base tap; expiration while
held chooses Tap+Hold or the base key until release. The initial base input
cannot be suppressed by a later action. Mode 2 requires Hold = `KC_TRNS`;
conflicting mode/action SETs are refused without mutation. Other-key interruption
chooses the base tap path. The existing strict `elapsed > term` boundary and
current per-slot term policy remain. Actions and mode are captured on the first
press so a settings edit cannot change the gesture's output or release owner.

### Tap Dance independent timing

Channel 16 values 57..64 hold TD0..TD7's separate hold time, BE16 0..65535;
zero follows the original term. Values 65..72 enable hold-on-other-key, byte
0/1 only. Hold-time GET/SET echo has marker `0xD3` in value byte 2; the flag
has `0xD3` in byte 1. Mode GET/SET echo retains byte 1 `0xD2` and advertises
these extra controls with byte 2 `0xD3`. Old firmware is never probed for the
new values without that capability. These controls are edited in the custom app;
official definitions omit their bindings and do not query the advanced timing
values. Invalid and short SETs never mutate settings.

Modes 1/2 capture both thresholds and the flag with the first press. Released
dances use the existing term, measured from the preceding press; held dances
use the separate hold time (or the captured original term when zero). With
hold-on-other enabled, an assigned first hold in mode 1 or second hold in
modes 1/2 resolves before the other key's source-layer lookup. Explicit silence
counts as assigned. Mode 2's first press remains the physical base input.
Default flag 0 retains interruption-as-tap. Mode 0 ignores these extra settings.
A first hold already resolved by its deadline ends that consecutive-press gesture.
The second physical press never adds a speculative base input before a hold.

## 4. State Sync revision meaning

Selector `0x06` publishes three RAM uint32 equality tokens: KEYMAP, MACRO, and CONFIG. They start at 1 and skip 0 on wrap. They are invalidation tokens, not data values and not EEPROM addresses.

- KEYMAP advances after changed keymap/encoder bytes have verified physical receipts. MACRO advances only after the completion marker has a verified receipt; opener/payload staging and unchanged retries do not advance it. These are durable invalidations, not per-command counters. Failed or pending storage must not publish completed MACRO.
- CONFIG custom setters bump only when the value observable by GET changes. A same-value custom SET is a no-op and must not bump.
- Layout-options write bumps CONFIG. A checked EEPROM reset publishes affected durable KEYMAP/MACRO changes and bumps CONFIG only after successful completion.
- Custom SAVE schedules persistence only and does not itself bump. The read-only polling TEXT does not bump. VIA-core RGB state and read-only version/system paths are outside this CONFIG revision contract.

`src/ap/modules/qmk/port/era_state_sync.c` owns token storage/advance. Find mutation sites from `era_state_sync_bump_keymap()`, `era_state_sync_bump_macro()`, and `era_state_sync_bump_config()` in current source; this document does not maintain a handler inventory.

The app treats revision inequality only as invalidation and re-reads authoritative values through existing VIA GET. Revision-bracketing, candidate commit, capability opt-in, and cache continuity are app-owned in `the-via-eerraa/docs/adr/0001-state-sync-protocol.md`.

## 5. selector `0x06` — State Sync v1

`id_get_keyboard_value` (`0x02`) + selector `0x06`; envelope version `0x01`; all multibyte integers are big-endian. SET is not routed for this selector. The payload is exactly 32 bytes.

### Request

| Byte | Meaning |
| ---: | --- |
| `0` | GET keyboard value `0x02` |
| `1` | selector `0x06` |
| `2` | requested envelope version `0x01` |
| `3` | `0` |
| `4..5` | host request tag, BE16 |
| `6..31` | `0` |

### Response

| Byte | Meaning |
| ---: | --- |
| `0` | `0x02` |
| `1` | `0x06` |
| `2` | firmware envelope version `0x01` |
| `3` | status: OK `0x00`, unsupported version `0x01`, invalid `0x02` |
| `4..5` | echoed host tag, BE16 |
| `6` | domain mask; OK is KEYMAP `0x01` \| MACRO `0x02` \| CONFIG `0x04` = `0x07` |
| `7` | `0` |
| `8..11` | keymap revision, BE32 |
| `12..15` | macro revision, BE32 |
| `16..19` | config revision, BE32 |
| `20..31` | `0` |

Validation order is normative. A version other than `0x01` returns unsupported-version even if reserved bytes are nonzero. With a supported version, any nonzero reserved request byte (`3` or `6..31`) returns invalid. The tag is echoed in both cases; revisions are filled only for OK.

`length < 32` is not an INVALID v1 envelope. The direct handler returns false; raw-HID admission rejects the short frame before dispatch, so there is no selector response. Do not synthesize a v1 error envelope for a short report.

App responsibility: send tagged 32-byte GETs through the per-path serialized transport, match the echoed tag, require the complete v1/domain-mask shape, and use changed tokens only to invalidate/re-read existing VIA values. The authoritative host rules are in `the-via-eerraa/docs/adr/0001-state-sync-protocol.md`.

`src/ap/modules/qmk/port/era_state_sync.c` owns the firmware encoder. `tools/era_via_host_tests/test_era_via_exact_ms.c` covers OK, unsupported version, reserved-byte invalid, tag echo, and short-frame false return.

## 6. Keyboard USB setting TEXT and retired diagnostics

User diagnostics and dedicated firmware instrumentation are removed. The observation
loss (report timing, queue peak/drop sessions, loop stalls and USB event histories)
is accepted; internal transport counters are not a user-facing replacement.
Keyboard-value selector `0x07` remains reserved. GET/SET return `id_unhandled`
by changing only byte 0; selector, tag and all other request bytes are preserved.
This retirement does not change `VIA_PROTOCOL_VERSION` or custom SET command id `0x07`.

Custom channel 13/value 4 is read-only. A 32-byte GET returns a NUL-terminated
ASCII value in bytes 3..31 (29 bytes including NUL); unused bytes are zero.
Only `1000 Hz (FS)`, `2000 Hz (HS)`, `4000 Hz (HS)`, `8000 Hz (HS)` and
`Unavailable` are valid. Short GETs are rejected before writing the payload.
SET has no side effects; channel SAVE retains its no-op meaning. Value 1 is
pending selection, value 2 is Apply, and retired value 3 remains reserved.
Neither GET nor the read-only SET changes CONFIG revision or EEPROM.

The HID owner copies session validity, its actual keyboard IN endpoint's
`is_used`/`bInterval`, and negotiated speed under the existing lock. Conversion
happens after unlocking. Configured and same-session suspended states are valid;
failed initialization/teardown and raw Reset are invalid even if metadata remains.
Pending/saved BootMode, shared descriptors and a different composite class's
pointer are not the source. Existing response-generation checks discard a reply
if its transport session retires after the snapshot.

The TEXT means **keyboard IN interval setting at the last successful read**,
not measured host polling or input latency. A pending choice or Apply request
must not optimistically replace it. Firmware support revision
`VIA_FIRMWARE_VERSION=1` is assigned by the common QMK CMake configuration;
`id_firmware_version` returns that 32-bit value. It is independent of the date
version, VIA protocol revision and EEPROM reset key.

Stock JSON and release packages omit polling TEXT. Keep Boot Polling Mode and
Apply; after reboot and a fresh GET, the dropdown reflects the stored BootMode.
It is not the actual negotiated FS/HS endpoint interval. usevia.txt directs
endpoint observation to usekb.cc. A matching firmware revision does not make
stock TEXT refresh reliable and is not a reason to add it to release packages.

The custom app follow-up must own this observation outside generic CONFIG/menu
caches, scoped to device, connection generation and definition. It must verify
the current connection's support version before GET, use optional handling for
unhandled only, strictly validate the payload, invalidate failed/stale values,
and query on reconnect/screen activation/explicit refresh. Transport errors
retain the existing transport policy. Ordinary CONFIG replacement must not
restore a stale value. No new wire generation or polling loop is required.
The peer is read-only in this firmware change; its existing diagnostic contract
is a migration input, not evidence that the app change is complete.

`tools/firmware_regression_tests/polling_cases.py` checks production command
bodies and all five board routers; `test_usb_polling.h` checks the real HID
accessor. Software fixtures do not prove official VIA refresh after Apply/reboot,
legacy-firmware behavior on that client, or physical endpoint timing.

## 7. MOUSE precision

V261004R1 stores integer report counts and real millisecond ramp durations.
Custom VIA offers one local **Advanced settings** switch at the bottom of MOUSE;
it changes presentation only, never SET/SAVE or drafts. Stock VIA retains the
six basic dropdown controls. A read projects to the nearest preset without
changing stored precision; ties choose the lower preset. Wheel acceleration
projects zero ramp to Off, otherwise the nearest Mild/Strong target count,
ignoring duration for this classification. Choosing a preset writes that preset.

The existing V3 Custom Value channel is QMK 13 / H7S 17. IDs 1–6 keep their
legacy byte payloads. ID 7 is read-only capability: payload `E4 01`. IDs 8–14
carry BE16; GET adds `E4` after the two value bytes:

| ID | Value | Integer range |
| --- | --- | --- |
| 8 | Cursor start report count | 1–127 |
| 9 | Cursor target report count | 1–127 |
| 10 | Cursor ramp ms | 0–65535 |
| 11 | Cursor interval ms | 1–255 |
| 12 | Wheel interval ms | 1–255 |
| 13 | Wheel target report count | 1–127 |
| 14 | Wheel ramp ms | 0–65535 |

Zero ramp uses constant start count (wheel: one step). A smaller target than
start ramps down. Counts are HID report units, not guaranteed screen pixels;
OS pointer/scroll processing still applies. Ramps use elapsed time from the
first held direction, independently for cursor and wheel. Adding an axis does
not restart a ramp; releasing the last direction or clearing starts the next
press fresh. Existing cadence, diagonal correction and acceleration keys remain.

Probe support on the current connection before querying exact fields. Only
unhandled or an all-zero legacy H7S reply means unsupported. Timeout, malformed
and disconnect remain errors. Once advertised, exact fields are required CONFIG
values, subject to the existing device/definition/generation candidate lifetime.
The date version is not a capability. Invalid exact SET is unhandled and leaves
runtime/storage unchanged. SET changes runtime; SAVE acknowledges persistence.
A failed SAVE remains retryable even if a GET already equals the draft.

MOUSE v2 remains 16 bytes with version at byte 10 and signature at byte 12.
V261004R1 changes both global EEPROM reset keys: the first boot from an older
storage identity resets **all** keymaps, macros and settings. Back up first;
backup formats do not necessarily contain every feature. No v1 migration is
performed. Split units must use matching firmware, as required by the existing
storage contract.

## 8. Verification boundary

For document-only changes run `python -X utf8 tools/era_doc_refs.py`. If the document checker changes, also run `python -X utf8 tools/era_doc_refs_selftest.py`; its planted failures are negative fixtures and it restores the tree.

When VIA firmware behavior changes, run `pwsh -NoProfile -File tools/era_via_host_tests/run.ps1` plus only the build/hardware checks required by `docs/manual_verify.md`. App tests in `the-via-eerraa` and remote revisions are separate evidence and must not be reported as checked unless they were actually run against an identified peer revision.
