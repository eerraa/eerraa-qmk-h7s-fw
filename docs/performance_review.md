# 키 스캔/USB 불안정성 감시 경로 점검 메모

## 개요
- `matrix info on`으로 확인 가능한 스캔 레이트가 USB 불안정성 감지 기능 도입 이후 감소한 정황을 토대로, 키 스캔 경로와 USB SOF 모니터 경로를 단계별로 추적하였다.
- 본 메모는 현재 구현의 흐름과 연산 비용이 높은 구간을 정리하고, 스캔 성능 회복을 위한 개선 아이디어를 제시한다.

## 1. 키 입력 처리 경로
### 1.1 하드웨어 스캔 초기화
- `keysInit()`은 GPIO, DMA, 타이머를 순차적으로 초기화하여 TIM16 트리거에 맞춰 행/열을 자동으로 순환 스캔하게 구성한다.【F:src/hw/driver/keys.c†L18-L106】
- DMA 채널 1은 행 출력 버퍼(`row_wr_buf`)를 GPIOA ODR로 반복 전송하고, DMA 채널 2는 열 입력을 `col_rd_buf`에 순환 저장한다.【F:src/hw/driver/keys.c†L107-L212】【F:src/hw/driver/keys.c†L213-L320】
- 결과적으로 스캔 주기마다 `col_rd_buf`에는 최신 매트릭스 비트맵이 16비트 단위로 누적된다.【F:src/hw/driver/keys.c†L303-L320】

### 1.2 QMK 포트 계층의 매트릭스 스캔
- `matrix_scan()`은 DMA 버퍼를 임시 배열로 복사한 뒤, 이전 상태와 비교하여 변화 여부를 확인한다.【F:src/ap/modules/qmk/port/matrix.c†L50-L96】
  - `micros()`로 스캔 시작/종료 시각을 읽고 `key_scan_time`에 저장한다.【F:src/ap/modules/qmk/port/matrix.c†L60-L82】
  - 전체 행을 `memcmp` 후 달라진 경우에만 `raw_matrix`를 갱신하고, 디바운서(`debounce`)를 호출한다.【F:src/ap/modules/qmk/port/matrix.c†L84-L95】
  - 변화가 감지되면 USB HID 타임 로그를 찍어 후속 통계에 활용한다.【F:src/ap/modules/qmk/port/matrix.c†L92-L95】

### 1.3 키 이벤트 생성
- `keyboard_task()`는 `matrix_task()`를 통해 매트릭스 변화를 확인한다.【F:src/ap/modules/qmk/quantum/keyboard.c†L663-L700】
  - `matrix_task()`는 `matrix_scan()` 호출 직후 `matrix_previous`와 비교하여 변경이 없으면 조기 종료한다.【F:src/ap/modules/qmk/quantum/keyboard.c†L530-L556】
  - 행 단위 XOR 결과를 바탕으로 `action_exec(MAKE_KEYEVENT(...))`를 호출하여 키 프레스/릴리즈 이벤트를 생성한다.【F:src/ap/modules/qmk/quantum/keyboard.c†L559-L580】
  - 이벤트 처리 이후 스캔 카운터를 누적하여 CLI `matrix info`에서 스캔 레이트를 확인할 수 있다.【F:src/ap/modules/qmk/quantum/keyboard.c†L198-L216】

### 1.4 병목 가능성
- `matrix_scan()`은 매 주기마다 `curr_matrix` 임시 배열을 스택에 만들고 전체 행을 복사/비교하므로, 고주기(8kHz) 환경에서는 스택 접근과 `memcmp`/`memcpy` 비용이 누적될 수 있다.【F:src/ap/modules/qmk/port/matrix.c†L58-L89】
- 변화가 없더라도 `micros()` 두 번과 `matrix_info()` 호출이 발생하며, `matrix_info()`는 `DEBUG_MATRIX_SCAN_RATE`가 켜진 상태면 1초마다 로그를 출력한다.【F:src/ap/modules/qmk/port/matrix.c†L59-L113】

## 2. USB 불안정성(SOF) 감시 흐름
### 2.1 호출 지점
- USB HID 클래스의 SOF 인터럽트 콜백(`USBD_HID_SOF`)에서 매 125µs(HS 모드 기준)마다 `usbHidMeasurePollRate()`가 실행된다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1098-L1110】
- 해당 함수는 마이크로초 타이머를 읽고 `usbHidMonitorSof()`로 SOF 간격을 넘겨 USB 안정성 점수를 계산한다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1211-L1233】

### 2.2 모니터 상태 머신
- `usbHidSofMonitorApplySpeedParams()`는 현재 USB 속도에 따라 기대 주기, 안정 범위, 감쇠 주기, 워밍업 프레임 수를 캐시한다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L468-L517】
- `usbHidMonitorSof()`는 다음 단계를 따른다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1261-L1439】
  1. 장치 상태 변화/서스펜드/속도 전환 시 모니터 구조체를 초기화하고 홀드오프·워밍업 타이머를 재설정한다.
  2. 홀드오프나 워밍업이 끝나지 않았으면 조기 종료한다.
  3. 정상 범위(`stable_threshold_us`) 이하인 프레임에서는 점수를 감쇠시키고 반환한다.
  4. 초과한 경우 `(delta_us + expected_us - 1) / expected_us`로 누락 프레임 수를 계산하여 점수에 누적한다.
  5. 점수가 임계치(`degrade_threshold`)에 도달하면 `usbRequestBootModeDowngrade()`를 호출하여 폴링 속도 다운그레이드를 예약하고, 홀드오프 타이머를 연장한다.
- 다운그레이드 요청은 메인 USB 처리 루프 `usbProcess()`에서 실제 리셋/EEPROM 기록으로 이어진다.【F:src/hw/driver/usb/usb.c†L176-L236】

### 2.3 병목 가능성
- SOF마다 `micros()`/`millis()` 호출, 구조체 갱신, 32비트 나눗셈 등이 실행되어 8kHz 환경에서는 누적 오버헤드가 크다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1211-L1439】
- 다운그레이드 감지를 위해 `usbRequestBootModeDowngrade()`가 반복 호출되며, 상태에 따라 EEPROM 기록이나 리셋 트리거로 이어질 수 있어 오버헤드를 더한다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1396-L1436】【F:src/hw/driver/usb/usb.c†L176-L236】

## 3. 성능 개선 아이디어
### 3.1 키 스캔 경로
- DMA 버퍼(`col_rd_buf`)를 직접 참조하도록 `matrix_scan()`을 리팩터링하면, 현재 수행 중인 `memcpy`와 임시 버퍼 할당을 제거해 스캔당 메모리 대역폭 소모를 줄일 수 있다. 이때 디바운서가 참조 중인 `raw_matrix`와의 수명 관리에 주의해야 한다.【F:src/ap/modules/qmk/port/matrix.c†L50-L96】【F:src/hw/driver/keys.c†L303-L320】
- `matrix_scan()`에서 변화가 없을 때는 `micros()` 2회 호출과 `matrix_info()` 호출을 생략하도록 빠른 경로를 추가하면, 고속 폴링 시 타이머 접근 비용을 절약할 수 있다.【F:src/ap/modules/qmk/port/matrix.c†L59-L113】
- `usbHidSetTimeLog()`는 실제 변화가 있는 경우에만 필요한데, 현재는 디바운서에서 `changed`가 참일 때마다 호출된다. 이벤트 버스트가 많은 레이턴시 구간에서는 로깅 빈도를 제한하거나 배치 처리하는 것이 도움이 될 수 있다.【F:src/ap/modules/qmk/port/matrix.c†L90-L96】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1508-L1516】

### 3.2 USB SOF 모니터
- SOF 모니터는 모든 마이크로프레임에서 실행되므로, 홀드오프/워밍업 단계에서는 최소한의 필드만 갱신하고 `millis()` 호출을 건너뛰는 "저비용" 경로를 도입하면 부담을 줄일 수 있다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1261-L1342】
- 누락 프레임 계산에서 32비트 나눗셈을 매번 수행하는 대신, 오차 누적 합계를 비트 시프트 기반으로 근사하거나, 복수 프레임당 한 번만 정규화하면 CPU 사용량을 낮출 수 있다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1358-L1384】
- 다운그레이드 요청은 이미 `usbProcess()`에서 지연 실행되므로, SOF 콜백 측에서는 요청 상태 캐시만 갱신하고 나머지 결정은 주기적인 폴링 태스크로 옮기면 인터럽트 지연을 더 줄일 수 있다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1396-L1436】【F:src/hw/driver/usb/usb.c†L176-L236】

## 결론
- 키 스캔 경로는 DMA 기반으로 구성되어 있지만, CPU 측에서 매 주기마다 수행하는 복사/비교/타이머 호출이 누적될 경우 스캔 레이트 감소로 이어질 여지가 있다.
- USB SOF 모니터는 안정성 진단에는 효과적이지만, 8kHz 인터럽트 컨텍스트에서 비교적 무거운 로직을 수행하고 있어 추가 최적화 없이는 스캔 경로에 간접 영향을 줄 수 있다.
- 상기 개선안을 검토하여 메모리 복사 제거, 타이머 호출 최적화, SOF 모니터의 경량화 등을 병행하면 USB 안정성 감시 기능을 유지하면서도 스캔 레이트를 회복할 가능성이 있다.
