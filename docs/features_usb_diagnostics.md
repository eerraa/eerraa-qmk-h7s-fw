# USB 전달 진단 가이드 (V260824R1)

## 1. 목적과 비목표

이 기능은 STM32H7S 키보드가 실제로 요청한 HID 리포트가 USB IN 완료에 도달하기까지의 시간을 짧은 세션으로 관측합니다. 진단 결과는 사실 데이터만 제공하며 안정성 점수, 자동 폴링 변경, EEPROM 기록, 자동 재부팅을 수행하지 않습니다.

- 지원 세션: 10초, 30초, 60초(기본 UI 30초)
- 기본 폴링 모드: FS 1 kHz. HS 2/4/8 kHz는 사용자가 직접 선택·적용합니다.
- 일반 장치에는 selector를 probe하지 않습니다. 앱 정의의 `usbDiagnostics` opt-in이 있어야 합니다.
- SOF 간격은 HID 전달 완료가 아니므로 수집하지 않습니다. 원시 이벤트 스트림도 내보내지 않습니다.

## 2. 측정 경계

| 지표 | 시작 | 종료/기준 | 단위 |
| --- | --- | --- | --- |
| HID 전달 지연 | `usbHidSendReport()`가 리포트를 받은 시각. 큐에 들어가도 이 시각을 보존 | 키보드 IN 엔드포인트 `DataIn` 완료 | µs |
| 정규화 분포 | 위 전달 지연 | 세션 시작 시 폴링 간격의 0.5×, 0.75×, 1×, 1.25×, 1.5×, 2×, 4×, 초과 | 누적 건수 |
| 펌웨어 루프 간격 | 연속 `qmkUpdate()` 진입 | 다음 `qmkUpdate()` 진입 | µs |
| 펌웨어 stall | 루프 간격이 1000 µs 초과 | 고정 물리 임계값이며 점수가 아님 | 건수 |
| 리포트 드롭 | 키보드/EXK 재시도 큐 삽입 실패 | 포화형 부팅/세션 카운터 | 건수 |
| USB 하드 이벤트 | reset, HID configuration, suspend, speed change 콜백 | 포화형 부팅/세션 카운터 + 최근 8개 타임라인 | 건수 |

리포트 최소/평균/최대, 약 1초 창의 최대값, 큐 깊이 최고점, 8개 누적 histogram을 제공합니다. 앱은 연속 snapshot의 누적 histogram 차이로 창별 분위수를 근사합니다.

**모드·회차 비교는 절대 µs가 아니라 위상 독립 지표로 하십시오.** 2차 실기에서 같은 펌웨어·같은 모드인데 재열거만으로 평균이 231→558 µs(FS), 232→184 µs(4K)로 이동했습니다. 리포트가 디바운스 1 ms 틱 경계에서 방출되고 그 틱과 호스트 USB 프레임의 위상차가 부팅마다 무작위로 정해지기 때문이며, `min`이 곧 그 위상입니다. 비교에는 **span(max−min)**, queue depth peak, report drops, `>2× interval` 건수, main-loop gap을 쓰십시오. 정규화 배수는 "그 모드가 자기 간격 예산 안에 있었는가"라는 별개 질문에만 답합니다.

정규화 기준은 **세션 시작 시점에 선택된 BootMode**의 간격이며 실제로 enumerate된 link speed가 아닙니다. FS 1K는 항상 Full Speed로, HS 2/4/8K는 항상 High Speed로 열거되므로 둘이 어긋나면(예: HS 8K 상태로 FS 전용 허브에 연결) 배수·분위수·trend는 선택 모드를 설명하지 못합니다. snapshot chunk 0이 mode와 negotiated speed를 함께 보내므로 앱이 이 불일치를 판정해 경고합니다. 마이크로초 원값과 카운터는 이 경우에도 유효합니다.

## 3. 런타임 구조와 비용

```
항상 켜짐(저비용)
  USB reset/configure/suspend/speed + report queue drop
    → 포화형 RAM 카운터

사용자 세션 중에만
  qmkUpdate() → loop gap
  report request → queue wait → USB DataIn → delivery latency
  hard event / >1 ms loop gap → 최근 8개 ring timeline

VIA 약 1 Hz 요청
  chunk 0에서 일관된 frozen snapshot 생성
  같은 sequence의 후속 chunk만 반환
```

idle 경로는 SOF마다 타임스탬프를 읽지 않습니다. `qmkUpdate()`에는 active 플래그 분기만 있고, 세션 중에만 루프당 TIM5 counter를 한 번 읽습니다. 리포트가 발생한 경우 요청과 DataIn에서 각각 한 번 읽습니다. snapshot 복사는 약 1 Hz로 제한됩니다.

고정 RAM 상한은 진단 세션 내부 상태 272 B, wire frozen snapshot 236 B, 부팅 카운터 20 B와 상태값 6 B입니다. `usbDiagnosticsCapture()`의 임계구역 복사량은 세션 272 B + 부팅 카운터 20 B = 292 B입니다. 세션 272 B 중 histogram 누계/threshold는 60 B(32 B + 28 B), 최근 8개 event timeline payload는 96 B(12 B × 8)입니다. frozen snapshot에도 host 전달용 histogram 32 B와 timeline 96 B가 포함됩니다. 키보드 재시도 큐에는 요청 시각/세션 ID 6 B가 각 원소에 추가되어 128개 기준 768 B가 늘어납니다. heap 할당과 EEPROM 사용은 없습니다.

동일 May65/ARM GCC/MinGW 설정으로 기준 `V260823R1`과 비교한 링크 결과는 RAM 64,396 B → 65,572 B로 순증가 1,176 B, FLASH 122,124 B → 122,168 B로 순증가 44 B입니다. 신규 진단 core와 VIA encoder 오브젝트의 code/constant 합은 3,272 B지만 제거한 SOF monitor/instrumentation 코드가 대부분 상쇄합니다.

## 4. 32바이트 VIA 계약

기존 VIA `GET_KEYBOARD_VALUE(0x02)` / `SET_KEYBOARD_VALUE(0x03)`에 selector `0x07`을 추가합니다. 프로토콜은 v1, big-endian입니다.

### 4.1 요청

| byte | 의미 |
| --- | --- |
| 0 | command: GET `0x02` 또는 SET `0x03` |
| 1 | selector `0x07` |
| 2 | protocol version `0x01` |
| 3 | operation |
| 4–5 | host request tag (BE16) |
| 6 | duration seconds 또는 snapshot chunk index |
| 7–8 | snapshot sequence (chunk 0은 0, 후속 chunk는 chunk 0 응답값) |
| 9–31 | 반드시 0 |

operation은 capabilities `0x00`(GET), snapshot `0x01`(GET), start `0x10`(SET), stop `0x11`(SET), clear `0x12`(SET)입니다.

### 4.2 공통 응답

| byte | 의미 |
| --- | --- |
| 0–5 | command, selector, v1, operation, tag |
| 6 | status: OK 0, unsupported version 1, invalid 2, busy 3, no session 4, stale snapshot 5 |
| 7 | state: idle 0, running 1, complete 2, manually stopped 3 |
| 8–9 | session ID (BE16, 0은 없음) |
| 10–11 | frozen snapshot sequence (BE16) |
| 12 | chunk index |
| 13 | chunk count |
| 14–31 | 18바이트 operation payload |

tag와 sequence는 서로 다른 역할입니다. tag는 전송 큐에서 요청/응답을 맞추고, sequence는 여러 패킷이 같은 frozen snapshot인지 보장합니다. version/tag 외 예약 바이트가 더럽거나 command/operation 조합이 맞지 않으면 `INVALID`입니다.

### 4.3 capabilities payload

capability bit는 report timing `0x01`, histogram `0x02`, firmware timing `0x04`, timeline `0x08`, boot counters `0x10`입니다. 이어서 duration mask `0x07`, histogram 8개, timeline 8개, 권장 snapshot 1000 ms, endian=1, time unit=µs, 9바이트 펌웨어 버전 문자열이 옵니다.

### 4.4 snapshot chunk

기본 chunk는 0–7이며, timeline event 두 개당 chunk 하나가 추가됩니다(최대 총 12개).

| chunk | payload |
| --- | --- |
| 0 | mode, speed, duration, event count, elapsed ms, expected interval µs, report samples, bucket/timeline 수 |
| 1 | latency min/average/max/window max, queue depth peak |
| 2–3 | 8개 누적 histogram (`uint32` × 4) |
| 4 | loop samples, loop max/window max, >1 ms stall count, stall threshold |
| 5 | boot report drops/resets/configurations/suspends |
| 6 | boot speed changes, session report drops/resets/configurations |
| 7 | session suspends/speed changes, timeline overwrite count |
| 8–11 | event 두 개: type 1 B + relative ms 4 B + value 4 B |

## 5. 세션·연결 실패 의미론

- duration 만료는 펌웨어가 wrap-safe deadline 비교로 `complete` 처리합니다.
- stop은 현재 running 세션만 `stopped`로 바꿉니다. clear는 running 중 거절되며 세션 결과만 지웁니다. 부팅 카운터는 지우지 않습니다.
- USB reset/reconfigure가 MCU reset 없이 발생하면 세션과 카운터는 RAM에서 계속됩니다. 실제 MCU reset이면 RAM 세션은 소멸하며 앱이 연결 세대 변경을 감지해 중단 결과를 표시합니다.
- chunk 0 이후 다른 snapshot이 만들어지면 이전 sequence의 후속 요청은 `STALE_SNAPSHOT`입니다. 앱은 그 snapshot 전체를 버리고 다음 주기에 다시 시작합니다.
- 부팅 누계/세션 누계는 `UINT32_MAX`에서 포화합니다. TIM5 wrap과 60초 deadline은 unsigned 차와 signed deadline 비교로 처리합니다.

## 6. 운영 및 검증

1. VIA 앱의 Diagnostics에서 10/30/60초를 선택하고 시작합니다.
2. 타이핑/마우스 키 등 재현하려는 입력을 수행합니다.
3. 하드 이벤트가 있으면 케이블·허브·호스트 포트를 점검하고, 사용자가 직접 더 낮은 BootMode를 적용한 뒤 별도 세션으로 비교합니다.
4. 펌웨어/진단 프로토콜 버전이 다른 기록은 한 비교 그룹으로 합치지 않습니다.
5. 앱이 "선택 모드와 협상 속도 불일치"를 표시하면 정규화 수치를 비교에 쓰지 말고, 선택 모드가 요구하는 속도로 열거되는 포트/허브로 옮겨 재측정합니다.

자동 복구 부재 검사는 `USB_MONITOR`, `usbInstability`, `usbRequestBootModeDowngrade` 심볼이 소스에 없는지 확인합니다. 계약/포화/wrap 테스트는 `pwsh -NoProfile -File tools/era_via_host_tests/run.ps1`, 보드 빌드는 May65 CMake 명령으로 검증합니다.
