# V251010R5 계측/모니터 조건부 컴파일 재검토 보고

## 1. 점검 항목 요약
- **폴링 계측 경로**: `_DEF_ENABLE_USB_HID_TIMING_PROBE`로 제어되는 HID 폴링 타이밍/큐 수집 경로의 활성·비활성 시나리오를 추적했습니다.
- **스캔 계측 경로**: `_DEF_ENABLE_MATRIX_TIMING_PROBE` 여부에 따라 매트릭스 스캔 타이밍 캡처가 안전하게 스텁 처리되는지 확인했습니다.
- **HID 계측 경로**: CLI/큐 통계를 포함한 HID 계측 진입점이 `_DEF_ENABLE_USB_HID_TIMING_PROBE`로만 노출되는지 검증했습니다.
- **USB 불안정성 탐지 로직**: `_USE_USB_MONITOR` 토글 시 SOF 모니터 상태/다운그레이드 로직이 완전히 포함·배제되는지 확인했습니다.

## 2. 코드 흐름 검증 결과
1. `_DEF_ENABLE_MATRIX_TIMING_PROBE = 0` 빌드에서 `matrixInstrumentationCaptureStart()`가 즉시 0을 반환하고, `matrixInstrumentationPropagate()`가 빈 본문으로 컴파일돼 매트릭스 스캔 루프가 순수 경로를 유지함을 확인했습니다.
2. `_DEF_ENABLE_USB_HID_TIMING_PROBE = 0` 구성에서는 `usbHidInstrumentationNow()` 인라인 스텁이 `_USE_USB_MONITOR` 활성 여부에 따라만 `micros()` 접근을 수행하고, CLI/큐 통계 함수는 안전하게 0 값을 반환합니다.
3. `_USE_USB_MONITOR = 0` 가정을 적용하여 `usbd_hid.c`를 추적한 결과, SOF 모니터 정의 블록이 전체 HID 구현까지 감싸고 있어 모니터 비활성 빌드에서 핵심 함수가 제거되는 구조적 오류를 발견했습니다.

## 3. 수정 사항
- `src/hw/driver/usb/usb_hid/usbd_hid.c`
  - SOF 모니터 전용 상태/헬퍼 정의 이후 `_USE_USB_MONITOR` 가드를 조기 종료하여, 모니터 비활성 빌드에서도 HID 본체가 항상 컴파일되도록 수정했습니다.
  - `usbHidResolveDowngradeTarget()`/`usbHidMonitorSof()` 정의를 전용 가드로 감싸 USB 모니터 전용 경로만 조건부로 포함되도록 재구성했습니다.
- `src/hw/hw_def.h`
  - `_DEF_FIRMWATRE_VERSION`을 `"V251010R5"`로 갱신해 변경 사항을 반영했습니다.

## 4. 추가 권고
- `_USE_USB_MONITOR=0` 조합의 실제 빌드 검증을 정기 CI에 포함하면 재발을 방지할 수 있습니다.
- 계측 관련 매크로를 변경할 때는 `docs/matrix_poll_rate_instrumentation.md`와 본 보고서를 참고해 상호 의존성이 없는지 재확인하십시오.
