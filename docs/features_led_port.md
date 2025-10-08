// V251011R1 호스트 LED 즉시 처리 및 WS2812 DMA 인라인 갱신 문서화
# BRICK60 LED/WS2812 Codex 레퍼런스 (V251011R1)

## 1. 파일 개요
- **핵심 포트 모듈**
  - `led_port_indicator.c`: 인디케이터 RGB 캐시, EEPROM 마이그레이션, RGBlight 합성을 담당합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L7-L138】【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L204-L265】
  - `led_port_host.c`: 호스트 LED SET_REPORT를 즉시 `led_set()`으로 위임하고 캐시를 동기화합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_host.c†L1-L64】
  - `led_port_via.c`: VIA 서브 커맨드 파서와 EEPROM 플러시 엔트리를 제공합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_via.c†L7-L179】
- **공유 헤더 및 열거형**
  - `led_port.h`: CAPS/SCROLL/NUM 타입 열거형과 초기화 엔트리를 노출합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port.h†L5-L15】
  - `led_port_internal.h`: `led_config_t`, `indicator_profile_t`, 범용 헬퍼 프로토타입을 정의합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_internal.h†L8-L46】
- **WS2812 드라이버 경로**
  - `rgblight_drivers.c`: 더티 채널만 WS2812 버퍼를 갱신하고 즉시 DMA를 재기동합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/driver/rgblight_drivers.c†L4-L55】
  - `ws2812.c`/`ws2812.h`: `ws2812Refresh()`가 임계구역을 인라인으로 처리해 DMA를 즉시 재시작하고, `ws2812RequestRefresh()`는 ISR 안전 요청만 수행합니다.【F:src/common/hw/include/ws2812.h†L23-L26】【F:src/hw/driver/ws2812.c†L184-L238】
  - `keyboard.c`: WS2812 서비스 호출이 제거되어 루프 말미가 단순화되었습니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L823-L902】
  - `hw.c`: 부팅 시 WS2812 드라이버를 초기화합니다.【F:src/hw/hw.c†L50-L76】

## 2. 인디케이터 데이터 구조 및 캐시
| 심볼 | 정의 | 주요 필드 | 역할/비고 |
| --- | --- | --- | --- |
| `led_config_t` | EEPROM 싱크 유니온 (`raw` ↔ `{enable, HSV}`) | `enable`, `HSV hsv` | 32비트 정렬로 VIA/EEPROM 데이터가 공유됩니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_internal.h†L10-L22】 |
| `indicator_profile_t` | 채널별 메타데이터 | `default_config`, `start`, `end`, `host_mask`, `flush` | 기본 HSV, 호스트 비트, EEPROM 플러시 콜백을 묶습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_internal.h†L23-L35】 |
| `indicator_profiles[]` | CAPS/SCROLL/NUM 테이블 | 각 채널별 RGB 범위/마스크 | 인디케이터 기본값과 플러시 함수를 상수화합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L17-L39】 |
| `indicator_rgb_cache[]` / `indicator_rgb_dirty[]` | RGB 캐시 및 더티 플래그 | 캐시된 `RGB`, `bool` | HSV 변경 시에만 재계산하여 CPU 사용을 줄입니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L7-L93】 |
| `indicator_last_active_mask` | 마지막 DMA 커밋 시 활성 비트 | `uint8_t` | 점등 상태가 유지되면 DMA 재시작을 생략합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L11-L134】 |

- `led_port_indicator_refresh()`는 캐시 더티 여부와 호스트 마스크 변화를 모두 검사해, 변화가 없으면 `rgblight_set()` 호출을 생략합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L99-L138】
- `led_port_indicator_get_rgb()`는 더티 플래그가 설정된 채널만 `hsv_to_rgb()`로 갱신합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L81-L97】

## 3. 초기화 및 EEPROM 마이그레이션 흐름
1. `led_init_ports()`는 EECONFIG 슬롯 초기화 후 모든 채널을 순회합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L204-L235】
2. 채널마다 색상 캐시를 더티 처리하고, `led_port_indicator_config_valid()`로 무결성을 검사합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L221-L235】【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L140-L175】
3. 유효하지 않은 데이터는 프로파일 기본값으로 대체하고 즉시 EEPROM을 플러시합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L223-L233】
4. 구버전 enable 비트를 감지하면 `needs_migration` 플래그를 통해 저장을 재호출합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L159-L175】【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L230-L233】

## 4. 호스트 LED 즉시 처리 흐름
- `usbHidSetStatusLed()`는 캐시와 비교해 변화가 있을 때만 `led_set()`을 호출하고, 호출 전 캐시를 먼저 갱신해 재진입을 방지합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_host.c†L22-L46】
- `host_keyboard_leds()`는 큐 없이 최신 캐시를 그대로 반환합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_host.c†L52-L64】
- `led_update_ports()`는 QMK가 전달한 `led_t`를 저장하고 변화 시 인디케이터 재합성을 트리거합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_host.c†L40-L51】
- `rgblight_indicators_kb()`는 현재 호스트 비트에 따라 채널 범위를 순회하며 RGB 버퍼를 덮어씁니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L237-L265】

## 5. VIA 커맨드 규격
| 서브 커맨드 | 요구 길이 | 동작 | 후속 처리 |
| --- | --- | --- | --- |
| `id_qmk_led_enable` | 1바이트 | Enable 읽기/쓰기 | 값이 바뀌면 `led_port_indicator_refresh()` 재호출 | 
| `id_qmk_led_brightness` | 1바이트 | HSV `v` 접근 | 변경 시 색상 캐시를 더티 처리 | 
| `id_qmk_led_color` | 2바이트 | HSV `h/s` 접근 | 변경 시 색상 캐시를 더티 처리 |

- 모든 커맨드는 타입/길이 가드를 통과해야 하며 실패 시 `id_unhandled`를 되돌립니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_via.c†L21-L58】
- Getter/Setter는 페이로드가 부족하면 즉시 `false`를 반환합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_via.c†L61-L174】
- 설정 값이 실제로 변경된 경우에만 합성 루틴을 호출해 불필요한 DMA 요청을 줄입니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_via.c†L125-L179】

## 6. RGBlight ↔ WS2812 파이프라인
1. `rgblight_set()`이 호출되면 포트 드라이버의 `ws2812_setleds()`가 마지막 프레임과 비교해 변경 채널만 `ws2812SetColor()`로 반영합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/driver/rgblight_drivers.c†L8-L49】
2. 변경이 존재할 때 `ws2812RequestRefresh(limit)`로 DMA 프레임 길이를 계산하고, 이어서 `ws2812Refresh()`가 즉시 DMA를 재기동합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/driver/rgblight_drivers.c†L40-L55】【F:src/hw/driver/ws2812.c†L184-L238】
3. `ws2812Refresh()`는 인터럽트 여부를 확인해 안전할 때만 DMA를 중단/재시작하며, 임계구역 내부에서 `ws2812_pending`과 `ws2812_pending_len`을 처리합니다.【F:src/hw/driver/ws2812.c†L184-L238】
4. DMA 완료 인터럽트는 `ws2812HandleDmaTransferCompleteFromISR()`에서 바쁨 상태를 해제해 다음 요청을 허용합니다.【F:src/hw/driver/ws2812.c†L241-L249】
5. `ws2812Init()`은 부팅 시 타이머/채널을 설정하고 전체 프레임 길이 캐시를 채워 초기 DMA 요청을 준비합니다.【F:src/hw/hw.c†L50-L76】【F:src/hw/driver/ws2812.c†L44-L137】

## 7. RGB Matrix 연동
- RGB Matrix가 WS2812 드라이버를 공유하도록 `rgb_matrix_ws2812_array` 버퍼와 `ws2812_dirty` 플래그가 정의되어 있습니다.【F:src/ap/modules/qmk/quantum/rgb_matrix/rgb_matrix_drivers.c†L135-L197】
- `flush()`는 더티 플래그가 true일 때 `ws2812_setleds()`와 `ws2812Refresh()`를 연달아 호출해 WS2812 경로와 동일한 DMA 재기동 흐름을 사용합니다.【F:src/ap/modules/qmk/quantum/rgb_matrix/rgb_matrix_drivers.c†L149-L197】

## 8. 확장 및 검증 체크리스트
1. **채널 추가 시** `LED_TYPE_MAX_CH`, `indicator_profiles[]`, EECONFIG 매크로를 동시에 확장하고 기본 HSV/범위를 지정합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port.h†L5-L15】【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L13-L39】
2. **EEPROM 포맷 변경 시** `led_port_indicator_config_valid()` 마이그레이션 분기와 `led_port_indicator_flush_config()` 호출 지점을 조정합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L140-L235】
3. **WS2812 경로 수정 시** DMA 인터페이스(`ws2812RequestRefresh()`/`ws2812Refresh()`/`ws2812HandleDmaTransferCompleteFromISR()`) 호출 위치가 유일한지 검증합니다.【F:src/common/hw/include/ws2812.h†L23-L26】【F:src/hw/driver/ws2812.c†L184-L249】
4. **성능 점검**: 인디케이터가 꺼진 상태에서 `indicator_last_active_mask`가 0으로 유지되는지 확인하고, VIA 커맨드는 실패 시 `id_unhandled`로 응답하는지 CLI에서 검증합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L102-L138】【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_via.c†L21-L58】

---
이 문서는 Codex가 BRICK60 인디케이터/WS2812 경로를 빠르게 파악하고, 즉시 처리 기반 DMA 호출 지점을 일관되게 유지하도록 돕기 위한 참고 자료입니다.
