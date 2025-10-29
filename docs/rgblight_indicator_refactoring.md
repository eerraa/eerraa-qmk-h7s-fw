# V251012R8 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: V251012R2~R7에서 정비한 Brick60 RGB 인디케이터 파이프라인.
- 목표: `rgblight_indicator_sync_state()` 호출 경로와 필요성을 보수적으로 재평가하고, 초기화 직후 렌더 타이밍을 확인.

## 초기화 루틴 재검토
- `keyboard_init()` 흐름을 추적하면 `led_init_ports()` → `rgblight_init()` 순으로 실행되며, 두 루틴 모두에서 `rgblight_indicator_sync_state()`가 호출됨을 확인.【F:src/ap/modules/qmk/quantum/keyboard.c†L515-L556】【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/indicator_port.c†L36-L62】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L388-L417】
- `led_init_ports()` 단계에서는 EEPROM에서 복원한 인디케이터 구성을 즉시 `rgblight` 내부 상태에 반영하고, USB 호스트가 미리 전달한 LED 상태(예: Caps Lock ON)가 있다면 `rgblight_indicator_apply_host_led()` 경로를 통해 활성화 플래그와 렌더 요청이 보존됨.
- `rgblight_init()` 완료 시점에는 `is_rgblight_initialized`가 true로 전환되므로, 마지막 `rgblight_indicator_sync_state()` 호출이 보류 중이던 렌더 요청을 즉시 커밋해야만 초기 캡스락 상태가 타이머 틱을 기다리지 않고 복원됨.
- 동기화 호출을 제거하거나 지연시키면, 초기 호스트 LED가 활성화된 경우 첫 렌더가 타이머 인터럽트 1회 이후로 밀릴 수 있어 가시적인 지연이 발생할 여지가 있으므로, 호출 자체는 유지하는 것이 보수적으로 안전하다고 판단.

## 불필요 코드 / 사용 종료된 요소 정리
- `rgblight_indicator_sync_state()`가 내부적으로 `rgblight_indicator_apply_host_led()`를 재호출하면서 동일 상태 비교에 막혀 사실상 동작하지 않던 것을 확인.
- 함수 자체는 초기화 루틴에서 호출되고 있어 제거 시 부작용 위험이 존재하므로, 구현만 정비해 목적에 맞게 동작하도록 수정.

## 성능 및 오버헤드 검토
- 초기화 시점(`rgblight_init()`, `led_init_ports()`)에서 동기화 함수가 효과를 내지 못하면, 캡스락 LED가 이미 켜져 있는 경우 첫 렌더가 타이머 주기를 한 번 기다리는 지연이 발생할 수 있음을 재확인.
- V251012R7에서 조정한 즉시 렌더 요청 경로가 `rgblight_indicator_sync_state()` 마지막 호출 시점(= `rgblight_init()` 종료 직후)에만 실질적으로 실행되므로, 초기화 직전의 중복 호출이나 인터럽트 부하 증가는 발생하지 않음을 검증.
- 기존 early-return 최적화(V251012R5)는 그대로 유지되어 불필요한 인터럽트 발생은 증가하지 않음.

## 제어 흐름 간소화
- 동기화 함수가 실제로 렌더 요청만 재발행하도록 단일 책임으로 축소되어, 초기화 루틴에서의 호출 의도와 구현이 일치.
- `rgblight_indicator_state` 내부 캐시는 유지하여 HSV→RGB 캐싱 효과(R4)와 동일한 흐름을 보존.

## 수정 적용 여부 판단
- 초기화 경로 분석 결과, 동기화 함수 호출은 `rgblight_init()` 종료 시점의 단일 커밋으로 수렴하므로, 기존 수정(V251012R7)을 유지하는 것이 보수적으로 안전함.
- 추가 부작용이 관찰되지 않았으며, 초기화 직후 렌더 지연 제거 효과가 유지됨.

**결론: 기존 V251012R7 수정 유지.**

---

## 이전 기록 (V251012R6)

- 외부에서 더 이상 참조하지 않는 `rgblight_indicator_get_config()` API를 제거하여 상태 캡슐화를 강화.
- 동일 구성/호스트 상태 반복 전달에 대한 early-return 최적화를 유지해 인터럽트 부하를 억제.

---

## 이전 기록 (V251012R4)

- `rgblight_indicator_compute_color()`를 도입하여 HSV→RGB 변환을 구성 변경 시 한 번만 수행하고, 렌더링 시에는 캐시된 `rgb_led_t` 값을 그대로 복사.
- 캐시된 색상을 구조체 복사로 채우면서 `setrgb()` 호출 오버헤드를 제거해 루프 내 함수 호출을 없앰.
- 구성 변경 시 캐시 갱신과 상태 전이를 동시에 처리하도록 `rgblight_indicator_update_config()`를 정리하여 데이터 흐름을 한 곳에서 관리.
- VIA 경로가 직접 업데이트 함수를 호출하도록 변경해 함수 간 의존 관계를 축소.
- HSV→RGB 변환 캐싱과 함수 호출 축소로 렌더링 오버헤드 감소.
- 포트 계층 래퍼 제거로 코드 흐름 간결화 및 유지보수 용이성 향상.
