# 결정 기록

본 문서는 코드만으로는 알 수 없는 판단 근거를 남깁니다. 세션 간 인수인계용이며,
Claude auto-memory는 Codex CLI와 공유되지 않으므로 공용 사실은 반드시 여기에 기록합니다.

---

## 2026-08-24 — 1차 실기 측정 결과와 보류 항목 처리 (V260824R1)

BRICK60 / Windows 11 / Edge / UGREEN 7-in-1 허브에서 FS 1K·HS 2K·4K·8K 각 30초 세션과
중단·절전 시나리오를 측정했다. 원본 응답과 9개 런 전체 덤프는
`eerraa-qmk-h7s-fw-handover/hardware-validation-V260823R2.md`에 있다.

**정상 확인**: report queue drop 9개 세션 전부 0(부팅 누계도 0), queue depth peak 최대 2,
메인 루프 최대 gap 15~50 µs·1 ms 초과 stall 0회·루프 267 kHz, 세션 중 USB 하드 이벤트 0,
snapshot 주기 999~1014 ms·sequence 9개 런 전부 연속, 진단 ON/OFF 체감차 없음.
브라우저가 1 Hz로 31회 폴링하는 동안 펌웨어 elapsed가 30,000 ms에 도달해 **micros() 1 µs
눈금이 실측으로 교차검증**되었다.

**핵심 발견 — 정규화 축이 모드 순위를 뒤집는다**:
실측 평균 지연은 FS 1K 231 µs / HS 2K 224 µs / HS 4K 232 µs / HS 8K 85 µs다. 즉
FS·2K·4K는 절대값이 사실상 같고 8K만 의미 있게 낮다. 그런데 간격으로 나눈 정규화값은
0.23× / 0.45× / 0.93× / 0.68×이라 UI가 FS를 최고, 4K를 최악으로 보여준다.

원인은 측정 오류가 아니라 **리포트 방출 시점의 위상**이다. FS 1K 히스토그램은 564개 중
(500,1000] µs 구간이 정확히 0개인데, 2차 군집이 `기본군집 + E` 위치(2K→+500, 4K→+250,
8K→+125, FS→+1000)에 있어 폴링 간격 자체는 설정대로 동작함이 확인된다. 기본 군집의
**절대 폭이 모든 모드에서 100~130 µs로 동일**하다. BRICK60은 `DEBOUNCE 5`이고 디바운스
만료 판정이 1 ms 틱(`timer_read_fast`) 단위라 리포트가 항상 밀리초 경계 직후에 나가며,
메인 루프가 267 kHz라 그 경계에 4 µs 이내로 붙는다. E=125 µs일 때는 이 대역이 구간
전체를 덮어 균일해 보이고, E=1000 µs일 때는 좁은 띠로 남아 "매우 빠름"처럼 보인다.
**지연을 지배하는 것은 USB 성능이 아니라 펌웨어의 ms 양자화 위상이다.** 따라서 모드 비교는
정규화값이 아니라 절대 µs로 해야 한다. 앱 비교표에 Avg/Max µs 열을 추가했다.

**UI 결함 — 이전 기록이 현재 결과처럼 표시됨**: 세션이 중단되면 `currentRun`/`snapshots`이
초기화되고 `comparableRuns[0]`으로 말없이 폴백해, "Unmatched firmware session" 배너와
이전 런의 "State: Complete"가 동시에 표시됐다(핸드오버 `c1.png`). Copy 버튼도 그 이전 런을
복사했다. 표시 중인 런의 출처를 명시하도록 수정했다.

**보류 항목 판정**:
- **D-1 수정함** — 아래 별도 절.
- **D-2 변경 없음** — snapshot 주기가 ~1005 ms로 안정. 20 ms 스로틀은 진단을 제약하지 않는다.
  일반 VIA 왕복 비용 문제는 남지만 진단 근거로는 착수하지 않는다.
- **D-3 미해결** — `[D3-01]`이 "모름"이라 서스펜드/미구성 중 폐기 경로의 실제 발생 여부를
  판정할 수 없다. 현재 UI 명칭이 "Report queue drops"로 큐 적재 실패에 정확히 한정되어
  있으므로 거짓은 아니다. 다음 실기에서 재확인한다.
- **D-4 변경 없음** — 마우스키 집중 사용 세션에서도 drop 0. keyboard/EXK 카운터를 분리하지
  않는다(§3 근거 없는 metric 추가 금지).
- **D-5 수정함(앱)** — 9개 런 전부 sequence 연속이라 실제로는 발생하지 않았으나, 창 경계가
  다른 두 계열을 한 점에 그리는 구조는 그대로이므로 host에서 불연속을 감지해 창 최대값을
  제외하도록 했다.
- **D-6 수정함(앱)** — 저장 실패를 UI로 노출.

**미확인으로 남은 것**: `[E-01]`~`[E-06]` 회귀 항목 전부, `[D1-01]`/`[D1-02]`(D-1 영향 빈도),
`[C-04]`/`[C-05]`(허브 FS 강등·다른 컨트롤러). 허브가 High Speed로 정상 협상되어 모드↔속도
경고 배너는 **양성 경로가 미검증**이다(음성 경로는 4개 모드 전부에서 정상 미표시 확인).

---

## 2026-08-24 — D-1 수정: HID IN 엔드포인트 busy 상태 분리 (V260824R1)

`USBD_HID_HandleTypeDef.state` 단일 플래그를 IN 엔드포인트 3개(keyboard `0x81`,
VIA `0x84`, EXK `0x85`)가 공유하고, `USBD_HID_DataIn`이 `epnum`과 무관하게 IDLE로
되돌리며, `USBD_HID_SOF`의 VIA 송신이 그 규약을 우회하던 구조를 교체했다.

- `usbd_hid.h`: `state` → `volatile uint8_t in_ep_busy[16]` (index = `ep_addr & 0x0F`)
- `usbHidEpTryAcquire()` / `usbHidEpRelease()` 도입. 전송 시작은 메인 루프와 TIM2 ISR
  양쪽에서 들어오므로 **검사와 점유를 PRIMASK 임계구역으로 원자화**했다(약 10 cycle).
  기존 check-then-set은 TIM2(prio 0)가 메인 루프를 선점할 때 같은 EP를 이중 무장할 수
  있었다.
- `USBD_HID_DataIn`은 완료된 `epnum`만 해제한다.
- VIA 응답 송신도 동일 규약에 편입해 "IN 전송 진행 중" 불변식을 성립시켰다.
- TIM2 백업 드레인은 각 큐가 자기 엔드포인트 비트만 본다.

**의도한 동작 변화**: keyboard와 EXK가 동시에 in-flight가 될 수 있어 재시도 큐 진입이
줄어든다. 따라서 **HID delivery latency와 queue depth의 기준선이 내려간다.**
`V260823R2` 측정치와 `V260824R1` 측정치를 같은 비교 그룹에 넣지 말 것. 앱은 펌웨어 버전이
다르면 비교 그룹을 분리하므로 자동으로 섞이지는 않는다.

빌드: H7S 5종 전부 통과. RAM 변동 없음, FLASH +96~104 B.
`_DEF_FIRMWARE_VERSION`은 사용자 지정 버전이 없어 `V260824R1`로 임시 지정했다 —
**공식 버전 확인 필요.**

---

## 2026-08-24 — 실기 검증 전 독립 감사 결과 (V260823R2, 펌웨어 코드 변경 없음)

`696fbda`(펌웨어) / `94fd725`(the-via-eerraa) 구현을 실기 검증 투입 가능 여부 기준으로
독립 재검토했다. **펌웨어 코드에서는 BLOCKER를 찾지 못했고 펌웨어 소스는 수정하지 않았다.**
BLOCKER 1건은 앱 표시 계층에서 발견해 `the-via-eerraa`에서만 고쳤다.

**실측으로 확정한 사실** (다음 세션이 다시 재보정하지 않도록 기록):
1. `micros()`는 정확히 1 µs다. HSE 24 MHz → PLL1(M2/N50/P1) → SYSCLK 600 MHz, HCLK
   `DIV2`=300 MHz, APB1 `DIV2`=150 MHz이므로 APB1 타이머 클럭은 300 MHz다.
   `microsInit()`의 prescaler 299가 정확히 1 MHz를 만든다. `bspInit()`의
   `SystemClock_Config()`가 `hwInit()→microsInit()`보다 먼저 실행되므로
   `SystemCoreClock`은 이미 600 MHz다. wrap 주기는 4,295초로 최대 세션 60초의 71배다.
2. `usbDiagnosticsCapture()`의 임계구역 복사량은 정확히 **292 B**다
   (`sizeof(session_internal)`=272 + `hard_counters`=20, mingw gcc로 실측).
   `usb_diagnostics_snapshot_t`는 236 B이며 기존 문서의 232 B 표기는 오차다.
3. `PRIMASK` 임계구역은 USB/TIM2 IRQ를 포함해 전역 차단이지만, 292 B 복사는 600 MHz
   M7에서 1 µs 미만이고 빈도가 약 1 Hz다. 8 kHz의 125 µs 예산 대비 구조적 위험이 없다.
4. 진단 OFF 오버헤드를 디스어셈블리로 확인했다. `USBD_HID_SOF`에는 진단 코드가 전혀
   없고(레거시 monitor의 SOF당 `micros()`가 사라진 순이득), `USBD_HID_DataIn`·
   `qmkUpdate`는 `usbDiagnosticsIsActive()` 호출 1회만 추가된다.
   `usbHidSendReport`는 리포트당 28 B 스택 zero-init과 `qbufferAvailable()` 1회가
   늘었고, 8 kHz 기준 합계 0.05 % 미만이다.
5. May65 RAM 65,572 B / FLASH 122,168 B로 보고값과 일치했고, 진단 opt-in H7S 5종
   (may65, intigrity80, brick60, brick65, sculpturei) 모두 ARM 빌드가 통과했다.

**BLOCKER (앱 측, 수정 완료)**: 펌웨어는 세션 시작 시점의 **선택 BootMode**로
`expected_interval_us`를 계산하는데, 이는 실제 enumerate된 link speed가 아니다.
HS 8K를 선택한 채 FS만 지원하는 hub/port에 연결하면 실제 간격은 1000 µs인데 기준은
125 µs가 되어 모든 표본이 `> 4.00×` 버킷에 들어간다. 정상 FS 연결을 장치 결함으로
오판하게 만드는 표시이며, 실기 체크리스트가 허브·다른 PC를 명시하므로 실제로 발생한다.
펌웨어 wire contract는 바꾸지 않았다 — snapshot이 mode와 negotiated speed를 이미 함께
보내므로 앱이 정합성을 판정해 경고·보고서 문구·비교표 row 표시로 처리한다. 선택 mode를
숨기거나 자동 보정하지 않는 편이 "8K로 동작하지 않았다"는 사실을 더 정확히 전달한다.

**보류 항목의 처리 방침 (사용자 결정, 2026-08-24)**:
실기 테스트를 먼저 수행하고 그 뒤에 보류 항목을 수정한다. 펌웨어는 어차피 전량
재배포되므로 **배포 위험이 아니라 아키텍처적 정당성**을 판단 기준으로 삼는다.
아래 §"보류 항목 아키텍처 판단"에 설계와 접점을 확정해 두었으므로, 실기 결과가
나오면 그 판단대로 착수한다.

**실기에서만 판정 가능한 항목 (POST-HARDWARE)**:
- `USBD_HID_DataIn`은 epnum과 무관하게 `p_hhid->state`를 IDLE로 되돌리고,
  `USBD_HID_SOF`의 VIA 응답 송신은 그 state를 BUSY로 잡지 않는다. **이는 이 커밋
  이전부터 존재한 드라이버 동작이다.** 키보드 IN이 in-flight인 순간 VIA IN이 완료되면
  드물게 latency 표본이 새 요청 시각과 짝지어져 과소 측정될 수 있다. 타이핑 중
  세션 기준 대략 25초에 1회 수준으로 추정했으나 실기 확인이 필요하다.
- VIA 응답은 마지막 enqueue로부터 20 ms가 지나야 SOF에서 배출된다(기존 동작).
  직렬 왕복이므로 12 chunk snapshot 1회는 최소 약 240 ms가 걸린다. 1 Hz 폴링에는
  여유가 있지만 실기에서 실제 주기를 확인한다.
- 스냅샷 읽기가 중간에 실패하면 그 주기의 chunk 0에서 이미 창 최대값이 초기화된다.
  이후 trend 점은 histogram 차분 창(2주기)과 창 최대값(1주기)의 범위가 어긋난다.
  드물고 방향이 보수적이라 수정하지 않았다.
- report drop 카운터는 keyboard 큐와 EXK 큐의 적재 실패를 합산한다.

---

## 2026-08-24 — 보류 항목 아키텍처 판단 (실기 테스트 후 착수)

배포 위험이 아니라 구조적 정당성으로만 판단한 결과다. 우선순위 순.

### D-1. HID IN 엔드포인트 busy 상태를 엔드포인트별로 분리 — **구조적 결함, 수정해야 함**

**현재**: `USBD_HID_HandleTypeDef.state`(IDLE/BUSY) **하나**를 IN 엔드포인트 3개가 공유한다.
keyboard `0x81`, VIA `0x84`, EXK `0x85`. `USBD_HID_DataIn`은 `epnum`과 무관하게 IDLE로
되돌리고(`usbd_hid.c:1041`), `USBD_HID_SOF`의 VIA 송신은 이 플래그를 읽지도 세우지도
않는다(`usbd_hid.c:1082`).

**왜 구조적으로 틀렸는가**: USB IN 엔드포인트는 각자 FIFO와 전송 상태를 가진 독립 자원이다.
플래그 하나로 묶으면 (1) EXK/VIA 전송이 keyboard 전송을 불필요하게 막아 8 kHz 경로에서
리포트를 재시도 큐로 밀어내고, (2) 무관한 엔드포인트의 완료가 남의 busy를 해제한다.
게다가 VIA 경로가 플래그를 우회하므로 "IN 전송이 진행 중"이라는 불변식 자체가 성립하지
않는다. 지금 VIA 재무장이 안전한 이유는 20 ms 스로틀이라는 **우연**이지 설계가 아니다.

**결과**: keyboard IN이 in-flight인 동안 VIA IN이 완료되면 state가 풀려서, 활성 EP1을
재무장할 수 있고(리포트 손실/FIFO 오염 가능) 진단의 `report_in_flight` 상관도 깨진다.

**올바른 설계**: 엔드포인트 번호로 인덱싱하는 busy 비트로 대체한다.
`USBD_HID_HandleTypeDef`의 단일 `state`를 `uint32_t in_ep_busy` 비트맵(또는 소형 배열)로
바꾸고, 각 `USBD_LL_Transmit` 직전에 해당 EP 비트를 세우고 `USBD_HID_DataIn(epnum)`이
**그 EP 비트만** 지운다. VIA 송신도 같은 규약에 편입한다(자기 비트 확인 후 세움).
`USBD_HID_Init`/`DeInit`에서 전체 clear.

**접점(전수 확인함, 9곳)**: `usbd_hid.h:100`, `usbd_hid.c:592`(Init), `863·866`(SendReport),
`899·902`(SendReportEXK), `1041`(DataIn), `1082`(SOF VIA 송신), `1332·1351`(TIM2 백업 드레인).

**부수 효과 — 반드시 인지할 것**: 수정 후에는 keyboard와 EXK가 동시에 in-flight가 될 수
있으므로 재시도 큐 진입이 줄고 **HID delivery latency와 queue depth의 기준선이 내려간다.**
따라서 **수정 전 실기 수치와 수정 후 수치를 직접 비교하면 안 되고**, 수정 후 전 모드
재측정이 필요하다. 이 때문에 실기 baseline을 먼저 확보하는 현재 순서가 옳다.

**검증**: H7S 5종 빌드 + 호스트 테스트 + 실기에서 타이핑/EXK(미디어·마우스키)/VIA 동시
부하 회귀. 특히 EXK와 keyboard 동시 전송이 정상인지 확인.

### D-2. VIA 응답 스로틀의 기준점 — **구조적으로 어색함, 재검토 대상**

`usbHidEnqueueViaResponse()`가 **enqueue마다** `via_report_pre_time = millis()`를 갱신하고
`USBD_HID_SOF`가 `millis()-via_report_pre_time >= 20`일 때만 배출한다(`usbd_hid.c:1079·1151`).
즉 지연 기준이 "마지막 **송신**"이 아니라 "마지막 **적재**"다. 요청이 20 ms 안에 계속
들어오면 배출 시점이 계속 밀리는 구조이며, 지금 문제가 없는 것은 호스트가 직렬이기
때문이다. 실질 효과는 **VIA 왕복 1회당 최소 20 ms**이고, 12-chunk snapshot 1회가
약 240 ms를 차지해 1 Hz 세션 동안 VIA 트래픽이 벽시계의 약 25 %를 점유한다.

이 20 ms가 과거 호스트 측 레이스 회피용이었다면 그 근거를 먼저 확인해야 한다. 근거가
없다면 기준점을 **송신 시각**으로 옮기거나(진짜 rate limit) 값을 낮추는 것이 옳다.
근거가 있다면 그 이유를 이 문서에 남긴다. 진단 기능이 현재 VIA 최대 소비자이므로
실기에서 snapshot 1회 실측 소요를 먼저 기록한 뒤 판단한다.

### D-3. 손실 경로 계수 범위 — **데이터로 결정**

`usbHidSendReport`의 리포트 손실 경로는 셋인데 하나만 계수된다.
(1) 재시도 큐 포화 → 계수됨, (2) `USBD_is_suspended()` → 조용히 폐기,
(3) TIM2 드레인 중 `dev_state != CONFIGURED` → 조용히 폐기(`usbd_hid.c:1340` 반환값 무시).
현재 UI 명칭이 "Report queue **drops**"로 (1)에 정확히 한정되어 있으므로 **거짓은 아니다.**
다만 (2)(3)도 실제 전달 실패다. 서스펜드/미구성은 이미 하드 이벤트로 노출되므로
사용자가 상관할 수 있다. 실기에서 (2)(3)이 실제로 관측되면 그때 계수 범위를 넓히고
명칭을 함께 바꾼다. 관측되지 않으면 현행 범위를 유지한다.

### D-4. keyboard/EXK 드롭 분리 — **데이터로 결정, wire 변경 필요**

현재 `report_drops`는 두 큐를 합산한다. 구조적으로는 별개 큐·별개 결과이므로 분리가
옳지만, 분리하려면 snapshot payload에 필드가 필요하다. chunk 7의 26~31바이트가 비어
있으나 앱이 이를 `bytesAreZero`로 검증하므로 양쪽 동시 변경이다. 실기에서 EXK 드롭이
실제로 발생하는지 먼저 확인하고, 발생하지 않으면 분리하지 않는다(§3 "근거 없이 metric
추가 금지").

### D-5. 앱 — trend 창 정합성 (`buildUsbDiagnosticsTrend`)

p99는 **채택된** snapshot 두 개 사이, 창 최대값은 **capture** 두 개 사이로 창 경계가 다르다.
snapshot 읽기가 중간 실패하면 그 주기의 chunk 0이 이미 창을 리셋했으므로 다음 점에서
두 계열의 범위가 어긋난다. 펌웨어는 이미 `sequence`(capture마다 증가)를 보내므로 호스트가
`sequence - previousSequence != 1`을 감지할 수 있다. **펌웨어를 바꿀 필요 없이** 호스트에서
불연속 구간의 창 최대값을 제외하거나 점을 분리하는 것이 올바른 처리다.

### D-6. 앱 — localStorage 저장 실패 노출

`saveUsbDiagnosticsRun()`이 `boolean`을 반환하지만 `finishActive()`가 무시해 조용한 데이터
유실 경로가 된다. 반환값을 UI 상태로 전파한다. 쿼터가 실제로 문제가 되면 IndexedDB로
재설계하지 말고(§26 금지) 오래된 run의 보관 snapshot 수를 줄인다.

---

## 2026-08-23 — 자동 USB 복구 폐기와 관측 전용 전달 진단 (V260823R2)

**문제 감사**:
1. 기존 `usbHidMonitor*`는 8 kHz SOF 간격을 점수화했지만 SOF는 HID 리포트 전달 완료가 아니다. 점수가 `usbRequestBootModeDowngrade()`의 ARM→COMMIT, BootMode EEPROM 쓰기, 지연 reset까지 촉발해 관측과 복구가 결합되어 있었다.
2. 별도 compile-time `usbd_hid_instrumentation.*`는 즉시 submit 또는 queue dequeue부터 DataIn까지를 재서, 큐 대기 시간이 빠지고 실제 생성→전달 지연을 일관되게 나타내지 못했다. SOF/raw log/CLI 계측도 사용자 진단과 중복됐다.
3. USB IRQ 우선순위는 2, TIM2는 0이며 `micros()`는 TIM5 CNT 읽기다. 일반 EEPROM main-loop 처리가 USB SOF IRQ를 직접 막는 구조는 아니므로, SOF 지연을 곧바로 펌웨어 stall이나 host delivery failure로 해석하지 않기로 했다.

**결정**:
1. `USB_MONITOR_ENABLE`, SOF 점수/warmup/timeout, 열거·속도·suspend 점수, 자동 8k→4k→2k→1k 큐, monitor EEPROM 토글(+32), VIA channel 13 ID 3을 모두 제거했다. +32 슬롯은 이후 주소를 움직이지 않도록 `EECONFIG_USER_RESERVED_32`로만 남겼다.
2. BootMode는 FS 1 kHz 기본과 사용자의 VIA/CLI 선택·Apply·EEPROM·응답 유예 reset만 유지한다. 진단 subsystem은 이 API를 호출하지 않는다.
3. 항상 켜지는 것은 포화형 하드 이벤트 카운터뿐이다: keyboard/EXK report queue drop, USB reset, HID configuration, suspend, speed change. EEPROM에 쓰지 않는다.
4. 10/30/60초 사용자 세션 동안만 keyboard report 요청 시각을 재시도 큐에 보존해 실제 keyboard IN `DataIn`까지 측정한다. 8개 정규화 histogram, min/average/max, 창 최대, 큐 최고점, `qmkUpdate()` 간격, 1 ms 초과 stall, 최근 8개 event ring을 고정 RAM에 둔다.
5. SOF 고해상도 표본, raw stream, matrix 실행시간, 합성 안정성 점수는 제외했다. 실제 전달과 인과가 다르거나 8 kHz hot path 비용·오판 가능성이 크기 때문이다. matrix compile-time 개발 계측은 USB 진단과 분리했다.
6. VIA 계약은 GET/SET_KEYBOARD_VALUE selector `0x07`, version 1, big-endian, 32 B 고정, request tag, session ID, frozen snapshot sequence, 18 B payload chunk다. chunk 0이 snapshot을 동결하고 후속 chunk는 같은 sequence만 받는다. 상세 byte 배치는 `docs/features_usb_diagnostics.md`가 규범이다.
7. 일반 VIA 장치 probe는 펌웨어가 아니라 앱의 정의 manifest opt-in으로 차단한다. 연결 세대 변경·unsupported version·stale chunk는 앱이 사실 상태로 처리하며 자동 recovery를 시도하지 않는다.

**비용과 검증**:
- idle SOF 경로에서 monitor 분기와 TIM5 읽기를 제거했다. idle main loop는 active 플래그 분기만 수행한다. 세션 중에는 main loop당 CNT 1회, keyboard report당 요청/완료 CNT를 읽고 snapshot은 약 1 Hz다.
- 고정 진단 상태는 session 272 B(histogram/threshold 60 B, event ring payload 96 B 포함) + frozen snapshot 232 B + boot counter 20 B + 상태값 6 B이다. 재시도 큐 원소에 6 B metadata를 추가해 128개 기준 768 B가 증가한다. 동일 May65 빌드의 `V260823R1` 대비 실측 순증가는 RAM 1,176 B(64,396 → 65,572 B), FLASH 44 B(122,124 → 122,168 B)이며 heap/EEPROM 증가는 없다.
- `tools/era_via_host_tests/run.ps1`에 capability/tag/reserved/version, session, multi-chunk consistency, stale sequence, normalized histogram, hard event, deadline wrap, 포화, RAM 상한 테스트를 추가했다.
- May65 `MinGW Makefiles` ARM 빌드 성공. 첫 NMake 자동 선택 실패는 CMake가 저장소 GNU make에 NMake `-?`를 보낸 환경 문제였고 소스 컴파일 전이었다. 실기 USB/허브 비교는 별도 필요하다.

---

## 2026-08-23 — MOUSE VIA 페이지 · exact-ms JSON 노출 · state-sync 보강 (V260823R1)

**배경**: custom VIA 앱(`the-via-eerraa`)과 custom QMK(`qmk_firmware_eerraa`)의 Non-Split ERA 기능을 이 H7S 코드베이스에 맞추는 작업. 검증 + 이식을 함께 수행했다.

**검증 결과 (V260821R1 exact-ms / state-sync)**:
1. exact-ms 배치(channel 15 ID 5, channel 16 ID 41~48, 2-byte BE, 100~500)는 앱이 번들한 정의(`the-via-eerraa/public/definitions/era/v3/1163001890.json`)와 **정확히 일치**했다. 앱의 `ExactMillisecondControl`은 `type: "range"` + `id_*_term_exact` 이름 + `options: [100, 500]`으로 트리거되고, 2바이트 BE로 읽고 쓴다.
2. **공식 VIA 호환 vs Custom VIA JSON 관리 분리**:
   - **이 리포(`eerraa-qmk-h7s-fw-via`)**: 공식 웹 VIA(`usevia.app`) 호환용 드롭다운(dropdown) 방식을 유지한다.
   - **Custom VIA 앱(`the-via-eerraa`)**: `era-definitions/custom/v3/`에서 exact-ms `range` 슬라이더([100, 500]) 및 `exactMsFamily: "h7s"` 계약을 적용한 JSON을 별도로 관리한다.
   - 펌웨어 C 계층은 공식 VIA의 legacy dropdown GET/SET과 Custom VIA의 exact-ms 2-byte BE GET/SET을 모두 완벽하게 지원하는 듀얼 호환 구조로 동작한다.
3. **결함 ②**: `era_state_sync_via_command()`에 INVALID 판정 팔이 없어 `ERA_STATE_SYNC_STATUS_INVALID` 상수가 죽어 있었고, 길이 검사도 6바이트였다. 참조 구현과 앱 파서(`parseStateSyncEnvelope`)가 모두 32바이트 봉투 + 전 구간 0을 요구하므로 그에 맞췄다.
4. **결함 ③**: CONFIG revision이 tapping/tapdance/layout options에서만 올라갔다. 참조는 `nvm_eeprom_changed_kb` 캐치올로 모든 EEPROM 설정 쓰기에서 올린다. 이 포팅층에는 그 훅이 없으므로 채널별로 **값이 실제로 바뀐 SET에서만** 올리도록 추가했다: 디바운스(14), KKUK(12), SOCD(10/11), 인디케이터(0), BootMode(13), USB 모니터(13), 그리고 `id_eeprom_reset`(세 도메인 전부).
5. **결함 ④ — 기존 이미지의 스택 오버플로**: `report_exk_q`가 원소 크기를 `sizeof(report_info_t)`(=`HW_KEYS_PRESS_MAX`+2 = **22**)로 생성하고 있었는데, 실제 원소는 `exk_report_info_t`(변경 전 **9바이트**)였다. `qbuffer`는 생성 시 받은 size만큼 복사하므로:
   - `qbufferRead(q, (uint8_t *)&report_info, 1)`이 스택의 9바이트 지역변수에 **22바이트를 쓴다 → 13바이트 스택 오버플로**. EXK 재시도 큐를 드레인할 때마다 발생한다.
   - `qbufferWrite`는 반대로 9바이트 지역변수에서 22바이트를 읽는다.
   - 백킹 배열(`exk_report_info_t[128]` = 1152B)도 큐가 2816B로 가정하므로 인덱스 52부터 배열 밖을 침범한다.

   발화 조건은 "EXK 전송이 BUSY로 실패해 큐에 적재된 뒤 드레인될 때"다. 미디어/시스템 키를 누른 순간 HID EP가 바쁜 경우에만 생기므로 드물어 현장에서 드러나지 않았다. `sizeof(exk_report_info_t)`로 교정했다. **NKRO 때문에 생긴 문제가 아니라 이번 조사에서 발견한 기존 결함**이며, EP를 32B로 키우면(원소 33B > 22B) 이번에는 반대로 절단이 되므로 어느 쪽이든 수정이 필요했다.

**신규 결정 (이식)**:
1. **마우스는 EXK 인터페이스의 리포트 ID 2로 내보낸다.** 리포트가 6바이트라 **기존 8바이트 엔드포인트에 그대로 들어가므로 엔드포인트 크기·FIFO 배분·인터페이스 개수가 모두 그대로다.** 인터페이스 0(키보드)은 한 바이트도 건드리지 않았다.
2. **NKRO는 구현했다가 되돌렸다 (오너 결정 2026-08-23).** 기본 상태가 이미 전환 없이 20키(`HW_KEYS_PRESS_MAX` = 20)이므로 240키로 늘리는 기능적 실익이 없는 반면, NKRO 리포트(32B)를 실으려면 EXK EP를 8B → 32B로 키워야 해서 **NKRO를 켜지 않는 사용자까지 재열거와 RAM +3KB(재시도 큐 1152B → 4224B)를 부담**한다. 6KRO↔NKRO 토글은 "부트 호환이냐 동시입력이냐"의 교환을 사용자에게 떠넘기는 장치인데, 현재 구성은 그 교환 자체가 없다. 상세와 재도입 시 설계는 `docs/features_usb_hid_reports.md` §5.
3. **VIA 채널 번호는 참조 QMK와 다르다.** 참조는 MOUSE=13을 쓰지만 이 코드베이스에서 13은 USB POLLING이 점유하므로 **MOUSE=17**을 새로 할당했다. value id 1~6 배치는 참조와 동일하게 유지했다. VIA는 정의를 (vendorId, productId)별로 캐시하므로 H7S 정의가 별도인 이상 번호 일치는 요구되지 않는다.
4. MOUSE 페이지는 참조 `era_mousekey.c`의 의미론을 그대로 옮겼다: 최고 속도와 가속 시간은 저장하지 않고 파생하며, 가속 off는 최고 속도가 아니라 **첫 스텝 속도**로 고정한다. 실측 기본값(Start 4px / Top 16px / 램프 1.0초 / 100 이벤트/초)도 참조의 2026-08-18 측정치를 그대로 쓴다. 근거는 `docs/features_mousekey.md` §6.
5. `MOUSEKEY_MOVE_DELTA` / `MOUSEKEY_WHEEL_DELTA`를 `ERA_MOUSEKEY_RUNTIME_DELTA` 아래에서만 변수로 승격했다(참조와 동일한 fork). 이 페이지가 유일한 쓰기 주체다.
6. `_DEF_FIRMWARE_VERSION`을 `V260823R1`로 올렸다. **5개 보드 모두 `AUTO_FACTORY_RESET_ENABLE 1`이므로 이 이미지 첫 부팅에서 EEPROM이 공장 초기화된다.** 신규 `EECONFIG_USER_MOUSEKEY` 슬롯(+152, 16B)은 그 경로에서 채워지고, 초기화가 없더라도 시그니처 불일치로 기본값이 기록된다.

**인터페이스 0의 부트 규격 편차 (조사 결과 + 유지 결정, 오너 2026-08-23)**: 이번 작업 중 확인한 사실이며 **V260823R1 이전부터 있던 상태**다. 전문은 `docs/features_usb_hid_reports.md` §3~4.
- `HW_KEYS_PRESS_MAX`가 20이라 인터페이스 0의 IN 리포트는 **22바이트/20키 배열**이다. 빌드 이미지에서 추출한 디스크립터가 `95 14 75 08`(REPORT_COUNT 20)로 이를 확증한다. **6KRO가 아니다.**
- 그럼에도 BIOS에서 동작해 온 이유는 22바이트의 **앞 8바이트가 부트 포맷과 바이트 단위로 같기** 때문이다.
- 규격 편차 두 가지: ① `SET_PROTOCOL(boot)` 이후에도 8바이트로 줄이지 않는다. ② 인터페이스 0의 `GET_REPORT`에 핸들러가 없어 STALL된다.
- **결정: 둘 다 고치지 않는다.** 고치려면 재시도 큐와 SOF 드레인이 얽힌 8000Hz 송신 경로를 프로토콜 상태에 따라 가변 길이로 바꿔야 하고, 현장 실패 보고가 없다.
- 이 사실이 NKRO를 되돌린 판단의 근거이기도 하다(위 신규 결정 2번).

**알려진 한계(수용)**: `mk_time_to_max`가 1바이트라 Cursor Acceleration의 도달 범위가 갱신 주기에 따라 좁아진다. 200 /s(5ms)에서 상한은 1.275초이므로 JSON이 함께 제시하는 1.5s·2.0s는 조용히 잘린다. 참조 QMK도 같은 옵션 목록·같은 동작이고, 주기별로 옵션 목록을 바꾸는 것은 VIA 정의 형식으로 표현할 수 없다. **되읽기가 실제 값을 정직하게 보고하는 것**으로 계약을 맞췄고, 호스트 테스트가 이를 검증한다(`docs/features_mousekey.md` §6-2).

**검증**: 5개 보드 전부 빌드 성공(경고 0). `tools/era_via_host_tests/run.ps1` 통과 — 기존 exact-ms/state-sync 케이스에 MOUSE 단위 환산(최고속도 반올림, 램프 시간 보존, 가속 off의 첫 스텝 고정, 클램프 정직성)과 state-sync INVALID 팔을 추가했다. EXK 리포트 디스크립터는 파서로 검증(129바이트, 컬렉션 균형, 리포트별 크기 MOUSE 6B / SYSTEM 3B / CONSUMER 3B)했고 같은 계약을 `usbd_hid.c`의 `_Static_assert` 3개로 고정했다. **실기 검증은 아직 하지 않았다** — USB 재열거, 마우스 키 동작, 미디어 키, 8kHz 유지 여부는 실기에서 확인이 필요하다.

**남은 항목(이 리포 밖)**: `the-via-eerraa`의 `public/definitions/era_advanced.json`에는 H7S 보드 중 `brick60-h7s`(0x45520022) 하나만 등록되어 있다. MAY65(0x0030)·INTIGRITY80(0x0028)·BRICK65(0x0023)·SCULPTUREI(0x0034)는 `stateSync`/`exactMsFamily` 미등록이라 앱이 state-sync 폴링을 하지 않는다. exact-ms 컨트롤은 JSON의 `options: [100, 500]`이 fallback을 이기므로 정상 동작한다.

---

## 2026-08-21 — VIA GET 0x06 리비전과 exact-ms term (V260821R1)

**결정**:
1. GET_KEYBOARD_VALUE selector `0x06` 리비전 봉투는 vendored `via.c` GET switch에서 버퍼만 채운다. `raw_hid_send` stub는 비워 두고, 실제 TX는 기존 `via_hid_task` → `usbHidEnqueueViaResponse` 단일 생산자를 유지한다.
2. Global exact-ms는 channel 15 ID 5, TD0–TD7 exact-ms는 channel 16 ID 41–48, wire는 2-byte big-endian uint16 ms. 기존 ID는 삭제/재할당하지 않는다.
3. exact SET은 100–500만 허용하고 범위 밖은 거절한다. load/sync는 유효한 uint16을 20ms 격자로 되돌리지 않는다. legacy GET만 floor-to-20ms 투영한다.
4. CONFIG revision은 값이 실제로 바뀐 tapping/tapdance SET 뒤에 올린다. KEYMAP/MACRO revision은 VIA keymap/macro write 명령 뒤에 올린다.
5. `_DEF_FIRMWARE_VERSION`을 `V260821R1`로 올려 AUTO_FACTORY_RESET 쿠키를 갱신한다. Brick60은 `AUTO_FACTORY_RESET_ENABLE 1`이므로 이 이미지 첫 부팅에서 EEPROM이 공장 초기화된다.

**검증**: `tools/era_via_host_tests/run.ps1` (exact/legacy vector + GET 0x06 + single-producer source check).

---

## 2026-07-20 — 에이전트 협업 재설정: graphify 지식그래프 도입 및 문서 체계 개편

**결정**:
1. graphify 지식그래프를 도입하고 산출물(`graphify-out/graph.json`, `GRAPH_REPORT.md`, `manifest.json`, `.graphify_labels.json`, 시맨틱 캐시)을 리포에 커밋한다. 코드 탐색은 그래프 질의를 1순위로 한다 (AGENTS.md §3).
2. 그래프 스코프는 `.graphifyignore`로 **"실제 컴파일되는 코드 + 프로젝트 문서"**로 한정한다. 전체 리포(1,046파일/247만 단어) 대신 360파일/19.7만 단어. 근거: 에이전트 이해 1순위 — 미컴파일 QMK 업스트림 원본(quantum 504→~100파일)과 벤더 HAL/CMSIS가 그래프에 있으면 커뮤니티/god node가 죽은 코드에 오염되고 "활성 기능"으로 오인할 위험이 있음. 토큰 절약은 2순위 부수 효과. 컴파일 목록의 기준은 `src/ap/modules/qmk/CMakeLists.txt`이며, 이 목록이 바뀌면 `.graphifyignore`를 함께 갱신해야 한다.
3. 세팅의 원격 유지: 커밋되는 `.claude/settings.json`의 SessionStart hook이 `tools/graphify/bootstrap.py`를 실행해 새 워크스페이스에서도 git hook 설치·그래프 동기화를 자동 복원한다. git post-commit/post-checkout hook과 graph.json merge driver는 graphify가 공용 git dir에 설치하므로 같은 리포의 모든 worktree(orca 워크스페이스)에 공유된다. Codex CLI는 AGENTS.md §3 체크리스트로 동일 부트스트랩을 실행한다.
4. 문서 감사(에이전트 39개, 전 문서 코드 대조) 결과: `docs/review.md`(V251124R6 리뷰 스냅샷 — 코드 변경 이력 주석과 커밋 9cbeb3b로 완전 복원 가능)와 `AGENTS.cursorrules`(AGENTS.md 구버전 사본, 고유 정보는 3308d68:AGENTS.md에서 복원 가능)를 폐기. 나머지 문서의 stale 항목 약 25건을 교정(자세한 내역은 이 커밋의 diff 참조).
5. 문서 역할 원칙 확정: 코드 구조·호출 관계는 그래프로 질의하고, `docs/`에는 코드에서 복원 불가능한 것(결정 기록·실측 회고·운영 가이드·릴리스 안내)만 둔다. 구조 설명만을 위한 신규 문서는 만들지 않는다.

**미해결(문서 공백, 감사에서 high 판정)**: ① 보드 추가/포팅 절차(board bring-up), ② USB 클래스/컴포지트(HID·CDC·CMP) 아키텍처. 필요해지는 시점에 운영 지식 중심으로 작성 검토.

---

## 2026-07-20 — MAY65 underglow 기본값 OFF 및 버전 상향 (V260720R1)

**결정**: `era/keynetix/may65`의 `RGBLIGHT_DEFAULT_ON`을 `false`로 변경하고,
`_DEF_FIRMWARE_VERSION`을 `V260701R2` → `V260720R1`로 상향한다.

**버전 상향이 필수인 이유**: `RGBLIGHT_DEFAULT_ON`은 펌웨어 전체에서
`eeconfig_update_rgblight_default()` 한 곳에서만 소비되고
(`quantum/rgblight/rgblight.c:885`), 이 함수는 `rgblight_init()`의
`if (!rgblight_config.mode)` 가드 안에서만 호출된다(`rgblight.c:918-921`).
즉 **EEPROM 팩토리 초기화 시 1회만** 기록되는 공장 초기값이지 매 부팅 정책이 아니다.
`AUTO_FACTORY_RESET_ENABLE 1`(may65 `config.h:21`)의 리셋 쿠키가
`_DEF_FIRMWARE_VERSION`에서 파생되므로(`hw_def.h:50-57`), 버전을 올리지 않으면
기존 기기의 EEPROM이 유지되어 저장된 `enable=1`이 계속 이긴다.
상수만 바꾸면 체감상 아무 변화가 없다.

**팩토리 리셋 수용 근거**: 버전 쿠키 상향은 동적 키맵·VIA 설정까지 전부 초기화한다
(`quantum/eeconfig.c`의 `eeconfig_init_quantum`). MAY65는 2026-07-20 기준
**아직 판매가 시작되지 않아** 잃을 사용자 설정이 없으므로 사용자가 명시적으로 허용했다.
단 이 면제는 MAY65 한정이며, 이미 출시된 보드(brick60/brick65/intigrity80/sculpturei)는
전역 버전 쿠키를 공유하므로 해당 보드 릴리스 시 영향을 별도로 판단해야 한다.

**인디케이터 무영향 근거**: underglow OFF는 인디케이터(물리 WS2812 0번)에 영향을 주지 않는다.
채널이 분리되어 있고(`HW_WS2812_CAPS 0` vs `HW_WS2812_RGB 1` / `RGB_CNT 26`),
`rgblight_render_frame()`의 비활성 분기는 underglow 버퍼
(`effect_start_pos..effect_end_pos`)만 0으로 밀며, 인디케이터 렌더 콜백은
`rgblight_config.enable`을 참조하지 않고 항상 실행된다(`rgblight.c:1632-1678`).

**검증**: `cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/keynetix/may65'` 빌드 성공,
`MAY65-V260720R1.uf2` 생성 확인(239,616 bytes).
