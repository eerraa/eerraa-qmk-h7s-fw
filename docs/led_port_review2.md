# V251010R1~V251010R6 LED 포트 경로 재점검 보고서

## 1. 검토 범위
- USB SET_REPORT 인터럽트에서 시작해 메인 루프에서 큐를 소모하는 `usbHidRequestStatusLedSync` → `usbHidServiceStatusLed` → `usbHidSetStatusLed` 흐름과 상태 캐시 반영 로직을 확인했습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1747-L1787】【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_host.c†L10-L62】
- QMK `keyboard_task()` 선행 처리 위치와 WS2812 서비스 호출 순서를 점검해, 루프 전반의 LED 업데이트 타이밍이 어떻게 배치되는지 재확인했습니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L809-L907】
- 인디케이터 합성, 캐시, VIA 설정 경로, WS2812 DMA 요청 큐의 상호 작용을 다시 추적했습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L5-L229】【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_via.c†L5-L182】【F:src/hw/driver/ws2812.c†L185-L225】

## 2. 코드 흐름 점검 결과
### 2.1 USB 호스트 LED 큐 처리
- `keyboard_task()` 초입에서 `usbHidServiceStatusLed()`를 직접 호출하도록 조정되어, 불필요한 대기 상태 점검 없이 한 번의 크리티컬 섹션으로 큐를 비울 수 있게 되었습니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L809-L907】
- `usbHidServiceStatusLed()`는 플래그를 확인한 뒤에만 크리티컬 섹션에 진입하고, 마지막으로 적용한 비트와 비교해 중복 DMA 트리거를 방지합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1757-L1787】
- 키보드 포트 계층은 지연 적용 플래그와 캐시를 분리해, 메인 루프에서 `host_keyboard_leds()`가 호출될 때만 실제 LED 상태를 갱신합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_host.c†L10-L62】

### 2.2 인디케이터 합성과 VIA 설정
- 인디케이터 모듈은 타입별 기본 프로파일, HSV 캐시, 더티 플래그를 유지하며, VIA 설정 변경 시 필요한 경우에만 `rgblight_set()`을 호출합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L16-L101】【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_via.c†L109-L182】
- `rgblight_indicators_kb()`는 현재 호스트 LED 비트를 읽어 활성화 대상만 순회하므로, 불필요한 RGB 버퍼 갱신을 회피하고 있습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L200-L228】

### 2.3 WS2812 DMA 서비스
- DMA 요청은 `ws2812RequestRefresh()`에서 큐잉되고, 메인 루프 끝자락의 `ws2812ServicePending()`이 단일 크리티컬 섹션에서 요청을 소비해 토글 횟수를 줄였습니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L904-L906】【F:src/hw/driver/ws2812.c†L195-L225】

## 3. 경량화 및 성능 개선 제안
1. **미사용 큐 상태 질의 API 정리**  
   `usbHidStatusLedPending()`은 더 이상 호출 경로가 남아 있지 않아, 헤더 공개 API로 유지할 이유가 없습니다. 함수와 선언을 정리하면 불필요한 인터럽트 마스크 조작 예제를 제거하고 문서화 부담을 줄일 수 있습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1757-L1765】【F:src/hw/driver/usb/usb_hid/usbd_hid.h†L173-L175】【05ff9b†L1-L6】
2. **WS2812 서비스의 사전 가드 도입**  
   현재 `ws2812ServicePending()`은 매 호출마다 인터럽트를 비활성화한 뒤에야 대기 상태를 확인합니다. `ws2812_pending` 또는 `ws2812_busy`가 이미 원하는 상태인지 비크리티컬 구간에서 빠르게 검사하고, 조건을 만족할 때만 크리티컬 섹션에 진입하도록 보완하면 루프 회당 인터럽트 마스크 조작 횟수를 추가로 줄일 수 있습니다.【F:src/hw/driver/ws2812.c†L206-L225】
3. **인디케이터 리프레시의 비점등 경로 최적화**  
   VIA 설정 변경 등으로 `led_port_indicator_refresh()`가 호출될 때 모든 인디케이터가 비활성 상태라면, 색상 캐시만 갱신하고 `rgblight_set()`을 생략하는 경량 경로를 두어 DMA 재시작을 피할 수 있습니다. 이를 위해 `indicator_led_state`와 프로파일 `host_mask`를 확인하는 체크를 추가하는 방안을 검토할 수 있습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_host.c†L47-L59】【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L40-L228】

---
본 검토는 R1 이후 누적된 구조 변경이 안정적으로 작동하는지 확인하고, R7 이후 적용 가능한 추가 최적화 포인트를 식별하는 데 초점을 맞추었습니다.