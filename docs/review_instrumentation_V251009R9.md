# V251009R9 계측 조건부 컴파일 리팩토링 보고서

## 1. 검토 배경
- V251009R8에서 도입한 인라인 헬퍼 구조가 조건부 컴파일 경로를 정리했으나, 동일 로직이 다수 파일에 분산되어 유지보수 비용이 여전히 높았습니다.
- 매트릭스/HID 계측과 USB 불안정성 모니터 간 의존 관계를 명확히 분리하고, 비활성 빌드에서의 호출 오버헤드를 최소화하기 위해 모듈화가 필요했습니다.

## 2. 리팩토링 요약
### 2.1 매트릭스 계측
- `matrixInstrumentation*` 호출부를 전용 헤더(`matrix_instrumentation.h`)의 인라인 함수로 재구성해 조건부 컴파일 분기를 한 곳으로 모았습니다.【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L1-L57】
- 계측값 저장소를 전용 모듈(`matrix_instrumentation.c`)에서만 정의하도록 하여, 다중 소스 간 전역 상태 공유를 제거했습니다.【F:src/ap/modules/qmk/port/matrix_instrumentation.c†L1-L4】
- `matrix.c`는 공통 흐름만 유지하고, 계측 활성 여부 판단 및 스캔 시간 출력 로직을 모듈화된 API로 대체했습니다.【F:src/ap/modules/qmk/port/matrix.c†L12-L170】

### 2.2 HID 계측 및 USB 불안정성 모니터
- HID 계측 전역 상태와 CLI, SOF 처리 훅을 `usbd_hid_instrumentation.[hc]`로 이전했습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L1-L18】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L1-L236】
- `usbd_hid.c`는 공통 USB 처리 흐름만 유지하며, 계측 관련 분기는 모듈 함수 호출로 대체했습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L56-L176】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1110-L1760】
- 공통 상수(`HID_KEYBOARD_REPORT_SIZE`, `KEY_TIME_LOG_MAX`)는 전용 내부 헤더로 이동해 다중 파일 간 정의 중복을 방지했습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_internal.h†L1-L5】

## 3. 영향 범위 및 검증
- 매트릭스/HID 계측이 모두 비활성화된 빌드에서 `micros()` 접근은 여전히 제거되며, 인라인 함수가 상수 리턴으로 정리됨을 확인했습니다.
- HID 계측과 USB 모니터의 조합에 따라 `usbHidInstrumentation*` 스텁이 자동으로 선택되어, SOF/IN 경로에 불필요한 분기가 남지 않습니다.
- CLI 경로는 계측 활성 시 기존 기능을 유지하고, 비활성 빌드에서는 안내 메시지를 동일하게 출력합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L159-L235】

## 4. 향후 권고
- 계측 모듈에 대한 단위 테스트 또는 스텁 호출 검증을 추가하여, 빌드 플래그 조합별 컴파일 결과를 CI에서 정기적으로 확인할 것을 권장합니다.
- 신규 계측 기능 추가 시에는 전용 모듈을 확장하고, 본체 파일에는 공통 흐름만 남기도록 유지해야 합니다.
