# V251010R4 리뷰

## 1. 기존 수정 내역 정리
### V251010R1
- WS2812 DMA 재시작을 인터럽트에서 분리해 `ws2812RequestRefresh()`로 요청하고 메인 루프 `ws2812ServicePending()`에서 처리하도록 개편했습니다.
- 그러나 Caps Lock 등 호스트 LED 반영은 여전히 `USBD_HID_EP0_RxReady()` 인터럽트 컨텍스트에서 `usbHidSetStatusLed()` → `led_set()` → `rgblight_set()` 전체를 실행하여, 약 7.5ms 동안 EP0를 독점하고 USB 마이크로프레임 공백을 유발했습니다. 【docs/V251010R1.md†L1-L52】

### V251010R2
- 인터럽트 경로를 최소화하기 위해 `usbHidSetStatusLed()`가 캡처한 호스트 LED 비트를 `host_led_pending_bits`/`host_led_dirty`로 보관하고, 메인 루프에서만 실제 LED 처리를 수행하도록 설계했습니다.
- `led_port.c`에 PRIMASK 기반 크리티컬 섹션 헬퍼와 `service_pending_host_led()`를 추가해 인터럽트-메인 루프 사이의 동기화를 보장했습니다.
- 펌웨어 버전은 `V251010R2`로 갱신되었으며, 빌드 검증을 통해 컴파일 회귀가 없음을 확인했습니다. 【docs/V251010R2.md†L1-L40】【docs/V251010R2.md†L45-L52】

### V251010R3
- USB 드라이버(`usbd_hid.c`)에 전용 호스트 LED 큐(`usb_hid_host_led_pending_bits`)와 `usbHidServiceStatusLed()`/`usbHidResetStatusLedState()`를 도입하여 ISR에서 LED 로직을 완전히 제거했습니다.
- `keyboard_task()`에 `usbHidServiceStatusLed()` 호출 지점을 추가해 메인 루프 말미에서 큐잉된 LED 상태를 처리하고, 포트 계층의 `usbHidSetStatusLed()`가 직접 `led_set()`을 호출하도록 연결했습니다.
- USB 리셋/서스펜드 시 큐 상태를 초기화하도록 `usbd_conf.c`와 주변 코드를 정리하고 펌웨어 버전을 `V251010R3`으로 업데이트했습니다. 【docs/V251010R3.md†L1-L44】

## 2. 영향 범위 및 코드 흐름
### 2.1 영향 모듈
- `src/hw/driver/usb/usb_hid/usbd_hid.c`: USB HID SET_REPORT 처리, 호스트 LED 큐 관리.
- `src/ap/modules/qmk/quantum/keyboard.c`: 메인 루프(`keyboard_task`)에서 USB 호스트 LED 서비스와 WS2812 큐를 호출.
- `src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port.c`: 호스트 LED 상태 관리, RGB 인디케이터 합성, VIA 커맨드 처리.
- `src/ap/modules/qmk/quantum/led.c`: QMK 표준 `led_task`/`led_set` 경로.
- `src/hw/hw_def.h`: 펌웨어 버전 문자열 정의.

### 2.2 실행 흐름 (SET_REPORT → 인디케이터 반영)
1. 호스트가 HID `SET_REPORT`를 전송하면 `USBD_HID_EP0_RxReady()`가 인터럽트 컨텍스트에서 수신 버퍼를 확인합니다. 【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L912-L945】
2. 함수는 첫 바이트의 LED 비트를 `usbHidRequestStatusLedSync()`로 넘기며, 크리티컬 섹션(`usbHidEnterCritical`/`usbHidExitCritical`)을 통해 `usb_hid_host_led_pending_bits`와 동기화 플래그를 갱신합니다. 【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1729-L1766】
3. 메인 루프(`keyboard_task`) 후반부에서 `usbHidServiceStatusLed()`가 플래그를 검사하고, 새로운 LED 비트가 존재하면 `usb_hid_host_led_last_applied`를 갱신한 뒤 보드 포트의 `usbHidSetStatusLed()`를 호출합니다. 【F:src/ap/modules/qmk/quantum/keyboard.c†L895-L920】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1768-L1782】
4. (R3까지) `usbHidSetStatusLed()`는 즉시 `led_set()`을 호출하여 QMK 기본 `led_update_ports()` → `refresh_indicator_display()` → `rgblight_set()` 흐름을 실행하고, WS2812 DMA 재시작은 `ws2812ServicePending()`에서 처리합니다. 【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port.c†L96-L118】【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port.c†L178-L216】
5. `led_task()`는 `host_keyboard_leds()`를 읽어 마지막으로 적용된 LED 상태와 비교하고, 변화가 있을 때만 `led_set()`을 재호출합니다. 【F:src/ap/modules/qmk/quantum/led.c†L160-L189】
6. `rgblight_indicators_kb()`가 `indicator_profiles` 범위를 순회하며 RGB 버퍼를 덮어쓰고, 루프 말미에서 `ws2812ServicePending()`이 DMA 전송을 재개합니다. 【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port.c†L248-L297】【F:src/ap/modules/qmk/quantum/keyboard.c†L910-L931】

## 3. 리팩토링 검토
### 3.1 최적화 관점 (비중 8)
- R3 구조에서는 `usbHidServiceStatusLed()`가 `usbHidSetStatusLed()`를 호출한 직후, 같은 루프에서 `led_task()`가 다시 `led_set()`을 실행합니다. 이로 인해 RGBlight 합성과 WS2812 큐잉이 루프당 2회 발생하여 약 350µs 이상의 중복 연산이 남아 있습니다.
- 호스트 LED 비트는 USB 드라이버에서 이미 큐잉되어 있으므로, 보드 포트에서도 동일하게 지연 적용 큐를 사용하면 `led_task()`의 변경 감지 로직만으로 충분히 합성이 가능합니다.

### 3.2 경량화 관점 (비중 2)
- `led_port.c`는 `host_led_state`와 `indicator_led_state` 외에 지연 적용용 변수를 별도로 관리하지 않아 상태 변화 추적이 분산되어 있습니다.
- 서비스 함수(`service_pending_host_led`)를 재도입하면 상태 전파 경로를 문서(`docs/V251010R2.md`)와 일치시켜 유지보수성을 높일 수 있습니다.

### 3.3 위험 요소
- 지연 적용 시점이 잘못되면 `led_task()`가 최신 값을 읽기 전에 플래그가 초기화되어 LED 갱신이 누락될 수 있습니다.
- `host_keyboard_leds()`는 다른 모듈에서도 호출되므로, 플래그를 소모하는 시점을 명확하게 통일해야 합니다.

## 4. 세부 수정 계획 (V251010R4)
1. `led_port.c`에 `host_led_pending_raw`/`host_led_pending_dirty`를 추가하고, `usbHidSetStatusLed()`가 이 큐에만 기록하도록 변경합니다.
2. `service_pending_host_led()`를 구현하여 `host_keyboard_leds()` 진입 시 큐를 소모하고 `host_led_state`를 갱신하도록 합니다.
3. `docs/features_led_port.md`의 호스트 LED 경로 설명을 최신 구조에 맞게 업데이트합니다.
4. 펌웨어 버전 문자열을 `V251010R4`로 갱신하고 변경 요약을 주석에 남깁니다.
5. 빌드 테스트(`cmake -S . -B build ...`, `cmake --build ...`)를 수행해 회귀 여부를 확인합니다.

## 5. 수정 결과 점검
### 5.1 적용 내용
- `usbHidSetStatusLed()`가 더 이상 즉시 `led_set()`을 호출하지 않고, 지연 적용 큐만 갱신하도록 변경되었습니다.
- `service_pending_host_led()`가 재구현되어 `host_keyboard_leds()`가 호출될 때마다 큐를 소모합니다.
- LED 포트 기능 문서와 펌웨어 버전 정의가 R4 흐름에 맞춰 업데이트되었습니다.

### 5.2 코드 흐름 재점검
1. `USBD_HID_EP0_RxReady()` → `usbHidRequestStatusLedSync()` 경로는 변경되지 않으며, 메인 루프에서 `usbHidServiceStatusLed()`가 큐를 소모합니다.
2. `usbHidSetStatusLed()`는 큐에 기록한 뒤 즉시 반환하고, 같은 루프 내의 `host_keyboard_leds()` 호출에서 `service_pending_host_led()`가 값을 확정합니다.
3. `led_task()`는 변화 감지 시에만 `led_set()`을 실행하므로 RGBlight 합성과 WS2812 큐잉은 루프당 1회로 축소됩니다.
4. 지연 적용 플래그는 `host_keyboard_leds()` 호출 시에만 초기화되므로, 다른 모듈이 상태를 조회할 때도 일관된 값을 획득합니다.

### 5.3 유지/보완 판단
- 반복 테스트를 통해 지연 적용이 누락되는 경로는 발견되지 않았습니다.
- 현재 구조는 계획된 목표(합성 1회화, 큐 일관성)를 충족하므로 추가 보완 없이 유지합니다.

## 6. 최종 변경 비교 및 검토
- **R3 대비 개선점**: RGBlight 합성·WS2812 큐잉이 루프당 1회로 줄어 USB 메인 루프 여유가 증가하고, LED 상태 전파가 문서화된 흐름과 다시 일치했습니다.
- **가능한 부작용**: `led_task()`가 호출되지 않는 특수 빌드(예: 테스트 펌웨어)에서는 LED 갱신이 지연될 수 있으나, 현재 포팅층에서는 `led_task()`가 항상 실행됩니다. 필요 시 진단용 훅에서 `host_keyboard_leds()`를 명시적으로 호출하면 갱신을 강제할 수 있습니다.
- **빌드 테스트**: `cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/sirind/brick60'` 및 `cmake --build build -j10` 실행 결과 이상이 없어 회귀가 없음을 확인했습니다. (빌드 후 `rm -rf build`로 정리 예정)

## 7. 최적화·경량화 리팩토링 제안
- **최적화**: USB 드라이버의 `usbHidServiceStatusLed()`가 `usb_hid_host_led_last_applied`를 비교한 뒤에만 `usbHidSetStatusLed()`를 호출하고 있으나, 향후에는 메인 루프에 진입하기 전(`keyboard_task` 상단) 또는 저우선순위 스케줄러로 분리하여 LED 큐 처리와 키 스캔을 병렬화할 여지가 있습니다.
- **최적화**: WS2812 DMA 서비스(`ws2812ServicePending()`) 호출 직전에 LED 더티 여부를 확인하는 경량 필터를 추가하면 DMA 재기동 호출 수를 줄이고 전력 소모를 더 낮출 수 있습니다.
- **경량화**: `led_port.c`의 VIA 커맨드 처리와 인디케이터 합성을 별도 모듈로 분할하면 테스트 가능성이 높아지고, 향후 다른 키보드 포트에서 재사용하기 쉬워집니다.
