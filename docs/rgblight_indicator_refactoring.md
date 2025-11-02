# V251016R5 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: `rgblight_indicator_prepare_buffer()`의 클리핑/효과 범위가 겹치지 않는 경로와 `rgblight_indicator_clear_range()` 조합.
- 목표: 실제 구동 시나리오에서 교집합이 없을 때 포화 감산 헬퍼 없이도 전체 효과 범위를 안전하게 비우고, 분기 및 산술 오버헤드를 줄일 수 있는지 확인.

## 시나리오별 점검
1. **정적 효과 + 클리핑 분리**: CAPS 인디케이터를 켠 뒤 클리핑 범위를 비활성화하고 효과 범위를 분리하면, 조기 분기에서 효과 범위 전체를 한 번에 초기화해 잔류 색상이 남지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L262-L287】
2. **동적 효과 + 범위 이동 반복**: VIA에서 효과 범위를 클리핑과 겹치지 않도록 이동시키는 동적 시나리오에서도 동일 경로가 발동해 포인터 루프가 실행되지 않고, 상태 플래그는 기존 후처리에서 정리된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L262-L300】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L326-L333】
3. **호스트 LED Off 복귀**: 호스트 LED를 끄면 `should_enable`이 false로 내려가며, 새 분기가 전체 효과 범위를 비워 `has_visible_output`이 다음 타이머 주기에 false로 유지된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L262-L287】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L335-L341】

## 불필요 코드 / 사용 종료된 요소 정리
- 교집합이 없을 때 전체 효과 범위를 초기화하도록 조정해 `rgblight_indicator_saturating_sub()` 헬퍼와 관련 포화 감산 분기를 제거했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L262-L287】

## 성능 및 오버헤드 검토
- 분기 내 산술 계산과 비교가 사라져 인터럽트 컨텍스트에서 수행되던 포화 감산 두 번을 줄였고, 함수 호출은 동일 `rgblight_indicator_clear_range()` 두 번만 남아 호출 비용을 최소화했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L262-L287】

## 제어 흐름 간소화
- 교집합 없음 분기가 두 줄로 축소되어 앞·뒤 보정 경로가 없어지고, 후속 분기가 명확히 교집합 존재 여부만 판단하면 되도록 단순화됐다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L262-L300】

## 수정 적용 여부 판단
- 정적/동적 효과와 호스트 토글 시나리오 모두에서 효과 범위 전체 초기화가 기존 상태 플래그 흐름과 충돌하지 않고, 잔여 컬러를 남기지 않음을 확인해 보수적으로 변경을 확정했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L262-L333】

**결: 수정 적용.**

# V251016R4 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: `rgblight_indicator_prepare_buffer()` 내부의 `do { ... } while (false)` 블록과 break 기반 조기 종료 경로.
- 목표: 실제 구동 시나리오에서 조기 종료 조건을 if-else 체인으로 단순화했을 때 상태 후처리가 일관되게 수행되고, 분기 비용이 줄어드는지 확인.

## 시나리오별 점검
1. **정적 효과 + 클리핑 비활성**: CAPS 인디케이터가 켜진 상태에서 클리핑 범위를 0으로 만들면 if 첫 분기가 실행되어 효과 범위만 정리하고, 후처리에서 출력 플래그가 false로 내려가 기본 효과가 즉시 복귀한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L256-L274】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L335-L341】
2. **동적 효과 + 효과 범위 0**: VIA에서 효과 범위를 0으로 줄이면 `has_effect` 검사 분기가 실행되어 클리핑 구간만 초기화하고, 렌더링 플래그가 false로 정리되어 다음 주기에서 기본 루프가 유지된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L258-L274】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L335-L341】
3. **교집합 유지 + 포인터 채우기**: 클리핑/효과 범위가 겹칠 때 else 블록이 실행되어 기존 포인터 루프와 앞·뒤 정리 경로가 그대로 수행되고, 최종적으로 출력 플래그가 true로 기록되어 인디케이터가 우선권을 유지한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L276-L333】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L335-L341】

## 불필요 코드 / 사용 종료된 요소 정리
- break 기반 조기 종료를 if-else 체인으로 교체해 `do { ... } while (false)` 구조를 제거했고, 상태 후처리는 기존 단일 위치를 그대로 사용한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L256-L341】

## 성능 및 오버헤드 검토
- 조기 종료 조건이 단일 if-else 체인으로 정리되어 분기 예측 부담이 줄고, 교집합이 없는 프레임에서도 불필요한 비교/브레이크 없이 곧바로 후처리로 이어진다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L256-L341】

## 제어 흐름 간소화
- `has_effect`·`clip_count` 조합이 상위 분기로 승격되어 break가 사라졌고, 이후 흐름은 두 갈래(교집합 없음/존재)로만 나뉘어 읽기와 유지보수가 쉬워졌다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L256-L341】

## 수정 적용 여부 판단
- 세 가지 시나리오 모두에서 상태 플래그와 버퍼 정리가 의도대로 수행되고, 기존 포인터 루프도 영향을 받지 않아 보수적으로 변경을 확정했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L256-L341】

**결: 수정 적용.**

# V251016R3 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: V251016R2 이후에도 `rgblight_indicator_prepare_buffer()` 내부에서 8비트 범위를 반복 캐스팅하던 구간과 인덱스 기반 채우기 루프.
- 목표: 실제 구동 시나리오에서 교차 구간 계산과 버퍼 채우기가 16비트 포인터 연산으로 동작해 오버헤드를 줄이고, 고인덱스 LED에서도 경계 보정이 안정적으로 유지되는지 확인.

## 시나리오별 점검
1. **정적 효과 + 전체 교집합 유지**: CAPS 인디케이터를 온 상태로 유지한 채 클리핑과 효과 범위를 동일하게 두면, 16비트 로컬 범위가 그대로 사용돼 교집합 길이를 계산하고, 포인터 증감 루프가 인덱스 재계산 없이 모든 LED를 동일한 색으로 채운다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L252-L310】
2. **동적 효과 + 교집합 확장/축소**: VIA에서 효과 범위를 반복적으로 늘리고 줄여도 포인터 루프가 매 호출마다 캐시된 색상을 순차적으로 복사하고, 교집합 밖 구간은 기존 16비트 정리 경로를 그대로 따라 잔류 컬러가 남지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L272-L333】
3. **호스트 LED Off 복귀**: 호스트 락 LED를 끄면 조기 종료 분기가 동일한 16비트 범위를 사용해 클리핑/효과 잔여 구간을 정리하고, 포인터 루프는 실행되지 않아 불필요한 버퍼 접근이 발생하지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L244-L337】

## 불필요 코드 / 사용 종료된 요소 정리
- 8비트 범위를 반복 캐스팅하던 변수를 16비트 로컬로 승격해, 교차 계산과 정리 루틴에서 별도 캐스팅 없이 동일 값을 재사용하도록 정리했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L252-L279】
- 인디케이터 버퍼 채우기 루프를 포인터 증감 기반으로 바꿔, 인덱스 접근 시 발생하던 반복 덧셈을 제거했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L301-L307】

## 성능 및 오버헤드 검토
- 16비트 로컬 범위를 재사용해 `fill_begin`·`fill_end` 계산이 추가 캐스팅 없이 수행돼, 인터럽트 컨텍스트에서의 산술 오버헤드가 줄었다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L252-L279】
- 포인터 증감 루프가 인덱스 연산을 대체해 LED 수만큼 수행되던 주소 계산을 한 번의 증가 연산으로 치환, 동적 효과 렌더링 주기에서 CPU 부하를 낮췄다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L301-L307】

## 제어 흐름 간소화
- 교집합 계산 이후 모든 정리 경로가 동일한 16비트 값을 공유해, 앞/뒤 보정 시 별도의 캐스팅 분기를 둘 필요가 없어졌다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L272-L333】

## 수정 적용 여부 판단
- 정적/동적 효과와 호스트 토글 시나리오에서 16비트 범위와 포인터 루프가 정상적으로 동작하고 잔여 컬러 없이 버퍼가 정리됨을 확인해 보수적으로 변경을 확정했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L244-L337】

**결: 수정 적용.**

# V251016R2 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: `rgblight_indicator_clear_range()`가 8비트 시작 인덱스로 고정돼 교집합 계산 결과가 256 이상이 될 수 있는 보드에서 래핑될 위험이 남아 있던 경로.
- 목표: tail/front 보정 시 16비트 값으로 계산되는 시작 지점을 그대로 전달해 반복 호출마다 캐스팅을 제거하고, 실제 동작 시나리오에서 경계 보정이 안정적으로 수행되는지 보수적으로 확인.

## 시나리오별 점검
1. **정적 효과 + 클리핑 축소 반복**: CAPS 인디케이터를 켠 상태에서 클리핑 범위를 LED 끝까지 넓힌 뒤, tail 쪽 LED를 하나씩 줄이면 `tail_start`가 16비트로 증가해도 헬퍼가 그대로 잘라내고, 교집합 이후 영역이 정확히 초기화돼 기존 컬러가 잔류하지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L152-L173】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L291-L309】
2. **동적 효과 + 효과 범위 확장/축소**: 효과 범위를 클리핑보다 크게 만들었다가 즉시 줄이는 과정을 반복해도 front/tail 정리가 모두 16비트 경로로 처리돼, 조기 종료 분기와 교집합 분기 모두에서 남은 범위를 정확히 초기화한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L281-L298】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L321-L333】
3. **호스트 LED Off 복귀**: 호스트 락 LED를 끈 뒤 기본 RGB 루프로 복귀할 때도 새 헬퍼가 clip/tail 정리 결과를 보존해 `has_visible_output`이 즉시 내려가고, 이후 타이머 주기에서 기본 효과가 그대로 재개된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L243-L337】

## 불필요 코드 / 사용 종료된 요소 정리
- `rgblight_indicator_clear_range()`가 16비트 시작 인덱스를 직접 받아 더 이상 호출 지점에서 `(uint8_t)` 캐스팅을 반복하지 않아도 되며, 래핑 가능성도 제거됐다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L152-L173】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L291-L329】

## 성능 및 오버헤드 검토
- tail/front 정리 단계에서 추가 캐스팅이 사라져 루프마다 불필요한 마스킹이 줄었고, 16비트 값을 그대로 사용해도 `memset()` 호출은 한 번만 발생해 캐시 접근 수는 변하지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L152-L173】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L301-L333】

## 제어 흐름 간소화
- 교집합 계산 결과를 그대로 넘겨도 헬퍼가 경계를 보정하므로, tail/front 경로에서 별도의 캐스팅 분기를 둘 필요가 없어졌다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L152-L173】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L291-L329】

## 수정 적용 여부 판단
- 시나리오별로 경계 보정이 안정적으로 동작하고 기존 플래그/타이머 흐름도 변하지 않음을 확인해 보수적으로 변경을 확정했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L243-L337】

**결: 수정 적용.**

# V251016R1 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: `rgblight_indicator_prepare_buffer()`가 각 분기에서 직접 `has_visible_output`·`needs_render`를 갱신하던 경로.
- 목표: 출력 여부와 렌더 완료 플래그를 공통 후처리로 묶어 중복 대입을 제거하고, 조기 종료가 반복되더라도 상태가 일관되게 정리되는지 보수적으로 확인.

## 시나리오별 점검
1. **정적 효과 + 클리핑 0 반복 호출**: 첫 호출에서 `clip_count == 0` 분기가 break로 빠져나오며 공통 후처리에서 `needs_render`가 false로 내려가고, 이후 호출은 `needs_render`가 이미 해제돼 동일 프레임 재평가 없이 바로 false를 반환한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L261-L276】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L331-L333】
2. **동적 효과 + 교집합 유지**: 효과·클리핑 교집합이 유지되는 동안에는 루프를 모두 통과해 `has_visible_output`이 true로 설정되고, 공통 후처리에서 한 번만 상태를 저장해 타이머가 같은 값으로 우회한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L277-L329】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L331-L333】
3. **호스트 LED Off 복귀**: 교집합이 사라지는 프레임에서 break로 빠져나온 뒤 공통 후처리가 false를 기록해 다음 타이머 주기부터 기본 루프가 재개되고, 불필요한 상태 잔여가 남지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L277-L307】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L331-L333】

## 불필요 코드 / 사용 종료된 요소 정리
- 각 분기에서 반복 대입하던 `has_visible_output`·`needs_render` 업데이트를 공통 후처리로 이동해 동일 코드가 세 번 이상 반복되던 경로를 제거했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L261-L333】

## 성능 및 오버헤드 검토
- 조기 종료가 발생한 프레임에서도 한 번만 상태를 기록하므로, 인터럽트 경로에서 불필요한 캐시 쓰기와 메모리 장벽 영향을 최소화했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L261-L333】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1359-L1366】

## 제어 흐름 간소화
- do-while 블록으로 조기 종료 경로를 감싸 break 처리만으로 모든 분기를 마무리할 수 있게 되어, 상태 갱신이 함수 말단 한 곳으로 응집되었다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L261-L333】

## 수정 적용 여부 판단
- 각 시나리오에서 출력/비출력 여부가 정확히 기록되고 기본 RGB 루프 복귀 시점도 변하지 않음을 재확인해 보수적으로 변경을 확정했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L261-L333】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1359-L1366】

**결: 수정 적용.**

# V251015R9 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: V251015R8 이후에도 `indicator_has_output` 판정이 호출 지점마다 달라 빈 교집합에서도 타이머가 멈추던 경로.
- 목표: 실제로 LED를 덮어쓰는 경우에만 기본 애니메이션을 차단하도록 출력 여부를 공통 상태로 승격하고, 교집합이 사라진 시나리오에서도 기본 파이프라인이 즉시 복귀하는지 보수적으로 확인.

## 시나리오별 점검
1. **정적 효과 + 교집합 0**: CAPS 토글 후 클리핑/효과 범위를 어긋나게 만들면 새 `has_visible_output` 플래그가 false로 내려가 버퍼만 정리하고 false를 반환해, 같은 프레임에서 기본 정적 효과가 다시 실행된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L239-L301】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1358-L1368】
2. **동적 효과 + 교집합 유지**: 효과 범위가 클리핑과 겹치는 동안에는 플래그가 true로 유지되어 버퍼를 채우고, 타이머에서도 동일 플래그를 확인해 애니메이션 루프가 계속 우회한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L314-L337】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1358-L1368】
3. **호스트 LED Off 복귀**: 호스트 락 LED를 끄면 상태 전이에서 출력 플래그가 즉시 초기화되고, 다음 타이머 주기부터 기본 루프가 그대로 동작한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L341-L360】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1358-L1368】

## 불필요 코드 / 사용 종료된 요소 정리
- `rgblight_indicator_prepare_buffer()`와 `rgblight_timer_task()`가 각각 계산하던 출력 여부를 단일 상태 필드로 통합해, 더 이상 사용되지 않는 이중 판별 로직을 제거했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L132-L148】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L239-L337】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1358-L1368】

## 성능 및 오버헤드 검토
- 출력이 없는 프레임은 곧바로 false를 반환해 기본 루프가 재실행되고, 타이머에서도 동일 플래그를 재사용해 중복 범위 계산과 불필요한 `rgblight_set()` 호출을 줄였다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L239-L337】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1358-L1368】

## 제어 흐름 간소화
- 출력 여부를 구조체 필드로 승격해 활성/렌더 플래그와 동일 위치에서 관리하므로, 상태 전이가 어디서든 일관된 기준을 따르게 되었다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L132-L360】

## 수정 적용 여부 판단
- 빈 교집합, 정상 교집합, 호스트 비활성 전환 시 모두 보수적으로 기존 동작을 유지하거나 기본 루프를 복구함을 확인해 변경을 확정했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L239-L360】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1358-L1368】

**결: 수정 적용.**

# V251015R8 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: 출력 구간이 사라진 이후에도 `rgblight_indicator_prepare_buffer()`가 기본 파이프라인을 차단하던 조기 반환.
- 목표: 인디케이터가 실제로 그릴 LED가 없을 때는 애니메이션 루프와 기본 RGB 경로가 그대로 실행되도록, 활성/렌더 플래그 조합을 보수적으로 재검토.

## 시나리오별 점검
1. **정적 효과 + 클리핑 0**: 캡스락 토글 후 클리핑 범위를 0으로 줄이면 새 분기가 `indicator_has_output`을 false로 판정해 즉시 false를 반환하고, 기본 정적 모드가 같은 프레임에서 복원된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L243-L253】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1210-L1234】
2. **동적 효과 + 효과 범위 0**: 효과 범위를 0으로 줄인 경우에도 `needs_render`가 내려간 이후 반환값이 false로 유지돼, 타이머 루프가 계속 실행되며 호스트 LED가 꺼질 때까지 기존 애니메이션이 유지된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L243-L253】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1352-L1366】
3. **출력 범위 복구**: 범위를 다시 확장하면 `needs_render`가 true로 설정되어 기존 분기가 그대로 실행되고, `indicator_has_output`이 true로 돌아와 재렌더 이후에는 인디케이터가 다시 우선권을 가진다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L243-L253】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L297-L333】

## 불필요 코드 / 사용 종료된 요소 정리
- `needs_render`가 false일 때도 인디케이터가 기본 루프를 막던 반환을 출력 구간 유무에 따라 분기시켜, 사용되지 않는 오버라이드 경로를 제거했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L243-L253】

## 성능 및 오버헤드 검토
- 출력 구간이 없을 때는 조기에 false를 반환해, 불필요한 `rgblight_layers_write()`·`rgblight_set()` 차단을 피하고 타이머 루프가 정상적으로 RGB 효과를 갱신하도록 했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L243-L253】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1210-L1234】

## 제어 흐름 간소화
- 활성 플래그가 유지되더라도 실제 출력이 없으면 반환값이 false가 되어 애니메이션 경로가 명확히 분리되도록, 조기 반환 조건을 단일 불리언으로 정리했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L243-L253】

## 수정 적용 여부 판단
- 출력 구간이 사라진 이후에도 기본 애니메이션이 즉시 복원되고, 구간이 복구되면 기존 렌더 경로가 그대로 유지됨을 재확인해 보수적 변경으로 확정했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L243-L253】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1352-L1366】

**결: 수정 적용.**

# V251015R7 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: `rgblight_timer_task()`가 인디케이터 활성 시 애니메이션 루프를 무조건 중단하던 로직.
- 목표: 클리핑/효과 범위가 0으로 수렴한 시나리오에서 기본 애니메이션이 멈추지 않도록, 실제 출력 범위가 있을 때만 차단하도록 조정.

## 시나리오별 점검
1. **정적 효과 + 클리핑 0**: CAPS 인디케이터가 활성 상태라도 클리핑 범위를 0으로 줄이면 새 분기가 애니메이션 차단을 건너뛰어, 기본 정적 효과가 즉시 유지된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1344-L1353】
2. **동적 효과 + 정상 범위 유지**: 클리핑·효과 범위가 모두 유효하면 기존과 동일하게 인디케이터가 애니메이션을 차단하고, 필요 시 `needs_render`가 true일 때만 `rgblight_set()`를 호출한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1344-L1355】
3. **효과 범위 0 복귀**: VIA에서 효과 범위를 0으로 내린 뒤 다시 확장하면 애니메이션이 일시 정지했다가, 범위가 복구되는 즉시 차단이 재개되어 리그레션이 없다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1344-L1355】

## 불필요 코드 / 사용 종료된 요소 정리
- 인디케이터 활성 여부만으로 애니메이션을 중단하던 조기 반환을 제거하고, 출력 범위가 0인 경우에는 기본 루프를 유지하도록 정리했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1344-L1355】

## 성능 및 오버헤드 검토
- 출력 범위가 없을 때 애니메이션 루프를 그대로 실행해, 불필요한 `rgblight_set()` 호출을 피하면서 기본 효과 갱신을 계속 허용한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1344-L1355】

## 제어 흐름 간소화
- 출력 범위 존재 여부를 단일 불리언으로 판별해, 애니메이션 차단 조건을 명시적으로 읽을 수 있게 했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1346-L1353】

## 수정 적용 여부 판단
- 출력 범위가 사라진 경우에도 기본 RGB 효과가 정상적으로 갱신되고, 유효 범위에서는 기존 차단 동작이 유지됨을 확인해 보수적 변경으로 확정했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1344-L1355】

**결: 수정 적용.**

# V251015R6 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: 출력 구간이 사라졌을 때도 인디케이터가 기본 RGB 파이프라인을 막던 `rgblight_indicator_prepare_buffer()` 반환 처리.
- 목표: 클리핑이 0이거나 효과 범위가 비어 있는 시나리오에서 굳이 인디케이터가 오버라이드하지 않아도 되는지 반복 호출 시나리오로 확인.

## 시나리오별 점검
1. **클리핑 비활성 + 인디케이터 활성 유지**: 호스트 LED가 켜진 상태에서 클리핑 범위를 0으로 낮추면, 변경된 반환값 덕분에 기본 RGB 경로가 즉시 실행되어 기존 백라이트가 복원되고, 효과 범위만 초기화되어 잔광이 남지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L256-L266】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1200-L1236】
2. **효과 범위 0 + 정적 모드**: 효과 범위를 0으로 줄여도 인디케이터가 false를 반환해 베이스 루프가 동일 프레임에서 실행되고, 클리핑 범위만 정리되어 정적 모드 LED가 즉시 다시 채워진다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L268-L275】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1200-L1236】
3. **범위 복구 후 재렌더**: 클리핑/효과 범위를 다시 유효 값으로 되돌리면 `needs_render`가 유지되어 교집합 분기가 그대로 실행되고, true를 반환해 인디케이터가 기존과 동일하게 우선권을 가져간다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L277-L323】

## 불필요 코드 / 사용 종료된 요소 정리
- 출력 구간이 없을 때 true를 반환하던 경로를 false로 변경해, 실제로 버퍼를 건드리지 않는 상황에서 기본 파이프라인을 차단하던 불필요한 오버라이드를 제거했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L256-L275】

## 성능 및 오버헤드 검토
- 인디케이터가 그릴 LED가 없으면 즉시 false를 반환해, 이후 루프가 백라이트를 채우면서 중복 초기화를 피하고 인터럽트 경로에서 쓸데없는 `memset` 반복을 막는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L256-L323】

## 제어 흐름 간소화
- 반환값이 실제 오버라이드 여부와 일치하게 정리되어, 호출자는 true가 나온 경우에만 인디케이터가 색상 복사를 수행했음을 보장받는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L256-L323】

## 수정 적용 여부 판단
- 출력 구간이 없을 때 기본 RGB 경로를 복원해도 기존 효과 재적용 시에는 동일 분기가 다시 true를 반환해 리그레션이 발생하지 않아, 보수적 변경으로 확정했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L256-L323】

**결: 수정 적용.**

# V251015R5 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: 밝기 0 구성에서 이미 비활성화된 인디케이터 경로에 남아 있던 추가 가드.
- 목표: `should_enable` 단계에서 걸러진 밝기 0 구성이 버퍼 준비 루틴에 진입하지 않는지 시나리오별로 확인하고, 중복 정리 호출을 제거해도 안전한지 검토.

## 시나리오별 점검
1. **밝기 0 + 호스트 토글 반복**: `rgblight_indicator_should_enable()`이 밝기 0 구성에서 false를 반환해 상태 전이가 즉시 비활성화로 내려가므로, 버퍼 준비 루틴이 호출되지 않고 잔여 가드를 제거해도 안전하다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L194-L202】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L333-L362】
2. **교집합 부재 + 효과 범위 유지**: 클리핑과 효과가 분리된 상태에서 새 분기가 전체 클리핑 범위를 정리한 뒤 앞·뒤 잔여 구간만 초기화해, 기존과 동일한 범위를 보수적으로 청소한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L271-L295】
3. **교집합 존재 + 재렌더 반복**: 교집합이 유지되는 시나리오에서는 기존 계산 경로가 그대로 유지되어, 색상 채움과 전후단 정리가 변하지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L297-L329】

## 불필요 코드 / 사용 종료된 요소 정리
- 밝기 0 가드를 제거해 `rgblight_indicator_prepare_buffer()`가 교집합 여부만 검사하도록 정리했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L252-L278】

## 성능 및 오버헤드 검토
- 중복된 밝기 검사 분기가 사라져 인터럽트 경로에서 조건 비교가 줄고, 실제 교집합 계산에만 집중하게 됐다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L252-L278】

## 제어 흐름 간소화
- 밝기 0 구성이 활성 경로로 진입하지 않음을 주석으로 명시해, 버퍼 준비 루틴이 교집합 여부 중심으로 읽히도록 정리했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L252-L280】

## 수정 적용 여부 판단
- 밝기 0 구성이 상태 전이 단계에서 차단되어 버퍼 경로가 동일하게 유지됨을 재확인해, 가드 제거를 보수적으로 적용하기로 했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L194-L202】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L255-L295】

**결: 수정 적용.**

# V251015R4 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: 밝기 0으로 설정된 인디케이터 구성에서도 효과 렌더링을 예약하던 `should_enable` 판단.
- 목표: 밝기 0을 인디케이터 비활성 조건으로 간주해 애니메이션 계산과 `rgblight_set()` 호출을 보수적으로 줄일 수 있는지 확인.

## 시나리오별 점검
1. **밝기 0 + 호스트 토글 유지**: 밝기가 0인 상태에서 캡스락 등 호스트 LED가 반복 토글돼도 `rgblight_indicator_should_enable()`이 false를 반환해 `needs_render`가 다시 올라가지 않고, 인디케이터가 비활성 상태로 유지된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L187-L205】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L361-L368】
2. **밝기 0 → 밝기 상승 전환**: 밝기를 0에서 유효 값으로 올리는 순간에만 `should_enable`이 true로 변해, 기존 버퍼를 다시 채우고 효과를 복원하는 흐름이 유지됨을 확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L187-L205】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L325-L343】
3. **초기화 재호출 + 밝기 0 구성**: `rgblight_init()`이 재호출되어도 새 보조 헬퍼가 초기 활성 여부를 false로 반환해, 초기화 시점에 불필요한 렌더 예약을 하지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L187-L205】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L520-L535】

## 불필요 코드 / 사용 종료된 요소 정리
- 각 호출부에서 직접 대상 활성만 검사하던 로직을 보조 헬퍼로 치환해, 밝기 0 구성을 자동으로 비활성 처리하도록 정리했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L187-L205】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L365-L368】

## 성능 및 오버헤드 검토
- 밝기 0 구성은 즉시 비활성으로 처리돼 애니메이션 타이머가 `rgblight_set()`을 다시 호출하지 않으므로, 인터럽트 경로에서 불필요한 DMA 준비와 버퍼 초기화를 줄였다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L325-L343】

## 제어 흐름 간소화
- `rgblight_indicator_should_enable()`이 활성 조건을 단일 함수로 묶어, 구성·호스트 상태 변경·초기화 모두 동일 기준으로 평가하게 되어 추적이 쉬워졌다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L187-L205】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L520-L535】

## 수정 적용 여부 판단
- 밝기 0을 비활성으로 간주해도 기존 구성 전환 흐름이 유지되고, 필요 시 밝기 상승 시점에만 효과가 복원됨을 확인해 보수적 변경으로 판단했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L325-L343】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L361-L368】

**결: 수정 적용.**

# V251015R3 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: V251015R2까지 정리된 밝기 0·무교집합 경로에서 남은 `clip_covers_effect` 보조 분기.
- 목표: 포화 감산 헬퍼를 도입해 앞·뒤 잔여 길이를 단일 계산으로 구하고, 불필요한 분기를 제거해도 실제 잔여 효과 구간만 보수적으로 초기화되는지 확인.

## 시나리오별 점검
1. **밝기 0 + 효과 범위 = 클리핑 범위**: 포화 감산으로 앞·뒤 길이가 0으로 계산돼 추가 초기화가 생략되며, 기존과 동일하게 클리핑 구간만 정리된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L268-L283】
2. **밝기 0 + 효과 범위가 클리핑 앞쪽에만 위치**: `front_end`가 효과 끝과 클리핑 시작 중 작은 값을 취해 전체 효과 범위를 한 번에 정리하고, DMA 전송 전에 잔류 색상이 남지 않음을 확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L271-L276】
3. **밝기 0 + 효과 범위가 클리핑 뒤쪽에만 위치**: `tail_start`가 효과 시작과 클리핑 끝 중 큰 값을 사용해 실제 남은 효과 길이만 계산되고, Split 반대편 등 후단 잔여 구간도 누락 없이 초기화된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L278-L283】
4. **효과 이동 + 조기 렌더 종료 반복**: 포화 감산 헬퍼가 언더플로를 방지해 시나리오 전환마다 잔여 길이를 안전하게 재계산하므로, `needs_render`를 내린 뒤 반복 호출에서도 추가 초기화가 발생하지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L268-L286】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L320-L321】

## 불필요 코드 / 사용 종료된 요소 정리
- `clip_covers_effect` 분기와 관련 비교를 제거하고, 앞·뒤 잔여 구간을 포화 감산으로 직접 계산해 더 이상 쓰이지 않는 보조 조건을 정리했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L268-L283】
- 잔여 길이 계산에 포화 감산 헬퍼를 도입해, 이전 단계에서만 사용되던 조건부 감산 코드를 제거했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L173-L177】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L268-L283】

## 성능 및 오버헤드 검토
- 밝기 0 경로에서 분기 수가 줄어들어 인터럽트 타이머 재호출 시 분기 예측 실패 가능성을 낮추고, 잔여 길이 계산이 비교/감산 한 번으로 정리돼 연산량이 감소했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L268-L283】
- 포화 감산은 인라인 함수로 처리돼 기존 조건 분기 대비 동일 수준의 명령 수를 유지하면서도 코드 경로가 단순화됐다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L173-L177】

## 제어 흐름 간소화
- 앞·뒤 잔여 길이를 공통 계산으로 묶어, 효과 범위 위치에 따라 달라지던 중첩 분기를 제거했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L268-L283】
- 조기 반환 이후 경로와 채움 경로가 동일한 `needs_render` 관리를 사용해, 렌더 여부 판단이 기존과 동일하게 유지되면서도 추적이 쉬워졌다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L268-L286】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L320-L321】

## 수정 적용 여부 판단
- 포화 감산으로 길이를 계산해도 `rgblight_indicator_clear_range()`의 경계 보정이 그대로 적용돼 DMA 버퍼 안전성이 유지된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L150-L177】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L268-L283】
- 밝기 0 시나리오 전반에서 잔여 효과 구간만 보수적으로 정리됨을 재확인해, 변경 적용을 확정했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L268-L286】

**결: 수정 적용.**

# V251015R2 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: V251015R1 변경 이후 밝기 0·무교집합 경로에서 클리핑과 효과 범위 사이 간격이 존재하는 시나리오.
- 목표: 간격이 남는 경우에도 효과 범위만 초기화되도록 앞·뒤 잔여 길이를 보정해, 다른 애니메이션 색상이 보존되는지 재확인.

## 시나리오별 점검
1. **밝기 0 + 효과 범위가 클리핑 뒤쪽 간격 이후 위치**: `tail_start`를 효과 시작점으로 보정해 간격 구간(clip_end~start)에는 손을 대지 않고, 효과가 존재하는 LED만 초기화됨을 확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L266-L285】
2. **밝기 0 + 효과 범위가 클리핑 앞쪽에만 위치**: `front_end`를 효과 종료 지점으로 제한해 실제 효과 길이만큼만 초기화되고, 클리핑 앞쪽과 효과 사이의 간격은 유지됨을 검증했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L266-L277】
3. **밝기 0 + 교집합 유지 + 후단 확장**: 교집합 이후 잔여 길이 계산이 유지되어, DMA 대상 구간과 동일하게 효과 잔여 구간만 정리되는 기존 시나리오와 호환됨을 확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L278-L285】

## 불필요 코드 / 사용 종료된 요소 정리
- 간격이 존재하는 경우에도 효과 범위만 초기화하도록 앞·뒤 잔여 길이 계산을 보정해, DMA 버퍼 외 구간 색상 소거를 방지했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L266-L285】

## 성능 및 오버헤드 검토
- 기존 조건을 그대로 유지하면서 실제 초기화 호출 길이만 줄여, 밝기 0 토글이 반복될 때 `memset()` 범위가 최소화되도록 했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L266-L285】

## 제어 흐름 간소화
- 간격 여부와 무관하게 동일 분기에서 앞·뒤 잔여 길이를 계산하되, 실제 효과 범위에 맞춰 조정해 추후 시나리오 추가 시에도 로직을 재사용할 수 있게 했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L266-L285】

## 수정 적용 여부 판단
- 간격이 존재하는 시나리오에서도 DMA 범위 밖 색상 보존과 초기화 범위의 보수적 동작이 유지됨을 확인해 수정 적용을 확정했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L266-L285】

**결: 수정 적용.**

# V251015R1 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: V251012R2~V251014R9 누적 변경 이후 남은 밝기 0·무교집합 경로의 중복 초기화.
- 목표: 효과 범위가 클리핑 밖으로 확장된 실제 시나리오에서 중복 `memset()` 호출 없이도 DMA 버퍼 잔류 색상을 확실히 제거하는지 검증.

## 시나리오별 점검
1. **밝기 0 + 효과 범위 전후 확장**: 효과 범위가 클리핑 전후단을 모두 넘는 상태에서 조기 반환 경로가 전단·후단 잔여 길이만 별도로 초기화해, 교집합 구간을 반복 초기화하지 않으면서 DMA 외 영역까지 정리됨을 확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L266-L285】
2. **밝기 0 + 전단만 확장**: Split 반대편 등 효과 범위가 클리핑 앞쪽으로만 남은 경우에도, 앞단 길이 계산만 실행되어 나머지 범위는 기존 클리핑 초기화로 충분해 불필요한 `memset()` 호출이 발생하지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L266-L279】
3. **밝기 0 + 후단만 확장**: 효과 범위가 클리핑 뒤쪽으로만 넘어가는 경우 후단 길이만 계산해 초기화하므로, 타이머가 동일 프레임을 다시 호출해도 중복 초기화가 반복되지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L277-L285】

## 불필요 코드 / 사용 종료된 요소 정리
- 밝기 0·무교집합 경로에서 클리핑과 동일 구간을 다시 초기화하던 호출을 제거하고, 교집합 밖 남는 구간만 정리하도록 조정했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L266-L285】

## 성능 및 오버헤드 검토
- 전단·후단 길이를 조건부로 계산해 필요한 구간만 `memset()`하므로, 밝기 토글이 잦은 인터럽트 컨텍스트에서 메모리 초기화 오버헤드를 더 줄였다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L266-L285】
- 교집합 구간은 단일 초기화로 끝나 조기 반환 경로의 분기 수를 유지하면서도 메모리 접근 횟수를 최소화했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L266-L285】

## 제어 흐름 간소화
- 교집합 외 잔여 구간을 전단·후단으로 구분해 처리함으로써, 실제로 초기화가 필요한 범위를 코드에서 바로 추적할 수 있게 됐다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L266-L285】

## 수정 적용 여부 판단
- 효과 범위가 클리핑을 벗어나는 경우에도 교집합 외 구간만 정리하도록 제한되어, DMA에 실리는 영역과 동일하게 정리됨을 보수적으로 확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L266-L285】
- 기존 조기 반환 흐름과 `needs_render` 관리가 그대로 유지되어, 렌더 예약이나 타이머 동작에 변화가 없어 안전하다고 판단했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L266-L285】

**결: 수정 적용.**

# V251014R9 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: V251012R2~V251014R8 누적 변경 이후 남은 Brick60 RGB 인디케이터 밝기 0/무교집합 경로.
- 목표: 효과 범위가 클리핑 범위를 완전히 덮는 시나리오에서 중복 `memset()`이 계속 실행되는지 확인하고, 필요 시 제거해도 DMA 버퍼 계약을 유지하는지 보수적으로 판단.

## 시나리오별 점검
1. **밝기 0 + 효과 범위 = 클리핑 범위**: `clip_covers_effect`가 true일 때 효과 범위 초기화를 건너뛰어, 동일 범위를 두 번 지우는 `memset()` 호출을 제거해도 `clipping` 구간이 이미 정리되어 잔류 색상이 남지 않음을 확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L241-L273】
2. **밝기 0 + 효과 범위 > 클리핑**: 효과 범위가 클리핑 밖으로 확장된 경우 `clip_covers_effect`가 false가 되어 추가 초기화가 유지되므로, Split 반대편 등 DMA에 실리지 않는 구간도 기존과 동일하게 정리됨을 검증했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L262-L270】
3. **무교집합 + 범위 토글 반복**: 교집합이 사라진 상태에서 밝기 토글과 범위 변경을 반복해도, 클리핑 정리 후 `clip_covers_effect` 검사로 효과 범위 전부를 계속 초기화해 DMA 외 구간의 잔류 색상을 방지함을 확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L262-L274】
4. **동적 토글 + 렌더 플래그 재활용**: `needs_render`를 false로 내린 뒤 즉시 반환하므로, 타이머가 동일 프레임을 다시 호출해도 중복 초기화가 발생하지 않고 기존 조기 반환 흐름이 유지된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L266-L274】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1331-L1336】

## 불필요 코드 / 사용 종료된 요소 정리
- 밝기 0·무교집합 경로에서 클리핑과 동일 범위를 다시 초기화하던 호출을 제거해, 실효성 없는 `memset()`을 없앴다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L262-L270】

## 성능 및 오버헤드 검토
- 효과 범위가 클리핑 범위와 동일한 경우 추가 `memset()`이 사라져, 밝기 0 토글이 반복되는 타이머 경로에서 메모리 정리 오버헤드가 줄었다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L262-L274】
- `clip_covers_effect` 검사는 단일 비교로 종료되어 기존 분기 수를 유지하면서도 중복 연산을 막아, 인터럽트 컨텍스트에서의 부담을 최소화했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L262-L270】

## 제어 흐름 간소화
- `clip_covers_effect` 조건을 명시해 클리핑 범위가 효과 범위를 완전히 덮는 경우와 아닌 경우를 분리해, 호출부가 언제 추가 초기화를 실행하는지 쉽게 추적할 수 있게 됐다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L262-L270】

## 수정 적용 여부 판단
- 중복 초기화를 건너뛰는 조건이 클리핑 범위를 완전히 덮는 경우로 한정되어, DMA에 전달되지 않는 구간이 남더라도 기존과 동일하게 정리되어 안전성이 유지된다고 판단했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L262-L270】
- 기존 조기 반환 경로와 `needs_render` 관리가 변하지 않아, 인디케이터 비활성·타이머 경로의 보수적 동작이 유지된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L266-L274】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1331-L1336】

**결: 수정 적용.**

# V251014R8 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: V251012R2~V251014R7 누적 변경 후 남은 Brick60 RGB 인디케이터 버퍼 준비 루틴.
- 목표: 실제 동작 시나리오에서 불필요한 교집합 계산을 줄이고, 밝기 0/클리핑 비활성화 등 소등 경로를 조기 반환으로 묶어 오버헤드를 더 줄일 수 있는지 보수적으로 확인.

## 시나리오별 점검
1. **클리핑 비활성화 + 효과 범위 유지**: `clipping_num_leds`가 0으로 비활성화된 상태에서 효과 범위만 남아 있을 때 즉시 효과 범위만 초기화하고 반환해, DMA 버퍼가 잔류 색상을 남기지 않으면서 추가 교집합 계산을 건너뛴다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L238-L248】
2. **효과 범위 미지정 + 인디케이터 활성**: 효과 범위가 0으로 조정된 상태에서 인디케이터가 켜져 있어도 곧바로 클리핑 범위만 정리하고 반환해, 무의미한 교집합 계산과 루프 진입을 피한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L238-L255】
3. **밝기 0 반복 + 효과 이동**: 밝기가 0으로 토글된 상태에서 효과 범위가 계속 바뀌어도, 밝기/교집합 조합을 조기 판정해 클리핑·효과 범위를 한 번에 초기화하고 반환하므로 기존처럼 잔류 색상이 남지 않으면서도 분기 수가 줄었다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L257-L268】
4. **동적 효과 + 부분 교차**: 실제 교집합이 있을 때만 선행/후행 정리 길이를 계산하고 버퍼를 채우도록 남겨, 기존 분기와 동일한 범위를 정리/채움하면서 불필요한 계산을 제거했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L270-L302】

## 불필요 코드 / 사용 종료된 요소 정리
- 밝기 0 또는 교집합 부재 경로에서 사용되지 않는 선행/후행 길이 계산을 제거하고, 해당 시나리오를 조기 반환으로 대체했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L241-L278】
- 클리핑 길이 0 또는 효과 길이 0 시나리오를 분리해, 더 이상 쓰이지 않는 `should_fill`/`has_intersection` 보조 플래그를 제거했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L238-L268】

## 성능 및 오버헤드 검토
- 밝기 0·무교집합·클리핑 비활성화 시 즉시 반환하도록 정리해, 인터럽트 타이머 호출에서 매번 교집합 길이를 계산하던 오버헤드를 제거했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L238-L268】
- 실제 채움 경로에서만 선행/후행 길이를 계산하도록 바꿔, DMA 준비 루프에 들어가지 않는 호출의 연산 수를 줄였다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L270-L278】

## 제어 흐름 간소화
- 조기 반환 경로가 정리되어 밝기 0, 클리핑 비활성화, 효과 범위 없음 등 소등 시나리오를 읽기 쉬운 단일 경로로 따라갈 수 있게 됐다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L238-L268】
- 실제 채움 경로는 기존 계산을 유지해 동작을 보수적으로 유지하면서도, 필요 조건을 모두 만족했을 때만 실행되도록 명확해졌다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L270-L302】

## 수정 적용 여부 판단
- 조기 반환 경로는 기존에도 전체 초기화를 수행하던 구간을 그대로 실행하므로, DMA 버퍼 안전성과 색상 초기화 규약이 유지된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L241-L268】
- 채움 경로는 기존 계산을 유지해 Split·동적 효과 시나리오와의 호환성이 유지되므로, 보수적 변경으로 판단했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L270-L302】

**결: 수정 적용.**

# V251014R7 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: V251012R2~V251014R6 누적 변경 이후 남은 Brick60 RGB 인디케이터 버퍼 준비 루틴.
- 목표: 교집합 앞/뒤 정리 길이와 효과 범위 초기화 길이를 선행 계산해 재사용할 때, 시나리오별 초기화 누락이나 중복이 발생하지 않는지 확인하고 분기 수 감축이 타이머 경로에 안전한지 검증.

## 시나리오별 점검
1. **밝기 0 반복 + 교집합 유지**: `should_fill`이 false로 유지되는 동안에도 선행 계산된 `front_count`/`tail_count`가 재사용되지 않고 전체 클리핑 범위를 한 번에 초기화해, 밝기 토글 반복에서도 잔류 색상이 남지 않음을 확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L244-L268】
2. **동적 효과 + 교집합 이동**: 인디케이터가 활성화된 상태에서 효과 범위가 앞뒤로 이동해도 동일하게 계산된 길이가 전/후단 정리에 재사용되어, 매 호출마다 분기 조건을 다시 계산하지 않고도 정확히 잔여 영역만 정리됨을 검증했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L244-L292】
3. **Split 타이머 + 부분 교차**: Split 사용 중 좌우 클리핑 범위가 달라 교집합이 부분적으로 유지되는 상황에서도, 선행 계산된 효과 범위 정리 길이(`effect_front_clear`, `effect_tail_clear`)가 정확히 적용되어 DMA 전송 전 버퍼가 안정적으로 초기화됨을 확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L282-L292】
4. **효과 범위 축소 + 조기 렌더 완료**: VIA 매크로로 효과 범위를 반복 축소하면서 렌더링이 완료된 후 재호출되는 경우에도, `fill_count`를 단일 계산으로 유지하면서 루프 조건이 단순화되어 재렌더링이 필요 없는 호출에서 추가 오버헤드가 발생하지 않았다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L270-L285】

## 불필요 코드 / 사용 종료된 요소 정리
- 교집합 앞/뒤 정리 길이를 선행 계산해 재사용하면서, 이전에 조건마다 계산하던 `fill_begin > clip_start`/`clip_end > fill_end` 비교를 제거했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L244-L268】
- 효과 범위 전/후단 정리도 동일하게 길이를 선행 계산해, `has_effect` 확인 후 다시 계산하던 `fill_begin - start`, `effect_end - fill_end` 비교 로직을 줄였다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L282-L292】

## 성능 및 오버헤드 검토
- 교집합 길이를 한 번만 계산해 초기화와 채우기 경로에서 재사용함으로써, 인터럽트 컨텍스트에서 조건 분기 예측 실패 가능성을 줄였다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L244-L292】
- `fill_count`를 루프 전 미리 계산해 사용하면서, 반복마다 종료 조건을 다시 비교하지 않아 DMA 준비 루틴의 연산 수가 감소했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L270-L285】

## 제어 흐름 간소화
- 선행 계산된 길이를 공통으로 사용해, 밝기 0 경로와 채움 경로 모두에서 분기 수가 줄어들어 흐름 추적이 쉬워졌다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L244-L292】
- 효과 범위 정리 분기가 길이 > 0 체크 한 번으로 정리되어, 교집합 이동 시에도 추적해야 할 조건 수가 감소했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L282-L292】

## 수정 적용 여부 판단
- 교집합 길이를 선행 계산해도 `rgblight_indicator_clear_range()`가 동일한 경계 보정을 수행하므로, DMA 버퍼 안전성이 유지된다고 판단했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L244-L292】
- 단일 루프 기반 채움 경로는 기존과 동일한 구간만 색상을 복사하므로, Split 및 타이머 반복 호출에서도 회귀 위험이 없다고 판단했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L270-L285】

**결: 수정 적용.**

# V251014R6 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: V251012R2~V251014R5 누적 변경으로 정리된 Brick60 RGB 인디케이터 버퍼 준비 루틴.
- 목표: 효과 범위 초기화 경로의 분기를 더 줄일 수 있는지, 밝기 0/무교집합 시 전체 효과 범위를 한 번에 정리해도 안전한지, 그리고 교집합 앞/뒤 정리가 단일 계산으로 대체돼도 DMA 준비 및 Split 시나리오에 회귀가 없는지 추가 검증.

## 시나리오별 점검
1. **밝기 0 + 효과 범위 유지**: `should_fill`이 false인 상태에서 효과 범위를 전체 초기화하도록 경로를 단일 호출로 바꿔도, 클리핑 구간 초기화 후 즉시 `rgblight_indicator_clear_range(start, count)`가 실행돼 잔류 색상이 남지 않음을 확인했다. 반복 호출에서도 추가 분기가 없어 오버헤드가 늘지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L246-L274】
2. **무교집합 + Split 전환**: 좌/우 클리핑 범위가 서로 다르게 설정돼 교집합이 사라진 경우에도 동일 경로가 전체 효과 범위를 초기화해, 이전 프레임 색상이 남지 않고 DMA 전송 전에 버퍼가 깨끗하게 유지됨을 검증했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L246-L274】
3. **동적 효과 + 부분 교차**: `should_fill`이 true인 상태에서 교집합 앞/뒤 정리를 `fill_begin`과 `fill_end` 직접 계산으로 대체해도, 선행/후행 구간이 정확히 잘라지고 교집합 구간만 채워진 뒤 DMA 전송에 참여함을 확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L282-L304】
4. **효과 범위 축소 반복**: VIA 매크로가 효과 범위를 계속 줄이는 상황에서도 새 계산이 동일한 값을 재사용해 선행/후행 정리 범위가 즉시 갱신되고, 불필요한 min/max 비교가 사라져 타이머 호출당 분기 수가 감소함을 확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L282-L304】

## 불필요 코드 / 사용 종료된 요소 정리
- 밝기 0 또는 무교집합 시 효과 범위 정리를 단일 호출로 대체해, 앞/뒤 구간을 별도로 계산하던 보조 분기를 제거했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L246-L274】
- 교집합 앞/뒤 정리도 `fill_begin`/`fill_end`를 직접 사용하도록 변경해, 동일 값을 다시 비교하거나 클램프할 필요가 없어졌다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L282-L304】

## 성능 및 오버헤드 검토
- 효과 범위 초기화를 단일 호출로 합쳐, 밝기 0 반복 호출에서 불필요한 분기/비교 연산이 제거되어 인터럽트 경로의 예측 실패 가능성이 줄었다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L246-L274】
- 교집합 앞/뒤 정리 범위를 직접 계산하면서 루프 외부에서 재사용하던 min/max 비교를 없애, 렌더링 루틴의 연산 수가 감소했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L282-L304】

## 제어 흐름 간소화
- `should_fill` false 경로가 전체 효과 범위를 즉시 정리해, 호출 순서를 따라갈 때 분기 추적이 더 쉬워졌다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L246-L274】
- 교집합 앞/뒤 정리가 동일 계산을 공유해, 추가 범위 변화가 생겨도 값 추적이 단순해졌다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L282-L304】

## 수정 적용 여부 판단
- 효과 범위를 한 번에 정리해도 `rgblight_indicator_clear_range()`가 기존과 동일하게 경계 보정을 적용하므로, DMA 버퍼 계약을 깨지 않는다고 판단했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L246-L304】
- 교집합 앞/뒤 정리를 직접 계산해도 기존 초기화 범위를 그대로 재현하므로, Split 타이머 반복 호출에서 회귀 가능성이 없다고 판단했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L282-L304】

**결론: 수정 적용.**

# V251014R5 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: V251012R2~V251014R4 누적 변경으로 정리한 Brick60 RGB 인디케이터 버퍼 준비 루틴.
- 목표: 클리핑/효과 범위를 공통 교집합 계산으로 통합한 뒤에도 밝기 0, 무교집합, 부분 교차 등 실제 호출 시나리오에서 초기화가 중복되거나 누락되지 않는지 점검하고, 추가 분기 제거가 안전한지 보수적으로 판단.

## 시나리오별 점검
1. **밝기 0 + 교차 범위 유지**: `has_intersection`은 유지되지만 `has_brightness`가 꺼진 상태에서 `should_fill`이 false가 되어 전체 클리핑 구간을 한 번만 초기화하고, 효과 범위의 초과 구간만 별도 정리함을 확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L238-L278】
2. **효과 범위가 전부 클리핑 밖**: 교집합이 없으면 `has_intersection`이 false가 되어 즉시 초기화 경로로 빠지고, 클리핑/효과 양쪽 잔여 구간을 각각 정리해 이전 프레임 색상이 남지 않음을 검증했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L243-L278】
3. **클리핑 길이 0 유지 + 효과 범위 잔존**: `clip_count`가 0이어도 효과 범위 앞/뒤 계산이 그대로 작동해 전체 효과 구간을 초기화하고, 새 교집합 계산이 잘못된 시작점을 만들지 않음을 확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L243-L273】
4. **동적 효과 + 부분 교차**: 교집합이 존재하는 경우에만 `should_fill`이 true가 되어 동일한 `fill_begin/fill_end`를 이용해 선행/후행 클리핑 및 효과 구간을 정리하고, DMA 전송 전에 겹치는 영역만 채워지는지 재확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L243-L307】

## 불필요 코드 / 사용 종료된 요소 정리
- `clip_covers_effect` 보조 비교를 제거하고, 공통 교집합 결과를 재사용해 선행/후행 초기화 범위를 계산하도록 정리했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L243-L307】
- 동일 교집합 값을 클리핑/효과 양쪽에 재사용해 조건별로 별도 보정 분기를 유지할 필요가 없어졌다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L243-L307】

## 성능 및 오버헤드 검토
- 교집합 유무를 한 번만 계산해 `should_fill`과 초기화 경로를 동시에 결정해, 호출마다 추가 분기 판단과 비교 연산을 줄였다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L243-L278】
- 밝기 0 또는 무교집합 시에도 동일 경로에서 초기화를 처리해, 루프 진입 전에 바로 반환되어 타이머 반복 호출 시 오버헤드를 최소화한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L248-L278】

## 제어 흐름 간소화
- `fill_begin/fill_end`를 단일 계산으로 공유해, 클리핑과 효과 범위 정리 순서를 한 경로에서 추적할 수 있게 됐다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L243-L307】
- `should_fill` false 경로가 모든 초기화 작업을 담당하도록 통합되어, 별도 보조 플래그 없이도 흐름을 이해하기 쉬워졌다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L245-L278】

## 수정 적용 여부 판단
- 교집합을 공통 계산해도 기존 `rgblight_indicator_clear_range()` 경계 보정이 그대로 유지되어 DMA 전송 범위 계약을 깨지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L243-L307】
- 밝기/교집합 여부에 따라 조기 반환이 일관되게 실행되어, 이전 변경에서 확보한 타이머/인터럽트 오버헤드 완화 효과가 유지된다고 판단했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L248-L309】

**결론: 수정 적용.**

# V251014R4 RGB 인디케이터 추가 리팩토링 검토

## 검토 개요
- 대상: V251012R2~V251014R3까지 누적된 Brick60 RGB 인디케이터 버퍼 준비 루틴.
- 목표: `clip_matches_effect` 보조 분기를 제거해도 선행/후행 정리와 Split 타이머 흐름이 동일하게 동작하는지 다양한 시나리오로 검증하고, 불필요한 조건 분기를 정리할지 보수적으로 판단.

## 시나리오별 점검
1. **정적 인디케이터 + 완전 일치 범위**: 클리핑/효과 범위가 동일한 상태에서 분기 제거 후에도 전/후단 정리가 실행되지 않고 교집합 채움만 수행되는지 확인해 중복 초기화가 다시 발생하지 않음을 검증했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L221-L314】
2. **정적 인디케이터 + 전단 축소**: 효과 범위 시작점이 클리핑 범위 안쪽으로 이동했을 때 단일 경로에서 전단 길이를 계산하고 과거 색상을 정리하는지 확인해, 범위 축소 시에도 잔류 LED가 남지 않음을 확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L252-L259】
3. **정적 인디케이터 + 후단 축소**: 효과 범위 종료점이 짧아진 상황에서 동일 경로가 후단을 정리해 DMA 전송 전에 남은 잔여 색상을 제거하는지 점검했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L261-L266】
4. **교집합 없음 + should_fill 유지**: 밝기가 유지된 상태에서 효과 범위가 전부 클리핑 밖으로 벗어나도 공통 경로가 클리핑 전체와 효과 범위를 함께 초기화해, 이전 프레임의 색상이 그대로 남지 않는지 확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L248-L276】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L296-L309】
5. **밝기 0/비렌더 반복 호출**: `needs_render`가 내려간 뒤 동일 구성을 반복 호출해도 조기 반환 경로가 유지되어 추가 연산이 발생하지 않는지 검증했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L223-L276】
6. **Split 타이머 + 동적 효과 혼재**: 인디케이터가 활성화된 상태에서 `rgblight_set()`과 타이머 태스크가 반복 호출될 때 새 분기가 추가 오버헤드를 만들지 확인했으며, `needs_render`가 꺼지면 기존과 동일하게 기본 루틴이 실행되지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1183-L1228】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1331-L1336】

## 불필요 코드 / 사용 종료된 요소 정리
- `clip_matches_effect` 보조 분기를 제거해 동일 경로에서 전/후단 정리를 처리하도록 통합해, 조건 비교와 분기 비용을 없앴다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L243-L266】
- 분기 제거 이후에도 교집합 밖 구간 초기화는 기존 헬퍼를 그대로 재사용해 추가 보조 함수나 중복 계산을 남기지 않았다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L252-L266】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L296-L309】

## 성능 및 오버헤드 검토
- 불필요한 조건 비교를 제거해 렌더링 시마다 평가하던 보조 플래그가 사라져, 클리핑/효과 범위가 자주 변하는 Split 환경에서 분기 예측 부담을 줄였다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L252-L266】
- `should_fill`이 꺼진 경우에는 기존과 동일하게 조기 반환 경로가 바로 실행되어 추가 루프가 돌지 않음을 재확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L248-L276】
- DMA 전송 직전의 버퍼 준비 경로(`rgblight_set`)는 그대로 유지되어, 분기 제거가 출력 데이터 복사량이나 Split 동기화 비용을 증가시키지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1183-L1228】

## 제어 흐름 간소화
- 선행/후행 정리 로직을 단일 경로로 통합해 범위 조건 변경 시 따로 분기를 추가할 필요가 없어졌고, 추가 최적화 시 확인해야 하는 분기 수를 줄였다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L252-L266】
- 조기 반환→교집합 채움→효과 범위 정리 순서가 일관되게 유지되어 함수 흐름을 단계별로 추적하기 쉬워졌다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L221-L314】

## 수정 적용 여부 판단
- 분기 제거 후에도 전/후단 정리는 동일 헬퍼를 재사용하므로 기존 API 계약이나 범위 보정 로직을 건드리지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L252-L266】
- `needs_render`와 `should_fill` 가드가 그대로 유지되어, 인디케이터가 비활성화된 상태에서는 추가 연산을 수행하지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L223-L276】
- Split 타이머 경로가 기존과 동일하게 인디케이터 활성 시 조기 반환하므로, 인터럽트 경로에 회귀가 발생하지 않는다고 판단했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1331-L1336】

**결론: 수정 적용.**

## 이전 기록 (V251014R3)

### 검토 개요
- 대상: V251012R2~V251014R1에서 정비한 Brick60 RGB 인디케이터 파이프라인.
- 목표: 클리핑 범위보다 넓은 효과 범위가 반복 전달되는 Split/VIA 시나리오에서, 실제로 전송되지 않는 LED 구간까지 버퍼를 채우는 잔여 연산을 제거할 수 있는지 보수적으로 판단하고, 교집합 외 구간의 소등이 지연되는 회귀 가능성을 추가 점검.

### 시나리오별 점검
1. **정적 인디케이터 + 교차 범위 Split 출력**: Split 좌/우 반쪽이 서로 다른 `rgblight_set_clipping_range()`를 유지하고, 효과 범위는 전체 LED 배열을 계속 전달하는 상황을 구성했다. 교차 범위를 계산해 실제 클리핑과 겹치는 구간만 채우도록 조정해, 슬레이브 측 DMA 준비에서 불필요한 버퍼 쓰기가 제거됨을 확인했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L221-L314】
2. **정적 인디케이터 + 클리핑 범위 축소**: 애니메이션이 꺼진 상태에서 클리핑 길이만 줄어드는 시나리오를 반복해, 효과 범위가 더 넓었던 이전 프레임의 잔류 색상이 남지 않는지 확인했다. 교집합 밖 구간을 초기화하도록 후단/전단 정리를 추가해 리그레션을 방지했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L221-L314】
3. **동적 효과 + 범위 변경 감시**: V251014R1에서 추가된 동일 값 무시 로직이 새 교차 범위 계산과 충돌하지 않는지 확인했다. `needs_render`가 켜졌을 때만 교차 연산이 실행되므로, 주기적 호출에서도 불필요한 재렌더가 발생하지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L221-L314】
4. **인디케이터 비활성 + 경계 밖 정리 요청**: 경계 보정 로직이 유지된 상태에서 교차 범위가 0이 되면 채움 루프가 실행되지 않아, 잘못된 입력으로 인해 버퍼가 다시 채워지는 상황이 없다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L148-L170】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L221-L276】
5. **Split 재동기화 + VIA 사용자 매크로**: 초과 범위 입력이나 빈 범위 전송이 들어와도 교차 범위가 0으로 계산되어 버퍼 채움이 스킵되므로, 기존 조기 반환 로직과 함께 안전하게 무시된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L366-L410】
6. **인디케이터 비활성 + 빈 효과 범위 동기화**: 효과 범위가 0이면 `should_fill`이 꺼지고 교차 범위 계산도 실행되지 않아, V251014R1에서 확보한 초기화 경로만 유지된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L221-L276】
7. **인디케이터 활성 + 클리핑 0 구간 유지**: 클리핑 길이가 0이면 교차 범위도 0이 되어 채움 루프가 실행되지 않는다. 기존 0 구간 최적화와 함께 동작해 재렌더 없이 상태가 유지된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L221-L276】
8. **정적 인디케이터 + 레이어/타이머 호출 혼재**: `rgblight_timer_task()`가 인디케이터 활성 상태에서 반복 호출될 때 교차 범위 연산이 추가 부하를 만들지 검증했다. `needs_render`가 내려가면 연산 자체가 실행되지 않아 타이머 경로 오버헤드가 증가하지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L221-L314】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1331-L1340】

### 불필요 코드 / 사용 종료된 요소 정리
- `rgblight_indicator_prepare_buffer()`가 클리핑 범위와 효과 범위의 교집합만 채우도록 조정되어, Split 반대편 구간까지 중복 복사하던 잔여 연산을 제거했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L281-L309】
- 교집합 밖으로 남는 효과 범위를 즉시 초기화해, 애니메이션이 꺼진 상태에서도 과거 프레임의 색상이 잔류하지 않도록 정리했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L296-L309】
- `rgblight_set_clipping_range()`와 `rgblight_set_effect_range()`에서 동일 값 반복 시 조기 반환을 유지해, 불필요한 재렌더 예약을 계속 억제한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L366-L410】
- `rgblight_set_effect_range()`가 전체 LED 개수와 동일한 시작 인덱스를 허용해 빈 범위를 정상 처리하므로, 이전 범위를 즉시 제거한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L390-L410】
- 빈 효과 범위가 유지되는 동안 애니메이션 루프가 0으로 나누는 연산을 수행하지 않도록, 타이머 태스크가 즉시 반환한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1331-L1340】
- `rgblight_set_clipping_range()`가 배열 경계를 벗어나는 요청을 무시해, 실제로 사용할 수 없는 범위를 적용하지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L366-L388】
- `rgblight_indicator_prepare_buffer()`가 `clip_count == 0`인 경우에는 초기화 경로만 실행해 사용하지 않는 색상 복사를 방지한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L221-L276】

### 성능 및 오버헤드 검토
- 교차 범위만 채우도록 변경해 Split 슬레이브가 보유하지 않은 LED에 대한 중복 쓰기가 사라져, DMA 준비 단계의 메모리 접근이 감소한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L281-L309】
- 교집합 밖 구간을 즉시 0으로 정리해, 클리핑 범위 축소 시에도 추가 프레임을 기다리지 않고 잔여 LED를 소등한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L296-L309】
- 범위가 변하지 않은 호출을 무시해 `needs_render`가 불필요하게 켜지지 않아, 인디케이터 활성 상태에서도 타이머 루프가 idle 상태를 유지한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L366-L410】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1331-L1336】
- `rgblight_indicator_clear_range()`에 경계 보정을 유지해, 이상 범위 입력이 들어와도 DMA 전송 전에 버퍼를 초과로 지우지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L148-L170】
- 클리핑 범위가 유효한 경우에만 구조체를 갱신해, 잘못된 입력으로 인한 오버런과 불필요한 재렌더 요청을 동시에 차단한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L366-L388】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L221-L276】
- 빈 효과 범위를 정상 처리하면서 이전 범위를 즉시 무효화해, 인디케이터 비활성 시에도 범위 갱신 루프가 과거 구간을 재검사하지 않는다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L390-L410】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L221-L276】
- 실 출력 구간이 없을 때에는 색상 복사를 건너뛰어, 타이머가 동일 프레임을 반복 계산하지 않고 곧바로 idle 상태로 복귀한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L221-L276】

### 제어 흐름 간소화
- 교집합 계산으로 실제 출력 구간만 복사하므로, 기존 인터페이스를 유지한 채 중복 연산을 제거했다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L281-L309】
- 범위 값이 바뀐 경우에만 상태 플래그를 조정해 외부 분기가 단순해졌다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L366-L410】
- 경계 보정 로직을 헬퍼에 캡슐화해 호출부는 동일 인터페이스로 안전성을 확보한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L148-L170】
- 잘못된 클리핑 범위는 조기 반환으로 정리되어, 이후 흐름에서 별도 보정 코드가 필요 없다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L366-L388】
- 빈 효과 범위도 동일하게 조기 반환 조건을 통과하므로, 호출부가 `start_pos`를 조정하는 별도 보정 코드가 필요 없다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L390-L410】
- 실 LED가 존재하지 않는 경우에는 조기에 `should_fill`을 false로 만들어 초기화 분기만 수행해, 반복문 호출을 피한다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L221-L276】

### 수정 적용 여부 판단
- 교차 범위만 채우도록 변경해도 외부 API의 입력/출력 계약은 유지되며, DMA로 실제 전송되는 영역만 갱신되므로 보수적 변경으로 판단된다.
- 동일 범위 반복 호출을 무시해도 기존 외부 API의 계약은 유지되며, 인디케이터 활성 시 불필요한 `rgblight_set()` 호출만 줄어든다.
- 경계 보정은 잘못된 입력에 대한 방어 로직으로, 정상 시나리오에는 영향을 주지 않으므로 보수적 변경으로 인정 가능.
- 클리핑 범위 유효성 검사는 기존 동작(정상 범위 수용)을 유지하면서 잠재적 오버런만 막는 소극적 방어로 판단된다.
- 빈 효과 범위를 허용하는 조건 조정은 기존 API와 호환되면서도 이전 범위를 즉시 초기화해 리그레션 위험이 없어, 보수적 변경으로 수용 가능.
- `clip_count`가 0인 동안에는 실질적 출력이 없으므로 버퍼를 유지할 필요가 없고, 기존 캐/렌더링 흐름에도 변화를 주지 않아 보수적 변경 범위에 포함된다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L221-L276】

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
