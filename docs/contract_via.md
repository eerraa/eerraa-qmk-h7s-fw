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
| 컨트롤 | 채널 | value id |
| --- | --- | --- |
| 글로벌 TAPPING term (exact) | 15 | 5 |
| TD0–TD7 term (exact) | 16 | 41–48 |
| MOUSE 6개 컨트롤 | 17 | 1–6 |
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

## 5. selector `0x06` — state sync 봉투

`GET_KEYBOARD_VALUE(0x02)` + `id_era_state_sync`. **32 B 고정**, version 1, 도메인 셋
(`ERA_STATE_SYNC_DOMAIN_KEYMAP`·`_MACRO`·`_CONFIG`). 예약 구간이 0이 아니거나 길이가 모자라면
`ERA_STATE_SYNC_STATUS_INVALID`다.

길이 검사가 6 B였던 적이 있는데 그것으로는 부족하다 — 앱 파서와 참조 구현이 둘 다 **32 B 봉투
전 구간**을 본다. 응답 버퍼만 채우고 TX는 §1 경로를 탄다.

## 6. selector `0x07` — 진단 봉투는 관측 전용이다

`GET_KEYBOARD_VALUE(0x02)`/`SET_KEYBOARD_VALUE(0x03)` + `id_era_usb_diagnostics`, v1,
big-endian, 32 B 고정.

| 요청 byte | 의미 | | 응답 byte | 의미 |
| --- | --- | --- | --- | --- |
| 0–2 | command, selector, version | | 0–5 | command, selector, v1, operation, echo tag |
| 3 | operation | | 6 | status |
| 4–5 | host request tag (BE16) | | 7 | state |
| 6 | duration 초 또는 chunk index | | 8–9 | session ID (BE16) |
| 7–8 | snapshot sequence (chunk 0은 0) | | 10–11 | frozen snapshot sequence |
| 9–31 | 반드시 0 | | 12–13 | chunk index / count |
| | | | 14–31 | 18 B payload |

operation은 capabilities `0x00`·snapshot `0x01`(GET), start `0x10`·stop `0x11`·clear `0x12`
(SET). status는 OK 0 / unsupported version 1 / invalid 2 / busy 3 / no session 4 / stale
snapshot 5. chunk 0이 snapshot을 동결하고 후속 chunk는 **같은 sequence만** 받는다. 기본
chunk는 0–7이고 timeline event 두 개마다 하나가 붙어 최대 12개다.

**tag와 sequence는 역할이 다르다.** tag는 전송 큐에서 요청–응답을 맞추고, sequence는 여러
패킷이 같은 동결 snapshot인지 보장한다.

**펌웨어는 판정하지 않는다.** 안정성 점수, stable/unstable 라벨, 자동 폴링 변경, EEPROM 기록,
자동 재부팅이 전부 없다. 이건 누락이 아니라 계약이며 근거는 `docs/contract_usb.md` §4에 있다.

### 6-1. 진단 수치를 비교해도 되는 축

**절대 µs는 회차 간 비교에 쓸 수 없다.** 같은 펌웨어·같은 모드인데 재열거만으로 평균이
FS에서 231 → 558 µs로, HS 4K에서 232 → 184 µs로 움직인 실측이 있다. 리포트가 디바운스 1 ms
틱 경계에서 방출되고 그 틱과 호스트 USB 프레임의 위상차가 **부팅마다 무작위로 다시 정해지기**
때문이다. `min`이 곧 그 위상이다.

비교에 쓸 수 있는 것은 위상 독립 지표뿐이다 — **span(max − min)**, queue depth peak, report
drops, `>2× interval` 건수, main-loop gap.

정규화 기준은 **세션 시작 시점에 선택된 BootMode**의 간격이지 실제로 열거된 link speed가
아니다. HS 8K를 고른 채 FS 전용 허브에 꽂으면 모든 표본이 최상위 버킷에 들어간다. snapshot이
mode와 negotiated speed를 함께 보내므로 **앱이** 그 불일치를 판정한다 — 펌웨어는 선택 모드를
숨기거나 자동 보정하지 않는다. "8K로 동작하지 않았다"가 더 정확한 사실이기 때문이다.

펌웨어 버전이 다른 기록은 같은 비교군에 넣지 않는다. `V260824R1`의 IN 엔드포인트 busy 분리
(`in_ep_busy`)가 재시도 큐 진입을 줄여 **지연과 큐 깊이의 기준선 자체를 내렸다.**

### 6-2. 계측 비용의 상한

이 서브시스템이 8 kHz 경로에 들어가도 되는 근거는 고정된 RAM과 짧은 임계구역이다. 늘리는
변경은 이 근거를 다시 세워야 한다.

- 세션 내부 상태 272 B + wire frozen snapshot 236 B + 부팅 카운터 20 B + 상태값 6 B. heap과
  EEPROM 사용은 없다.
- `usbDiagnosticsCapture()`의 임계구역 복사량은 **292 B**(세션 272 + 카운터 20)이며 mingw
  gcc로 실측했다. 전역 인터럽트 차단이지만 600 MHz M7에서 1 µs 미만이고 빈도가 약 1 Hz라
  125 µs 예산 대비 구조적 위험이 없다.
- 키보드 재시도 큐 원소마다 요청 시각·세션 ID 6 B가 붙어 128개 기준 768 B가 는다.
- idle 경로는 SOF마다 타임스탬프를 읽지 않는다. 세션 중에만 루프당 TIM5 카운터를 한 번 읽고,
  리포트마다 요청·완료에서 각각 한 번 읽는다.

`micros()`는 **정확히 1 µs 눈금**이다. HSE 24 MHz → PLL1 → SYSCLK 600 MHz, APB1 타이머 클럭
300 MHz에 prescaler 299가 정확히 1 MHz를 만들고, wrap 주기 4,295초는 최대 세션 60초의 71배다.
브라우저가 1 Hz로 31회 폴링하는 동안 펌웨어 elapsed가 30,000 ms에 도달해 실측으로도 교차
검증됐다.

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
