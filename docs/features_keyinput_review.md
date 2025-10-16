# 키 입력 경로 리팩토링/최적화 리뷰 (기준: V251009R5)

## 1. 분석 범위
- DMA 기반 키 스캔 드라이버(`src/hw/driver/keys.c`)
- QMK 매트릭스 브리지(`src/ap/modules/qmk/port/matrix.c`)
- 매트릭스 태스크 및 이벤트 분배(`src/ap/modules/qmk/quantum/keyboard.c`)
- HID 전송 및 진단 계층(`src/ap/modules/qmk/port/protocol/host.c`, `src/hw/driver/usb/usb_hid/usbd_hid.c`)

## 2. 주요 관찰 및 개선 여지
### 2.1 DMA 키 스캔 계층
- `keysPeekColsBuf()`로 DMA 버퍼를 직접 노출하는 현재 구조는 추가 복사를 제거해 최적화가 이미 적용돼 있습니다.【F:src/hw/driver/keys.c†L297-L304】
- V251009R2에서 `keys` CLI 명령을 제거해 DMA 자동 갱신 경로만 유지되고, 디버그는 QMK 레이어로 위임했습니다.【F:src/hw/driver/keys.c†L36-L330】【F:src/ap/modules/qmk/port/matrix.c†L101-L144】

### 2.2 QMK 매트릭스 브리지
- `matrix_scan()`은 스캔마다 두 번의 `micros()` 호출로 `key_scan_time`을 계산하고, 변화가 있을 때마다 HID 타임로그를 갱신합니다.【F:src/ap/modules/qmk/port/matrix.c†L50-L101】 폴백 복사 블록이 제거돼 DMA 경로만 검증하면 되므로, 계측 최적화를 적용할 경우 영향 범위가 더욱 명확해졌습니다. 다만 `key_scan_time`은 디버그 CLI 또는 로깅이 활성화될 때만 소비되므로, 평시에도 매 스캔마다 계측하는 것은 여전히 불필요한 타이머 접근 오버헤드가 됩니다.
- 개발 단계에서만 필요했던 계측 경로이므로, `_DEF_ENABLE_MATRIX_TIMING_PROBE`를 `src/hw/hw_def.h`에 정의해 매크로 조건부 컴파일로 묶는 방안을 권장합니다. 기본값은 `0`으로 두고 개발 환경에서만 `1`로 재정의하면, 펌웨어 릴리스 빌드에서 스캔 루프 계측 코드가 완전히 제외되어 타이머 접근과 로그 버퍼 연산이 제거됩니다.
- V251010R4 이후 버전에서는 `_DEF_ENABLE_MATRIX_TIMING_PROBE=0`이 기본으로 선언되고, 관련 인라인 래퍼가 비활성 빌드에서 `micros()` 접근을 제거하도록 정리되어 위 제안이 반영되었습니다.【F:src/hw/hw_def.h†L9-L18】【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L13-L46】
- USB 타이밍 로그는 USB 불안정성 모니터의 핵심 진단 수단이지만, 개발용 CLI 계측 경로는 `_DEF_ENABLE_USB_HID_TIMING_PROBE`로 별도 제어하도록 분리했습니다. 기본값은 `0`이라 릴리스 빌드에서는 HID 타이밍/큐 통계 누적이 제외되고, `_USE_USB_MONITOR`만으로도 불안정성 감지 로직이 유지됩니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1013-L1083】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1481-L1597】

### 2.3 매트릭스 태스크 & 이벤트 처리
- `matrix_task()`는 행 변화가 없으면 즉시 탈출하고, 고스트 판정도 캐시로 줄이는 등 이미 공격적으로 최적화돼 있습니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L615-L725】 추가적인 CPU 절감 포인트는 크지 않았습니다.

### 2.4 HID 전송/진단 계층
- `host_keyboard_send()`는 드라이버 유무와 무관하게 항상 `usbHidSendReport()`를 호출해 USB 계층에서 버퍼 복사/큐잉이 발생합니다.【F:src/ap/modules/qmk/port/protocol/host.c†L76-L123】 드라이버가 없는 경우에도 최소 한 번의 `memcpy`와 진단 로직이 수행됩니다.
- `usbHidSendReport()`는 전송 성공 시와 실패 시 모두 입력 버퍼를 복사하며, 실패 분기에서는 새 구조체를 다시 채워 큐에 넣습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1129-L1182】 또한 후속 진단 함수가 매 폴링마다 여러 차례 `micros()`를 호출해 CPU 부담이 있습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L104-L168】
- `_DEF_ENABLE_USB_HID_TIMING_PROBE=0`이 기본값으로 유지되면서, 릴리스 빌드에서는 해당 진단 함수가 인라인 스텁으로 대체되어 추가 `micros()` 접근이 제거됩니다.【F:src/hw/hw_def.h†L13-L18】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L13-L44】
- `usbHidInstrumentationOnDataIn()`과 `usbHidMeasureRateTime()`는 USB 불안정성 모니터 점수 계산과 직접 연결되지 않고, 계측 매크로가 켜졌을 때만 데이터 카운터·폴링 지연 통계를 쌓아 CLI에 노출하는 보조 루틴입니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L55-L158】 `USBD_HID_DataIn()`은 항상 이 두 함수를 호출하지만,【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1027-L1043】 `_DEF_ENABLE_USB_HID_TIMING_PROBE=0`에서는 호출부가 인라인 스텁으로 대체되어 비용이 사라지며, `_USE_USB_MONITOR=0` 구성에서도 동일하게 빈 구현이 유지됩니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L24-L68】
- 여기서 언급한 "드라이버"는 QMK 호스트 계층이 선택적으로 등록하는 `host_driver_t` 구현을 의미하며, BLE 등 외부 전송 경로가 활성화되었을 때만 `send_keyboard()` 등의 콜백이 호출됩니다.【F:src/ap/modules/qmk/port/protocol/host.c†L45-L113】【F:src/ap/modules/qmk/port/protocol/host_driver.h†L22-L28】
- `usbHidSendReport()` 호출 직후에는 `usbHidInstrumentationMarkReportStart()`가 리포트 전송 시작 시각을 기록하고, 성공 시에는 `usbHidInstrumentationOnImmediateSendSuccess()`가 큐 잔량을 기록합니다. 실패하여 큐에 적재된 리포트는 타이머 인터럽트에서 `usbHidInstrumentationOnReportDequeued()`가 처리 지연을 산출합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1138-L1148】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L104-L168】

### 2.5 USB 진단 함수 호출 경로
- `matrix_task()`는 매 주기마다 `matrix_scan()`을 호출하고, 키 상태 변화가 감지되면 `usbHidSetTimeLog(0, pre_time)`을 통해 해당 스캔의 시작 시각을 HID 계층에 전달합니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L618-L666】【F:src/ap/modules/qmk/port/matrix.c†L72-L103】 이후 `usb_hid/usbd_hid.c`에서는 이 타임스탬프를 `key_time_raw_pre`에 저장해 차후 `usbHidSendReport()` 완료 시점과의 차이를 계산하여 키 입력 처리 지연을 로깅합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1567-L1587】
- `matrix_info()`와 `matrix` CLI 명령은 디버그 플래그가 활성화되었을 때 `usbHidGetRateInfo()`를 호출해 최근 HID 폴링 통계를 구조체로 받아옵니다.【F:src/ap/modules/qmk/port/matrix.c†L104-L144】 반환된 `freq_hz`, `time_max`, `time_min`, `time_excess_max`, `queue_depth_max` 필드는 `usb_hid/usbd_hid.c`에서 유지되는 누산 결과를 그대로 노출하며, USB 불안정성 모니터가 집계한 폴링 주기와 큐 사용량 정보를 개발자에게 제공합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1557-L1565】

## 3. 수정 제안 및 판단
| ID | 제안 내용 | 기대 효과 | 잠재 부작용 | 적용 판단 |
| --- | --- | --- | --- | --- |
| A | `keysUpdate()` 더미 구현 제거 및 CLI 명령 삭제 | 디버그 경로 불필요 함수 호출 제거 및 유지보수 범위 축소 | 키 행/열을 직접 확인하려면 QMK 계층의 대체 도구가 필요 | 적용 (V251009R2) |
| B | `matrix_scan()`의 `key_scan_time` 계측을 `is_info_enable` 등 런타임 플래그 + `_DEF_ENABLE_MATRIX_TIMING_PROBE` 컴파일 가드로 이중 제어 | 평시 `micros()` 호출 감소로 스캔 루프 CPU 여유 확보, 릴리스 빌드에서 스캔 계측 코드 완전 제거 | 실시간 로깅을 즉시 활성화할 때 직전 스캔 값이 비어 있을 수 있음, 매크로 관리 복잡도 증가 | 적용 (V251010R4~R6에서 기본 가드 정비 및 인라인 스텁 적용) |
| C | `usbHidSendReport()` 실패 경로에서 이미 채워진 `hid_buf` 재활용 및 진단 루틴을 CLI 활성화 시에만 실행 | HID 전송 실패 시 불필요한 2차 복사 제거, 평시 `micros()` 호출 감소 | 큐 구조 변경 시 동시성/정합성 검증 필요, 진단 기본값 약화 가능성 | 부분 완화 (계측 가드는 기본 비활성화되었으나 2차 복사 구조는 유지) |

## 4. 결론
- 핵심 루프(`matrix_task()`)는 이미 최신 최적화가 반영돼 즉각적인 구조 변경 필요성은 낮습니다.
- 계측/디버그 로직이 기본값으로 상시 실행되고 있어, 런타임 플래그나 CLI 기반 토글을 도입하면 CPU 여유도를 확보할 수 있습니다.
- 제안 A, C는 영향 범위가 크므로 후속 테스트 계획 수립 후 적용 여부를 결정하는 것이 바람직합니다. 제안 B는 디버그 운용 요구사항을 확인한 뒤 조건부 적용을 검토할 수 있습니다.