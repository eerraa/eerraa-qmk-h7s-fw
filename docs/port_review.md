# BRICK60 LED 포트 개선안 검토 (V251009R2)

## 1. 성능 개선안

### 1-1. 인디케이터 범위 루프 상한 사전 계산
- **변경 전**
  ```c
  for (uint8_t offset = 0; offset < count; offset++) {
    uint8_t led_index = start + offset;
    if (led_index >= RGBLIGHT_LED_COUNT) {
      break;
    }
    rgblight_set_color_buffer_at(led_index, rgb.r, rgb.g, rgb.b);
  }
  ```
- **변경 후**
  ```c
  uint16_t limit = (uint16_t)start + indicator_profiles[i].count;
  if (limit > RGBLIGHT_LED_COUNT) {
    limit = RGBLIGHT_LED_COUNT;
  }
  for (uint8_t led_index = start; led_index < limit; led_index++) {
    rgblight_set_color_buffer_at(led_index, rgb.r, rgb.g, rgb.b);
  }
  ```
- **이득**
  - 루프 내에서 매 반복마다 덧셈과 비교, 분기 연산을 제거하여 RGB 버퍼 갱신 경로의 산술 연산 수를 줄입니다.
  - LED 개수 상한 계산을 사전에 수행해 인디케이터 범위 초과 시 조기 탈출합니다.
- **부작용**
  - 없음.
- **적용 여부**
  - 적용 (최종 코드 V251009R2 반영).

### 1-2. HSV→RGB 변환 결과 캐시 도입
- **변경 전**
  ```c
  RGB rgb = hsv_to_rgb(led_config[i].hsv);
  ```
  (각 인디케이터 렌더링 시마다 HSV→RGB 변환을 수행)
- **변경 후 제안**
  ```c
  if (led_profile_dirty[i]) {
    rgb_cache[i] = hsv_to_rgb(led_config[i].hsv);
    led_profile_dirty[i] = false;
  }
  RGB rgb = rgb_cache[i];
  ```
- **이득**
  - LED 설정이 자주 변하지 않는 시나리오에서 불필요한 HSV→RGB 변환을 줄여 CPU 점유율을 낮출 수 있습니다.
- **부작용**
  - `led_config_t`는 EEPROM과 1:1 매핑된 4바이트 구조체라 캐시를 도입하려면 별도 배열과 더티 플래그를 추가해야 하며, 초기화·동기화 경로가 복잡해집니다.
  - RAM 사용량이 소폭 증가합니다.
- **적용 여부**
  - 보류 (현행 코드 유지). 설정 변경 빈도가 낮아 기대 이득이 제한적이고, EEPROM 구조를 보존해야 하므로 유지보수 부담이 큽니다.

## 2. 복잡도 완화 개선안

### 2-1. 인디케이터 메타데이터 테이블화
- **변경 전**
  ```c
  static const led_config_t indicator_defaults[LED_TYPE_MAX_CH] = { ... };
  static const struct { uint8_t start; uint8_t count; } indicator_ranges[LED_TYPE_MAX_CH] = { ... };

  switch (led_type) {
    case LED_TYPE_CAPS:
      eeconfig_flush_led_caps(true);
      break;
    ...
  }
  ```
- **변경 후**
  ```c
  typedef struct {
    led_config_t         default_config;
    uint8_t              start;
    uint8_t              count;
    uint8_t              host_mask;
    indicator_flush_fn_t flush;
  } indicator_profile_t;

  static const indicator_profile_t indicator_profiles[LED_TYPE_MAX_CH] = { ... };
  indicator_profiles[led_type].flush(true);
  ```
- **이득**
  - 기본 HSV, LED 범위, 호스트 LED 비트, EEPROM 플러시 함수를 단일 구조로 통합해 채널 추가/수정 시 단일 테이블만 갱신하면 됩니다.
  - switch 문과 분리된 배열 정의가 사라져 제어 흐름이 간결해집니다.
- **부작용**
  - 구조체 초기화가 길어져 초기 학습 비용이 소폭 증가할 수 있습니다.
- **적용 여부**
  - 적용 (최종 코드 V251009R2 반영).

### 2-2. 호스트 LED 판정 로직 단순화
- **변경 전**
  ```c
  switch (led_type) {
    case LED_TYPE_CAPS:
      return led_state.caps_lock;
    case LED_TYPE_SCROLL:
      return led_state.scroll_lock;
    case LED_TYPE_NUM:
      return led_state.num_lock;
    default:
      return false;
  }
  ```
- **변경 후**
  ```c
  if (!led_config[led_type].enable) {
    return false;
  }
  return (led_state.raw & indicator_profiles[led_type].host_mask) != 0;
  ```
- **이득**
  - 비트 마스크 비교로 분기 수를 줄이고, 인디케이터 채널이 늘어나도 switch 갱신이 필요 없습니다.
  - `led_type` 경계 검사를 함께 도입해 안전성을 높였습니다.
- **부작용**
  - 없음.
- **적용 여부**
  - 적용 (최종 코드 V251009R2 반영).
