# PC 연결 후 정지 원인 추정 (V251114R4 → V251115R2)

## 변경 요약
- VIA 런타임 디바운스 프로필 추가 및 EEPROM 슬롯 확장 (`src/ap/modules/qmk/qmk.c:16-31`, `src/ap/modules/qmk/port/debounce_profile.c`, `src/ap/modules/qmk/port/port.h:12-19`).
- 디바운스 엔진을 런타임 전환형으로 교체하여 알고리즘별로 동적 할당/해제를 수행 (`src/ap/modules/qmk/quantum/debounce_runtime.c:93-238`, `src/ap/modules/qmk/quantum/debounce/sym_eager_pk.c:49-101`, `.../sym_defer_pk.c:52-98`, `.../asym_eager_defer_pk.c:53-149`).
- VIA 커스텀 채널에 Key Response(ID 14) 추가 및 관련 JSON 정리 (`src/ap/modules/qmk/quantum/via.h:117-166`, `src/ap/modules/qmk/keyboards/era/sirind/brick60/port/via_port.c:26-35`).

## 정지 가능 원인 (가능성 순)
1. **런타임 디바운스 재초기화 실패 후 NULL 버퍼 접근** — *가능성 보류*  
   `debounce_runtime_apply_if_possible()`가 할당 실패를 기록해도 `debounce()`는 `pending_reinit`/`last_error`를 확인하지 않고 바로 `algo->run()`을 호출함 (`src/ap/modules/qmk/quantum/debounce_runtime.c:164-175`, `227-233`). 이전 버퍼는 이미 `free()`된 상태라 각 알고리즘의 실행부에서 `debounce_counters`가 NULL일 때 즉시 HardFault → USB/RGB 모두 정지 가능.  
   - 추가 조건: 부팅 이후 VIA로 프로필을 바꾸지 않아도, 초기 malloc 실패나 힙 손상으로 `debounce_counters`가 NULL이 될 때만 발생. 사용자가 언급한 “부팅 이후 설정 미변경, 정지 전까지 키 입력 정상” 조건에서는 재초기화 트리거가 거의 없어 우선순위를 낮춰야 하나, 힙 단편화/다른 모듈의 free 오염 가능성은 남음.
2. **VIA 슬라이더 조작 시 반복 malloc/free로 힙 단편화**  
   프로필 변경마다 기존 버퍼를 해제 후 새로 malloc하며 (`src/ap/modules/qmk/quantum/debounce/sym_eager_pk.c:49-62`, `.../sym_defer_pk.c:52-65`, `.../asym_eager_defer_pk.c:53-69`), VIA 채널은 연속 set_value를 제한하지 않음 (`src/ap/modules/qmk/keyboards/era/sirind/brick60/port/via_port.c:26-35`). 짧은 시간에 많은 재할당이 일어나면 힙 단편화 또는 OOM이 발생해 1번 경로로 HardFault가 유발될 가능성.
3. **보드 기본 설정과 다른 디바운스 초기값 사용**  
   CMake에서 `DEBOUNCE_TYPE` 자동 추출을 제거하고 런타임 기본값을 `SYM_DEFER_PK`(5/5ms)로 고정 (`src/ap/modules/qmk/CMakeLists.txt:1-66`, `src/ap/modules/qmk/quantum/debounce_runtime.c:79-85`). 보드 설정(`config.h`의 `DEBOUNCE_TYPE sym_eager_pk`)이 무시되어 스캔 타이밍이 변경되며, 8000Hz USB 스케줄 여유가 줄어 다른 모듈(USB 모니터 등)의 워치독 타이밍을 압박할 수 있음.

## 개선 제안
- `debounce()` 내에서 `debounce_runtime_is_ready()`를 검사하고 미적용/오류 상태에서는 안전 경로(현재 cooked 유지)로 빠지는 방어 로직 추가.
- VIA set_value에 변경 주기 제한 또는 동일 값 무시를 도입하고, 할당 실패 시 기존 설정으로 롤백하면서 오류를 사용자에게 표시.
- 런타임 기본값을 보드 설정과 일치시키거나, 초기 적용 시점에 로그/경고를 남겨 변경된 디바운스가 8000Hz 스케줄에 미치는 영향 점검.
