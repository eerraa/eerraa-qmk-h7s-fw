# V251010R1 모듈화 성능 회귀 점검

## 1. 검토 배경
- 선행 문서(V251009R9)는 HID 계측 모듈화 이후 릴리스 빌드에서도 공백 함수 호출이 남아 초당 수만 회의 경미한 오버헤드가 발생할 수 있음을 경고했습니다.
- V251010R1 커밋에서는 `_DEF_ENABLE_USB_HID_TIMING_PROBE=0` 기본 구성을 유지한 채, 계측 스텁을 전면 인라인화해 호출 자체가 삭제되는지 확인했습니다.【F:src/hw/hw_def.h†L9-L15】

## 2. 정적 오버헤드 점검
- 매트릭스 계측 경로는 기존과 동일하게 `static inline` 스텁을 통해 `micros()` 접근과 USB 전파 호출을 조건부로 제거하며, 추가 분기/호출이 컴파일 결과에 남지 않습니다.【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L9-L70】
- HID 계측 구현부(`usbd_hid_instrumentation.c`)는 `_DEF_ENABLE_USB_HID_TIMING_PROBE`가 1일 때만 함수 정의가 포함되도록 `#if` 블록으로 감싸져 있어, 비활성 빌드에서는 개별 심벌이 생성되지 않습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L11-L196】

## 3. 동적 오버헤드 점검
- 헤더에서 제공하는 릴리스 전용 스텁은 모두 `static inline` 빈 본문으로 정의되어 있으며, SOF/IN/타이머 훅과 큐 모니터링 래퍼 호출이 최적화 시 제거됩니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L22-L70】
- `usbHidInstrumentationNow()` 스텁은 USB 불안정성 모니터가 활성화된 경우에만 `micros()`를 호출하며, 그 외 경로에서는 즉시 0을 반환해 타이머 접근도 제거됩니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L26-L33】

## 4. 결론
- V251010R1에서 정적/동적 오버헤드 우려가 모두 해소되어, 기본 릴리스 빌드의 HID 경로에는 추가적인 호출 명령이 남지 않습니다.
- USB 불안정성 모니터를 위한 최소 타이머 접근은 그대로 유지되며, 계측 플래그를 1로 재정의하는 개발 빌드에서는 이전과 동일한 측정 기능이 동작합니다.
