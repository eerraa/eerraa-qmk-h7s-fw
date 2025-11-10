# 키 입력 경로 가이드

## 1. 목적과 범위
- STM32H7S + QMK 포팅 계층에서 8 kHz 스캔 → 디바운스 → 액션 → HID 전송까지의 모든 단계를 정리합니다.
- DMA 기반 하드웨어 스캐너, QMK 매트릭스/퀀텀 계층, VIA RAW HID 경로, USB HID 드라이버가 공유하는 API·데이터 구조를 빠르게 파악할 수 있도록 구성했습니다.
- 대상 모듈: `src/hw/driver/keys.c`, `src/ap/modules/qmk/port/matrix.c`, `src/ap/modules/qmk/quantum/keyboard.c`, `src/ap/modules/qmk/qmk.c`, `src/ap/modules/qmk/port/via_hid.c`, `src/ap/modules/qmk/port/protocol/host.c`, `src/hw/driver/usb/usb_hid/usbd_hid.c`.

## 2. 계층별 파일 맵
| 계층 | 경로 | 주요 역할 |
| --- | --- | --- |
| **하드웨어 스캐너** | `src/hw/driver/keys.c` | TIM16 + GPDMA로 열/행 버퍼를 자동 순환하며 `col_rd_buf`에 최신 상태를 저장합니다.
| **매트릭스 브리지** | `src/ap/modules/qmk/port/matrix.c` | DMA 버퍼를 `matrix_row_t` 배열로 투명하게 노출하고 디바운스 및 CLI 훅을 제공합니다.
| **QMK 퀀텀** | `src/ap/modules/qmk/quantum/keyboard.c` | `matrix_task()`에서 변화 행을 선별하고 `action_exec()` 체인으로 전달합니다.
| **포팅 메인 루프** | `src/ap/modules/qmk/qmk.c` | `qmkUpdate()`에서 VIA RX → `keyboard_task()` → EEPROM → idle 순으로 호출합니다.
| **USB 호스트 래퍼** | `src/ap/modules/qmk/port/protocol/host.c` | `host_keyboard_send()`가 QMK 리포트를 `usbHidSendReport()`에 전달합니다.
| **USB HID 드라이버** | `src/hw/driver/usb/usb_hid/usbd_hid.c` | HID IN 엔드포인트 전송/큐, VIA RAW HID 응답, 폴링 계측을 처리합니다.
| **VIA RAW HID** | `src/ap/modules/qmk/port/via_hid.c` | ISR에서 수집한 패킷을 qbuffer에 저장하고 메인 루프에서 처리합니다.

## 3. API 레퍼런스
### 3.1 하드웨어/매트릭스
| 함수 | 위치 | 설명 |
| --- | --- | --- |
| `bool keysInit(void)` | `src/hw/driver/keys.c` | GPIO/DMA/TIM16을 순서대로 초기화하고 8 kHz 스캔을 시작합니다.
| `bool keysInitGpio/Dma/Timer(void)` | `src/hw/driver/keys.c` | 행 출력, DMA 노드, 타이머 설정을 세분화해 유지보수를 쉽게 합니다.
| `const volatile uint16_t *keysPeekColsBuf(void)` | `src/hw/driver/keys.c` | `.non_cache`에 위치한 DMA 버퍼를 추가 복사 없이 읽을 수 있도록 노출합니다.
| `bool keysReadColsBuf(uint16_t *dst, uint32_t rows)` | `src/hw/driver/keys.c` | 필요 시 안전 복사 경로로 폴백합니다.
| `void matrix_init(void)` | `src/ap/modules/qmk/port/matrix.c` | `raw_matrix`, `matrix` 배열을 초기화하고 CLI 명령을 등록합니다.
| `uint8_t matrix_scan(void)` | `src/ap/modules/qmk/port/matrix.c` | DMA 버퍼를 순회하여 변화 행을 추적하고 디바운스 후 결과를 반환합니다.
| `matrix_row_t matrix_get_row(uint8_t row)` | `src/ap/modules/qmk/port/matrix.c` | 퀀텀 계층이 디바운스된 행을 직접 읽을 수 있게 합니다.
| `void matrixInstrumentation*()` | `src/ap/modules/qmk/port/matrix.c` + `matrix_instrumentation.*` | 스캔 시작 시간/지터를 기록하여 USB HID 계측과 공유합니다.

### 3.2 QMK 퀀텀 & 액션
| 함수 | 위치 | 설명 |
| --- | --- | --- |
| `bool matrix_task(void)` | `src/ap/modules/qmk/quantum/keyboard.c` | 변동 행이 없으면 바로 tick 이벤트만 생성하고, 변화가 있으면 고스트 필터링 → `action_exec()`을 호출합니다.
| `void generate_tick_event(void)` | `src/ap/modules/qmk/quantum/keyboard.c` | 1 kHz tick을 키 이벤트로 주입하여 오토 리핏/타이머 의존 기능을 유지합니다.
| `void keyboard_task(void)` | `src/ap/modules/qmk/quantum/keyboard.c` | `matrix_task()` 결과를 기반으로 퀀텀·RGB·포인팅 디바이스 등 후속 작업을 호출합니다.
| `void qmkUpdate(void)` | `src/ap/modules/qmk/qmk.c` | `via_hid_task()` → `keyboard_task()` → `eeprom_task()` → `idle_task()` 순으로 호출합니다.
| `void action_exec(keyevent_t event)` | `src/ap/modules/qmk/quantum/action.c` | 탭/홀드, 레이어, 콤보 해석 후 `send_keyboard_report()`로 직렬화합니다.
| `void host_keyboard_send(report_keyboard_t *report)` | `src/ap/modules/qmk/port/protocol/host.c` | QMK 호스트 인터페이스를 포팅 계층으로 연결하고, 필요 시 NKRO/LED 상태를 갱신합니다.

### 3.3 USB HID & VIA
| 함수 | 위치 | 설명 |
| --- | --- | --- |
| `bool usbHidSendReport(uint8_t *data, uint16_t len)` | `src/hw/driver/usb/usb_hid/usbd_hid.c` | 즉시 전송 또는 큐잉을 수행하며 계측 훅을 갱신합니다.
| `bool usbHidEnqueueViaResponse(const uint8_t *data, uint8_t len)` | `src/hw/driver/usb/usb_hid/usbd_hid.c` | VIA RAW HID 응답을 SOF 큐에 적재합니다.
| `bool usbHidSetTimeLog(uint16_t idx, uint32_t time_us)` | `src/hw/driver/usb/usb_hid_instrumentation.c` | 매트릭스 스캔 시각을 HID 계측 버퍼에 전달합니다.
| `bool usbHidGetRateInfo(usb_hid_rate_info_t *info)` | `src/hw/driver/usb/usb_hid_instrumentation.c` | 폴링 주기, 지연, 큐 깊이 통계를 제공합니다.
| `void via_hid_init(void)` | `src/ap/modules/qmk/port/via_hid.c` | RX 큐(qbuffer)를 초기화하고 USB ISR 콜백을 등록합니다.
| `void via_hid_task(void)` | `src/ap/modules/qmk/port/via_hid.c` | 메인 루프에서 RX 큐를 비우고 `raw_hid_receive()`를 호출한 뒤 응답을 큐잉합니다.
| `void usbHidSetViaReceiveFunc(void (*func)(uint8_t *, uint8_t))` | `src/hw/driver/usb/usb_hid/usbd_hid.c` | RAW HID OUT 엔드포인트와 VIA 큐를 연결합니다.

## 4. 데이터 구조
| 구조체 / 버퍼 | 위치 | 설명 |
| --- | --- | --- |
| `row_wr_buf[6]` | `src/hw/driver/keys.c` | TIM16 CH1 DMA가 순차적으로 행을 활성화할 때 사용하는 비트 패턴입니다.
| `col_rd_buf[MATRIX_ROWS]` | `src/hw/driver/keys.c` | `.non_cache` 섹션에 위치한 16비트 배열. DMA가 열 입력을 주기적으로 채우며 `volatile`로 선언되어 최신 값을 유지합니다.
| `matrix_row_t raw_matrix/matrix` | `src/ap/modules/qmk/port/matrix.c` | 디바운스 전/후 상태를 저장하는 QMK 표준 배열. `matrix_get_row()`를 통해 접근합니다.
| `matrix_previous[]` | `src/ap/modules/qmk/quantum/keyboard.c` | `matrix_task()`가 이전 스캔과 diff를 계산할 때 사용하는 캐시.
| `via_hid_packet_t` | `src/ap/modules/qmk/port/via_hid.c` | 길이 + 32바이트 버퍼를 포함하여 RAW HID 패킷을 큐에 저장합니다.
| `qbuffer_t via_hid_rx_q` | `src/ap/modules/qmk/port/via_hid.c` | VIA RX 패킷을 FIFO로 관리하며 오버플로우 카운터를 제공합니다.
| `usb_hid_rate_info_t` | `src/hw/driver/usb/usb_hid/usbd_hid.h` | 폴링 주파수, 최대/최소 간격, 초과 시간, 큐 깊이를 기록합니다.
| `usb_hid_time_log` (내부 배열) | `src/hw/driver/usb/usb_hid_instrumentation.c` | 매트릭스 스캔 시작 시간을 기록해 Poll Rate 지터를 재현합니다.

## 5. 제어 흐름
```
TIM16 → DMA (row_wr_buf → GPIOA ODR)
TIM16 → DMA (GPIOB IDR → col_rd_buf)
  ↓
keysPeekColsBuf()
  ↓
matrix_scan() → debounce()
  ↓
matrix_task()
   ├─ 고스트 검사 / switch_events()
   ├─ action_exec() → send_keyboard_report()
   └─ usbHidSetTimeLog() (선택)
  ↓
host_keyboard_send()
  ↓
usbHidSendReport() / 큐잉
```
- VIA RAW HID OUT 패킷은 `USBD_HID_DataOut()` → `usbHidSetViaReceiveFunc()` → `via_hid_receive()` → `via_hid_task()` → `raw_hid_receive()` 순서로 처리됩니다.
- VIA 응답은 `via_hid_task()`에서 `usbHidEnqueueViaResponse()`로 큐잉되며, SOF ISR이 전송을 담당합니다.

## 6. 계측 및 진단
- `_DEF_ENABLE_MATRIX_TIMING_PROBE=1`이면 `matrixInstrumentationCaptureStart()`가 타임스탬프를 기록하고 CLI `matrix info`에서 스캔 속도를 확인할 수 있습니다.
- `_DEF_ENABLE_USB_HID_TIMING_PROBE=1`일 때 `usbHidGetRateInfo()`가 의미 있는 값을 반환하며, `Poll Rate : ...` 로그가 활성화됩니다.
- `usbHidSetTimeLog()` 호출 시 HID 계측 모듈이 키 전송 지연을 히스토그램으로 축적합니다.
- VIA 큐 오버플로우는 ISR에서 카운터만 증가시키고, `via_hid_task()`가 `[!] VIA RX queue overflow : <n>` 로그를 출력합니다.

## 7. 운영 체크리스트
1. 매트릭스 행/열 수를 바꾸면 `row_wr_buf` 패턴과 DMA `DataSize`를 함께 수정해야 합니다.
2. `matrix_task()`에 신규 기능을 추가할 때는 고스트 검사를 통과한 뒤에만 `action_exec()`을 호출하도록 순서를 유지하십시오.
3. `host_keyboard_send()`에서 BT/CDC 경로를 추가하려면 `usbIsConnect()` 상태를 먼저 확인하고, HID 큐에 백업되었을 때의 처리 전략을 마련해야 합니다.
4. VIA RAW HID 기능을 비활성화하려면 `via_hid_task()` 호출을 `qmkUpdate()`에서 조건부로 감싸고, USB HID 엔드포인트에서 RX 콜백 등록을 해제해야 합니다.
5. USB 모니터나 계측을 켠 빌드에서는 `micros()` 호출이 많아지므로, `qmkUpdate()` 루프 내에서 블로킹 호출(예: 긴 CLI 출력)을 피해야 8 kHz 스캔 주기가 유지됩니다.

## 8. 트러블슈팅 가이드
- **특정 열이 항상 눌림 상태**: `keysInitGpio()`에서 해당 핀의 Pull 설정을 확인하고, `col_rd_buf`를 CLI에서 직접 덤프(필요 시 임시 명령 추가)하여 DMA 버퍼 값이 실제로 잘못되었는지 점검합니다.
- **디바운스 미적용**: `debounce()` 호출 이후 `matrixInstrumentationPropagate()` 로깅이 비정상인지 확인하고, `DEBOUNCE` 매크로가 기대값인지 재확인합니다.
- **HID 리포트가 지연됨**: `usbHidGetRateInfo()`의 `time_excess_max`, `queue_depth_max`를 확인하여 큐가 가득 차는지 판단하고, 필요 시 `usbHidSendReport()` 바로 앞에 있는 `usbHidInstrumentationOnImmediateSendSuccess()` 로그를 분석합니다.
- **VIA 명령 누락**: `[!] VIA RX queue overflow` 로그가 나타나면 `VIA_HID_RX_QUEUE_DEPTH`를 늘리거나 `qmkUpdate()` 내 다른 작업을 비동기로 옮겨 메인 루프를 가볍게 해야 합니다.

> **팁**: 스캔/폴링 타이밍을 동시에 확인하려면 `_DEF_ENABLE_MATRIX_TIMING_PROBE=1`, `_DEF_ENABLE_USB_HID_TIMING_PROBE=1`, `USB_MONITOR_ENABLE=1`로 빌드한 뒤 `matrix info on` 명령을 사용하세요. 매 초마다 스캔/폴링/큐 지표가 한꺼번에 출력됩니다.
