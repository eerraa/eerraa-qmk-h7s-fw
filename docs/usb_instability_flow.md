# USB 불안정성(SOF) 탐지 흐름 정리

## 1. 시스템 진입과 USB 처리 루프
- 펌웨어는 `main()`에서 `bspInit()`/`hwInit()` 이후 `apMain()`을 호출하여 메인 루프를 실행한다.【F:src/main.c†L3-L15】
- `apMain()`의 루프는 `usbProcess()`를 포함한 주기 처리로 구성되어 있어, 인터럽트에서 감지된 USB 이벤트를 순차적으로 소비한다.【F:src/ap/ap.c†L9-L38】

## 2. USB 장치 초기화와 클래스 등록
- `usbBegin()`은 선택된 모드(키보드 펌웨어에서는 HID)를 기준으로 STM32 USB 디바이스 라이브러리를 초기화하고, HID 클래스를 등록한 뒤 장치를 시작한다.【F:src/hw/driver/usb/usb.c†L132-L213】
- 초기화 시점에 현재 부트 모드(예: HS 8K)가 로그로 기록되어, 다운그레이드 결정과 연계된다.【F:src/hw/driver/usb/usb.c†L70-L115】【F:src/hw/driver/usb/usb.c†L170-L213】

## 3. SOF 인터럽트 → 클래스 콜백 전달 경로
- USB OTG HS PCD 드라이버는 SOF 발생 시 `PCD_SOFCallback()`을 호출하고, 여기서 `USBD_LL_SOF()`로 스택 상단의 클래스 레이어에 이벤트를 전달한다.【F:src/hw/driver/usb/usbd_conf.c†L173-L195】
- ST USB 디바이스 코어는 활성 클래스 목록을 순회하며 SOF 콜백을 가진 클래스에 이벤트를 전파한다.【F:src/lib/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c†L882-L915】
- HID 클래스 구현에서는 `USBD_HID_SOF()`가 호출되어 폴링 속도 측정 루틴을 실행한다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1098-L1110】

## 4. 폴링 속도 측정과 SOF 모니터 진입
- `usbHidMeasurePollRate()`는 마이크로초 타이머를 읽어 `usbHidMonitorSof()`에 전달하며, 샘플링 윈도우마다 통계 값을 갱신한다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1206-L1238】
- `usbHidMonitorSof()`는 장치 상태 변화·서스펜드·속도 전환 등을 감지하여 모니터 구조체를 초기화하고, 홀드오프/워밍업 타이머를 재구성한다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1258-L1342】
- USB 속도에 따라 기대 주기·안정 범위·감쇠 주기·워밍업 프레임 수를 캐시해 인터럽트 컨텍스트에서 반복 계산을 줄인다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L452-L517】

## 5. SOF 간격 평가와 점수 누적
- 홀드오프와 워밍업이 끝난 뒤에는 SOF 간격(`delta_us`)을 측정하고, 안정 범위 미만일 때 점수를 감쇠시키거나 누락 프레임 수에 비례해 점수를 누적한다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1344-L1394】
- 점수가 임계치(`degrade_threshold`)를 넘으면 다음 폴링 모드(HS 4K → HS 2K → FS 1K)를 결정하고, 다운그레이드 요청을 발송한다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1396-L1436】

## 6. 다운그레이드 요청 큐와 실행
- `usbRequestBootModeDowngrade()`는 SOF 콜백에서 호출되어 요청 단계를 ARM/COMMIT 상태로 전환하고, 로그 및 재확인 타이머를 설정한다.【F:src/hw/driver/usb/usb.c†L176-L236】
- 메인 루프의 `usbProcess()`는 ARM 상태에서 시간 초과를 관리하고, COMMIT 단계에서 EEPROM에 새 모드를 저장한 후 리셋을 수행한다.【F:src/hw/driver/usb/usb.c†L200-L240】

## 7. 관찰된 병목 지점과 개선 포인트
- SOF 인터럽트 경로는 `micros()`/`millis()` 호출, 구조체 초기화, 32비트 나눗셈 등을 수행하므로 HS 8K 환경에서는 누적 오버헤드가 크다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1258-L1436】
- 인터럽트에서 즉시 다운그레이드 요청을 재평가하기보다, 최소 정보만 큐에 기록하고 나머지 판단을 `usbProcess()`로 이관하면 지연을 줄일 수 있다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1396-L1436】【F:src/hw/driver/usb/usb.c†L176-L240】
- 워밍업 및 홀드오프 구간에서는 저비용 경로를 도입해 타이머 접근을 줄이고, 누락 프레임 계산을 배치 처리하면 CPU 점유율을 낮출 가능성이 있다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1312-L1394】

