# 키 입력 기능 Codex 레퍼런스

## 1. 기능 개요
- **목표**: 스위치 매트릭스를 8kHz 루프에서 스캔하고, 디바운스/고스트 검출을 거쳐 HID 리포트까지 전달하는 STM32H7S 포팅 경로를 한눈에 파악합니다.
- **동작 요약**:
  1. GPDMA가 열 버퍼(`col_rd_buf`)를 지속적으로 채우고, QMK 포팅층이 DMA 버퍼를 직접 참조해 매트릭스를 구성합니다.
  2. `matrix_task()`가 변화 행을 선별하고 고스트를 차단한 뒤, `action_exec()`/`send_keyboard_report()` 흐름으로 키 이벤트를 전달합니다.
  3. `host_keyboard_send()` → `usbHidSendReport()` 체인이 HID IN 엔드포인트를 전송하거나 큐잉하며, USB 시간/큐 진단값을 업데이트합니다.
- **현재 펌웨어 버전**: `V251009R7`.【F:src/hw/hw_def.h†L9-L20】

## 2. 파일 토폴로지 & 책임
| 경로 | 핵심 심볼/함수 | 역할 |
| --- | --- | --- |
| `src/ap/modules/qmk/qmk.c` | `qmkInit()`, `qmkUpdate()` | QMK 초기화와 메인 루프를 등록하고 `keyboard_task()`를 주기적으로 호출합니다. |
| `src/ap/modules/qmk/port/matrix.c` | `matrix_scan()`, `matrix_info()` | DMA 버퍼를 `keysPeekColsBuf()`로 직접 읽어 QMK 매트릭스를 갱신하고, CLI/진단 훅을 제공합니다. |
| `src/hw/driver/keys.c` | `keysInit()`, `keysPeekColsBuf()` | 키 스캔 GPDMA 노드를 구성하고, `.non_cache` 버퍼를 `const volatile` 포인터로 노출해 추가 복사를 제거합니다. |
| `src/ap/modules/qmk/quantum/keyboard.c` | `matrix_task()`, `generate_tick_event()`, `keyboard_task()` | 행 변화 탐지, 고스트 필터링, 이벤트 타임스탬프 공유, 1kHz Tick 이벤트 생성을 담당합니다. |
| `src/ap/modules/qmk/quantum/action.c` & `action_util.c` | `action_exec()`, `send_keyboard_report()` | 키 이벤트를 탭/홀드·레이어·콤보 처리 후 HID 리포트 구조체로 직렬화합니다. |
| `src/ap/modules/qmk/port/protocol/host.c` | `host_keyboard_send()`, `host_nkro_send()` | 포팅층 호스트 드라이버와 USB HID 래퍼를 연결하며, 블루투스 경로와 LED 상태를 관리합니다. |
| `src/hw/driver/usb/usb_hid/usbd_hid.c` | `usbHidSendReport()`, `usbHidSendReportEXK()` | HID IN 전송/큐잉, 폴링 계측(`usbHidSetTimeLog`, 큐 깊이 스냅샷)과 원격 웨이크업을 처리합니다. |

## 3. 입력 데이터 획득 계층
### 3.1 DMA 기반 행/열 버퍼
- `keysInit()`은 `col_rd_buf`를 대상으로 하는 GPDMA 링크드 리스트를 구성해 스캔 결과를 자동 적재합니다. `.non_cache` 영역에 위치하여 CPU 캐시 동기화가 필요 없습니다.【F:src/hw/driver/keys.c†L240-L318】
- `keysPeekColsBuf()`는 `const volatile uint16_t *`를 반환해 매트릭스 레이어가 DMA 버퍼를 직접 조회하되, 컴파일러가 값을 캐시하지 못하도록 보장합니다.【F:src/hw/driver/keys.c†L301-L304】【F:src/common/hw/include/keys.h†L15-L19】
- 기존 `keysReadColsBuf()` 경로도 유지되어 필요 시 안전 복사 기반 접근이 가능합니다.【F:src/hw/driver/keys.c†L295-L299】

### 3.2 QMK 매트릭스 브리지 (`port/matrix.c`)
- `matrix_scan()`은 DMA 버퍼를 `matrix_row_t` 배열과 1:1 매핑하여 변화가 있는 행만 `raw_matrix`에 반영하고, QMK 디바운스(`debounce()`)를 거쳐 `matrix[]`를 확정합니다. 폴백 복사 경로는 V251009R3에서 제거되어 DMA 경로만 유지됩니다.【F:src/ap/modules/qmk/port/matrix.c†L55-L99】
- 스캔 시작 시각과 변화 여부가 USB 진단에 전달되어 HID 폴링 초과 시간을 추적합니다 (`usbHidSetTimeLog`).【F:src/ap/modules/qmk/port/matrix.c†L96-L101】
- `matrix_can_read()`는 현재는 상시 true를 반환하지만, 전력/안전 모드에서 스캔을 차단하고 tick 이벤트만 생성하는 훅으로 확장 가능합니다.【F:src/ap/modules/qmk/port/matrix.c†L41-L58】
- `matrix_info()` CLI는 1초 주기 스캔/폴링 속도, 큐 심도, 스캔 소요 시간을 로그로 출력하도록 설계돼 있습니다.【F:src/ap/modules/qmk/port/matrix.c†L101-L125】

## 4. 매트릭스 태스크 & 이벤트 준비 (`quantum/keyboard.c`)
### 4.1 진입 조건과 진단
1. `matrix_task()`는 `matrix_can_read()`를 확인하고, 스캔 불가 시 1kHz tick 이벤트만 생성합니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L615-L633】
2. `matrix_scan_perf_task()`는 `_DEF_ENABLE_MATRIX_TIMING_PROBE`가 1인 빌드에서 초당 스캔 횟수를 수집해 CLI/로그에 제공할 수 있습니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L206-L231】

### 4.2 변화 행 탐지 & 고스트 제어
- `matrix_scan()` 결과 또는 이전 고스트 잔재(`ghost_pending`)가 있는 경우에만 행 루프를 실행하여 빈 스캔 비용을 줄입니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L642-L661】
- 고스트 판정은 두 단계로 최적화돼 있습니다.
  1. 물리 비트수가 0/1이면 조기 탈출해 키맵 조회를 생략합니다 (`V250924R8`).【F:src/ap/modules/qmk/quantum/keyboard.c†L252-L270】
  2. 고스트 후보 행은 `get_cached_real_keys()`로 동적 키맵 필터 결과를 캐시해 동일 스캔에서 반복 계산을 피합니다 (`V251001R4`).【F:src/ap/modules/qmk/quantum/keyboard.c†L232-L249】【F:src/ap/modules/qmk/quantum/keyboard.c†L667-L696】

### 4.3 이벤트 타임스탬프 공유
- 첫 변화 행에서만 `sync_timer_read32()`를 호출해 32비트 타임스탬프를 확보하고, 하위 16비트를 키 이벤트 시간으로 재사용합니다 (`V251001R3`).【F:src/ap/modules/qmk/quantum/keyboard.c†L671-L676】
- `pending_matrix_activity_time`를 통해 `last_matrix_activity_trigger()`가 동일 시각을 활용하도록 공유하여, 활동/입력 타이머 동기화를 유지합니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L139-L170】【F:src/ap/modules/qmk/quantum/keyboard.c†L720-L741】

### 4.4 행·열 순회와 이벤트 생성
- `row_changes`가 존재할 때만 열 루프에 진입하며, `__builtin_ctz()`와 `pending_changes &= pending_changes - 1` 패턴으로 set 비트만 순회합니다 (`V250924R7`).【F:src/ap/modules/qmk/quantum/keyboard.c†L697-L719】
- 첫 실제 이벤트가 확정된 순간에만 `should_process_keypress()`를 호출해 마스터 키보드 여부를 확인하고, 고스트만 존재하는 스캔에서는 불필요한 함수 호출·타이머 접근을 배제합니다 (`V251001R2`).【F:src/ap/modules/qmk/quantum/keyboard.c†L678-L695】
- `switch_events()`는 LED/RGB 매트릭스 등 키 전기 이벤트 기반 모듈을 호출하며, `action_exec()`는 QMK 고수준 키 처리로 연결됩니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L703-L719】【F:src/ap/modules/qmk/quantum/keyboard.c†L612-L614】【F:src/ap/modules/qmk/quantum/action.c†L73-L120】
- 루프 종료 후 `matrix_previous`를 최신 상태로 동기화하고, 고스트 발생 여부에 따라 `ghost_pending`을 유지합니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L720-L741】

### 4.5 Tick 이벤트 & 하우스키핑
- 스캔이 없을 때도 `generate_tick_event()`가 1kHz 빈도로 `action_exec(TICK_EVENT)`을 호출해 타임 기반 상태머신(탭댄스, 오토시프트 등)을 구동합니다 (`V251001R1`).【F:src/ap/modules/qmk/quantum/keyboard.c†L562-L613】
- `keyboard_task()`는 `matrix_task()` 결과에 따라 활동 타임스탬프를 갱신하고, `quantum_task()`·RGB·인코더·포인팅 디바이스 등 후속 태스크를 순차 실행합니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L742-L857】

## 5. 키 이벤트 처리 & HID 리포트 구성
### 5.1 `action_exec()` 파이프라인
- 이벤트는 탭/홀드, 오토시프트, 콤보, 키 오버라이드 등을 통과하며 필요 시 oneshot·모드 상태를 업데이트합니다.【F:src/ap/modules/qmk/quantum/action.c†L73-L138】【F:src/ap/modules/qmk/quantum/action.c†L139-L210】
- 최종적으로 `process_record()`가 키코드별 처리기를 실행하고, 키 상태 변화 시 `send_keyboard_report()` 또는 NKRO 전송을 요청합니다.【F:src/ap/modules/qmk/quantum/action.c†L211-L344】【F:src/ap/modules/qmk/quantum/action_util.c†L240-L323】

### 5.2 리포트 최적화
- 6KRO 경로(`send_6kro_report()`)는 마지막으로 전송한 리포트와 비교해 변경이 있을 때만 `host_keyboard_send()`를 호출합니다.【F:src/ap/modules/qmk/quantum/action_util.c†L272-L311】
- NKRO (`send_nkro_report()`)도 동일한 메모이제이션을 적용하여 불필요한 USB 트래픽을 줄입니다.【F:src/ap/modules/qmk/quantum/action_util.c†L312-L323】

## 6. HID 전송 계층
### 6.1 포팅 드라이버 (`port/protocol/host.c`)
- `host_keyboard_send()`는 USB가 깨어있으면 즉시 `usbHidSendReport()`를 호출하고, 포팅된 호스트 드라이버가 존재할 경우 동기화된 리포트를 다시 전달합니다. 블루투스 전환 시에는 RF 경로로 우선 송신합니다.【F:src/ap/modules/qmk/port/protocol/host.c†L71-L117】
- 시스템/컨슈머/조이스틱 리포트도 동일 패턴으로 EXK 엔드포인트에 전달되며, 마지막으로 송신한 usage 값을 저장해 중복 송신을 방지합니다.【F:src/ap/modules/qmk/port/protocol/host.c†L151-L213】

### 6.2 USB HID 레이어 (`usbd_hid.c`)
- `usbHidSendReport()`는 USBD가 활성 상태면 DMA 버퍼를 HID 엔드포인트에 제출하고, 실패 시 내부 큐(`report_q`)에 적재해 재시도합니다. 성공 시 폴링 계측 플래그와 큐 깊이 스냅샷을 기록합니다 (`V250928R3`).【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1158-L1189】
- 디바이스가 절전 상태일 경우 원격 웨이크업을 시도하며, EXK 경로(`usbHidSendReportEXK()`)도 동일한 계측 루틴을 공유합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1137-L1174】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1191-L1234】

## 7. 활동 타임스탬프 & 상태 추적
- `last_matrix_activity_trigger()`는 `matrix_task()`가 기록한 32비트 타임스탬프를 소비해 행/입력 활동 시간을 동기화하며, 예외적으로 공유 값이 없을 때만 `sync_timer_read32()`를 재호출합니다 (`V251001R3`).【F:src/ap/modules/qmk/quantum/keyboard.c†L139-L170】
- 인코더·포인팅 디바이스 활동도 별도 타임스탬프를 유지하지만, `last_input_modification_time`과 최대값을 공유해 OLED 타임아웃 등 입력 기반 기능이 모든 소스에 반응하도록 합니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L77-L134】

## 8. 진단 & 확장 포인트
- `_DEF_ENABLE_MATRIX_TIMING_PROBE`가 1인 빌드에서는 `matrix_info` CLI를 통해 스캔 주기, 폴링 주파수, 큐 잔량, 스캔 시간(us)을 실시간으로 확인할 수 있습니다.【F:src/ap/modules/qmk/port/matrix.c†L101-L144】
- `matrix info on/off` 하위 명령은 주기 로그를 토글하고, 비활성화 시 `matrixInstrumentationReset()`으로 누적 데이터를 초기화합니다.【F:src/ap/modules/qmk/port/matrix.c†L115-L189】
- `matrix_can_read()`와 `should_process_keypress()`는 저전력 모드(슬레이브 반쪽, USB 절전 등)에서 스캔/이벤트 생성을 제한하기 위한 훅입니다.【F:src/ap/modules/qmk/port/matrix.c†L41-L58】【F:src/ap/modules/qmk/quantum/keyboard.c†L410-L442】
- `switch_events()`는 LED/RGB 매트릭스 동기화를 담당하는 확장 지점이며, 필요 시 다른 전기 이벤트 소비자를 추가할 수 있습니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L527-L541】

## 9. 계측 컴파일 매크로 & 빌드 시나리오
- `_DEF_ENABLE_MATRIX_TIMING_PROBE`와 `_DEF_ENABLE_USB_HID_TIMING_PROBE`는 `hw_def.h`에서 기본 0으로 정의되어 릴리스 빌드에서 계측 호출이 제거됩니다.【F:src/hw/hw_def.h†L9-L18】
- `_USE_USB_MONITOR` 기본값은 1로, 모니터 비활성화 빌드를 원할 때만 키보드 `config.h`에서 재정의하면 됩니다.【F:src/hw/hw_def.h†L9-L21】
- `matrixInstrumentationCaptureStart()`와 `matrixInstrumentationPropagate()`는 매크로 조합이 활성화된 경우에만 `micros()` 호출과 HID 타임스탬프 전달을 수행합니다.【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L13-L45】【F:src/ap/modules/qmk/port/matrix.c†L50-L78】
- HID 계층은 `_DEF_ENABLE_USB_HID_TIMING_PROBE`가 0이면 인라인 스텁으로 축소되지만, `_USE_USB_MONITOR`가 1인 경우 `usbHidInstrumentationNow()`가 `micros()`를 유지해 SOF 모니터와 Poll Rate 계측이 같은 타임스탬프를 공유합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L26-L68】
- **릴리스 기본(S1)**: 두 계측 매크로 0, `_USE_USB_MONITOR=1` 구성으로 SOF 모니터만 실행되어 Poll Rate 통계 누적이 비활성화됩니다.【F:src/hw/hw_def.h†L9-L21】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1198-L1412】
- **풀 계측(S5)**: 두 계측 매크로를 1로 설정하면 매트릭스 타임스탬프가 HID 계층에 전달되고, `usbHidMeasurePollRate()`가 큐 깊이와 폴링 초과 시간을 집계합니다.【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L13-L45】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1145-L1210】

## 10. Poll/HID 계측 연동 흐름
1. `matrix_scan()`이 시작될 때 `matrixInstrumentationCaptureStart()`가 활성 계측 조합에 한해 `micros()`를 읽습니다.【F:src/ap/modules/qmk/port/matrix.c†L50-L78】【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L13-L33】
2. 스캔 결과가 변경되면 `matrixInstrumentationPropagate()`가 HID 계층에 타임스탬프를 전달해 Poll Rate 분석의 기준 시각을 공유합니다.【F:src/ap/modules/qmk/port/matrix.c†L72-L78】【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L35-L45】
3. `usbHidSendReport()`는 계측이 켜져 있을 때만 리포트 시작 시각과 큐 깊이 스냅샷을 저장하여 후속 통계에 활용합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1145-L1159】
4. SOF 인터럽트마다 `usbHidMeasurePollRate()`가 폴링 간격, 초과 시간, 큐 최대치를 누적하고 필요 시 다운그레이드 로직을 호출합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1182-L1412】
5. `matrix_info()` CLI는 `usbHidGetRateInfo()`에서 누적된 통계를 가져와 Scan/Poll Rate와 큐 최대 깊이를 출력합니다.【F:src/ap/modules/qmk/port/matrix.c†L92-L144】

## 11. 버전 히스토리 (주요 변경)
- **V250924R5**: DMA 버퍼 직접 참조(`keysPeekColsBuf()`), `volatile` 지정으로 최신 스캔 확보.【F:src/ap/modules/qmk/port/matrix.c†L55-L83】【F:src/hw/driver/keys.c†L309-L312】
- **V250924R6**: `ghost_pending` 도입으로 고스트 해소 전까지 행 비교 유지.【F:src/ap/modules/qmk/quantum/keyboard.c†L633-L661】
- **V250924R7**: 열 비트 스캔(`__builtin_ctz`)으로 행 변화 처리 비용 축소.【F:src/ap/modules/qmk/quantum/keyboard.c†L697-L719】
- **V250924R8**: 고스트 판정 전 물리 비트 수로 조기 종료하여 키맵 필터 재계산 최소화.【F:src/ap/modules/qmk/quantum/keyboard.c†L252-L270】
- **V250928R3**: HID 큐 깊이/폴링 초과 계측을 `usbHidSendReport()`에 추가.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1167-L1189】
- **V251001R1**: Tick 이벤트가 스캔 시각 타임스탬프를 재사용하여 타이머 호출을 통합.【F:src/ap/modules/qmk/quantum/keyboard.c†L562-L613】
- **V251001R2**: `should_process_keypress()` 지연 호출로 고스트 반복 시 낭비 제거.【F:src/ap/modules/qmk/quantum/keyboard.c†L678-L695】
- **V251001R3**: 행 변화 시 32비트 타임스탬프 공유 및 활동 타이머 동기화.【F:src/ap/modules/qmk/quantum/keyboard.c†L139-L170】【F:src/ap/modules/qmk/quantum/keyboard.c†L671-L741】
- **V251001R4**: 고스트 판정용 행 캐시 도입으로 키맵 필터 재계산 제거.【F:src/ap/modules/qmk/quantum/keyboard.c†L232-L249】【F:src/ap/modules/qmk/quantum/keyboard.c†L667-L696】
- **V251009R1**: `keysUpdate()` API 제거 및 DMA 스냅샷 기반 경로만 유지.【F:src/common/hw/include/keys.h†L13-L19】【F:src/hw/driver/keys.c†L32-L304】【F:src/ap/modules/qmk/port/matrix.c†L55-L99】
- **V251009R2**: DMA 자동 갱신에 중복되던 `keys` CLI 명령 제거, 진단 흐름을 QMK `matrix info`로 통합.【F:src/hw/driver/keys.c†L32-L330】【F:src/ap/modules/qmk/port/matrix.c†L101-L144】
- **V251009R3**: `matrix.c` 폴백 블록 제거로 DMA 전용 경로를 확정하고 유지보수 범위를 축소.【F:src/ap/modules/qmk/port/matrix.c†L55-L99】
- **V251009R7**: HID 전송 계층의 계측 진입점을 `_DEF_ENABLE_USB_HID_TIMING_PROBE` 가드로 묶어 큐 깊이 스냅샷과 타임스탬프 기록을 조건부 실행.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1145-L1159】
- **V251009R9**: `matrix_instrumentation.h`를 도입해 매트릭스 계측 호출을 인라인 스텁으로 정리하고 컴파일 타임 제어를 일원화.【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L9-L45】
- **V251010R3/R4**: `matrix info` CLI 로그를 단일 빌드 가드로 통합하고, 계측 비활성 빌드에서는 `disabled` 안내를 출력.【F:src/ap/modules/qmk/port/matrix.c†L83-L144】

## 12. Codex 작업 체크리스트
1. DMA/매트릭스 계층을 수정할 때는 `keysPeekColsBuf()`가 여전히 `volatile` 포인터를 반환하고, `matrix_scan()`이 `debounce()`와 USB 시간 로그를 호출하는지 확인합니다.
2. `matrix_task()` 변경 시 `event_time_32`와 `pending_matrix_activity_time` 공유가 유지되는지, `generate_tick_event()` 호출이 누락되지 않았는지 검증합니다.
3. HID 리포트 구조를 확장할 경우 `host_keyboard_send()`뿐 아니라 `usbHidSendReport()` 큐 계측과 `send_6kro_report()`의 변경 감지 로직을 함께 점검합니다.
4. 슬레이브 분기/저전력 모드를 추가할 때는 `matrix_can_read()`·`should_process_keypress()`를 함께 조정하여 tick 이벤트와 활동 타이머가 기대대로 작동하는지 확인합니다.
