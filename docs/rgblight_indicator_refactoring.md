# V251014R3 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: V251012R2~V251014R1에서 정비한 Brick60 RGB 인디케이터 파이프라인.
- 목표: 클리핑 범위보다 넓은 효과 범위가 반복 전달되는 Split/VIA 시나리오에서, 실제로 전송되지 않는 LED 구간까지 버퍼를 채우는 잔여 연산을 제거할 수 있는지 보수적으로 판단하고, 교집합 외 구간의 소등이 지연되는 회귀 가능성을 추가 점검.

## 시나리오별 점검
1. **정적 인디케이터 + 교차 범위 Split 출력**: Split 좌/우 반쪽이 서로 다른 `rgblight_set_clipping_range()`를 유지하고, 효과 범위는 전체 LED 배열을 계속 전달하는 상황을 구성했다. 교차 범위를 계산해 실제 클리핑과 겹치는 구간만 채우도록 조정해, 슬레이브 측 DMA 준비에서 불필요한 버퍼 쓰기가 제거됨을 확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L233-L318】
2. **정적 인디케이터 + 클리핑 범위 축소**: 애니메이션이 꺼진 상태에서 클리핑 길이만 줄어드는 시나리오를 반복해, 효과 범위가 더 넓었던 이전 프레임의 잔류 색상이 남지 않는지 확인했다. 교집합 밖 구간을 초기화하도록 후단/전단 정리를 추가해 리그레션을 방지했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L233-L318】
3. **동적 효과 + 범위 변경 감시**: V251014R1에서 추가된 동일 값 무시 로직이 새 교차 범위 계산과 충돌하지 않는지 확인했다. `needs_render`가 켜졌을 때만 교차 연산이 실행되므로, 주기적 호출에서도 불필요한 재렌더가 발생하지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L233-L318】
4. **인디케이터 비활성 + 경계 밖 정리 요청**: 경계 보정 로직이 유지된 상태에서 교차 범위가 0이 되면 채움 루프가 실행되지 않아, 잘못된 입력으로 인해 버퍼가 다시 채워지는 상황이 없다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L148-L170】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L233-L276】
5. **Split 재동기화 + VIA 사용자 매크로**: 초과 범위 입력이나 빈 범위 전송이 들어와도 교차 범위가 0으로 계산되어 버퍼 채움이 스킵되므로, 기존 조기 반환 로직과 함께 안전하게 무시된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L366-L410】
6. **인디케이터 비활성 + 빈 효과 범위 동기화**: 효과 범위가 0이면 `should_fill`이 꺼지고 교차 범위 계산도 실행되지 않아, V251014R1에서 확보한 초기화 경로만 유지된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L233-L276】
7. **인디케이터 활성 + 클리핑 0 구간 유지**: 클리핑 길이가 0이면 교차 범위도 0이 되어 채움 루프가 실행되지 않는다. 기존 0 구간 최적화와 함께 동작해 재렌더 없이 상태가 유지된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L233-L276】
8. **정적 인디케이터 + 레이어/타이머 호출 혼재**: `rgblight_timer_task()`가 인디케이터 활성 상태에서 반복 호출될 때 교차 범위 연산이 추가 부하를 만들지 검증했다. `needs_render`가 내려가면 연산 자체가 실행되지 않아 타이머 경로 오버헤드가 증가하지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L233-L318】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1321-L1331】

## 불필요 코드 / 사용 종료된 요소 정리
- `rgblight_indicator_prepare_buffer()`가 클리핑 범위와 효과 범위의 교집합만 채우도록 조정되어, Split 반대편 구간까지 중복 복사하던 잔여 연산을 제거했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L280-L309】
- 교집합 밖으로 남는 효과 범위를 즉시 초기화해, 애니메이션이 꺼진 상태에서도 과거 프레임의 색상이 잔류하지 않도록 정리했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L300-L318】
- `rgblight_set_clipping_range()`와 `rgblight_set_effect_range()`에서 동일 값 반복 시 조기 반환을 유지해, 불필요한 재렌더 예약을 계속 억제한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L366-L410】
- `rgblight_set_effect_range()`가 전체 LED 개수와 동일한 시작 인덱스를 허용해 빈 범위를 정상 처리하므로, 이전 범위를 즉시 제거한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L390-L410】
- 빈 효과 범위가 유지되는 동안 애니메이션 루프가 0으로 나누는 연산을 수행하지 않도록, 타이머 태스크가 즉시 반환한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1321-L1331】
- `rgblight_set_clipping_range()`가 배열 경계를 벗어나는 요청을 무시해, 실제로 사용할 수 없는 범위를 적용하지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L366-L388】
- `rgblight_indicator_prepare_buffer()`가 `clip_count == 0`인 경우에는 초기화 경로만 실행해 사용하지 않는 색상 복사를 방지한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L233-L276】

## 성능 및 오버헤드 검토
- 교차 범위만 채우도록 변경해 Split 슬레이브가 보유하지 않은 LED에 대한 중복 쓰기가 사라져, DMA 준비 단계의 메모리 접근이 감소한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L280-L309】
- 교집합 밖 구간을 즉시 0으로 정리해, 클리핑 범위 축소 시에도 추가 프레임을 기다리지 않고 잔여 LED를 소등한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L300-L318】
- 범위가 변하지 않은 호출을 무시해 `needs_render`가 불필요하게 켜지지 않아, 인디케이터 활성 상태에서도 타이머 루프가 idle 상태를 유지한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L366-L410】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1238-L1316】
- `rgblight_indicator_clear_range()`에 경계 보정을 유지해, 이상 범위 입력이 들어와도 DMA 전송 전에 버퍼를 초과로 지우지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L148-L170】
- 클리핑 범위가 유효한 경우에만 구조체를 갱신해, 잘못된 입력으로 인한 오버런과 불필요한 재렌더 요청을 동시에 차단한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L366-L388】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1158-L1196】
- 빈 효과 범위를 정상 처리하면서 이전 범위를 즉시 무효화해, 인디케이터 비활성 시에도 범위 갱신 루프가 과거 구간을 재검사하지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L390-L410】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1158-L1204】
- 실 출력 구간이 없을 때에는 색상 복사를 건너뛰어, 타이머가 동일 프레임을 반복 계산하지 않고 곧바로 idle 상태로 복귀한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L233-L276】

## 제어 흐름 간소화
- 교집합 계산으로 실제 출력 구간만 복사하므로, 기존 인터페이스를 유지한 채 중복 연산을 제거했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L280-L309】
- 범위 값이 바뀐 경우에만 상태 플래그를 조정해 외부 분기가 단순해졌다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L366-L410】
- 경계 보정 로직을 헬퍼에 캡슐화해 호출부는 동일 인터페이스로 안전성을 확보한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L148-L170】
- 잘못된 클리핑 범위는 조기 반환으로 정리되어, 이후 흐름에서 별도 보정 코드가 필요 없다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L366-L388】
- 빈 효과 범위도 동일하게 조기 반환 조건을 통과하므로, 호출부가 `start_pos`를 조정하는 별도 보정 코드가 필요 없다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L390-L410】
- 실 LED가 존재하지 않는 경우에는 조기에 `should_fill`을 false로 만들어 초기화 분기만 수행해, 반복문 호출을 피한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L233-L276】

## 수정 적용 여부 판단
- 교차 범위만 채우도록 변경해도 외부 API의 입력/출력 계약은 유지되며, DMA로 실제 전송되는 영역만 갱신되므로 보수적 변경으로 판단된다.
- 동일 범위 반복 호출을 무시해도 기존 외부 API의 계약은 유지되며, 인디케이터 활성 시 불필요한 `rgblight_set()` 호출만 줄어든다.
- 경계 보정은 잘못된 입력에 대한 방어 로직으로, 정상 시나리오에는 영향을 주지 않으므로 보수적 변경으로 인정 가능.
- 클리핑 범위 유효성 검사는 기존 동작(정상 범위 수용)을 유지하면서 잠재적 오버런만 막는 소극적 방어로 판단된다.
- 빈 효과 범위를 허용하는 조건 조정은 기존 API와 호환되면서도 이전 범위를 즉시 초기화해 리그레션 위험이 없어, 보수적 변경으로 수용 가능.
- `clip_count`가 0인 동안에는 실질적 출력이 없으므로 버퍼를 유지할 필요가 없고, 기존 캐/렌더링 흐름에도 변화를 주지 않아 보수적 변경 범위에 포함된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L233-L276】

**결론: 수정 적용.**
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
