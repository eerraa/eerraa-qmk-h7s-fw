# BRICK60 LED 포트 Codex 레퍼런스 (V251009R9)

## 1. 파일 개요
- 대상: `src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port.c`
- 포함 헤더: `led_port.h`, `color.h`, `eeconfig.h`, `rgblight.h`
- 기능 요약: 호스트 LED 비트맵을 RGB 인디케이터로 재구성하고, VIA 커맨드를 통해 사용자 지정 HSV 설정을 EEPROM과 RGB 버퍼에 반영합니다.

## 2. 핵심 데이터 구조
| 심볼 | 정의 | 주요 필드 | 용도 |
| --- | --- | --- | --- |
| `led_config_t` | 32비트 union (`raw` ↔ `{enable, HSV}`) | `enable`, `HSV hsv` | EEPROM에 저장되는 LED 타입별 사용자 설정 |
| `indicator_profile_t` | 인디케이터 메타데이터 테이블 | `default_config`, `start`, `end`, `host_mask`, `flush` | 각 LED 타입의 기본 HSV/범위/EECONFIG 플러시 함수 |
| `indicator_profiles[]` | `LED_TYPE_MAX_CH` 길이 상수 테이블 | CAPS/SCROLL/NUM에 대한 기본값 | 타입별 기본 설정 로딩 및 범위 캐시 |
| `led_config[]` | 가변 배열 | `led_config_t` | RAM 캐시 (EEPROM 동기화 대상) |
| `indicator_rgb_cache[]` & `indicator_rgb_dirty[]` | RGB 캐시 및 더티 플래그 | `RGB`, `bool` | HSV→RGB 변환 최소화 |
| `host_led_state`, `indicator_led_state` | `led_t` | `raw` 비트필드 | 호스트/인디케이터 상태 동기화 |

## 3. 정적 헬퍼 함수 요약
- `led_config_from_type(uint8_t led_type)`
  - 범위 가드를 통과한 경우 `led_config[]` 포인터를 반환합니다.
- `indicator_profile_from_type(uint8_t led_type)`
  - 프로파일 테이블 포인터를 반환하며, 타입 범위를 벗어나면 `NULL`을 리턴합니다.
- `indicator_config_valid(uint8_t led_type, bool *needs_migration)`
  - EEPROM 슬롯 무결성을 검사하고, 구버전 데이터를 현재 레이아웃으로 마이그레이션합니다.
- `mark_indicator_color_dirty(uint8_t led_type)`
  - 캐시 플래그를 설정해 HSV 변경 시 RGB 재계산을 예약합니다.
- `get_indicator_rgb(uint8_t led_type, const led_config_t *config)`
  - 더티 플래그를 확인해 `hsv_to_rgb()`를 지연 평가합니다.
- `should_light_indicator(const led_config_t *config, const indicator_profile_t *profile, led_t led_state)`
  - 사용자 설정과 호스트 LED 비트를 모두 만족할 때만 true를 반환합니다.
- `refresh_indicator_display(void)`
  - RGBlight가 초기화된 경우에만 `rgblight_set()` 호출로 합성된 버퍼를 커밋합니다.
- `flush_indicator_config(uint8_t led_type)` / `via_qmk_led_save(uint8_t led_type)`
  - 프로파일 테이블의 `flush` 콜백을 통해 EEPROM을 업데이트합니다.

## 4. 주요 실행 흐름
### 4.1 초기화: `led_init_ports`
1. 각 LED 타입에 대응하는 EECONFIG 슬롯을 초기화합니다.
2. `LED_TYPE_MAX_CH` 만큼 순회하며, `led_config_from_type()` / `indicator_profile_from_type()`로 포인터를 획득합니다.
3. 포인터가 모두 유효하면 색상 캐시를 더티 상태로 표시합니다.
4. `indicator_config_valid()` 결과가 false이면 프로파일 기본값을 RAM에 복사하고 즉시 EEPROM을 플러시합니다.
5. 마이그레이션이 발생한 경우에도 플러시를 호출해 변경 사항을 확정합니다.

### 4.2 호스트 LED 입력: `usbHidSetStatusLed` → `led_set`
- USB 경로에서 전달된 `led_bits`를 `host_led_state`에 저장하고, QMK 기본 구현(`led_set`)을 호출해 표준 동작을 유지합니다.

### 4.3 QMK 갱신 루프: `led_update_ports`
1. QMK가 보고하는 `led_t`를 `host_led_state`와 `indicator_led_state`에 반영합니다.
2. 상태가 이전과 동일하면 조기 반환하여 중복 갱신을 방지합니다.
3. 변경이 감지되면 `refresh_indicator_display()`를 호출해 RGBlight 버퍼를 재합성합니다.

### 4.4 VIA 커맨드 처리: `via_qmk_led_command`
1. 인자 유효성(포인터, 길이, `LED_TYPE_MAX_CH`)을 검사합니다.
2. 첫 바이트(`command_id`)에 따라 다음 분기 수행:
   - `id_custom_set_value`: `via_qmk_led_set_value()` 호출. 성공 여부에 따라 `command_id`를 `id_unhandled`로 덮어씁니다.
   - `id_custom_get_value`: `via_qmk_led_get_value()` 호출. 실패 시 동일하게 `id_unhandled`로 응답합니다.
   - `id_custom_save`: `via_qmk_led_save()`를 통해 EEPROM 플러시를 실행합니다.
3. 페이로드 길이는 `length - 2`로 계산하여 서브 커맨드 헬퍼에 전달합니다.

### 4.5 RGBlight 합성: `rgblight_indicators_kb`
1. `host_keyboard_led_state()`를 읽어 현재 호스트 비트를 가져옵니다.
2. LED 타입 루프에서 설정/프로파일 포인터를 동시에 가드합니다.
3. `should_light_indicator()`가 true인 경우에만 RGB 캐시를 조회하고, 프로파일의 `[start, end)` 범위를 순회하며 `rgblight_set_color_buffer_at()`를 호출합니다.
4. 모든 타입을 처리한 뒤 `true`를 반환해 상위 RGBlight 루틴에 성공을 알립니다.

## 5. VIA 서브 커맨드 규격
| 서브 커맨드 | 최소 페이로드 길이 | 동작 | 추가 처리 |
| --- | --- | --- | --- |
| `id_qmk_led_enable` | 1바이트 | `enable` 읽기/쓰기 | 값 변경 시 즉시 `refresh_indicator_display()` 호출 |
| `id_qmk_led_brightness` | 1바이트 | `hsv.v` 읽기/쓰기 | 쓰기 시 `mark_indicator_color_dirty()`로 캐시 무효화 |
| `id_qmk_led_color` | 2바이트 | `hsv.h`, `hsv.s` 읽기/쓰기 | 쓰기 시 더티 플래그 세팅 및 합성 트리거 |
- 유효하지 않은 서브 커맨드는 `false`를 반환하여 `id_unhandled` 응답으로 전파됩니다.
- 데이터 포인터 또는 길이가 부족한 경우 즉시 실패 처리합니다.

## 6. 확장 시 체크리스트
1. **새 LED 타입 정의**: `led_port.h`의 `LED_TYPE_*` 열거형과 `LED_TYPE_MAX_CH`를 수정합니다.
2. **프로파일 등록**: `indicator_profiles[]`에 기본 HSV, 시작/종료 인덱스, 호스트 비트 마스크, 플러시 콜백을 추가합니다.
3. **EEPROM 슬롯**: `EECONFIG_DEBOUNCE_HELPER()` 매크로를 통해 신규 타입의 EECONFIG 인덱스를 선언합니다.
4. **호스트 비트 매핑**: `should_light_indicator()`가 참조하는 `host_mask`가 올바른 비트를 참조하는지 확인합니다.
5. **테스트 포인트**:
   - 전원이 꺼진 상태에서 EEPROM 기본값이 로드되는지.
   - VIA에서 enable/brightness/color 변경 후 즉시 RGBlight가 갱신되는지.
   - 잘못된 LED 타입/페이로드가 안전하게 무시되는지.

## 7. 방어 로직 요약
- 모든 LED 타입 접근은 `led_type >= LED_TYPE_MAX_CH` 가드로 보호됩니다.
- VIA 커맨드는 포인터/길이 검사 후에만 처리되며, 실패 시 호출자에게 `id_unhandled`를 반환합니다.
- RGB 캐시 배열은 더티 플래그와 범위 가드(`if (led_type >= LED_TYPE_MAX_CH)`)로 보호됩니다.
- EEPROM 무결성 검사에서 `UINT32_MAX` 패턴과 구버전 enable 비트(최하위 2비트)를 검증합니다.

## 8. 디버깅 팁
- RGB 변환은 `hsv_to_rgb()` 한 곳에서만 수행되므로, 캐시가 갱신되지 않으면 `indicator_rgb_dirty[]` 상태를 확인합니다.
- VIA 동작 이상 시 `via_qmk_led_command()`에서 `command_id`가 `id_unhandled`로 바뀌었는지 확인하면 문제 지점을 좁힐 수 있습니다.
- 인디케이터가 켜지지 않으면 `host_led_state.raw`와 `indicator_profiles[].host_mask`의 비트 매칭을 우선 점검합니다.
