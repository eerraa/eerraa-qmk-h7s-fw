# HID 계측·모니터 정합성 최종 수정 계획 (V251010R6)

## 1. 목표 및 범위
- V251010R6 코드 기준으로 HID 전송 경로를 슬롯 기반 단일 복사 구조로 재구성할 때, 기존 계측(_DEF_ENABLE_USB_HID_TIMING_PROBE)과 USB 불안정성 모니터(_USE_USB_MONITOR)의 동작을 보존합니다.【F:docs/review_usb_hid_instrumentation_conditional_V251010R6.md†L1-L63】【F:docs/usb_hid_monitor_alignment_review_V251010R6.md†L1-L24】
- 조건부 컴파일 매크로의 추가 도입 필요성을 재평가하고, 계측·모니터 로직을 신규 경로에 맞춰 재배치하는 절차를 정의합니다.【F:docs/review_usb_hid_instrumentation_conditional_V251010R6.md†L65-L124】【F:docs/usb_hid_monitor_alignment_review_V251010R6.md†L26-L83】

## 2. 계측 훅 유지 전략
1. `usbHidSendReport()` 재구성 시 즉시 전송 성공/실패 분기에서 기존 계측 훅을 재사용하며, 슬롯 예약·커밋 시점에 큐 잔량과 타임스탬프 메타데이터를 업데이트합니다.【F:docs/usb_hid_monitor_alignment_review_V251010R6.md†L26-L55】
2. 타이머 콜백(`HAL_TIM_PWM_PulseFinishedCallback`)과 IN 콜백(`USBD_HID_DataIn`)의 훅 위치는 변경하지 않고, 슬롯 메타데이터를 전달하는 헬퍼를 추가하여 지연·폴링 통계를 유지합니다.【F:docs/usb_hid_monitor_alignment_review_V251010R6.md†L26-L55】
3. 계측 비활성 빌드에서 메타데이터가 잔존하지 않도록, 슬롯 구조 확장은 `_DEF_ENABLE_USB_HID_TIMING_PROBE`로 감싸서 인라인 스텁 동작과의 일관성을 확보합니다.【F:docs/review_usb_hid_instrumentation_conditional_V251010R6.md†L65-L124】【F:docs/usb_hid_monitor_alignment_review_V251010R6.md†L57-L64】

## 3. 모니터 경로 정합성
1. SOF 처리(`usbHidMeasurePollRate`)는 타임스탬프 획득과 모니터 갱신을 유지하고, 신규 슬롯 API와 독립적으로 `_USE_USB_MONITOR` 경로가 동일 타이밍 소스를 참조하도록 보장합니다.【F:docs/review_usb_hid_instrumentation_conditional_V251010R6.md†L43-L63】【F:docs/usb_hid_monitor_alignment_review_V251010R6.md†L34-L49】
2. 모니터 비활성 빌드에서는 기존과 동일하게 스텁이 호출되므로, 신규 경로 추가 시 별도의 분기 삽입 없이 컴파일러 최적화에 맡깁니다.【F:docs/review_usb_hid_instrumentation_conditional_V251010R6.md†L43-L63】【F:docs/usb_hid_monitor_alignment_review_V251010R6.md†L34-L49】
3. 다운그레이드 정책, 워밍업, 점수 계산은 버퍼 구조와 무관하므로, 회귀 테스트에서는 SOF 기반 상태 전이가 유지되는지만 확인합니다.【F:docs/usb_hid_monitor_alignment_review_V251010R6.md†L34-L49】

## 4. 구현 단계
1. **슬롯 메타데이터 확장**: 큐 슬롯 구조에 계측 전용 필드를 추가하고 `_DEF_ENABLE_USB_HID_TIMING_PROBE`로 보호합니다. 계측이 꺼진 빌드에서는 구조체가 기존 크기를 유지해야 합니다.【F:docs/review_usb_hid_instrumentation_conditional_V251010R6.md†L65-L124】【F:docs/usb_hid_monitor_alignment_review_V251010R6.md†L57-L64】
2. **즉시 전송 경로 재배치**: 전송 성공 시 슬롯 커밋을 즉시 수행하고, 실패 시 타이머 재전송 루프에서 동일 슬롯을 재사용하여 복사 횟수를 1회로 유지합니다.【F:docs/usb_hid_monitor_alignment_review_V251010R6.md†L34-L83】
3. **타이머/IN 훅 보강**: 타이머 콜백과 IN 콜백에서 슬롯 ID를 전달받아 계측 함수에 넘기고, 큐 잔량을 신규 API에서 가져오는 헬퍼를 구현합니다.【F:docs/usb_hid_monitor_alignment_review_V251010R6.md†L26-L83】
4. **조건부 컴파일 검증**: `_DEF_ENABLE_USB_HID_TIMING_PROBE`와 `_USE_USB_MONITOR`의 기본 값(0/1 조합)을 각각 빌드해 심벌 제거와 모니터 유지가 의도대로 동작하는지 확인합니다.【F:docs/review_usb_hid_instrumentation_conditional_V251010R6.md†L1-L63】
5. **단위 테스트 및 CLI 점검**: 계측 활성 빌드에서 CLI 출력(`usbHidInstrumentationHandleCli`)과 매트릭스 연동(`usbHidSetTimeLog`)이 신규 슬롯 메타데이터를 반영하는지 확인합니다.【F:docs/review_usb_hid_instrumentation_conditional_V251010R6.md†L99-L124】

## 5. 위험 및 대응
- **큐 포화 위험**: 즉시 전송 성공 시 슬롯 해제가 지연되면 큐 잔량이 감소하지 않을 수 있으므로, 계측 훅에서 슬롯 상태를 검증하는 어설션을 추가하고 통합 테스트에서 장시간 입력을 재현합니다.【F:docs/usb_hid_monitor_alignment_review_V251010R6.md†L75-L83】
- **오버헤드 재유입**: 조건부 컴파일 영역이 누락되면 계측 비활성 빌드에서 불필요한 메타데이터 연산이 발생할 수 있으므로, 빌드 아티팩트에서 `usbHidInstrumentation` 심벌 존재 여부를 확인합니다.【F:docs/review_usb_hid_instrumentation_conditional_V251010R6.md†L65-L124】
- **모니터 회귀**: SOF 루틴 수정 시 `_USE_USB_MONITOR` 활성 빌드에서 타임스탬프가 0으로 고정되지 않는지 확인하고, 다운그레이드 정책이 정상적으로 발동하는지 QA 시나리오에 포함합니다.【F:docs/review_usb_hid_instrumentation_conditional_V251010R6.md†L43-L63】【F:docs/usb_hid_monitor_alignment_review_V251010R6.md†L34-L49】

## 6. 적용 여부 판단
- 단일 복사 경로는 즉시 전송 실패 빈도가 낮은 환경에서도 스택 복사를 제거하고 ISR 실행 시간을 단축하여 안정성에 긍정적입니다.【F:docs/usb_hid_monitor_alignment_review_V251010R6.md†L65-L83】
- 계측·모니터 정합성 유지가 가능한 것으로 판단되며, 조건부 컴파일 구조가 기존과 동일하게 동작하는지 확인하는 회귀 테스트만 통과하면 적용을 권장합니다.【F:docs/review_usb_hid_instrumentation_conditional_V251010R6.md†L65-L124】【F:docs/usb_hid_monitor_alignment_review_V251010R6.md†L26-L83】
