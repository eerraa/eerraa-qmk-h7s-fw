# VIA wire 계약

Genre: contract
Canonical for: 앱·VIA 호스트와 주고받는 것 전부 — raw-HID TX 단일 생산자, 채널 주소 배정,
exact-ms 값 계층, revision 상승 시점, selector `0x06`/`0x07` 봉투, MOUSE 단위 환산,
그리고 각각을 무는 검사

반대편 계약은 앱 저장소가 들고 있다(`docs/MAP.md` §7). **어긋나면 이쪽이 틀렸을 수도 있다.**
고치기 전에 양쪽을 대조한다.

## 1. raw-HID TX 생산자는 하나뿐이다

VIA 응답은 `src/ap/modules/qmk/port/via_hid.c`의 `via_hid_task()`가
`usbHidEnqueueViaResponse()`로 적재하는 경로 하나로만 나간다. `raw_hid_send()`는 **빈 스텁으로
남는다.** `src/ap/modules/qmk/quantum/via.c`의 GET switch는 응답 버퍼만 채우고 TX를 소유하지
않는다.

**왜**: 두 곳이 TX를 소유하면 32 B 봉투가 서로 섞이고, SOF 배출 순서가 요청–응답 짝을 깬다.
호스트는 직렬 왕복을 가정하므로 이 깨짐이 앱에서 "응답 없음"이 아니라 **다른 요청의 응답**으로
보인다.

**무는 것**: `tools/era_via_host_tests/check_single_producer.py`가 소스를 직접 읽어 세 가지를
본다 — `raw_hid_send()` 본문이 비어 있는가, `via_hid_task()`가 enqueue를 부르는가, `via.c`가
`id_era_state_sync`를 TX 없이 채우는가.

## 2. 채널 주소는 참조 QMK와 다르다

<!-- era-doc-refs: wire-values -->
| 컨트롤 | 채널 | value id |
| --- | --- | --- |
| 글로벌 TAPPING term (exact) | 15 | 5 |
| TD0–TD7 term (exact) | 16 | 41–48 |
| MOUSE 6개 컨트롤 | 17 | 1–6 |
<!-- era-doc-refs: end -->

**계열마다 주소가 다르고, 그래도 된다.** VIA는 정의를 `(vendorId, productId)`로 캐시하므로
H7S 정의가 별도인 이상 RP2040(`qmk_firmware_eerraa`)과 번호가 같을 필요가 없다. 각 계열이
자기 배치를 먼저 굳혔고, 이미 배포된 번호를 옮기는 것이 사용자 EEPROM을 깨는 쪽이다.

| 컨트롤 | RP2040 참조 | H7S | 왜 다른가 |
| --- | --- | --- | --- |
| 글로벌 TAPPING term | 채널 15 / value 5 | 같음 | — |
| TD0–TD7 term | 채널 0(custom) / value 72–79 | **채널 16 / value 41–48** | H7S는 Tap Dance에 전용 채널 16을 먼저 할당했고 슬롯 액션이 1–40을 쓴다. exact term은 그 뒤에 붙었다 |
| MOUSE | 채널 13 | **채널 17** | H7S에서 13은 USB POLLING이 점유한다. value id 1–6 배치는 참조와 같다 |

SOCD는 이름도 다르다 — 참조는 `socd` 접두사를, 여기는 `id_qmk_kill_switch_lr`·`_ud`를 쓴다.

전체 채널 배치는 `docs/MAP.md` §4. 앱은 이 표의 반대편을 `the-via-eerraa/docs/MAP.md` §3에
들고 있으므로 **한쪽을 고치면 반드시 양쪽을 본다.**

새 채널을 할당할 때는 `docs/MAP.md` §4에서 비어 있는 번호를 고르고, **5개 보드의 공식 JSON과
앱 커스텀 정의에 동시에** 넣는다. 한쪽만 바뀌면 펌웨어에 있는 기능에 손이 닿지 않는다 —
MOUSE가 실제로 그렇게 배포된 적이 있다. `menu` 검사가 이 저장소 쪽 절반을 막는다.

## 3. exact-ms 값 계층

- exact SET은 **2 B big-endian uint16, 100–500**만 받는다. 범위 밖과 2 B 미만 패킷은 거절하고
  저장값을 유지한다.
- legacy dropdown(값 × 10 ms, 20 ms 격자)은 공식 `usevia.app` 호환용으로 펌웨어에 남는다.
  **legacy SET만** 20 ms 격자로 내림한다.
- **legacy GET은 저장된 exact 값을 격자로 투영만 하고 저장값을 바꾸지 않는다.** load와
  `tapping_term_sync_state_from_storage()`도 유효한 uint16을 격자로 되돌리지 않는다.

**왜 이 비대칭이 계약인가**: 같은 펌웨어를 공식 앱과 커스텀 앱이 동시에 상대한다. GET이
저장값을 건드리면 공식 앱에서 한 번 조회한 것만으로 사용자가 커스텀 앱에서 넣은 137 ms가
120 ms로 잘린다. 읽기는 쓰기가 아니다.

Tap Dance 슬롯 term은 글로벌 `TAPPING_TERM`과 **완전히 독립**이다. 손상된 슬롯(시그니처·버전·
범위)은 init에서 200 ms 기본값으로 복원하고 dirty 표시를 남긴다.

Tap Dance 상태머신은 **Vial 호환**을 계약으로 든다 — 액션이 지정되지 않은 단계는 tap으로
대체되고, 슬롯에 tap/hold만 있으면 1탭에서 term을 기다리지 않고 즉시 완료한다. VIA는
`customKeycodes` **배열 순서대로** `QK_KB_0`~`QK_KB_7`을 보내고 펌웨어가 `TD(0)`~`TD(7)`으로
치환하므로, **순서를 바꾸면 이미 저장된 사용자 키맵의 슬롯 배정이 달라진다.**

## 4. 값이 실제로 바뀐 SET만 revision을 올린다

CONFIG revision을 올리는 채널은 디바운스(14), KKUK(12), SOCD(10·11), 인디케이터(0),
BootMode(13)이고, `id_eeprom_reset`은 세 도메인 전부를 올린다. KEYMAP·MACRO revision은 VIA
keymap/macro write 명령 뒤에 올린다.

참조 QMK는 EEPROM 쓰기 캐치올 훅 한 곳에서 올리지만 이 포팅층에는 그 훅이 없어 채널별로
올린다. **동일 값 SET은 no-op이다** — 아니면 앱이 자기 쓰기에 스스로 반응해 되읽기 폭풍이
난다.

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

## 7. MOUSE 단위 환산에서 실제로 어려운 세 곳

VIA가 그리는 것과 QMK 엔진이 드는 것이 같지 않다. 이 셋은 코드만 봐서는 복원되지 않는다.

**7-1. 최고 속도는 저장되지 않는다.** 엔진이 드는 것은 (스텝, 배수) 쌍이고 한 최고 속도를
만드는 조합이 여러 개다. raw 쌍을 그대로 노출하면 서로 다투는 컨트롤이 된다. 그래서 페이지는
첫 스텝과 최고 스텝을 px로 제시하고 배수를 파생시킨다. Start Speed를 바꿀 때 최고 속도를
붙잡아 둔다. 비율은 **내림이 아니라 반올림**이다 — 내림하면 스텝 8·상한 127에서 비율 15가
되어 120으로 되읽히고, 반올림하면 16이 되어 엔진 클램프가 정확히 127로 되돌려준다.

**7-2. 가속은 화면에서 시간, 엔진에서 이벤트 수다.** `mk_time_to_max`는 이동 이벤트를 센다.
그대로 통과시키면 갱신 주기를 올릴 때 건드리지 않은 램프가 절반으로 줄어든다. 그래서
지속시간으로 들고 `time_to_max × interval`로 파생시킨다. 표시 단위 50 ms는 해상도가 아니라
**왕복 정확성**이다 — 이벤트 수로 내렸다가 시간으로 올릴 때 반올림 오차가 나지 않는 단위가
50 ms다(33 ms는 어긋나고 20·10 ms도 일부 조합에서 어긋난다).

`mk_time_to_max`가 1바이트라 도달 범위는 갱신 주기가 빨라질수록 좁아진다. 200 /s(5 ms)에서
상한은 **1.275 s**이므로 JSON이 함께 제시하는 1.5 s·2.0 s는 조용히 잘린다. 참조 QMK도 같은
옵션 목록에 같은 동작이고, 주기별로 옵션 목록을 바꾸는 것은 VIA 정의 형식으로 표현할 수 없다.
그래서 **되읽기가 정직한 것**으로 계약을 맞췄다 — 새로고침하면 실제로 든 짧은 램프가 보인다.
호스트 테스트가 두 성질을 검증한다: 담기는 조합은 정확히 왕복하고, 담기지 않는 조합은
요청보다 짧은 값을 돌려준다.

**7-3. 가속 Off는 "최고 속도 고정"이 아니라 "첫 스텝 고정"이다.** `mk_time_to_max`가 0이면
엔진 판정이 첫 반복부터 참이라 `mk_max_speed`가 모든 이벤트의 스텝 크기가 된다. 여기에 최고
속도를 넣으면 쓸 수 없다 — 50 이벤트/초에서 32 px는 초당 1600 px이고 호스트 포인터 가속까지
곱해진다. 그래서 off일 때 런타임 배수에 1을 넣어 모든 분기를 `mk_move_delta` 하나로 접고,
저장된 배수는 건드리지 않는다. 램프를 다시 고르면 사용자가 고른 비율이 그대로 돌아온다.

`id_custom_set_value`는 **클램프된 실제 값**을 곧바로 에코한다. set이 값을 자를 수 있고 VIA는
돌아온 값을 그리므로, 에코를 빼면 화면과 펌웨어가 갈라진다.
