# V251012R9 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: V251012R2~R8에서 정비한 Brick60 RGB 인디케이터 파이프라인.
- 목표: 인디케이터 우선 렌더링 시 LED 버퍼 초기화 오버헤드를 추가로 점검.

## 불필요 코드 / 사용 종료된 요소 정리
- 추가로 제거할 공개 API나 래퍼는 확인되지 않음.

## 성능 및 오버헤드 검토
- `rgblight_indicator_prepare_buffer()`가 항상 전체 LED 배열을 `memset()`하던 경로를, 실제 전송 범위(`clipping_start_pos`, `clipping_num_leds`)에 한정해 초기화하도록 조정해 필요 없는 메모리 클리어를 제거.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L199-L216】
- 인디케이터 색상 캐시와 전환 플래그 구조는 유지되어, 타이머 우회 및 인터럽트 부하 최적화에 변화 없음.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L200-L239】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1065-L1214】

## 제어 흐름 간소화
- 버퍼 초기화 범위만 조정하고 상태 머신 흐름은 변경하지 않아, 기존 전이 조건 및 즉시 렌더링 보장은 그대로 유지.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L200-L239】

## 수정 적용 여부 판단
- 동일한 렌더링 결과를 유지하면서도 메모리 초기화 범위를 줄일 수 있어, 보수적 관점에서도 이득이 확실함.

**결론: 수정 적용.**

---

## 이전 기록 (V251012R8)

- `rgblight_indicator_sync_state()`가 `rgblight_init()` 내부에서만 사용되고 외부 호출자가 존재하지 않아, 공개 API로 유지할 이유가 없음을 확인.
- 초기화 단계에서 `rgblight_indicator_commit_state()`를 직접 호출하도록 변경하여, 헤더 선언과 간접 호출을 함께 제거.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L403-L410】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.h†L192-L197】
- 래퍼 제거로 초기화 시 함수 호출이 한 번 줄어들어, 마이크로컨트롤러 초기화 구간의 오버헤드를 더 이상 발생시키지 않음.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L403-L410】
- `rgblight_indicator_commit_state()`는 기존과 동일하게 상태 캐시와 early-return 최적화를 사용하므로, 인터럽트 및 타이머 경로의 부하 변화 없음.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L200-L239】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1065-L1214】

## 이전 기록 (V251012R7)

- 초기화 직후 인디케이터 렌더링 지연을 제거하고, 포트 계층에서의 중복 호출을 정리.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/indicator_port.c†L44-L63】【F:src/ap/modules/qmk/quantum/keyboard.c†L548-L577】
- 상태 전이와 렌더 요청을 분리해 early-return 최적화를 유지하면서도 즉시 렌더를 보장.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L200-L239】

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
