# V251010R1~V251010R5 LED 포트 경로 리뷰

## 1. 검토 범위
- USB 호스트 LED 비동기 처리 경로: `usbHidRequestStatusLedSync` → `usbHidStatusLedPending` → `usbHidServiceStatusLed` → `usbHidSetStatusLed` 흐름과 캐시 적용 로직을 점검했습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1735-L1787】【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_host.c†L10-L62】
- QMK 메인 루프에서의 선행 처리 위치, WS2812 서비스 호출, 인디케이터 합성 루틴을 확인했습니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L809-L913】【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L7-L229】
- VIA 설정 명령 및 EEPROM 플러시 경로를 포함한 설정 인터페이스를 검토했습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_via.c†L5-L182】
- WS2812 DMA 요청 큐와 메인 루프 서비스 경로를 검토했습니다.【F:src/hw/driver/ws2812.c†L185-L237】

## 2. 코드 흐름 점검 결과
### 2.1 USB 호스트 LED 비동기 처리
- ISR에서 `usbHidRequestStatusLedSync()`가 마지막 `SET_REPORT` 비트를 저장하고 플래그를 세팅한 뒤, 메인 루프가 `usbHidServiceStatusLed()`에서 크리티컬 섹션으로 비트를 회수한 뒤 `usbHidSetStatusLed()`(키보드 포트 구현)로 전달하는 구조가 유지됩니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1747-L1786】
- 키보드 포트 구현은 LED 상태를 두 단계로 분리하여, 즉시 캐시만 갱신하고 실 적용은 `host_keyboard_leds()` 호출 시 수행되도록 큐잉합니다. 이를 통해 EP0 ISR에서 RGBlight 합성까지 진행되던 R2 이전 구조의 지연을 제거했습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_host.c†L10-L62】
- 메인 루프 `keyboard_task()`는 USB 모듈과 매트릭스 스캔 전에 호스트 LED 큐를 우선 처리해, 상태가 지연 없이 `led_task()`까지 전파됩니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L809-L913】

### 2.2 인디케이터 합성과 캐시
- 인디케이터 합성 모듈은 LED 타입별 프로파일과 HSV 캐시를 관리하며, 색상 변경 시 `led_port_indicator_mark_color_dirty()`로만 캐시 무효화를 수행해 DMA 재전송을 최소화하고 있습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L7-L229】
- `led_update_ports()`는 호스트 LED 상태 캐시를 먼저 갱신한 뒤, 변화가 있을 때만 인디케이터 리프레시를 호출하여 불필요한 RGBlight 플러시를 피합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_host.c†L47-L57】

### 2.3 VIA 설정 경로
- VIA `set_value` 경로는 입력 유효성 검증을 강화하고, 밝기/색상 변경 시에만 캐시를 무효화하여 설정 변경이 최소한의 DMA 재시작만 유발하도록 구성되었습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_via.c†L109-L182】
- 설정 저장은 프로파일별 EEPROM 플러시 함수로 일원화되어, LED 타입에 따른 중복 코드를 제거했습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_via.c†L16-L59】

### 2.4 WS2812 DMA 서비스
- WS2812 경로는 인터럽트 컨텍스트에서 DMA 요청만 등록하고, 메인 루프에서 `ws2812ServicePending()`이 큐를 소모하는 구조입니다. 다만 현재는 `ws2812HasPendingTransfer()`로 선행 확인 후 다시 크리티컬 섹션에 진입해 서비스를 수행하고 있습니다.【F:src/hw/driver/ws2812.c†L195-L237】【F:src/ap/modules/qmk/quantum/keyboard.c†L907-L913】

## 3. 경량화 및 성능 개선 제안
1. **호스트 LED 큐 확인의 중복 크리티컬 섹션 제거**  
   - `keyboard_task()`에서 `usbHidStatusLedPending()` 호출로 크리티컬 섹션에 한번, 이후 `usbHidServiceStatusLed()`에서 다시 한번 진입합니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L809-L816】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1757-L1786】  
   - `usbHidServiceStatusLed()`는 내부에 자체 가드가 있으므로, 메인 루프에서 직접 호출하도록 변경하면 인터럽트 마스크 연산을 절반으로 줄일 수 있습니다. HS 8kHz 스케줄링 환경에서 반복 호출 비용을 줄이는 데 유효합니다.

2. **WS2812 서비스 진입 조건 단일화**  
   - 현재 `ws2812HasPendingTransfer()`와 `ws2812ServicePending()`가 각각 크리티컬 섹션을 생성해, 대기 상태에서도 인터럽트 마스크 조작이 두 번 일어납니다.【F:src/hw/driver/ws2812.c†L206-L237】  
   - 메인 루프에서 `ws2812ServicePending()`만 호출하도록 정리하면, 요청이 없을 때도 한 번만 인터럽트를 비활성화하고 즉시 복원하므로 CPU 점유를 낮출 수 있습니다. DMA 요청이 잦은 RGB 씬에서 미세한 지연 감소가 기대됩니다.

3. **인디케이터 리프레시 조건 추가 최적화 검토**  
   - VIA 설정 변경으로 `led_port_indicator_refresh()`가 호출될 때, 실제로 호스트 LED가 모두 비활성 상태라면 DMA 재시작 없이 색상 캐시만 갱신하는 경량 경로를 추가할 수 있습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L40-L101】【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_via.c†L109-L182】  
   - 예: `indicator_led_state`와 프로파일 `host_mask`를 조합해 현재 점등 중인 채널이 없을 때는 `rgblight_set()` 대신 색상 캐시만 업데이트하도록 분기하면, 설정 연속 조정 시 DMA 연속 재시작을 줄일 수 있습니다.

4. **호스트 LED 적용 지연 최소화 모니터링**  
   - 현재 `host_keyboard_leds()` 호출 시에만 큐를 소모하므로, 만약 사용자가 `led_task()` 비활성 설정을 적용할 경우(커스텀 펌웨어 시나리오) 큐가 처리되지 않을 가능성이 있습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_host.c†L10-L62】【F:src/ap/modules/qmk/quantum/led.c†L176-L204】  
   - 이 경우를 대비해 `usbHidServiceStatusLed()` 내부에서 최소 한 번은 `service_pending_host_led()`에 준하는 즉시 적용 루틴을 제공하거나, `keyboard_task()` 선행 처리에서 `led_port_host_store_cached_state`를 직접 호출하는 보조 경로를 마련할 수 있는지 확인이 필요합니다.

위 제안들은 기존 구조를 유지하면서 인터럽트 마스크 조작 횟수와 DMA 재시작 빈도를 줄이는 방향으로 설계되었습니다. 테스트 시에는 USB HS 8kHz 환경에서의 마이크로프레임 간격, WS2812 DMA 타이밍, VIA 설정 플로우의 회귀 여부를 우선 확인해야 합니다.