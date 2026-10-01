# VIA wire contract

Genre: contract
Canonical for: official VIA compatibility, exact-value encoding, State Sync revision semantics, the single raw-HID TX producer, selector `0x06`/`0x07` wire rules, and firmware/app responsibility boundaries

Implementation-owned inventories are not repeated here. Current command/channel/value ids are in `src/ap/modules/qmk/quantum/via.h`; dispatch is in `src/ap/modules/qmk/quantum/via.c` and `<board>/port/via_port.c`; official VIA definitions are `src/ap/modules/qmk/keyboards/era/**/json/*-VIA.JSON`; the firmware version is `_DEF_FIRMWARE_VERSION` in `src/hw/hw_def.h`.

The app-side owners are `the-via-eerraa/docs/adr/0001-state-sync-protocol.md` for State Sync/exact-ms and `the-via-eerraa/docs/adr/0002-h7s-usb-diagnostics.md` for diagnostics. Local document checks prove only this repository; a peer or remote revision is not verified merely because these pointers exist.

## 1. Official VIA compatibility and shipped ids

Official VIA with the firmware-local official definitions must remain usable without the custom app. Custom-app controls are additive; they do not replace or silently reinterpret the legacy path.

Shipped channel/value meanings are wire ABI. Do not renumber or reuse a shipped id to compact a hole. VIA-reserved channels 1, 3, 4, and 5 remain reserved; retired channel 13 value 3 remains reserved by `docs/contract_usb.md` §4. New features take unused additive ids and must be reflected in firmware, every affected official JSON, and the app definition in the same compatibility change. Use `via.h` and the JSON as the current id inventory rather than copying the full list here.

GET keyboard-value selectors `0x06` (§5) and `0x07` (§6) are ERA additions in an id range QMK extends in order. QMK VIA protocol 13 (QMK release 2026-05-31) gives `0x06` to QMK's id_keycodes_version, which official VIA reads from protocol-13 boards before remapping. Keep `VIA_PROTOCOL_VERSION` at `0x000C` until both selectors move to new ids together with the app (`the-via-eerraa/docs/adr/0001-state-sync-protocol.md`); shipped firmware keeps its ids.

The local `menu` check verifies that firmware-routed channels remain reachable from official JSON. It does not verify the peer app overlay.

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

### RGB Sleep exact seconds

Channel 18 value 2 uses the same BE16 Custom Value encoding and accepts 1..65535 seconds inclusive. Zero or `length < 5` is unhandled and leaves storage unchanged. Value 1 remains the official 1/3/5/10/30/60-minute preset; its GET projects exact seconds onto that list without rewriting exact storage. Both setters target the same timeout. Value 3 is the one-byte master; OFF preserves timeout.

The persisted master flag is encoded in the existing storage-version byte so previously shipped version-1 slots migrate as enabled without changing the four-byte slot layout. `src/ap/modules/qmk/port/rgb_sleep.c` owns the implementation; `the-via-eerraa/docs/MAP.md` §3 points to the app exact-sec owner.

`tools/era_via_host_tests/test_era_via_exact_ms.c` and `tools/era_via_host_tests/test_rgb_sleep.c` cover exact bounds, short packets, projection/no-write behavior, persistence/migration, and unknown ids.

## 4. State Sync revision meaning

Selector `0x06` publishes three RAM uint32 equality tokens: KEYMAP, MACRO, and CONFIG. They start at 1 and skip 0 on wrap. They are invalidation tokens, not data values and not EEPROM addresses.

- KEYMAP and MACRO mutation commands bump their domain when the mutation command is accepted; those paths intentionally do not compare old/new payloads first.
- CONFIG custom setters bump only when the value observable by GET changes. A same-value custom SET is a no-op and must not bump.
- Layout-options write bumps CONFIG. EEPROM reset bumps all three domains.
- Custom SAVE schedules persistence only and does not itself bump. Selector `0x07` diagnostics does not bump. VIA-core RGB state and read-only version/system paths are outside this CONFIG revision contract.

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

## 6. selector `0x07` — H7S USB diagnostics v1

Diagnostics uses GET keyboard value `0x02` / SET keyboard value `0x03` + selector `0x07`, protocol version `0x01`, a 32-byte payload, and big-endian multibyte integers. It is request/reply only; firmware must not emit unsolicited diagnostics packets.

This protocol is observation-only with respect to polling policy. START selects only a diagnostic duration; mode selection/apply/reboot stays on the existing BootMode controls. Diagnostics must not automatically downgrade polling, write diagnostic history to EEPROM, reset the device, or become State Sync recovery. `docs/contract_usb.md` §4 owns that product boundary; the app side is `the-via-eerraa/docs/adr/0002-h7s-usb-diagnostics.md`.

### Request

| Byte | Field |
| ---: | --- |
| `0` | GET `0x02` or SET `0x03` |
| `1` | selector `0x07` |
| `2` | protocol `0x01` |
| `3` | operation |
| `4..5` | host tag, BE16 |
| `6` | START duration seconds or SNAPSHOT chunk index; otherwise 0 |
| `7..8` | snapshot sequence, BE16; chunk-0 SNAPSHOT sends 0 |
| `9..31` | reserved, all 0 |

| Operation | Id | Command |
| --- | ---: | --- |
| capabilities | `0x00` | GET |
| snapshot | `0x01` | GET |
| start | `0x10` | SET |
| stop | `0x11` | SET |
| clear | `0x12` | SET |

START duration is exactly 10, 30, or 60 seconds.

### Response

| Byte | Field |
| ---: | --- |
| `0` | echoed command |
| `1` | `0x07` |
| `2` | `0x01` |
| `3` | echoed operation |
| `4..5` | echoed tag, BE16 |
| `6` | status |
| `7` | state: idle 0, running 1, complete 2, stopped 3 |
| `8..9` | session id, BE16; none is 0 |
| `10..11` | frozen snapshot sequence, BE16 |
| `12` | chunk index |
| `13` | chunk count |
| `14..31` | 18-byte operation payload |

| Status | Id |
| --- | ---: |
| OK | `0x00` |
| unsupported version | `0x01` |
| invalid | `0x02` |
| busy | `0x03` |
| no session | `0x04` |
| stale snapshot | `0x05` |

Version validation precedes request-shape validation. With a supported version, wrong GET/SET-to-operation pairing, nonzero reserved bytes, nonzero sequence on chunk 0, invalid START duration, or an invalid chunk index returns INVALID. `length < 32` follows §5: the direct handler returns false and raw-HID transport sends no selector reply.

Concurrent START returns BUSY. STOP without a running session returns NO SESSION. CLEAR while running returns BUSY. SNAPSHOT chunk 0 freezes a new nonzero sequence; later chunks must present that sequence or receive STALE SNAPSHOT.

### Capabilities payload (`14..31`)

| Payload byte | Field |
| ---: | --- |
| `0` | flags: report timing `0x01`, histogram `0x02`, firmware timing `0x04`, timeline `0x08`, boot counters `0x10` |
| `1` | duration mask for 10/30/60 s, bits `0x07` |
| `2` | histogram bins: 8 |
| `3` | timeline capacity: 8 |
| `4..5` | recommended snapshot interval: 1000 ms, BE16 |
| `6` | endian: 1 = big |
| `7` | time unit: 1 = µs |
| `8` | firmware-version ASCII length, maximum 9 |
| `9..17` | `_DEF_FIRMWARE_VERSION` ASCII and zero padding |

START OK payload is duration, BootMode, and expected interval µs (BE32). STOP/CLEAR payload is zero. The expected interval is derived from selected BootMode at START, not negotiated link speed.

### Snapshot chunks

| Chunk | 18-byte payload |
| ---: | --- |
| `0` | mode U8, speed U8, duration U8, event count U8, elapsed ms U32, expected interval µs U32, report samples U32, bin/timeline count U8×2 |
| `1` | latency min / average / max / window max U32×4, queue peak U16 |
| `2..3` | histogram U32×4 each |
| `4` | loop samples / max / window max / stall count U32×4, stall threshold U16 |
| `5` | boot drops / resets / configurations / suspends U32×4 |
| `6` | boot speed changes, session drops / resets / configurations U32×4 |
| `7` | session suspends / speed changes / timeline overwrites U32×3, zero padding |
| `8..11` | two events each: type U8 + relative ms U32 + value U32 |

Base chunk count is 8; timeline data adds one chunk per two events, up to 12. Sequence 0 is skipped on wrap. `src/ap/modules/qmk/port/era_usb_diagnostics.c` owns envelope encoding; `src/hw/driver/usb/usb_hid/usb_diagnostics.c` owns captured measurements.

### Instrumentation safety bound

The accepted implementation is RAM-only with no heap and no EEPROM diagnostics history. The current bounded footprint is a 272-byte live session, 236-byte frozen wire snapshot, 20-byte boot counters, plus 6 bytes of sequence/valid/speed/next-id state. Snapshot capture copies 292 bytes under the global IRQ mask at about 1 Hz. Idle does not read TIM5 for diagnostics; an active session reads the 1 µs counter once per main loop and at report request/completion. The 32-bit microsecond counter wraps after about 4295 s, well beyond the 60 s maximum session.

These numbers are an 8 kHz-path safety/performance bound, not an invitation to duplicate structure layouts elsewhere. A change that materially grows RAM, IRQ-masked copy, timer-read frequency, or session duration must re-measure and restate the bound.

App responsibility: strict 32-byte parsing, per-path serial exchange, echoed-tag matching, frozen-sequence chunk reads, capability opt-in, display/persistence of long-term history, and comparison caveats. The app keeps mode changes user-driven and must not turn observations into an automatic stability verdict. See `the-via-eerraa/docs/adr/0002-h7s-usb-diagnostics.md`.

`tools/era_via_host_tests/test_usb_diagnostics.c` covers firmware envelope/status/chunk behavior. Hardware/host measurements remain separate; host tests do not prove physical latency or stability.

## 7. MOUSE unit conversion

VIA exposes pixels and milliseconds while the QMK engine stores step/ratio and event counts. The user-visible quantities are the contract; `src/ap/modules/qmk/port/mousekey_config.c` owns conversion.

- Top speed is derived from first-step × ratio. Recompute the ratio with rounding and engine clamping when first speed changes; do not floor it or expose the raw pair as independent user controls.
- Acceleration duration is held in time. Convert it to `mk_time_to_max` from the current interval with rounding. If the one-byte event count cannot represent the requested duration, GET reports the representable shorter value rather than echoing an impossible request.
- Acceleration Off means the first-step speed is used immediately. Runtime max-speed is folded to 1 while the stored ratio is preserved, so re-enabling acceleration restores the user's ramp. GET of top speed continues to report the stored effective top.

`tools/era_via_host_tests/test_era_via_exact_ms.c` (`test_mousekey`) covers these round trips.

## 8. Verification boundary

For document-only changes run `python -X utf8 tools/era_doc_refs.py`. If the document checker changes, also run `python -X utf8 tools/era_doc_refs_selftest.py`; its planted failures are negative fixtures and it restores the tree.

When VIA firmware behavior changes, run `pwsh -NoProfile -File tools/era_via_host_tests/run.ps1` plus only the build/hardware checks required by `docs/manual_verify.md`. App tests in `the-via-eerraa` and remote revisions are separate evidence and must not be reported as checked unless they were actually run against an identified peer revision.
