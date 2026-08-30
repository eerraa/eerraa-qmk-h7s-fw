# VIA wire contract

Genre: contract
Canonical for: what the app and VIA host exchange — the single raw-HID TX
producer, channel and value-id assignment, exact-ms value encoding,
when KEYMAP/MACRO/CONFIG revisions bump, selector `0x06`/`0x07` byte
envelopes, MOUSE unit conversion, and the checks that bite each of those

The other side lives in the app repo (`docs/MAP.md` §7). A mismatch can
mean this side is wrong. Compare both before editing.

## 1. One raw-HID TX producer

VIA replies leave through `via_hid_task()` in
`src/ap/modules/qmk/port/via_hid.c`, which calls
`usbHidEnqueueViaResponse()`. `raw_hid_send()` in that file is an empty
stub. `src/ap/modules/qmk/quantum/via.c` still calls `raw_hid_send()` at
the end of `raw_hid_receive()`; that call is a no-op. The GET switch,
including `id_era_state_sync`, fills the 32 B buffer and does not own
TX. `via_hid_task()` enqueues that same buffer after
`raw_hid_receive()` returns.

Two producers would mix 32 B envelopes on the VIA IN endpoint, and SOF
drain order would break request–response pairing. The host assumes a
serial round-trip; a mix shows up as another request's reply, not as
"no reply".

`tools/era_via_host_tests/check_single_producer.py` reads the sources:
`raw_hid_send()` body stays empty, `via_hid_task()` enqueues, and
`via.c` fills `id_era_state_sync` without TX.

> **REFUSED:** filling in `raw_hid_send()` or adding a second VIA TX
> path.
> **WHY:** `via.c` already calls `raw_hid_send()` on every command; a
> live stub would transmit twice, once from that call and once from
> `via_hid_task()` enqueue, and mix envelopes on one IN endpoint.
> **REOPENS:** none while VIA IN remains a single 32 B endpoint.

## 2. Channel numbers are not the RP2040 layout

The generated channel table is `docs/MAP.md` §4 (`via-channels`). Do
not copy it here. Firmware routing means that board's
`<board>/port/via_port.c` accepts the channel. JSON exposure means the
official `*-VIA.JSON` lets the user reach that screen. Routing without
exposure leaves a firmware feature with no UI — `menu` fails that case.

Channel 2 (`id_qmk_rgblight_channel`) is handled in VIA core
(`src/ap/modules/qmk/quantum/via.c`), so board routing is `-` and JSON
exposure is still `O`. Channels 1, 3, 4, and 5 are VIA-reserved and
unused here; do not assign a new feature to those numbers. Channel 13
value id 3 is retired (`docs/contract_usb.md` §4); do not reuse it.

This file owns the value-id rows. `table` regenerates them from
`src/ap/modules/qmk/quantum/via.h`.

<!-- era-doc-refs: wire-values -->
| Control | Channel | value id |
| --- | --- | --- |
| Global TAPPING term (exact) | 15 | 5 |
| TD0–TD7 term (exact) | 16 | 41–48 |
| MOUSE six controls | 17 | 1–6 |
<!-- era-doc-refs: end -->

Peer `the-via-eerraa/docs/adr/0001-state-sync-protocol.md` and
`the-via-eerraa/docs/MAP.md` §3 use the same H7S ids. VIA caches a
definition by `(vendorId, productId)`, so H7S numbers need not match
RP2040 (`qmk_firmware_eerraa`). Each family froze its layout first;
moving a shipped number breaks the app overlay and the official JSON.

| Control | RP2040 | H7S |
| --- | --- | --- |
| Global TAPPING term (exact) | channel 15 / value 5 | Same |
| TD0–TD7 term (exact) | channel 0 / values 72–79 | channel 16 / values 41–48. Channel 16 is the Tap Dance channel; slot actions occupy values 1–40, exact terms append after that |
| MOUSE (six controls) | channel 13 | channel 17. Channel 13 is `id_qmk_usb_polling`. Value ids 1–6 match the reference |

SOCD command names here are `id_qmk_kill_switch_lr` and
`id_qmk_kill_switch_ud`. The reference uses a `socd` prefix.

A new channel takes an unused number from `docs/MAP.md` §4 and is added
to all five official JSON files and the app custom definition together.
This repo's half is `menu`. Cross-repo match is not checked
(`docs/MAP.md` §7).

> **REFUSED:** moving a shipped H7S channel or value id.
> **WHY:** official JSON and the app overlay already address those
> numbers; a move desyncs one side and leaves the firmware feature
> unreachable or writes the wrong slot.
> **REOPENS:** an additive id, shipped on both sides at once. Compacting
> a hole is not that.

## 3. exact-ms encoding

Exact SET is a 2-byte big-endian `uint16` on `id_custom_set_value`
(`0x07`) / `id_custom_get_value` (`0x08`). Inclusive range is 100–500
(`TAPPING_TERM_MIN_MS` / `TAPPING_TERM_MAX_MS` in
`src/ap/modules/qmk/port/tapping_term.c`; `TAPDANCE_TERM_MIN_MS` /
`TAPDANCE_TERM_MAX_MS` in `src/ap/modules/qmk/port/tapdance.c`). Out of
range, or fewer than two value bytes (`length < 5` on the 32 B report),
is refused and the store is unchanged.

Official `*-VIA.JSON` still expose the 1-byte × 10 ms legacy dropdown
(global channel 15 value 1; TD slot `*_term` values 5, 10, … 40) and do
not expose `_term_exact`. Firmware implements both. The custom-app JSON
is the other side (`the-via-eerraa` ADR 0001).

Legacy SET floors onto the 100–500 / 20 ms grid. Legacy GET projects
the stored exact millisecond value onto that grid and does not rewrite
storage. Load and `tapping_term_sync_state_from_storage()` also leave a
valid uint16 unsnapped. GET is not a write: an official-app query must
not clip a custom-app 137 ms down to 120 ms.

Tap Dance slot terms are independent of global `TAPPING_TERM`. Slot
validity is `docs/contract_eeprom.md` §1.

`tools/era_via_host_tests/test_era_via_exact_ms.c` bites the range,
the short packet, and the GET/SET asymmetry.

## 4. Revisions bump when the published value changes

Tokens are RAM `uint32`, start at 1, and skip 0 on wrap
(`era_state_sync_next()` in `src/ap/modules/qmk/port/era_state_sync.c`).
This porting layer has no QMK EEPROM-write catch-all, so each handler
bumps its domain.

KEYMAP, on the write command, no compare:
`id_dynamic_keymap_set_keycode`, `id_dynamic_keymap_reset`,
`id_dynamic_keymap_set_buffer`, `id_dynamic_keymap_set_encoder`.

MACRO, same: `id_dynamic_keymap_macro_set_buffer`,
`id_dynamic_keymap_macro_reset`.

CONFIG, compare-then-bump (a same-value SET is a no-op):

| Channel | Handler |
| --- | --- |
| 14 debounce | `src/ap/modules/qmk/port/debounce_profile.c` |
| 12 KKUK | `src/ap/modules/qmk/port/kkuk.c` |
| 10 / 11 SOCD | `src/ap/modules/qmk/port/kill_switch.c` |
| 0 indicator | `<board>/port/indicator_port.c` |
| 13 BootMode select (value 1) | `src/ap/modules/qmk/port/bootmode.c`. Apply (value 2) does not bump |
| 15 tapping | `src/ap/modules/qmk/port/tapping_term.c` |
| 16 tapdance | `src/ap/modules/qmk/port/tapdance.c` |
| 17 mousekey | `src/ap/modules/qmk/port/mousekey_config.c` |

Always bump, no compare: `via_set_layout_options()`.
`id_eeprom_reset` bumps all three domains.

Does not bump: channel 2 rgblight (VIA core), channel 8 version,
channel 9 system, `id_custom_save` (EEPROM flush only; BootMode save is
a no-op), selector `0x07`.

A no-op custom SET must not bump. Otherwise the app treats its own
write as a remote CONFIG change and storms GET.

> **REFUSED:** bumping CONFIG on a custom SET that did not change the
> published value.
> **WHY:** the app refreshes CONFIG from revision inequality; a self
> echo would loop GET against the same store.
> **REOPENS:** none while State Sync poll uses equality tokens.

## 5. selector `0x06` — State Sync envelope

`id_get_keyboard_value` (`0x02`) + `id_era_state_sync` (`0x06`).
Version `ERA_STATE_SYNC_ENVELOPE_VERSION` (`0x01`). Integers are
big-endian. The layout is the 32 B VIA payload. SET is not routed on
this selector; `via.c` marks `id_unhandled`. TX is §1.

Byte layout matches `the-via-eerraa/docs/adr/0001-state-sync-protocol.md`.

### Request

| Byte | Meaning |
| ---: | ------- |
| `0` | `id_get_keyboard_value` (`0x02`) |
| `1` | `0x06` |
| `2` | `0x01` |
| `3` | `0` |
| `4..5` | host request tag, BE16 |
| `6..31` | `0` |

### Response

| Byte | Meaning |
| ---: | ------- |
| `0` | `0x02` |
| `1` | `0x06` |
| `2` | `0x01` (firmware writes envelope version, not the request version byte) |
| `3` | status: `ERA_STATE_SYNC_STATUS_OK` `0x00`, `UNSUPPORTED_VERSION` `0x01`, `INVALID` `0x02` |
| `4..5` | echoed tag, BE16 |
| `6` | domain mask; OK writes `ERA_STATE_SYNC_DOMAIN_MASK_INITIAL` (`0x07`) |
| `7` | `0` |
| `8..11` | keymap revision, BE32 |
| `12..15` | macro revision, BE32 |
| `16..19` | config revision, BE32 |
| `20..31` | `0` |

Domain bits: `ERA_STATE_SYNC_DOMAIN_KEYMAP` `0x01`,
`ERA_STATE_SYNC_DOMAIN_MACRO` `0x02`, `ERA_STATE_SYNC_DOMAIN_CONFIG`
`0x04`. Version is checked before reserved bytes. A nonzero reserved
byte (`3` or `6..31`) is `ERA_STATE_SYNC_STATUS_INVALID` (status 2);
the tag is still echoed and revisions are not filled.

`length < 32` is not INVALID. `era_state_sync_via_command` returns
false and `via.c` sets `id_unhandled` (`0xFF`); the buffer is not
rewritten as a v1 envelope. Peer observation, VIA unedited: the app
parseStateSyncEnvelope returns null when length is not 32 (neither
`0xFF` nor INVALID).

`tools/era_via_host_tests/test_era_via_exact_ms.c` bites the OK
envelope, reserved INVALID, unsupported version, tag echo, and the
31-byte false return.

> **REFUSED:** answering `ERA_STATE_SYNC_STATUS_INVALID` on
> `length < 32`.
> **WHY:** a short buffer is not an envelope;
> `era_state_sync_via_command` returns false and `via.c` marks
> `id_unhandled` (`0xFF`).
> **REOPENS:** none while VIA IN is a 32 B report.

## 6. selector `0x07` — diagnostics envelope

`id_get_keyboard_value` (`0x02`) / `id_set_keyboard_value` (`0x03`) +
`id_era_usb_diagnostics` (`0x07`). Protocol
`ERA_USB_DIAGNOSTICS_PROTOCOL_VERSION` (`0x01`), big-endian, 32 B.
Firmware answers the request; it is not an unsolicited producer.
Observation-only product boundary is `docs/contract_usb.md` §4
(do not copy it here). Byte layout matches
`the-via-eerraa/docs/adr/0002-h7s-usb-diagnostics.md`.

### Request

| Byte | Field |
| ---: | ----- |
| `0` | command: GET `0x02` or SET `0x03` |
| `1` | `0x07` |
| `2` | `0x01` |
| `3` | operation |
| `4..5` | host tag, BE16 |
| `6` | duration seconds or snapshot chunk index |
| `7..8` | snapshot sequence; chunk 0 must send 0 |
| `9..31` | reserved, must be 0 |

Operations: capabilities `ERA_USB_DIAGNOSTICS_OP_CAPABILITIES` `0x00`
and snapshot `ERA_USB_DIAGNOSTICS_OP_SNAPSHOT` `0x01` are GET. Start
`ERA_USB_DIAGNOSTICS_OP_START` `0x10`, stop
`ERA_USB_DIAGNOSTICS_OP_STOP` `0x11`, and clear
`ERA_USB_DIAGNOSTICS_OP_CLEAR` `0x12` are SET. Start duration is
10 / 30 / 60 only.

Wrong command/operation pairing, a nonzero reserved byte (`9..31`),
chunk 0 with a nonzero sequence, or a duration other than 10 / 30 / 60
is `ERA_USB_DIAGNOSTICS_STATUS_INVALID` (status 2). Unsupported
version is checked first and wins over INVALID.

`length < 32` is the same unhandled path as §5: the handler
returns false and `via.c` sets `id_unhandled` (`0xFF`).

### Response

| Byte | Field |
| ---: | ----- |
| `0..5` | command, selector, v1, operation, echoed tag |
| `6` | status |
| `7` | state: idle 0, running 1, complete 2, stopped 3 |
| `8..9` | session ID, BE16; none is 0 |
| `10..11` | frozen snapshot sequence, BE16 |
| `12` | chunk index |
| `13` | chunk count |
| `14..31` | 18 B operation payload |

Codes: OK 0, unsupported version 1, invalid 2, busy 3, no session 4,
stale snapshot 5. Tag matches the request in the transport queue.
Sequence is the frozen snapshot those chunks belong to — not a second
tag. Chunk 0 freezes a new nonzero sequence; later chunks must send
that same sequence or the reply is stale.

START OK payload is duration, BootMode, expected interval µs (BE32).
STOP with no running session is no session. CLEAR while running is
busy. Concurrent START is busy.

### Capabilities payload

| Payload byte | Field |
| -----------: | ----- |
| `0` | flags: report timing `0x01`, histogram `0x02`, firmware timing `0x04`, timeline `0x08`, boot counters `0x10` |
| `1` | duration mask 10 / 30 / 60, bits `0x07` |
| `2..3` | histogram bins `USB_DIAGNOSTICS_HISTOGRAM_BUCKETS` 8, timeline capacity `USB_DIAGNOSTICS_TIMELINE_CAPACITY` 8 |
| `4..5` | recommended snapshot interval 1000 ms, BE16 |
| `6..7` | endian 1 (big), time unit 1 (µs) |
| `8` | firmware version length, max 9 |
| `9..17` | ASCII `_DEF_FIRMWARE_VERSION` and zero padding |

### Snapshot chunks

| Chunk | 18 B payload |
| ----: | ------------ |
| `0` | mode U8, speed U8, duration U8, event count U8, elapsed ms U32, expected interval µs U32, report samples U32, bin/timeline count U8×2 |
| `1` | latency min / average / max / window max U32×4, queue peak U16 |
| `2..3` | histogram U32×4 each |
| `4` | loop samples / max / window max / stall count U32×4, stall threshold U16 |
| `5` | boot drops / resets / configurations / suspends U32×4 |
| `6` | boot speed changes, session drops / resets / configurations U32×4 |
| `7` | session suspends / speed changes / timeline overwrites U32×3, zero padding |
| `8..11` | two events each: type U8 + relative ms U32 + value U32 |

Base chunk count is `ERA_USB_DIAGNOSTICS_BASE_CHUNKS` (8). One extra
chunk per two timeline events, max 12. Sequence 0 is skipped on wrap,
same as State Sync tokens.

`tools/era_via_host_tests/test_usb_diagnostics.c` bites capability
bytes, reserved INVALID, unsupported version, duration, busy / stop /
clear, frozen multi-chunk, stale sequence, wrap, and saturation.

> **REFUSED:** answering `ERA_USB_DIAGNOSTICS_STATUS_INVALID` on
> `length < 32`.
> **WHY:** a short buffer is not an envelope;
> `era_usb_diagnostics_via_command` returns false and `via.c` marks
> `id_unhandled` (`0xFF`).
> **REOPENS:** none while VIA IN is a 32 B report.

### 6-1. Axes that survive a phase redraw

Absolute microseconds are not comparable across runs. The same
firmware and mode moved FS average 231 → 558 µs and HS 4K 232 → 184
µs on re-enumeration alone. Reports leave on the debounce 1 ms tick
boundary, and that tick's phase against the host USB frame is redrawn
at random every boot. `min` is that phase.

Comparable axes are phase-independent: span (max − min), queue-depth
peak, report drops, `>2× interval` counts, main-loop gap.

The histogram's expected interval is the BootMode selected at START,
not the negotiated link speed. HS 8K on an FS-only hub puts every
sample in the top bucket. The snapshot already carries mode and
negotiated speed; the app judges the mismatch. Firmware does not hide
the selected mode or auto-correct it.

Do not mix firmware versions in one comparison set. `V260824R1`
split IN-endpoint busy (`in_ep_busy`) and lowered the latency and
queue-depth baseline.

### 6-2. Instrumentation cost bound

This subsystem sits on the 8 kHz path because RAM is fixed and the
critical section is short. A change that grows either must re-state
that bound.

- Session `usb_diagnostics_session_internal_t` 272 B, wire frozen
  snapshot `usb_diagnostics_snapshot_t` 236 B, boot counters 20 B,
  plus 6 B of sequence / valid / speed / next-id. No heap. No EEPROM.
- `usbDiagnosticsCapture()` copies 292 B (session 272 + counters 20)
  under a global IRQ mask. About 1 Hz.
- Keyboard retry-queue entries carry 6 B of request time and session
  id (`report_info_t`); 128 slots add 768 B. `_Static_assert` locks
  that padding.
- Idle does not read TIM5 for this subsystem. `qmkUpdate()` calls
  `usbDiagnosticsTask(micros())` only while
  `usbDiagnosticsIsActive()`. A live session reads the counter once
  per loop and once each at report request and DataIn complete.

`micros()` is a 1 µs tick. TIM5 prescaler is
`(SystemCoreClock / 2) / 1000000 - 1` (299 at 600 MHz). Wrap is
`UINT32_MAX` + 1 µs, 4295 s, 71× the longest 60 s session. A 1 Hz
host poll reaching firmware elapsed 30 000 ms at poll 31 is the field
cross-check.

## 7. MOUSE unit conversion — three places code alone does not reconstruct

VIA draws pixels and milliseconds. The QMK engine stores
`(mk_move_delta, mk_max_speed)` and `mk_time_to_max` as event counts.
Channel 17 value ids 1–6 are in the generated table above. Conversion
is `src/ap/modules/qmk/port/mousekey_config.c`. `MOUSEKEY_MOVE_MAX` is
127. `id_custom_set_value` echoes the clamped value so the page draws
what the firmware kept.

`tools/era_via_host_tests/test_era_via_exact_ms.c` (`test_mousekey`)
bites 7-1, 7-2, and 7-3.

### 7-1. Top speed is not stored

The engine holds a (step, ratio) pair. Several pairs make the same top
speed. Exposing the raw pair would fight the page. The page shows first
step and top step in px; the firmware derives the ratio. Changing Start
Speed holds the current top speed and recomputes `max_speed`. The
ratio is rounded, not floored: step 8 and cap 127 floor to ratio 15
(reads back 120) and round to 16 (engine clamp returns 127).

> **REFUSED:** flooring the speed ratio, or exposing the raw
> `(move_delta, max_speed)` pair as two independent knobs.
> **WHY:** floor misses the 127 cap on round-trip; two knobs that
> multiply would drag top speed when the user moves start speed.
> **REOPENS:** none while `move_unit()` still clamps at
> `MOUSEKEY_MOVE_MAX`.

### 7-2. Acceleration is time on the page, event count in the engine

`mk_time_to_max` counts move events. Passing the page value through
would halve an untouched ramp when the user raises the update rate.
The page holds duration; firmware stores
`time_to_max ≈ duration / interval` (rounded) and, on interval SET,
re-derives events from the held duration. Display unit
`MOUSEKEY_CFG_RAMP_UNIT_MS` is 50. That unit is round-trip accuracy,
not resolution: every duration × interval pair the official JSON
offers either round-trips exactly or, if it does not fit in one
byte of events, reads back shorter than requested — never the
request.

`mk_time_to_max` is `uint8`, so reach shrinks as the interval shortens.
At 200 /s (5 ms) the cap is 255 × 5 ms = **1.275 s**. Official JSON
offers 1.5 s (30) and 2.0 s (40) on the same page as 5 ms, so those
pairs clip. GET of 1.5 s at 5 ms returns 26 (1.3 s). Honest readback
is the contract; per-rate option lists are not expressible in VIA
JSON.

### 7-3. Acceleration Off locks the first step, not top speed

`mk_time_to_max == 0` makes the engine's `repeat >= time_to_max` test
true from the first repeat, so `mk_max_speed` would be every event's
step. Writing the stored top speed there is unusable: 32 px at 50
events/s is 1600 px/s before host pointer acceleration. Off therefore
sets runtime `mk_max_speed` to 1, folding every branch onto
`mk_move_delta`, and leaves the stored ratio untouched. Turning the
ramp back on restores the user's ratio. GET of top speed still reports
the stored top (`mousekey_config_effective_max_speed()`), which is why
the JSON can hide that control behind `showIf` accel ≠ 0.

> **REFUSED:** treating accel Off as "lock to top speed", or writing 0
> into the stored ratio.
> **WHY:** Off-as-top-speed is a jump the pointer host then
> accelerates further; clearing the stored ratio would forget the
> user's ramp.
> **REOPENS:** none while `mk_time_to_max == 0` means "already at
> max" on the first event.
