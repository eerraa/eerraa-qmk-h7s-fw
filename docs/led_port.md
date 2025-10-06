# BRICK60 LED 포트 최적화 검토 (V251009R7)

## 1. 개요
- 대상 파일: `src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port.c`
- 검토 범위: 인디케이터 설정 로딩 경로, EEPROM 무결성 검사, LED 갱신 흐름의 안정성 및 가드 처리.
- 기존 버전(V251009R5)은 인디케이터 메타데이터 테이블화, RGB 캐시, 호스트 LED 상태 캐시 등을 도입해 전반적인 성능은 양호하지만, 타입 가드 처리의 일관성이 다소 미흡했습니다.

## 2. 주요 관찰 사항
- `led_config_from_type()` / `indicator_profile_from_type()` 헬퍼가 이미 존재하지만, 초기화 루틴과 EEPROM 검증 루틴 일부가 여전히 전역 배열을 직접 참조하고 있어 향후 LED 타입이 확장될 때 방어 코드가 누락될 가능성이 있었습니다.
- EEPROM 데이터가 손상되었을 때 `indicator_config_valid()`가 곧바로 `led_config[led_type]`에 접근하여 범위를 벗어날 여지가 있었습니다. 현재 호출부에서는 안전하지만, 유지보수 측면에서 헬퍼 사용이 일관되지 않은 점이 눈에 띄었습니다.
- RGB 합성·캐시 경로는 이미 더티 플래그 기반으로 최적화되어 있으며, 추가적인 성능 문제는 관찰되지 않았습니다.

## 3. 개선 제안 및 평가

### 3-1. LED 타입 가드 일관화
- **변경 전**
  ```c
  for (uint8_t i = 0; i < LED_TYPE_MAX_CH; i++) {
    bool needs_migration = false;
    if (!indicator_config_valid(i, &needs_migration)) {
      led_config[i] = indicator_profiles[i].default_config;
      flush_indicator_config(i);
      continue;
    }
  }

  static bool indicator_config_valid(uint8_t led_type, bool *needs_migration)
  {
    uint32_t raw = led_config[led_type].raw;
    ...
    if (legacy_enable <= 1) {
      led_config[led_type].enable = legacy_enable;
      ...
    }
  }
  ```
- **변경 후**
  ```c
  for (uint8_t i = 0; i < LED_TYPE_MAX_CH; i++) {
    bool needs_migration = false;
    led_config_t *config = led_config_from_type(i);
    const indicator_profile_t *profile = indicator_profile_from_type(i);

    if (config == NULL || profile == NULL) {
      continue;  // V251009R6 LED 타입 가드 일관화
    }

    if (!indicator_config_valid(i, &needs_migration)) {
      *config = profile->default_config;
      ...
    }
  }

  static bool indicator_config_valid(uint8_t led_type, bool *needs_migration)
  {
    led_config_t *config = led_config_from_type(led_type);
    if (config == NULL) {
      return false;  // V251009R6 LED 타입 가드 일관화
    }
    ...
    config->enable = legacy_enable;
  }
  ```
- **이득**
  - 초기화 루틴과 EEPROM 검증 루틴이 동일한 타입 가드를 공유하여, LED 타입 확장 시에도 방어 로직을 중복 작성할 필요가 없습니다.
  - 손상된 EEPROM 데이터를 처리할 때 발생할 수 있는 인덱스 접근 실수를 사전에 차단합니다.
- **부작용 검토**
  - 헬퍼를 통해 포인터를 획득하므로, 기존과 동일하게 `LED_TYPE_MAX_CH` 범위 내에서는 동작이 동일합니다.
  - 타입이 잘못 전달되면 조용히 무시하게 되는데, 이는 기존에도 유사하게 `led_type >= LED_TYPE_MAX_CH` 체크를 수행하므로 동작 일관성이 유지됩니다.
- **적용 여부**
  - 적용 (최종 코드 V251009R6 반영).

### 3-2. 인디케이터 루프에서 포인터 재사용 및 범위 캐시 활용도 개선
- **변경 전**
  ```c
  for (uint8_t i = 0; i < LED_TYPE_MAX_CH; i++) {
    const indicator_profile_t *profile = indicator_profile_from_type(i);
    if (!should_light_indicator(i, profile, led_state)) {
      continue;
    }

    RGB rgb = get_indicator_rgb(i);
    uint8_t start = profile->start;
    uint8_t limit = profile->end;

    for (uint8_t led_index = start; led_index < limit; led_index++) {
      rgblight_set_color_buffer_at(led_index, rgb.r, rgb.g, rgb.b);
    }
  }
  ```
- **변경 후**
  ```c
  for (uint8_t i = 0; i < LED_TYPE_MAX_CH; i++) {
    led_config_t *config = led_config_from_type(i);
    const indicator_profile_t *profile = indicator_profile_from_type(i);
    if (config == NULL || profile == NULL) {
      continue;  // V251009R7 LED 타입별 포인터 재사용 가드
    }

    if (!should_light_indicator(config, profile, led_state)) {
      continue;  // V251009R7 헬퍼에 구성 포인터 직접 전달
    }

    RGB rgb = get_indicator_rgb(i, config);
    uint8_t start = profile->start;
    uint8_t limit = profile->end;

    for (uint8_t led_index = start; led_index < limit; led_index++) {
      rgblight_set_color_buffer_at(led_index, rgb.r, rgb.g, rgb.b);
    }
  }
  ```
- **이득**
  - `rgblight_indicators_kb()` 내부에서 동일한 LED 타입에 대해 `led_config_from_type()`를 세 차례 호출하던 중복을 제거하여, EEPROM 구성 캐시 조회가 더 명확해졌습니다.
  - `should_light_indicator()`와 `get_indicator_rgb()`가 이미 확보한 포인터를 재사용하면서 향후 LED 타입 확장 시에도 가드 누락 위험이 줄어듭니다.
- **부작용 검토**
  - 포인터가 `NULL`인 경우를 즉시 건너뛰도록 방어 로직을 추가했으며, 이는 기존에도 헬퍼 내부에서 수행하던 체크이므로 동작 동일성이 유지됩니다.
  - `get_indicator_rgb()` 시그니처가 변경되어 호출부에 `config` 포인터가 필요하지만, 호출 지점이 제한적이라 영향 범위가 명확합니다.
- **적용 여부**
  - 적용 (최종 코드 V251009R7 반영).

### 3-3. 인디케이터 즉시 재합성 지연 검토
- **변경 전 제안**
  - `via_qmk_led_set_value()`에서 설정값이 바뀔 때마다 `refresh_indicator_display()`를 즉시 호출하여 RGBlight 합성을 트리거합니다.
- **변경 후 제안**
  - 다수의 설정 변경이 연속으로 발생할 가능성을 고려하여, 플래그만 설정하고 합성은 주기적인 루프에서 일괄 수행하는 방식을 검토했습니다.
- **이득 예상**
  - 설정 변경 명령이 연속으로 들어올 때 RGBlight 업데이트 호출 횟수를 줄여 CPU 사용률을 더 낮출 수 있습니다.
- **부작용 검토**
  - 설정 변경 직후 인디케이터 응답이 지연될 수 있으며, VIA UI와의 상호작용에서 사용자 경험이 저하될 가능성이 있습니다.
  - 현재 펌웨어는 8kHz USB 폴링을 유지해야 하므로, 이벤트를 지연시키는 별도 루프를 추가하면 타이밍 분석 비용이 증가합니다.
- **적용 여부**
  - 미적용. 즉시 갱신 경로가 안정적으로 동작하고, 지연으로 얻는 이득보다 사용자 피드백 지연 위험이 더 크다고 판단했습니다.

## 4. 추가 검토 결과
- RGB 합성 경로, 더티 플래그 관리, 호스트 LED 동기화는 이미 최신 개선안(V251009R5) 수준을 유지하고 있으며, 별도의 성능 저하 징후나 리팩토링 필요성이 관찰되지 않았습니다.
- EEPROM 마이그레이션 로직은 `legacy_enable` 변환 외에는 수정이 필요하지 않으며, 추가적인 상태 캐시나 메모리 최적화는 기대 이득이 제한적이라 현행을 유지하기로 결정했습니다.

## 5. 결론
- LED 타입 가드 일관화를 유지하면서, 인디케이터 합성 루프에서 포인터를 재사용하도록 리팩토링하여 가드 로직과 캐시 활용을 더욱 명확히 했습니다.
- 즉시 재합성 지연 전략은 부작용 우려로 도입하지 않았으며, 현재 설계가 요구 조건에 부합한다고 결론내렸습니다.
- 본 변경으로 펌웨어 버전을 `V251009R7`으로 갱신했습니다.
