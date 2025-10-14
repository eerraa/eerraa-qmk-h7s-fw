# V251009R7 계측 경로 상호 작용 점검 보고서

## 1. 분석 개요
- 점검 대상: 매트릭스 계측(`_DEF_ENABLE_MATRIX_TIMING_PROBE`), HID 계측(`_DEF_ENABLE_USB_HID_TIMING_PROBE`), USB 불안정성 탐지(`_USE_USB_MONITOR`).
- 목적: 각 기능 활성 조합에 따라 호출되는 주요 함수 흐름을 정리하고, 비활성 시에도 불필요한 계측 루틴이 실행되는지 검토.

## 2. 기능별 호출 흐름 요약
### 2.1 매트릭스 계측 활성 시
- `matrix_scan()` 시작 시 `micros()`로 스캔 시작 시각을 확보하고(`pre_time`), 스캔 루프 후 `key_scan_time`에 저장.【F:src/ap/modules/qmk/port/matrix.c†L52-L89】
- HID 계측이 함께 활성화된 경우에만 타임스탬프를 HID 계층으로 전달해 키 처리 지연을 측정합니다.【F:src/ap/modules/qmk/port/matrix.c†L86-L92】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1588-L1603】

### 2.2 HID 계측 활성 시
- IN 트랜잭션 완료 시 `usbHidMeasureRateTime()`이 호출되어 폴링 주기 및 큐 잔량을 누적.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1081-L1109】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1506-L1595】
- SOF 인터럽트마다 `usbHidMeasurePollRate()`가 샘플 윈도우를 유지하고 폴링 통계를 집계.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1256-L1289】

### 2.3 USB 불안정성 탐지 활성 시
- SOF마다 `usbHidMonitorSof()`가 호출되어 마이크로프레임 간격을 측정하고 다운그레이드 정책을 제어.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1256-L1481】
- 다운그레이드 목표 계산 및 속도별 파라미터 캐시를 통해 상태 머신이 유지.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1291-L1481】

## 3. 시나리오별 호출 경로 및 오버헤드 평가
| 시나리오 | 호출되는 주요 루틴 | 불필요한 호출 여부 |
| --- | --- | --- |
| 매트릭스 계측만 활성 | `micros()`→`key_scan_time` 계산 (HID 타임로그 전송 없음) | 불필요 호출 없음 (HID 계측이 꺼져 있으면 로그 전달 차단). |
| HID 계측만 활성 | SOF마다 `usbHidMeasurePollRate()`, IN 완료 시 `usbHidMeasureRateTime()` | 정상 동작, 오버헤드 필요. |
| USB 불안정성 탐지 기능만 활성 | SOF마다 `usbHidMonitorSof()` | 정상 동작, 오버헤드 필요. |
| 모두 비활성 | SOF 루틴과 매트릭스 스캔 모두에서 `micros()` 호출이 생략됨. | 불필요 호출 없음 (V251009R7 개선). |

## 4. 문제점 및 조치
- 문제: 개선 전에는 모든 계측이 비활성화된 빌드에서도 `usbHidMeasurePollRate()`와 `matrix_scan()`이 각각 `micros()`를 호출하여 CPU 자원이 낭비되었다.【F:src/ap/modules/qmk/port/matrix.c†L52-L92】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1256-L1289】
- 조치: 활성화된 계측이 없을 때는 `micros()` 호출과 HID 타임로그 전달을 생략하도록 조건부 컴파일을 추가해, 비활성 시나리오에서 타이머 레지스터 접근이 제거되도록 수정함.【F:src/ap/modules/qmk/port/matrix.c†L52-L92】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1256-L1289】

## 5. 기대 효과
- SOF 8kHz 환경에서 불필요한 `micros()` 호출을 차단하여 인터럽트 서비스 루틴 부하 감소.
- 매트릭스 스캔 루프에서도 타이머 접근을 제거해 평시 CPU 여유 확보.
- 계측 기능 활성 시 동작은 기존과 동일하게 유지되어 디버그 편의성은 보존.
