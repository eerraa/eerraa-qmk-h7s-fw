# V251013R10 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: V251012R2~V251013R8에서 정비한 Brick60 RGB 인디케이터 파이프라인.
- 목표: 동일 범위 반복 갱신으로 인한 재렌더 트리거를 다시 확인하고, 분리 키보드/사용자 매크로가 잘못된 클리핑 범위를 요구하는 경우를 방어할 수 있는지 검토하여 보수적으로 개선 여부 판단.

## 시나리오별 점검
1. **정적 인디케이터 + Split 초기화**: 좌/우 반쪽이 `rgblight_set_clipping_range()`를 각각 호출하며, 이미 동일 값이 적용된 상태에서 다시 호출될 수 있다. 이때 `needs_render`가 다시 켜져 `rgblight_set()`가 불필요하게 깨워지므로 중복 호출을 억제한다.
2. **동적 효과 + 범위 변경 감시**: 레이어 또는 사용자 커맨드가 `rgblight_set_effect_range()`를 주기적으로 호출하지만 범위가 변하지 않는 경우가 있다. 현재 구조는 매 호출마다 재렌더 플래그가 세팅되어 애니메이션 루프가 인디케이터 활성 상태에서도 계속 인터럽트를 유발하므로, 동일 값은 무시한다.
3. **인디케이터 비활성 + 경계 밖 정리 요청**: 포트 계층이 잘못된 범위를 요청하거나 효과 범위가 0인 상태에서 클리핑 범위만 크게 잡히면 `rgblight_indicator_clear_range()`가 배열 경계를 넘어설 수 있어, 헬퍼가 범위를 보정하도록 방어 로직을 추가한다.
4. **Split 재동기화 + VIA 사용자 매크로**: 슬레이브 보드가 재연결되면서 기본 펌웨어는 0개의 LED를 갖는 쪽에 `rgblight_set_clipping_range(total_leds, 0)`을 투입하고, 동시에 VIA 매크로가 이전 상태를 그대로 재적용하면 `start_pos + num_leds`가 전체 개수를 초과한 값이 전달될 수 있다. 기존 코드에서는 클리핑 범위를 즉시 갱신해 포인터가 배열 밖을 가리킬 수 있으므로, 범위가 유효하지 않으면 무시해 기존 안전한 상태를 유지하도록 검토한다.
5. **인디케이터 비활성 + 빈 효과 범위 동기화**: 호스트에서 인디케이터를 비활성화할 때 `rgblight_set_effect_range(RGBLIGHT_LED_COUNT, 0)`과 같이 전체 개수와 동일한 시작 위치로 빈 범위를 지시하는 시나리오가 반복된다. 기존 구조는 `start_pos >= count` 조건 때문에 요청을 무시하여 이전 효과 범위가 남아 있었으므로, 빈 범위를 허용해 즉시 안전하게 초기 상태로 복귀시키는 방안을 검토한다.
6. **애니메이션 모드 + 빈 효과 범위 유지**: 빈 효과 범위를 허용한 이후 애니메이션 모드가 그대로 실행되면 내부 계산(`% rgblight_ranges.effect_num_leds`)에서 0으로 나누는 예외가 발생할 수 있다. 빈 범위가 적용된 동안에는 타이머 루프에서 애니메이션 함수 호출을 건너뛰어 하드폴트를 예방해야 한다.

## 불필요 코드 / 사용 종료된 요소 정리
- `rgblight_set_clipping_range()`와 `rgblight_set_effect_range()`에서 동일 값 반복 시 조기 반환을 추가해, 이미 제거된 래퍼 대신 남아 있던 중복 동작을 정리했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L139-L156】
- `rgblight_set_effect_range()`가 전체 LED 개수와 동일한 시작 인덱스를 허용해 빈 범위로 초기화하는 경로를 활성화하여, 더 이상 사용할 수 없는 이전 범위를 즉시 제거한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L381-L389】
- 빈 효과 범위가 유지되는 동안 애니메이션 루프가 0으로 나누는 연산을 실행하지 않도록, 타이머 태스크가 즉시 반환해 안전하게 무시한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1312-L1316】
- `rgblight_set_clipping_range()`가 배열 경계를 벗어나는 요청을 무시하도록 조기 반환을 추가해, 실제로 사용할 수 없는 범위 설정을 더 이상 적용하지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L356-L367】

## 성능 및 오버헤드 검토
- 범위가 변하지 않은 호출을 무시함으로써 `needs_render`가 불필요하게 켜지는 빈도를 제거해, 타이머 루프가 인디케이터 활성 상태에서도 idle 상태를 유지한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L139-L156】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1230-L1267】
- `rgblight_indicator_clear_range()`에 경계 보정을 도입해, 이상 범위 입력으로 인해 DMA 전송 전에 버퍼를 초과로 지우는 상황을 차단한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L118-L133】
- 클리핑 범위가 유효한 경우에만 구조체를 갱신하므로, 잘못된 입력으로 인해 `rgblight_set()` 호출 시 포인터 산출이 배열 끝을 넘어가면서 발생할 수 있는 오버런과 그에 따른 불필요한 재렌더 요청을 동시에 차단한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L356-L375】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1166-L1204】
- 빈 효과 범위를 정상 처리하면서 이전 범위를 즉시 무효화해, 인디케이터 비활성 시에도 범위 갱신 루프가 과거 구간을 재검사하지 않아 인터럽트/타이머 경로 부담을 줄인다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L381-L389】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1158-L1204】
- 빈 효과 범위 적용 시 애니메이션을 조기 종료하도록 하여, 인터럽트 컨텍스트에서 modulo 연산이 0으로 나누기를 일으키는 것을 방지한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1312-L1316】

## 제어 흐름 간소화
- 범위 값이 바뀐 경우에만 상태 플래그를 조정해, 호출부 로직을 변경하지 않고도 상태 머신 외부 분기가 단순해졌다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L139-L156】
- 경계 보정 로직을 헬퍼에 캡슐화해, 호출부는 기존 인터페이스를 그대로 유지하면서 안전성만 향상된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L118-L133】
- 잘못된 클리핑 범위는 조기 반환으로 정리되어, 이후 흐름에서 조건 분기나 별도의 보정 코드가 필요하지 않다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L356-L367】
- 빈 효과 범위도 동일하게 조기 반환 조건을 통과하므로, 호출부가 `start_pos`를 안전 구간으로 조정하는 별도 보정 코드를 둘 필요가 없다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L381-L389】

## 수정 적용 여부 판단
- 동일 범위 반복 호출을 무시해도 기존 외부 API의 계약은 유지되며, 인디케이터 활성 시 불필요한 `rgblight_set()` 호출만 줄어든다.
- 경계 보정은 잘못된 입력에 대한 방어 로직으로, 정상 시나리오에는 영향을 주지 않으므로 보수적 변경으로 인정 가능.
- 클리핑 범위 유효성 검사는 기존 동작(정상 범위 수용)을 유지하면서, 잠재적 오버런만 막는 소극적 방어로 판단된다.
- 빈 효과 범위를 허용하는 조건 조정은 기존 API와 호환되면서도 이전 범위를 즉시 초기화해 리그레션 위험이 없어, 보수적 변경으로 수용 가능.

**결론: 수정 적용.**

---

## 이전 기록 (V251013R6)

- 인디케이터가 활성화된 상태에서 추가 렌더 요청이 없으면 `rgblight_indicator_prepare_buffer()`가 즉시 반환하도록 early-return을 추가해, 타이머가 주기적으로 `rgblight_set()`를 깨우더라도 메모리 초기화 및 색상 복사를 반복하지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L206-L271】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1238-L1263】
- 클리핑/효과 범위가 변경되면 인디케이터 버퍼를 다시 적용하도록 `rgblight_set_clipping_range()`와 `rgblight_set_effect_range()`가 재렌더 플래그를 세팅해, 위 early-return이 활성화돼도 새로운 범위가 즉시 반영된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L260-L272】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1238-L1263】

---

## 이전 기록 (V251013R5)

- `rgblight_indicator_prepare_buffer()` 내부에 초기화 전용 헬퍼(`rgblight_indicator_clear_range()`)를 두어, 0 길이 `memset()` 호출과 동일 포인터 계산을 반복하지 않도록 정리했다. 클리핑/효과 경계 계산은 그대로 재활용해 안전하게 필요한 범위만 초기화한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L206-L271】
- VIA에서 동일 구성을 반복 전달하는 경우 `indicator_via_set_value()`가 `rgblight_indicator_update_config()` 호출을 건너뛰도록 가드해, 인터럽트 컨텍스트에서 불필요한 상태 비교와 함수 호출이 다시 발생하지 않도록 했다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/indicator_port.c†L58-L104】
- 기존 색상 캐시 및 애니메이션 우회 조건은 유지되어 렌더링 요청 수나 인터럽트 경로에는 변화 없음.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L206-L271】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1078-L1224】

---

## 이전 기록 (V251013R3)

- `rgblight_indicator_prepare_buffer()`가 효과 범위와 클리핑 범위의 포함 관계를 계산해, 실제로 남는 전단/후단 구간만 `memset()`으로 초기화하도록 조정. 소등 시에는 전체 클리핑 범위를 한 번만 정리하고, 부분 점등 시에는 잔여 영역만 정리해 버퍼 재작성 횟수를 줄임.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L205-L244】
- 클리핑 범위가 효과 범위를 완전히 덮는 경우에는 추가 초기화를 건너뛰어 불필요한 메모리 접근을 제거. 기존 캐시 구조와 애니메이션 우회 조건은 그대로 유지되어 타이머 경로에 영향 없음.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L205-L244】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1078-L1224】

---

## 이전 기록 (V251013R1)

- 인디케이터가 클리핑 범위를 전부 덮으면서 점등하는 경우, 초기화가 중복되어 버려지는 것을 방지하도록 `rgblight_indicator_prepare_buffer()`가 조건적으로 `memset()`을 수행하게 조정함. 부분 점등 또는 소등(밝기 0) 시에는 기존과 동일하게 클리핑 범위를 초기화해 안전성을 유지.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L202-L225】
- 인디케이터 색상 캐시와 전환 플래그 구조는 유지되어, 타이머 우회 및 인터럽트 부하 최적화에 변화 없음.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L200-L239】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1065-L1214】
- 버퍼 초기화 조건만 조정해 상태 머신 흐름은 변경하지 않았으며, 기존 전이 조건 및 즉시 렌더링 보장은 그대로 유지.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L202-L239】

---

## 이전 기록 (V251012R9)

- `rgblight_indicator_prepare_buffer()`가 항상 전체 LED 배열을 `memset()`하던 경로를, 실제 전송 범위(`clipping_start_pos`, `clipping_num_leds`)에 한정해 초기화하도록 조정해 필요 없는 메모리 클리어를 제거.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L202-L225】
- 인디케이터 색상 캐시와 전환 플래그 구조는 유지되어, 타이머 우회 및 인터럽트 부하 최적화에 변화 없음.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L200-L239】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1065-L1214】
- 버퍼 초기화 범위만 조정하고 상태 머신 흐름은 변경하지 않아, 기존 전이 조건 및 즉시 렌더링 보장은 그대로 유지.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L202-L239】

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
