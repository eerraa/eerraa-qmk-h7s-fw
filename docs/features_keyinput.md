# 키 입력 경로 가이드

## 1. 목적과 범위
- STM32H7S + QMK 포팅 계층에서 8 kHz 매트릭스 스캔 → 디바운스 → 퀀텀 액션 → USB HID 전송까지의 모든 단계를 정리합니다.
- DMA 기반 하드웨어 스캐너, QMK `matrix.c` 브리지, 퀀텀 액션 체인, VIA RAW HID 경로가 어떤 API를 공유하는지 빠르게 파악할 수 있도록 구성했습니다.
- 대상 모듈: `src/hw/driver/keys.c`, `src/ap/modules/qmk/port/matrix.c`, `src/ap/modules/qmk/quantum/{keyboard.c,action.c}`, `src/ap/modules/qmk/qmk.c`, `src/ap/modules/qmk/port/{protocol/host.c,via_hid.c}`, `src/hw/driver/usb/usb_hid/usbd_hid.c`.

## 2. 계층별 맵
| 계층 | 파일 | 책임 |
| --- | --- | --- |
| 하드웨어 스캐너 | `src/hw/driver/keys.c` | TIM16 + GPDMA로 행/열 스캐닝을 자동 순환하고 `.non_cache` 버퍼에 최신 상태를 유지합니다. |
| 매트릭스 브리지 | `src/ap/modules/qmk/port/matrix.c` + `matrix_instrumentation.*` | DMA 버퍼를 직접 참조해 디바운스/계측/CLI 훅을 제공합니다. |
| 퀀텀 코어 | `src/ap/modules/qmk/quantum/keyboard.c` + `quantum/action.c` | `matrix_task()` 변화 감지 → `action_exec()` → `host_keyboard_send()` 흐름을 담당합니다. |
| 포팅 메인 루프 | `src/ap/modules/qmk/qmk.c` | `qmkUpdate()`가 VIA RX → `keyboard_task()` → EEPROM → Idle 순으로 호출됩니다. |
| USB 호스트 래퍼 | `src/ap/modules/qmk/port/protocol/host.c` | QMK 보고서를 `usbHidSendReport()`에 전달하고 NKRO/LED 상태를 동기화합니다. |
| USB HID/계측 | `src/hw/driver/usb/usb_hid/usbd_hid.c` + `usb_hid_instrumentation.c` | HID IN 엔드포인트, VIA RAW HID 큐, 폴링 계측을 처리합니다. |

## 3. 하드웨어 스캐너 (`src/hw/driver/keys.c`)
- `keysInit()`은 GPIO → DMA → TIM16 순으로 초기화한 뒤 타이머/채널을 스타트합니다.
- 행 출력(`row_wr_buf`)은 TIM16 CH1 DMA(GPDMA1 Channel1)로 순환하며, 열 입력은 TIM16 Update 트리거와 연결된 DMA(GPDMA1 Channel2)가 `col_rd_buf[MATRIX_ROWS]` 버퍼에 주기적으로 샘플링합니다.
- `col_rd_buf`는 `.non_cache` 섹션 + `volatile` 선언으로 코어/ DMA 일관성을 보장합니다.
- 주요 API
  - `const volatile uint16_t *keysPeekColsBuf(void)` : DMA 버퍼를 추가 복사 없이 제공.
  - `bool keysReadColsBuf(uint16_t *dst, uint32_t rows)` : 필요 시 안전 복사.
  - `bool keysGetPressed(uint16_t row, uint16_t col)` : CLI/디버그용 단일 스위치 조회.

## 4. 매트릭스 브리지 (`src/ap/modules/qmk/port/matrix.c`)
- `matrix_scan()`은 `keysPeekColsBuf()`를 직접 순회하며 `raw_matrix`를 갱신한 뒤 `debounce()`를 호출합니다.
- 스캔 시작 시각은 `matrixInstrumentationCaptureStart()`로 기록되고, 완료 후 `matrixInstrumentationLogScan()`/`Propagate()`가 HID 계측 버퍼에 스캔 지터를 보고합니다.
- `_DEF_ENABLE_MATRIX_TIMING_PROBE`가 1일 때 `matrix info` CLI가 1초마다 스캔/폴링 속도, 큐 길이, 계측 결과를 출력합니다.
- `matrixInstrumentationIsCompileEnabled()`를 통해 계측 빌드 여부를 런타임에 확인할 수 있습니다.

## 5. 퀀텀 & 액션 계층
- `keyboard_task()`(`src/ap/modules/qmk/quantum/keyboard.c`)는 `matrix_task()` 결과에 따라 키 이벤트를 생성하고, `action_exec()` 체인을 통해 탭/홀드, 레이어, 콤보 등을 해석합니다.
- `generate_tick_event()`는 1 kHz 타이머 이벤트를 키 이벤트로 변환하여 오토 리핏이나 RGB 애니메이션을 유지합니다.
- `host_keyboard_send()`(`src/ap/modules/qmk/port/protocol/host.c`)는 QMK 보고서를 포팅 계층으로 넘기고, HID LED 상태를 반영합니다.
- `qmkUpdate()`(`src/ap/modules/qmk/qmk.c`)는 `via_hid_task()` → `keyboard_task()` → `eeprom_task()` → `idle_task()` 순으로 호출되어 VIA RAW HID 패킷이 HID 리포트보다 먼저 처리되도록 보장합니다.

## 6. USB HID & VIA RAW HID
- `usbHidSendReport()`/`usbHidSendReportEXK()`는 보고서를 즉시 전송하거나 큐에 적재하고, 계측 모듈(`usb_hid_instrumentation.c`)에 타임스탬프를 남깁니다.
- `usb_hid_rate_info_t` 구조체는 폴링 주파수, bInterval, 큐 깊이 등을 CLI/로그에 제공하며 `matrix info`에서 재사용됩니다.
- VIA RAW HID는 `src/ap/modules/qmk/port/via_hid.c`가 인터럽트 컨텍스트에서 수신 버퍼에 적재하고, 메인 루프에서 처리 후 `usbHidEnqueueViaResponse()`로 응답합니다.

## 7. 진단 & CLI
- `matrix info` : 스캔/폴링 속도, 계측 활성화 여부, 큐 최대 길이를 출력합니다. `matrix info on/off`로 주기 출력 제어.
- `matrix row <value>` : 디버그 목적의 임시 행 덮어쓰기.
- `_DEF_ENABLE_MATRIX_TIMING_PROBE=0`인 릴리스 빌드에서는 계측 기능이 제외되며 CLI가 이에 대한 안내를 출력합니다.

## 8. 운영 팁
1. 행/열 수를 변경하면 `row_wr_buf`와 GPIO 초기화 핀 배열을 함께 수정해야 합니다.
2. DMA 노드 설정은 HAL Linked-List API를 사용하므로, 타이머/채널을 바꿀 경우 `GPDMA1_REQUEST_TIM16_*` 요청과 채널 속성을 같이 검토합니다.
3. 매트릭스 계측은 USB HID 계측(`usb_hid_instrumentation.c`)과 동일 버퍼를 공유하므로, 두 경로 모두 `_DEF_ENABLE_*_TIMING_PROBE` 매크로를 맞춰야 일관된 데이터를 얻을 수 있습니다.
4. VIA RAW HID 응답이 지연되면 `usbProcess()`의 리셋 큐 또는 USB monitor가 영향을 줄 수 있으므로, USB 관련 로그(`usb`, `boot`, `usb monitor`)도 함께 확인하십시오.
