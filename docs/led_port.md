# BRICK60 LED 포트 최적화 검토 (V251009R6)

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

## 4. 추가 검토 결과
- RGB 합성 경로, 더티 플래그 관리, 호스트 LED 동기화는 이미 최신 개선안(V251009R5) 수준을 유지하고 있으며, 별도의 성능 저하 징후나 리팩토링 필요성이 관찰되지 않았습니다.
- EEPROM 마이그레이션 로직은 `legacy_enable` 변환 외에는 수정이 필요하지 않으며, 추가적인 상태 캐시나 메모리 최적화는 기대 이득이 제한적이라 현행을 유지하기로 결정했습니다.

## 5. 결론
- LED 타입 가드의 일관성 확보를 위한 포인터 기반 접근으로 안정성을 강화했고, 기타 영역은 현행 유지가 적절하다고 판단했습니다.
- 본 변경으로 펌웨어 버전을 `V251009R6`으로 갱신했습니다.
