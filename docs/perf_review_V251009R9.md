# V251009R9 모듈화 성능 영향 검토

## 1. 평가 개요
- 대상: V251009R9에서 매트릭스/USB HID 계측 경로를 전용 모듈로 분리한 변경사항.
- 목적: 기본 빌드(계측 비활성)에서 메인 루프 및 USB 경로에 추가 오버헤드가 발생하는지 확인.
- 방법: 현재 코드 구조의 조건부 컴파일 경로와 함수 호출 패턴을 검토해 정적/동적 오버헤드 가능성을 분석.

## 2. 매트릭스 스캔 경로
- `matrix_scan()`은 `matrixInstrumentation*` API를 통해 계측을 위임하지만, 해당 API가 전부 `static inline`으로 선언되어 컴파일 타임에 상수 반환 또는 공백 본문으로 정리됩니다.【F:src/ap/modules/qmk/port/matrix.c†L43-L68】【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L1-L55】
- 기본 설정(`_DEF_ENABLE_MATRIX_TIMING_PROBE=0`)에서 `matrixInstrumentationCaptureStart()`는 `0U`를 즉시 반환하므로 `micros()` 접근이 제거되고, 후속 함수도 모두 빈 본문으로 소거되어 추가 분기/호출이 생성되지 않습니다.【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L13-L52】
- 따라서 매트릭스 스캔 루프의 실행 경로와 타이밍은 V251009R8과 동일한 형태로 유지되며, 추가 오버헤드 징후는 없습니다.

## 3. USB HID 경로
- HID 계측 훅이 `usbd_hid_instrumentation.[ch]`로 이동하면서, 본체(`usbd_hid.c`)는 계측 활성 여부와 무관하게 공용 래퍼 함수를 호출합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1136-L1189】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1490-L1524】
- 기본 빌드에서 계측 함수 본문은 전부 비어 있지만, 외부 링키지 함수 호출 자체는 남아 있어 폴링 주기마다 `BL/BX` 수준의 호출-복귀 오버헤드가 발생합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L59-L144】
- 최빈 경로 기준 호출 빈도는 아래와 같습니다.
  - `usbHidInstrumentationOnDataIn()` : IN 전송 완료 시마다 1회 → HS 8kHz 기준 초당 최대 8,000회.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1018-L1044】
  - `usbHidInstrumentationOnSof()` : SOF 처리 시 1회 → 폴링 주기와 동일 빈도.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1184-L1191】
  - `usbHidInstrumentationOnReportDequeued()` / `usbHidInstrumentationOnImmediateSendSuccess()` : 키 이벤트가 큐로 전송될 때마다 0~1회.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1136-L1153】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1502-L1514】
  - `usbHidInstrumentationOnTimerPulse()` : TIM2 PWM 콜백마다 1회 (기존에도 인터럽트는 유지).【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1468-L1504】
- Cortex-M7(480 MHz)에서 공백 함수 호출은 대략 4~6사이클 수준으로 알려져 있으며, 8kHz 호출 시 초당 약 32k~48k 사이클(0.007%~0.01% CPU)로 추정됩니다. 현재 스캔/USB 경로의 여유를 감안하면 체감 성능 저하는 없겠으나, 완전 제거되던 이전 구조 대비 미세한 오버헤드가 새로 생긴 것은 사실입니다.
- 만약 계측을 기본적으로 비활성화하는 릴리스 빌드에서 이 오버헤드까지 제거하고 싶다면, `usbd_hid_instrumentation.h`에 `static inline` 스텁을 두거나, 매크로로 호출 자체를 조건부 컴파일하는 방식으로 재조정하는 것이 좋습니다.

## 4. 결론
- 매트릭스 메인 루프: 모듈화 이후에도 인라인 스텁으로 정리되어 실행 속도 저하 우려 없음.
- USB HID 경로: 빈 함수 호출이 남아 초당 수만 회 수준의 경미한 오버헤드가 추가됨. 현재 성능 한계에는 영향이 없으나, 과거 대비 증가한 호출 수를 인지할 필요가 있음.
- 권장 조치: 릴리스 빌드 최적화를 중시한다면 HID 계측 스텁을 인라인화하거나 호출부를 `#if _DEF_ENABLE_USB_HID_TIMING_PROBE`로 감싸는 후속 정비를 고려.
