# V251012R7 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: V251012R2~R6에서 정비한 Brick60 RGB 인디케이터 파이프라인.
- 목표: 초기화 직후 인디케이터 렌더링 지연을 제거하고, 남은 보조 API의 필요성을 보수적으로 재평가.

## 불필요 코드 / 사용 종료된 요소 정리
- `rgblight_indicator_sync_state()`가 내부적으로 `rgblight_indicator_apply_host_led()`를 재호출하면서 동일 상태 비교에 막혀 사실상 동작하지 않던 것을 확인.
- 실제 호출 지점은 `keyboard_init()`의 끝에서 실행되는 `rgblight_init()`이며,【F:src/ap/modules/qmk/quantum/keyboard.c†L548-L577】 초기화가 끝나기 전(`led_init_ports()`)에 수행된 호출은 `is_rgblight_initialized` 가드로 인해 실효성이 없었음.
- 초기화가 완료된 뒤 단 한 번만 동기화가 필요하므로, 포트 계층(`led_init_ports()`)에서의 중복 호출을 제거해 부작용 위험을 줄이고 유지비를 낮춤.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/indicator_port.c†L44-L63】

## 성능 및 오버헤드 검토
- 초기화 시점(`rgblight_init()`, `led_init_ports()`)에서 동기화 함수가 효과를 내지 못해, 캡스락 LED가 이미 켜져 있는 경우 첫 렌더가 타이머 주기를 한 번 기다리는 지연이 발생.
- 동기화 함수에서 전이 여부를 다시 계산하고 `rgblight_indicator_commit_state()`에 즉시 렌더 요청을 전달하도록 수정하여, 초기화 직후 바로 `rgblight_set()`이 실행되도록 함.
- 기존 early-return 최적화(V251012R5)는 그대로 유지되어 불필요한 인터럽트 발생은 증가하지 않음.

## 제어 흐름 간소화
- 동기화 함수가 실제로 렌더 요청만 재발행하도록 단일 책임으로 축소되어, 초기화 루틴(`rgblight_init()`)에서의 호출 의도와 구현이 일치.
- `rgblight_indicator_state` 내부 캐시는 유지하여 HSV→RGB 캐싱 효과(R4)와 동일한 흐름을 보존.

## 수정 적용 여부 판단
- 초기화 직후 렌더 지연이 제거되며, 추가적인 부작용이 발생하지 않는 것을 확인.
- 동기화 함수는 여전히 필요하지만 구현을 정비함으로써 유지보수 비용이 줄어듦.

**결론: 수정 적용.**

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
