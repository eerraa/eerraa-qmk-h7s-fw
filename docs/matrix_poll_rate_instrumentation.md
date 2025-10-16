# `matrix.c` Poll Rate 계측 경로 추적

## 1. 빌드 플래그 구성
- `_DEF_ENABLE_MATRIX_TIMING_PROBE`와 `_DEF_ENABLE_USB_HID_TIMING_PROBE` 기본값은 0으로, 릴리스 빌드에서 매트릭스/USB 계측을 모두 비활성화합니다.【F:src/hw/hw_def.h†L9-L15】
- `matrix_instrumentation.h`는 위 매크로를 활용해 계측 헬퍼를 정적 인라인으로 제공하며, 플래그가 0일 경우 공백 구현으로 축소돼 타이머 접근이 제거됩니다.【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L9-L51】

## 2. `matrix_scan()` 진입 시점
1. 스캔 시작 시 `matrixInstrumentationCaptureStart()`가 호출돼 활성화된 계측 조합에 한해 `micros()`를 읽어 옵니다.【F:src/ap/modules/qmk/port/matrix.c†L52-L92】【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L13-L19】
2. DMA 버퍼에서 키 행을 복사한 뒤 `matrixInstrumentationLogScan()`이 CLI 토글(`is_info_enable`)과 매트릭스 계측 플래그를 동시에 만족할 때만 스캔 시간을 누적합니다.【F:src/ap/modules/qmk/port/matrix.c†L78-L101】【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L21-L33】
3. 스캔 결과가 변경되면 `matrixInstrumentationPropagate()`가 HID 계측 활성화 시에만 `usbHidSetTimeLog()`로 스캔 시작 타임스탬프를 전달하여 후속 폴링 분석에 사용합니다.【F:src/ap/modules/qmk/port/matrix.c†L86-L101】【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L35-L43】

## 3. HID 계층으로의 전파
- `usbHidSetTimeLog()`는 `_DEF_ENABLE_USB_HID_TIMING_PROBE`가 1일 때 매트릭스가 넘긴 타임스탬프를 `key_time_raw_pre`로 저장하고, 해당 이벤트를 측정 대상으로 표시합니다. 매크로가 0이면 즉시 false를 반환하여 릴리스 빌드에서 기록이 남지 않습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L213-L227】
- 큐에서 리포트를 꺼내거나 즉시 전송이 성공하면 각각 `usbHidInstrumentationOnReportDequeued()`와 `usbHidInstrumentationOnImmediateSendSuccess()`가 호출되어 폴링 간격 측정이 준비되고, 전송 완료 시점에는 `usbHidMeasureRateTime()`이 실행되어 실제 소요 시간, 초과 지연(`excess_us`), 큐 잔량 등을 누적합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1488-L1550】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L99-L168】
- SOF 인터럽트마다 `usbHidMeasurePollRate()`가 실행되어 `usbHidInstrumentationOnSof()`를 통해 샘플 윈도우(HS 8k/FS 1k 프레임) 단위로 폴링 빈도와 최소/최대 시간, 큐 최대치를 집계합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1182-L1210】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L43-L84】

## 4. 진단 CLI 노출
- `matrix info` CLI는 `usbHidGetRateInfo()`로 HID 계층에서 누적한 폴링 통계를 가져와 Poll Rate, 최소/최대/초과 시간, 큐 최대 사용량을 출력합니다. 계측 컴파일 플래그가 꺼져 있으면 스캔 시간 대신 `disabled` 메시지가 출력됩니다.【F:src/ap/modules/qmk/port/matrix.c†L101-L144】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L200-L224】
- `_DEF_ENABLE_MATRIX_TIMING_PROBE`가 1로 설정된 빌드에서는 1초 주기로 동일 정보를 자동 로그로 출력해 실시간 추적이 가능합니다.【F:src/ap/modules/qmk/port/matrix.c†L101-L126】

## 5. 릴리스 빌드에서의 잔존 경로
- 계측 호출 자체는 `matrix_instrumentation.h`의 인라인 스텁 덕분에 최종 바이너리에서 제거되지만, `matrix.c` → HID 계층으로 이어지는 Poll Rate 경로가 논리적으로 유지되어 빌드 플래그만 바꾸면 즉시 계측을 복구할 수 있습니다.【F:src/ap/modules/qmk/port/matrix.c†L52-L126】【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L9-L51】
- 릴리스 빌드에서 `usbHidGetRateInfo()`는 0 값과 false를 반환하므로 Poll Rate 출력은 모두 0으로 표시되며, CLI 메시지로 계측 비활성 상태를 안내합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L200-L224】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c†L226-L229】
