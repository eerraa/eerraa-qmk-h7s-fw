# V251010R1~V251010R7 LED 포트 경량화 재검토 (Review4)

## 1. 검토 범위
- 메인 루프에서 USB 호스트 LED 큐와 WS2812 DMA 서비스가 직렬로 처리되는 흐름을 추적했습니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L809-L907】
- USB SET_REPORT 인터럽트가 비동기 큐로 전환되는 경로(`usbHidRequestStatusLedSync` → `usbHidServiceStatusLed` → `usbHidSetStatusLed`)와 큐 소비 후 상태 캐시 반영 로직을 확인했습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1747-L1777】【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_host.c†L31-L62】
- 인디케이터 합성(`led_port_indicator_refresh`)과 RGBlight 버퍼 커밋, VIA 설정 갱신 시 색상 캐시 재계산이 어떻게 결합되는지 검토했습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L5-L255】【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_via.c†L178-L182】
- WS2812 DMA 요청/서비스 경로와 드라이버 레벨의 프레임 더티 체크를 다시 점검했습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/driver/rgblight_drivers.c†L8-L55】【F:src/hw/driver/ws2812.c†L185-L240】

## 2. 주요 동작 확인
### 2.1 USB 호스트 LED 큐
- `usbHidServiceStatusLed()`는 요청 플래그가 없으면 바로 반환하고, 크리티컬 섹션 내부에서 페이로드를 복사한 뒤 중복 상태를 제거합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1758-L1776】
- 큐에서 꺼낸 값은 `usbHidSetStatusLed()`를 통해 호스트 LED 캐시에 지연 적용되어, 동일한 비트가 반복될 때 불필요한 DMA 요청을 막습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_host.c†L31-L56】

### 2.2 인디케이터 합성
- 인디케이터 합성기는 RGB 캐시와 더티 플래그를 유지하고, 현재 호스트 LED 마스크를 기반으로 활성화 여부를 판정한 뒤 `rgblight_set()`으로 커밋합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L5-L127】
- VIA 설정 변경 시에는 즉시 `led_port_indicator_refresh()`를 호출해 캐시된 색상을 적용하도록 되어 있습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_via.c†L178-L182】

### 2.3 WS2812 DMA 경로
- 드라이버는 LED 배열과 마지막 전송 프레임을 비교해 변경이 있을 때만 비트 버퍼를 갱신하고 DMA 갱신을 예약합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/driver/rgblight_drivers.c†L8-L49】
- 메인 루프 말미의 `ws2812ServicePending()`은 pending/busy 상태를 확인한 뒤 단일 크리티컬 섹션에서 DMA 재시작을 담당합니다.【F:src/hw/driver/ws2812.c†L206-L229】

## 3. 개선 제안 및 적용 판단
### 제안 1: `led_port_indicator_refresh()`의 불필요한 DMA 커밋 줄이기
- **요약**: 현재 구현은 호스트 LED 마스크가 이전과 동일하더라도 `rgblight_set()`을 호출합니다. `indicator_rgb_dirty[]`가 모두 false이고 `active_mask`가 `indicator_last_active_mask`와 동일한 경우에는 커밋을 생략하도록 조건을 추가하면 RGBlight DMA 호출을 한 번 더 줄일 수 있습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L7-L127】
- **기대 효과**: 색상 값이 변하지 않은 상황에서 VIA 설정이나 호스트 LED 재전송으로 인한 중복 DMA 호출을 차단해 CPU 점유율과 버스 트래픽을 줄입니다.
- **예상 부작용**: 색상 변경 직후에도 더티 플래그가 남아 있어 커밋을 수행해야 하므로, 조건을 추가할 때 `indicator_rgb_dirty[]`에 하나라도 true가 있으면 무조건 커밋하도록 보완해야 합니다. 그렇지 않으면 색상 변경이 호스트 LED 점등 상태 유지 시 반영되지 않는 회귀가 발생합니다.
- **적용 판단**: **적용 권장** — 더티 플래그를 함께 검사하는 조건을 넣으면 회귀 위험이 통제 가능하며, DMA 호출 수를 눈에 띄게 줄일 수 있습니다.

### 제안 2: `usbHidRequestStatusLedSync()`에서 동일 상태 재큐잉 방지
- **요약**: 동일한 LED 비트가 이미 적용된 상태(`usb_hid_host_led_last_applied`)와 일치하고 추가 대기 플래그가 없는 경우, 크리티컬 섹션 진입 후 바로 반환하도록 해 중복 큐 진입을 차단합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1747-L1776】
- **기대 효과**: 호스트가 같은 LED 상태를 반복 전송하더라도 메인 루프에서 불필요한 크리티컬 섹션과 `usbHidSetStatusLed()` 호출이 발생하지 않아 USB ISR과 메인 루프 모두의 오버헤드가 감소합니다.
- **예상 부작용**: `usb_hid_host_led_last_applied` 갱신은 크리티컬 섹션 밖에서 수행되므로, 서비스 함수가 아직 값을 갱신하기 전에 동일 상태가 재수신되면 한 차례 더 큐잉이 일어납니다. 기존 동작과 동일한 수준의 오버헤드가 남을 뿐 기능 회귀는 없습니다.
- **적용 판단**: **적용 권장** — 추가 분기만으로 ISR/메인 루프 양쪽의 중복 처리를 줄일 수 있고, 실패 시에도 현재와 동일한 동작이 유지됩니다.

---
이번 Review4에서는 V251010R1~R7 누적 변경 이후 남아 있는 DMA 호출 및 큐 처리 중복을 중심으로 경량화 여지를 재식별했습니다.
