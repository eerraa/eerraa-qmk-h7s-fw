# HID 계측 조건부 컴파일 검토 (V251010R6)

## 1. 개요
V251010R6 코드 기준으로 HID 전송/진단 계층에 포함된 계측 함수들의 호출 경로와 조건부 컴파일 상태를 점검하고, `_DEF_ENABLE_MATRIX_TIMING_PROBE`, `_DEF_ENABLE_USB_HID_TIMING_PROBE`, `_USE_USB_MONITOR` 매크로를 활용한 추가 가드 도입 필요성을 평가했습니다. 기본 매크로 값은 `hw_def.h`에서 `_DEF_ENABLE_MATRIX_TIMING_PROBE=0`, `_DEF_ENABLE_USB_HID_TIMING_PROBE=0`, `_USE_USB_MONITOR=1`로 정의되어 있습니다.【F:src/hw/hw_def.h†L9-L24】

## 2. HID 계측 함수 목록
| 함수 | 호출 위치 | 역할 요약 |
| --- | --- | --- |
| `usbHidInstrumentationNow()` | `usbHidMeasurePollRate()` SOF 경로 | SOF 및 모니터용 기준 타임스탬프 확보.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L57-L64】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1184-L1192】 |
| `usbHidInstrumentationOnSof()` | `usbHidMeasurePollRate()` | 샘플 윈도우(HS 8k/FS 1k) 단위로 IN 빈도 및 지연 통계 라치.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L66-L90】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1184-L1192】 |
| `usbHidInstrumentationOnTimerPulse()` | `HAL_TIM_PWM_PulseFinishedCallback()` | 전송 타이머 펄스 수/종료 시간 기록.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L92-L97】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1498-L1509】 |
| `usbHidInstrumentationOnDataIn()` | `USBD_HID_DataIn()` | IN 완료 횟수 누적.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L99-L102】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1027-L1043】 |
| `usbHidInstrumentationOnReportDequeued()` | `HAL_TIM_PWM_PulseFinishedCallback()` | 큐에서 꺼낸 리포트의 지연 측정을 요청.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L104-L110】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1498-L1510】 |
| `usbHidInstrumentationOnImmediateSendSuccess()` | `usbHidSendReport()` | 즉시 전송 성공 시 큐 잔량과 폴링 지연 측정 준비.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L112-L118】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1129-L1144】 |
| `usbHidInstrumentationMarkReportStart()` | `usbHidSendReport()` | 리포트 전송 시작 시각을 기록.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L120-L123】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1129-L1144】 |
| `usbHidMeasureRateTime()` | `USBD_HID_DataIn()` | 폴링 간격, 초과 지연, 키 처리 로그를 계산/누적.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L125-L194】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1027-L1043】 |
| `usbHidGetRateInfo()` | CLI/매트릭스 진단 | 누적된 폴링 통계를 구조체로 반환.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L198-L215】 |
| `usbHidSetTimeLog()` | 매트릭스 계측 연동 | 매트릭스 계측에서 전달한 타임스탬프를 원시 로그로 저장.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L217-L227】 |
| `usbHidInstrumentationHandleCli()` | `usbhid` CLI 명령 | 계측 활성 시 통계 출력, 비활성 시 안내 메시지 출력.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L229-L309】 |

## 3. 조건부 컴파일 현황
- `usbd_hid_instrumentation.h`는 `_DEF_ENABLE_USB_HID_TIMING_PROBE` 값에 따라 함수 원형 또는 인라인 스텁을 제공하여, 계측 비활성 빌드에서 호출 오버헤드를 제거합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L11-L70】
- 구현부(`usbd_hid_instrumentation.c`) 역시 동일 매크로로 함수 정의 전체를 감싸, 계측을 끈 빌드에서는 심벌 자체가 생성되지 않습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L55-L196】
- SOF 경로에서는 `_USE_USB_MONITOR`가 1일 때 모니터를 위해 `usbHidInstrumentationNow()`가 `micros()`를 호출하고, 모니터 비활성 시에는 0을 반환하도록 분기되어 있습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L26-L33】
- 타이머/IN 경로의 호출부는 헤더 인라인 스텁 덕분에 `_DEF_ENABLE_USB_HID_TIMING_PROBE=0`일 경우 공백 본문으로 정리되어 추가 분기나 타이머 접근이 발생하지 않습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L35-L68】
- 매트릭스 계측과의 연동 함수(`usbHidSetTimeLog`)는 `_DEF_ENABLE_USB_HID_TIMING_PROBE`가 0이면 즉시 false를 반환하여 릴리스 빌드에서 로그 저장을 생략합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L217-L227】

## 4. 추가 조건부 컴파일 도입 가능성 평가
1. **SOF/모니터 경로**
   - `usbHidMeasurePollRate()`는 모니터 활성 여부와 무관하게 `usbHidInstrumentationOnSof()`를 호출하지만, 계측 비활성 빌드에서는 해당 함수가 인라인 공백으로 정리됩니다. 추가적인 `#if` 분기를 도입하더라도 최종 어셈블리에서 차이가 없으므로 필요성이 낮습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1184-L1192】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L35-L68】
   - `usbHidInstrumentationNow()`가 `_USE_USB_MONITOR` 활성 시 `micros()`를 호출하는 이유는 USB 불안정성 감지 로직이 동일 타임스탬프를 요구하기 때문이며, 모니터 비활성 시에는 호출이 0으로 축약됩니다. 별도의 매크로로 감쌀 경우 모니터 기능이 타이밍 소스를 잃으므로 불가합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1184-L1192】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L26-L33】

2. **타이머/큐 경로**
   - TIM2 콜백과 IN 완료 경로에서 호출되는 계측 함수들은 모두 `_DEF_ENABLE_USB_HID_TIMING_PROBE` 전용 인라인 스텁을 제공하므로, 현재 상태에서도 불필요한 `micros()` 호출이나 큐 연산이 포함되지 않습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1027-L1043】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1498-L1510】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L40-L68】
   - 추가적인 매크로 분기 도입은 코드 복잡도만 증가시키고 성능 이득이 없으므로 권장하지 않습니다.

3. **CLI 및 통계 인터페이스**
   - `usbHidGetRateInfo()`와 `usbHidInstrumentationHandleCli()`는 계측 비활성 빌드에서 각각 0 리턴 또는 안내 메시지 출력만 수행합니다. 심벌을 완전히 제거하려면 호출부의 인터페이스도 조건부로 변경해야 하며, 기존 CLI 명령 체계를 깨트릴 위험이 있어 유지가 필요합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L198-L309】
   - 매트릭스 측과의 연동(`usbHidSetTimeLog()`)도 동일하게 false 반환 후 종료하므로, 추가 가드로 얻을 수 있는 성능 이득이 없습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L217-L227】

4. **매트릭스 계측 매크로 연계**
   - 매트릭스 계측 헬퍼(`matrix_instrumentation.h`)는 `_DEF_ENABLE_MATRIX_TIMING_PROBE` 또는 `_DEF_ENABLE_USB_HID_TIMING_PROBE`가 1일 때만 HID 타임스탬프 전달을 시도하며, 두 매크로가 모두 0이면 빈 인라인으로 정리됩니다.【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L13-L45】
   - 따라서 HID 계측을 비활성화하면 매트릭스 경로에서도 HID 관련 기록이 제거되어 중복 오버헤드가 남지 않습니다.

## 5. 결론
- HID 전송/진단 계층의 계측 함수는 `_DEF_ENABLE_USB_HID_TIMING_PROBE`를 중심으로 이미 조건부 컴파일되어 있으며, 기본 설정(0)에서 함수 호출이 인라인 스텁으로 제거됩니다.
- `_USE_USB_MONITOR`가 1일 때 유지되는 `micros()` 호출은 USB 불안정성 감지의 필수 입력이므로 별도의 비활성화가 불가능합니다.
- 추가적인 매크로 분기 도입은 유지보수 비용만 증가시키므로, 현재 구조를 유지하고 필요 시 빌드 설정에서 `_DEF_ENABLE_USB_HID_TIMING_PROBE` 또는 `_DEF_ENABLE_MATRIX_TIMING_PROBE`를 조정하는 것이 최적입니다.
