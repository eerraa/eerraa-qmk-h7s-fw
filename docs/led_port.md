# BRICK60 LED 포트 최적화 검토 (V251009R9)

## 1. 개요
- 대상 파일: `src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port.c`
- 검토 범위: 인디케이터 설정 로딩 경로, EEPROM 무결성 검사, LED 갱신 흐름의 안정성 및 가드 처리, VIA 커맨드 인터페이스 유효성.
- 기존 버전(V251009R7)은 인디케이터 메타데이터 테이블화, RGB 캐시, 호스트 LED 상태 캐시 등을 도입해 전반적인 성능은 양호하지만, 범위 가드와 커맨드 길이 검증이 부족했습니다.

- `led_config_from_type()` / `indicator_profile_from_type()` 헬퍼를 통한 타입 가드 일관화는 여전히 중요한 안정성 축입니다.
- RGB 합성·캐시 경로는 더티 플래그 기반으로 최적화되어 있으며, 추가적인 성능 문제는 관찰되지 않았습니다.
- VIA 커맨드 처리 루틴이 입력 버퍼 길이를 검증하지 않아, 호스트의 비정상 패킷을 그대로 역참조할 위험이 있었습니다.
- RGB 캐시 조회 함수인 `get_indicator_rgb()`가 LED 타입 범위를 검증하지 않아, 외부에서 잘못된 타입이 전달될 경우 캐시 배열이 손상될 가능성이 있었습니다.

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

### 3-4. `get_indicator_rgb()` LED 타입 범위 가드 추가
- **변경 전**
  ```c
  static RGB get_indicator_rgb(uint8_t led_type, const led_config_t *config)
  {
    RGB rgb = {0, 0, 0};

    if (config == NULL) {
      return rgb;
    }

    if (indicator_rgb_dirty[led_type]) {
      indicator_rgb_cache[led_type] = hsv_to_rgb(config->hsv);
      indicator_rgb_dirty[led_type] = false;
    }

    return indicator_rgb_cache[led_type];
  }
  ```
- **변경 후**
  ```c
  static RGB get_indicator_rgb(uint8_t led_type, const led_config_t *config)
  {
    RGB rgb = {0, 0, 0};

    if (led_type >= LED_TYPE_MAX_CH) {
      return rgb;  // V251009R8 인디케이터 색상 캐시 범위 가드 추가
    }

    if (config == NULL) {
      return rgb;
    }

    if (indicator_rgb_dirty[led_type]) {
      indicator_rgb_cache[led_type] = hsv_to_rgb(config->hsv);
      indicator_rgb_dirty[led_type] = false;
    }

    return indicator_rgb_cache[led_type];
  }
  ```
- **이득**
  - 외부 호출 경로가 LED 타입을 잘못 전달하더라도 색상 캐시 배열에 접근하기 전에 즉시 차단하여 OOB 접근 위험을 제거합니다.
  - 반환값을 0 초기화된 `RGB`로 통일함으로써 후속 로직이 안전하게 동작합니다.
- **부작용 검토**
  - 정상 경로에서는 추가 비교 연산이 1회 늘어나지만, 함수 호출 빈도가 낮아 성능 영향은 무시할 수준입니다.
  - 잘못된 타입이 전달되면 조용히 0 RGB를 반환하는데, 이는 기존 가드 정책과 일관됩니다.
- **적용 여부**
  - 적용. 안전성 강화 효과가 명확하며, 부작용이 없습니다.

### 3-5. `via_qmk_led_command()` 입력 길이 검증 강화
- **변경 전**
  ```c
  void via_qmk_led_command(uint8_t led_type, uint8_t *data, uint8_t length)
  {
    if (led_type >= LED_TYPE_MAX_CH) {
      data[0] = id_unhandled;
      return;
    }

    uint8_t *command_id        = &(data[0]);
    uint8_t *value_id_and_data = &(data[2]);

    ...
  }
  ```
- **변경 후**
  ```c
  void via_qmk_led_command(uint8_t led_type, uint8_t *data, uint8_t length)
  {
    if (data == NULL || length == 0) {
      return;  // V251009R8 VIA 명령 유효성 검사: 데이터 포인터/길이 확인
    }

    if (led_type >= LED_TYPE_MAX_CH || length < 3) {
      data[0] = id_unhandled;
      return;
    }

    uint8_t *command_id        = &(data[0]);
    uint8_t *value_id_and_data = &(data[2]);

    ...
  }
  ```
- **이득**
  - VIA에서 비정상적인 짧은 패킷이 도착해도 즉시 차단하여 OOB 접근을 방지합니다.
  - 데이터 포인터 자체가 `NULL`로 전달된 예외 상황도 조용히 무시하여 펌웨어 안정성을 높입니다.
- **부작용 검토**
  - `length == 0`인 경우 응답 코드를 쓸 수 없지만, 호출자에게 전달할 버퍼가 없는 상황에서 안전하게 탈출하는 편이 합리적입니다.
  - 정상 패킷 경로에서는 조건 비교가 두 번 추가되지만, 명령 처리 빈도가 낮아 체감 영향은 없습니다.
- **적용 여부**
  - 적용. 안정성 확보를 위해 필수적인 가드입니다.

### 3-6. RGBlight 합성 호출 최소화 재검토
- **제안 내용**
  - `refresh_indicator_display()` 호출 횟수를 줄이기 위해, `via_qmk_led_set_value()`에서 설정값이 변경되더라도 더티 플래그만 설정하고 합성은 타이머 루틴에서 일괄 처리하는 방안을 재검토했습니다.
- **이득 예상**
  - 명령 폭주 상황에서 RGBlight 호출을 묶어 CPU 사용량을 더 낮출 가능성이 있습니다.
- **부작용 검토**
  - 이벤트 처리 지연으로 VIA UI 피드백이 늦어질 수 있으며, 8kHz 스케줄에 추가적인 주기 작업을 삽입해야 합니다.
- **적용 여부**
  - 미적용. 즉시 갱신 경로가 안정적이고, 지연을 도입할 필요성을 확인하지 못했습니다.

## 4. 추가 검토 결과
- RGB 합성 경로, 더티 플래그 관리, 호스트 LED 동기화는 이미 최신 개선안(V251009R5) 수준을 유지하고 있으며, 별도의 성능 저하 징후나 리팩토링 필요성이 관찰되지 않았습니다.
- EEPROM 마이그레이션 로직은 `legacy_enable` 변환 외에는 수정이 필요하지 않으며, 추가적인 상태 캐시나 메모리 최적화는 기대 이득이 제한적이라 현행을 유지하기로 결정했습니다.

### 3-7. VIA 페이로드 길이 기반 응답/설정 가드 강화
- **변경 전**
  ```c
  static void via_qmk_led_get_value(uint8_t led_type, uint8_t *data)
  {
    led_config_t *config = led_config_from_type(led_type);
    ...
    switch (*value_id) {
      case id_qmk_led_enable:
        value_data[0] = config->enable;
        break;
      case id_qmk_led_brightness:
        value_data[0] = config->hsv.v;
        break;
      case id_qmk_led_color:
        value_data[0] = config->hsv.h;
        value_data[1] = config->hsv.s;
        break;
    }
  }

  static void via_qmk_led_set_value(uint8_t led_type, uint8_t *data)
  {
    led_config_t *config = led_config_from_type(led_type);
    ...
    case id_qmk_led_color: {
      uint8_t hue        = value_data[0];
      uint8_t saturation = value_data[1];
      ...
    }
  }
  ```
- **변경 후**
  ```c
  static bool via_qmk_led_get_value(uint8_t led_type, uint8_t *data, uint8_t length)
  {
    if (data == NULL || length == 0) {
      return false;  // V251009R9 VIA 응답 버퍼 가용성 검증
    }
    ...
    case id_qmk_led_color:
      if (value_length < 2) {
        return false;  // V251009R9 VIA 응답 길이 부족 시 실패 처리
      }
      value_data[0] = config->hsv.h;
      value_data[1] = config->hsv.s;
      return true;
    default:
      break;
    }
    return false;
  }

  static bool via_qmk_led_set_value(uint8_t led_type, uint8_t *data, uint8_t length)
  {
    if (data == NULL || length == 0) {
      return false;  // V251009R9 VIA 설정 페이로드 가용성 검증
    }
    ...
    case id_qmk_led_color: {
      if (value_length < 2) {
        return false;  // V251009R9 VIA 설정 길이 부족 시 실패 처리
      }
      uint8_t hue        = value_data[0];
      uint8_t saturation = value_data[1];
      ...
    }
    default:
      return false;  // V251009R9 알 수 없는 VIA 서브커맨드 거부
    }
    ...
    return true;
  }
  ```
- **이득**
  - 응답/설정 서브커맨드마다 필요한 페이로드 길이를 검증하여, 짧은 VIA 패킷으로 인한 읽기/쓰기 범위를 사전에 차단합니다.
  - 처리 성공 여부를 호출자에게 반환해, 상위 `via_qmk_led_command()`가 즉시 `id_unhandled`를 통지할 수 있도록 했습니다.
- **부작용 검토**
  - 성공 여부를 반환하도록 시그니처가 변경되었으나, 정적 함수라 외부 영향은 없습니다.
  - 길이 검증이 추가되어 비교 연산이 늘었지만, 커맨드 빈도가 낮아 체감 성능 영향이 없습니다.
- **적용 여부**
  - 적용. 안전성 향상 효과가 확실하며, 정상 시나리오에는 영향이 없습니다.

### 3-8. `id_custom_save` 단축 패킷 허용 여부 재검토
- **제안 내용**
  - `via_qmk_led_command()`의 길이 가드가 모든 커맨드에 대해 `length >= 3`을 요구하므로, `id_custom_save`처럼 추가 데이터가 필요 없는 명령에는 다소 과한 조건이라는 의견을 재검토했습니다.
- **이득 예상**
  - 최소 길이를 2바이트로 완화하면, 호스트 구현이 2바이트 패킷만 보내더라도 호환성이 확보됩니다.
- **부작용 검토**
  - 짧은 패킷 허용 시 `value_id_and_data` 포인터 계산을 분기 처리해야 하며, 커맨드별로 상이한 길이 정책이 생겨 코드 복잡도가 증가합니다.
  - 현재 호스트 구현(VIA 기반)은 3바이트 이상을 전송하고 있어 실익이 확인되지 않았습니다.
- **적용 여부**
  - 미적용. 일관된 길이 규칙을 유지해 코드 단순성과 검증 용이성을 우선했습니다.

## 5. 결론
- LED 타입 가드 일관화를 유지하면서, 인디케이터 합성 루프에서 포인터 재사용을 지속 적용하고 RGB 캐시 접근 범위를 추가로 보호했습니다.
- VIA 명령 길이 검증을 페이로드 단위까지 확장하여, 비정상 패킷이 전달되더라도 OOB 접근 가능성을 원천적으로 제거했습니다.
- `id_custom_save` 패킷 길이 완화는 실익보다 복잡도 증가 요인이 커 현행을 유지하기로 결정했습니다.
- 본 변경으로 펌웨어 버전을 `V251009R9`으로 갱신했습니다.
