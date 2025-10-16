# V251010R6 계측 경로 조건부 컴파일 성능 검토

## 1. 검토 배경과 기본 설정
- `_DEF_ENABLE_MATRIX_TIMING_PROBE`, `_DEF_ENABLE_USB_HID_TIMING_PROBE`는 기본값 0으로 릴리스 빌드에서 매트릭스/USB 계측을 모두 제거하며, `_USE_USB_MONITOR`는 1로 유지되어 USB 불안정성 감지만 항상 동작합니다.【F:src/hw/hw_def.h†L9-L19】
- 매트릭스 계측 헬퍼는 인라인 스텁 구조를 사용해 매크로가 0일 때 `micros()` 호출과 HID 전달 경로를 완전히 생략합니다.【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L13-L45】
- HID 계측 헬퍼 역시 `_DEF_ENABLE_USB_HID_TIMING_PROBE`가 0이면 모든 함수가 인라인 스텁으로 대체되어 호출 오버헤드를 제거하며, USB 모니터만 활성화된 경우에는 `usbHidInstrumentationNow()`만 `micros()`를 수행해 SOF 감시 타이밍을 제공하도록 설계되어 있습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L11-L70】
- USB SOF 모니터는 `USBD_HID_SOF()`에서 매 프레임 `usbHidMeasurePollRate()`를 호출하고, 모니터가 켜져 있으면 SOF 편차 누적과 다운그레이드 판단을 담당합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1076-L1192】【F:src/hw/driver/usb/usb_hid.c†L1194-L1408】

## 2. 시나리오 정의
| 시나리오 | `_DEF_ENABLE_MATRIX_TIMING_PROBE` | `_DEF_ENABLE_USB_HID_TIMING_PROBE` | `_USE_USB_MONITOR` | 예상 주요 동작 |
| --- | --- | --- | --- | --- |
| S0 | 0 | 0 | 0 | 모든 계측/모니터 비활성. `micros()` 접근과 큐 통계 호출이 전부 제거됨. |
| S1 (기본) | 0 | 0 | 1 | SOF 모니터만 활성. 프레임마다 `micros()` 1회와 모니터 상태 기계 실행. |
| S2 | 1 | 0 | 1 | 매트릭스 계측 추가. 스캔마다 `micros()` 1회와 조건부 로그 누적. HID 전달은 비활성. |
| S3 | 0 | 1 | 0 | HID 계측만 활성. 큐 잔량/폴링 통계를 위한 `micros()`·CLI 경로 실행, SOF 모니터 비활성. |
| S4 | 0 | 1 | 1 | HID 계측과 SOF 모니터 동시 활성. HID/모니터가 같은 타임스탬프 소스를 공유. |
| S5 | 1 | 1 | 1 | 모든 계측/모니터 활성. 매트릭스→HID 타임스탬프 전달까지 전체 경로 사용. |

## 3. 시나리오별 성능 검토
### S0: 모든 기능 비활성
- 매트릭스 계측 함수가 상수 0을 반환하고, HID 계측 스텁도 전부 빈 본문이므로 컴파일된 코드에서 계측 호출 자체가 사라집니다.【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L13-L45】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L35-L68】
- `usbHidMeasurePollRate()` 내에서 `_USE_USB_MONITOR`가 0이므로 `usbHidMonitorSof()` 호출이 제거되고, SOF 후크는 빈 스텁으로 남아 USB 경로에 추가 분기 없이 정리됩니다.【F:src/hw/driver/usb/usb_hid.c†L1184-L1192】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L35-L68】
- 결과적으로 CPU 오버헤드는 완전히 제거되어, 계측 토글이 없는 빌드 대비 동일한 명령 시퀀스를 유지합니다.

### S1: 기본(모니터 전용) 구성
- `USBD_HID_SOF()`는 매 프레임 `usbHidMeasurePollRate()`를 호출하고, 이 함수는 SOF 모니터에 현재 타임스탬프를 전달합니다.【F:src/hw/driver/usb/usb_hid.c†L1076-L1192】
- HID 계측이 비활성이라 `usbHidInstrumentationOnSof()`는 빈 스텁이고, CPU 오버헤드는 `micros()` 1회와 SOF 모니터 상태 업데이트에 한정됩니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L26-L68】【F:src/hw/driver/usb/usb_hid.c†L1194-L1408】
- SOF 모니터는 상태 머신 내부에서 편차 누적·다운그레이드 판단을 수행하므로 약간의 분기 비용이 있지만, 폴링 타이밍 안정성 확보에 필요한 최소 로직입니다.【F:src/hw/driver/usb/usb_hid.c†L1213-L1399】

### S2: 매트릭스 계측 + 모니터
- `matrixInstrumentationCaptureStart()`가 스캔마다 `micros()`를 호출해 지속 시간을 측정하지만, 결과 기록은 `info_enabled`가 참일 때만 수행되어 디버그 요청 시에만 메모리 접근이 발생합니다.【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L13-L33】
- 스캔 변화가 있을 때 `matrixInstrumentationPropagate()`가 HID로 타임스탬프를 전달하려 시도하나, HID 계측이 꺼져 있어 빈 스텁으로 정리됩니다.【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L35-L45】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L217-L227】
- 따라서 추가적인 CPU 부하는 스캔 루프 내 `micros()` 1회와 조건부 메모리 기록 정도로 제한됩니다.

### S3: HID 계측 단독 활성
- HID 계측 구현부가 활성화되어 데이터 IN, 큐 디큐, 즉시 전송 성공 등 여러 지점에서 `micros()` 호출과 통계 누적을 수행합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L57-L160】
- `USBD_HID_DataIn()`과 `usbHidMeasureRateTime()`이 모두 계측 코드를 실행하지만, SOF 모니터가 꺼져 있어 `usbHidMonitorSof()`는 제외됩니다.【F:src/hw/driver/usb/usb_hid.c†L1027-L1192】
- CPU 부하는 폴링 루프마다 계측용 연산이 추가되는 수준이며, 개발·진단 빌드에서만 선택적으로 허용하면 릴리스 빌드 성능에는 영향이 없습니다.

### S4: HID 계측 + 모니터 동시 활성
- `usbHidInstrumentationNow()`가 모니터/계측 공통 타임스탬프 소스를 제공하여, SOF 모니터와 HID 계측이 동일한 `micros()` 호출을 공유합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L57-L90】
- SOF 모니터는 USB 안정성을 감시하고, 동시에 HID 계측은 폴링 초과·큐 잔량을 분석하므로 프레임당 `micros()` 호출 수는 대부분 공유되지만, 통계 계산을 위한 메모리 접근이 추가됩니다.【F:src/hw/driver/usb/usb_hid.c†L1184-L1408】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L125-L160】
- 개발 중 SOF 불안정성과 HID 큐 병목을 동시에 추적할 때 필요한 조합이며, 릴리스에서는 비활성화해 CPU 여유를 확보합니다.

### S5: 전체 기능 활성
- 매트릭스 계측이 스캔 시작 타임스탬프를 HID 계층으로 전달하고, HID 계측은 이를 기반으로 보고서를 분석하여 폴링 초과 시간·큐 잔량·키 처리 지연을 기록합니다.【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L35-L45】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L217-L227】【F:src/hw/driver/usb/usb_hid.c†L1138-L1149】
- SOF 모니터는 동일한 프레임 타임스탬프를 사용해 다운그레이드 임계치를 평가하므로, 타이머 접근은 공유되지만 스캔 루프·HID 경로 모두에서 계측 연산이 발생합니다.【F:src/hw/driver/usb/usb_hid.c†L1184-L1408】
- 가장 높은 CPU 부하 시나리오로, 개발·리그레션 테스트에서만 사용하는 것이 적절합니다.

## 4. 결론 및 제안
- 조건부 컴파일 구조 덕분에 릴리스 기본값(S1)은 SOF 모니터만으로 최소한의 타이머 접근과 상태 머신 연산을 유지하며, 계측 매크로를 비활성화하면 호출 자체가 제거되어 CPU 비용이 증가하지 않습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L26-L68】【F:src/hw/driver/usb/usb_hid.c†L1184-L1408】
- 개발 용도에서 계측을 활성화할 때는 시나리오별로 예상되는 `micros()` 호출 수와 메모리 누적량을 고려해 필요한 경로만 켜고, 릴리스 빌드에서는 `_DEF_ENABLE_MATRIX_TIMING_PROBE=0`, `_DEF_ENABLE_USB_HID_TIMING_PROBE=0`를 유지해 성능 회귀를 방지하십시오.【F:src/hw/hw_def.h†L9-L19】

## 5. 매크로 선언 위치 권고
- 기본 정의는 하드웨어 전역 설정인 `src/hw/hw_def.h`에 배치해 모든 모듈이 동일한 값을 참조하도록 유지합니다. 이 파일은 `QMK_KEYMAP_CONFIG_H`를 먼저 포함한 뒤, `#ifndef` 가드로 각 매크로의 기본값을 제공하므로 전역 기본값을 하나의 보드 버전에서 관리하기에 적합합니다.【F:src/hw/hw_def.h†L6-L19】
- 특정 키보드·키맵에서만 계측을 활성/비활성화해야 한다면 해당 키보드의 `config.h`에서 미리 매크로를 정의해 `hw_def.h` 기본값을 재정의할 수 있습니다. 예시는 `brick60` 구성에서 개발용으로 `_DEF_ENABLE_MATRIX_TIMING_PROBE`를 1로 올릴 때 사용하는 주석 안내에 정리되어 있습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/config.h†L24-L39】
- `_USE_USB_MONITOR` 역시 기본값을 `hw_def.h`가 제공하되, 동일한 방식으로 재정의가 가능하도록 `#ifndef` 가드를 두었으므로 USB 불안정성 모니터까지 포함한 모든 매크로를 단일 위치에서 관리하되 필요 시 키보드 단위로 덮어쓸 수 있습니다.【F:src/hw/hw_def.h†L6-L19】

## 6. `_USE_USB_MONITOR` 재정의 경로 검증
- CMake 단계에서 `QMK_KEYMAP_CONFIG_H`를 각 빌드 타깃에 컴파일 정의로 주입해, 모든 소스가 동일한 키보드별 `config.h`를 참조하도록 설정합니다. 이 값은 `${QMK_KEYBOARD_PATH}/config.h`로 확정되며, 이후에 추가되는 모든 소스 파일에 전역으로 적용됩니다.【F:src/ap/modules/qmk/CMakeLists.txt†L28-L41】
- `hw_def.h`는 가장 먼저 `QMK_KEYMAP_CONFIG_H`를 포함하고, 이어서 `_USE_USB_MONITOR`를 `#ifndef` 가드로 정의하므로 키보드 `config.h`에서 이미 0/1 값을 선언했다면 전역 기본값이 다시 덮어써지는 상황이 발생하지 않습니다.【F:src/hw/hw_def.h†L6-L20】
- 각 키보드 `config.h`는 `#pragma once`로 다중 포함을 방지하며, `_USE_USB_MONITOR`와 동일한 패턴으로 `_DEF_ENABLE_MATRIX_TIMING_PROBE` 등을 재정의해 온 관례가 있습니다. 동일 위치에서 `_USE_USB_MONITOR` 값을 설정하면 `hw_def.h` 포함 이전에 매크로가 확정되어 원하는 조합이 정확히 컴파일됩니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/config.h†L1-L39】【F:src/ap/modules/qmk/keyboards/baram/45k/config.h†L1-L31】
