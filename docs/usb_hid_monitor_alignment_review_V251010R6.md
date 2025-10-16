# HID 계측/모니터 경로 정합성 검토 (V251010R6)

## 1. 개요
- HID 전송 경로의 이중 복사를 제거하기 위해 제안된 단일 버퍼/슬롯 기반 구조가 기존 계측 및 USB 불안정성 모니터 로직과 충돌하지 않는지 재검토했습니다.
- 검토 범위는 `usbHidSendReport()`/타이머/IN 콜백으로 이어지는 계측 훅과 `_USE_USB_MONITOR`로 보호된 SOF 모니터 경로 전체입니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1129-L1510】

## 2. 현행 계측/모니터 구조 요약
- 키 리포트 전송 시점에는 `usbHidInstrumentationMarkReportStart()`와 즉시 전송 성공 훅이 호출되며, 실패 시 큐에 적재된 후 타이머 콜백에서 재전송하면서 큐 잔량 기반 계측이 진행됩니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1129-L1150】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L112-L194】
- IN 완료 콜백은 HID 상태를 IDLE로 되돌리고 `usbHidInstrumentationOnDataIn()` 및 `usbHidMeasureRateTime()`을 통해 폴링 주기를 수집합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1027-L1043】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L99-L194】
- SOF 기반 USB 불안정성 모니터는 `_USE_USB_MONITOR`가 1일 때 `usbHidMonitorSof()`를 호출하여 상태 점수와 다운그레이드 정책을 유지하며, 동일 지점에서 계측용 SOF 타임스탬프도 갱신됩니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1184-L1256】
- 기본 빌드에서는 `_DEF_ENABLE_USB_HID_TIMING_PROBE=0`, `_USE_USB_MONITOR=1`로 설정되어 있어 계측은 스텁으로 정리되고 모니터만 활성화됩니다.【F:src/hw/hw_def.h†L9-L21】

## 3. 신규 HID 경로와의 정합성 확보 방안
### 3.1 계측 로직
- 제안된 슬롯 기반 전송에서는 큐 예약 시점에 전송 타임스탬프를 확보하고, 슬롯 메타데이터로 큐 잔량과 전송 여부를 구분하도록 `usbHidInstrumentationOnImmediateSendSuccess()`/`OnReportDequeued()` 호출부를 재배치하면 기존 통계 구조를 유지할 수 있습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L104-L167】
- `qbufferAcquire()`/`qbufferCommit()`과 같은 신규 API에서 슬롯 주소를 직접 다루더라도, 계측용 타임스탬프는 현재처럼 `micros()` 기반으로 계산되므로 함수 본문을 수정할 필요는 없습니다. 다만 큐 잔량 스냅샷을 슬롯 상태에서 유도해야 하므로, 예약·해제 시점에 큐 길이를 반환하는 헬퍼를 함께 설계해야 합니다.【F:src/common/core/qbuffer.c†L33-L73】
- IN 콜백과 타이머 콜백의 계측 훅 위치는 그대로 유지 가능하며, 큐에서 직접 DMA 버퍼를 참조하더라도 `usbHidInstrumentationOnReportDequeued()` 호출 시점에서 슬롯 메타데이터를 전달하면 지연 히스토그램 계산 로직이 영향을 받지 않습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1498-L1510】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L125-L188】

### 3.2 USB 불안정성 모니터
- 모니터는 SOF 타임스탬프와 디바이스 상태 변화만을 참조하므로, 전송 버퍼 구조 변경과 무관하게 `usbHidMeasurePollRate()` 내부 흐름을 유지하면 됩니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1184-L1256】
- 신규 경로에서도 SOF 루틴이 동일하게 호출되므로 워밍업, 다운그레이드 결정, 점수 감소 로직이 변하지 않고, `_USE_USB_MONITOR` 비활성 빌드에서는 기존처럼 스텁으로 대체됩니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1188-L1255】

### 3.3 추가 검토 항목
- 계측 매크로가 0일 때 신규 슬롯 API가 잔여 메타데이터를 유지하지 않도록, 조건부 컴파일 영역에서만 메타데이터 구조체를 활성화해야 합니다.【F:src/hw/hw_def.h†L13-L21】
- 큐 슬롯 정렬은 현재도 `report_info_t`/`report_buf[128]`의 정적 배열로 8바이트 배수 크기를 보장하고 있으므로, 슬롯 공유 전송에서도 DMA 정렬 문제가 발생하지 않습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L101-L134】

## 4. 극한 입력 시 성능/안정성 비교
- 기존 경로는 즉시 전송 성공 시 단일 복사, 실패 시 최대 세 번의 `memcpy()`와 스택 임시 버퍼를 사용하여 큐에 저장하고 재전송합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1129-L1150】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1498-L1510】
- 초당 수십 개 수준의 키 입력은 HS 8k 폴링 대비 매우 낮은 밀도로, 128 슬롯 큐를 가득 채우기 전에 IN 콜백이 대부분 소비합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L101-L134】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1027-L1043】
- 단일 복사 경로로 전환하면 즉시 전송 성공 시에도 슬롯 예약/커밋이 추가되지만, 복사 횟수는 1회로 유지되고 스택 복사가 제거되어 캐시 쓰레싱과 인터럽트 지연 위험이 줄어듭니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1129-L1150】【F:src/common/core/qbuffer.c†L33-L73】
- 큐 슬롯을 DMA 버퍼로 직접 사용하면 타이머 콜백에서도 재복사가 사라져 ISR 실행 시간이 짧아지고, IN 콜백이 큐를 비우는 속도가 유지되므로 키 이벤트 누락 가능성은 기존 대비 증가하지 않습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1498-L1510】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1027-L1043】
- 다만 슬롯 예약 후 전송이 성공했을 때 즉시 `qbufferCommit()`/`Pop()` 처리가 누락되면 큐 길이가 줄어들지 않아 장기적으로 포화될 수 있으므로, 계측 훅에서 사용하는 큐 잔량 값을 새 API에 맞춰 검증해야 합니다.【F:src/hw/driver/usb/usb_hid_instrumentation.c†L104-L167】

## 5. 결론
- 슬롯 기반 단일 복사 구조에서도 계측 함수와 USB 모니터는 기존 호출 위치를 유지하면서 메타데이터 전달 방식을 조정하는 것만으로 완전히 재구성할 수 있습니다.
- 고빈도 입력 환경에서도 큐 용량과 IN 콜백 처리 속도는 그대로 유지되며, 복사 제거로 ISR 실행 시간이 단축되어 안정성 저하는 예상되지 않습니다.
- 신규 경로 도입 시에는 계측이 활성화된 빌드에서 슬롯 상태와 큐 잔량 리포팅이 정확히 동작하는지 단위 테스트를 추가하고, `_USE_USB_MONITOR` 경로가 동일 타임스탬프를 공유하는지 검증하는 절차를 권장합니다.
