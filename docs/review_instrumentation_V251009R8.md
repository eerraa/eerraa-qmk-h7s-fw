# V251009R8 계측 경로 리팩토링 검토 보고서

## 1. 재검토 개요
- 선행 문서(V251009R7)에서 확인된 잔류 `micros()` 호출 제거 조치가 현재 코드에도 유지됨을 확인했습니다.
- `_DEF_ENABLE_MATRIX_TIMING_PROBE`, `_DEF_ENABLE_USB_HID_TIMING_PROBE`, `_USE_USB_MONITOR` 조합별로 빌드 타임 최적화가 적용되어 불필요한 타이머 접근이 더 이상 발생하지 않음을 다시 검증했습니다.

## 2. 리팩토링 판단 및 실행 내역
### 2.1 매트릭스 계측 경로
- `matrix_scan()` 내부의 조건부 컴파일 분기가 다중 위치에 분산되어 가독성이 떨어졌습니다.
- 타이머 접근과 로그 전파 로직을 정적 인라인 헬퍼(`matrixInstrumentationCaptureStart`, `matrixInstrumentationLogScan`, `matrixInstrumentationPropagate`)로 분리하여 조건부 분기를 함수 내부로 모았습니다.【F:src/ap/modules/qmk/port/matrix.c†L15-L56】【F:src/ap/modules/qmk/port/matrix.c†L76-L98】
- 이로써 호출부는 공통 흐름을 유지하면서도, 비활성 빌드에서는 컴파일러가 헬퍼 구현 자체를 제거하여 최적화가 유지됩니다.

### 2.2 HID 계측 및 USB 불안정성 감시 경로
- SOF 처리부에서 계측과 모니터가 동일한 타임스탬프를 공유하지만, `micros()` 호출과 누적 로직이 함수 내 다중 `#if`로 뒤섞여 있었습니다.
- `usbHidInstrumentationNow()`와 `usbHidInstrumentationOnSof()` 헬퍼를 도입해 타이머 접근과 통계 갱신을 각각의 책임으로 분리하고, 호출부(`usbHidMeasurePollRate`)는 공통 흐름만 남겼습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1040-L1070】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1140-L1153】
- `_USE_USB_MONITOR` 단독 활성 시에도 타임스탬프 헬퍼는 비어 있는 구현으로 대체되어 성능 오버헤드가 발생하지 않습니다.

## 3. 영향 범위 검증
- 매트릭스/HID 계측이 모두 비활성화된 구성에서 `micros()` 호출이 제거되는지 확인했으며, 인라인 헬퍼가 상수 `0`을 반환하도록 컴파일됩니다.
- USB 불안정성 모니터만 활성화된 빌드에서도 이전과 동일하게 `usbHidMonitorSof()` 호출 경로가 유지되는지 확인했습니다.

## 4. 후속 권고
- 계측 플래그의 조합을 변경하는 CI 매트릭스를 구성해, 빌드 플래그별 컴파일 결과가 유지되는지 정기적으로 확인할 것을 권장합니다.
- USB 타이밍 계측을 다시 확장할 경우, 신규 로직은 기존 헬퍼를 재사용하여 조건부 분기를 추가하지 않도록 가이드를 마련하십시오.
