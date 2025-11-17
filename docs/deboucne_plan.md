# 디바운스 런타임 설정 계획

## 1. 목표
- DEBOUNCE_TYPE과 디바운스 시간(단일/프리/포스트)을 VIA에서 런타임으로 제어하고 즉시 적용한다.
- 모든 설정은 EEPROM에 저장되며, 런타임 적용이 불가능한 시나리오에서도 재부팅 후 자동 반영을 보장한다.
- VIA JSON은 영어 UI 라벨을 사용하며 `sym_defer_pk`, `sym_eager_pk`, `asym_eager_defer_pk` 각각에 최적화된 구성을 제공한다.

## 2. 펌웨어 구조 변경
1. **디바운스 프로필 관리자**
   - `src/ap/modules/qmk/port`에 `debounce_profile.[ch]` 추가 완료.
   - 구조체는 `type`, `pre_ms`, `post_ms`, 적용 상태 플래그, EEPROM 시그니처를 보관하며 기본값은 `sym_defer_pk` + 5ms.
   - 공개 API: `debounce_profile_init()/apply_current()/set_*()/save()` 등. `set` 계열 함수는 즉시 런타임을 재초기화하고, `id_custom_save` 명령에서만 EEPROM을 강제로 플러시한다.

2. **런타임 디바운스 엔진**
   - `src/ap/modules/qmk/quantum` 하위에 `debounce_runtime.c` 신설, 알고리즘별 init/run/free 함수를 등록해 런타임에 동적으로 교체한다.
   - `sym_*`/`asym_*` 알고리즘은 `debounce_runtime_press_delay()/release_delay()`를 통해 pre/post 값을 취득하도록 패치되었다.
   - `debounce_init()`이 호출되면 현재 프로필 기준으로 버퍼를 확보하고, 이후 프로필이 갱신될 때마다 동일 엔진을 통해 재초기화된다.

3. **EEPROM 통합**
   - `port.h`에 `EECONFIG_USER_DEBOUNCE`(오프셋 +44, 8B) 추가.
   - `eeconfig_init_user_datablock()`에서 디폴트 프로필을 기록하고 즉시 flush, 이후 `debounce_profile_save(true)`로 동기화한다.

## 3. VIA 통신 설계
1. **채널/ID 정의**
   - `via.h`에 `id_qmk_key_response` 채널 추가.
   - 사용 ID: `id_qmk_debounce_mode`, `id_qmk_debounce_time_single`, `id_qmk_debounce_time_pre`, `id_qmk_debounce_time_post`. (런타임 상태 토글은 최종 UI에서 제외)
2. **명령 처리**
   - `via_port.c`에서 채널 분기를 추가해 `debounce_profile_handle_via_command()`에 위임.
   - `set` 명령은 1~30ms 범위를 검증한 뒤 즉시 런타임 적용 및 EEPROM dirty 플래그만 세팅한다.
   - `save` 명령(`id_custom_save`)에서만 `debounce_profile_save(true)`를 호출해 EEPROM에 영구 저장한다.

## 4. VIA UI 구성 (영문, dropdown 기반)
> 참고: [VIA Custom UI 문서](https://caniusevia.com/docs/custom_ui)는 `type`으로 `button`, `toggle`, `range`, `dropdown`, `color`, `keycode`만 허용한다. 수치 표시가 필요한 컨트롤은 dropdown을 사용해 1ms 단위로 명시한다.

1. **FEATURE → “KEY RESPONSE” 섹션**
   - **Response Mode** (`type:"dropdown"`, 옵션: `["Balanced · same delay on press & release", 0]`, `["Fast Press · instant press / delayed release", 1]`, `["Advanced · separate press cooldown / release delay", 2]` → `id_qmk_debounce_mode`).
   - **Balanced (mode==0)** : `label:"Hold-off (Press & Release, ms)"`, 1~30ms dropdown → `id_qmk_debounce_time_single`.
   - **Fast Press (mode==1)** : `label:"Release Hold-off (ms)"`, 1~30ms dropdown → `id_qmk_debounce_time_post`.
   - **Advanced (mode==2)** :
     - `label:"Press Cooldown (ms, 0 = instant)"`, 1~30ms dropdown → `id_qmk_debounce_time_pre`.
     - `label:"Release Delay (ms)"`, 1~30ms dropdown → `id_qmk_debounce_time_post`.
2. **UI 주의사항**
   - 모드별 레이블에 실제 동작을 서술해 사용자가 press/release 지연 개념을 바로 이해하도록 한다.
   - 읽기 전용 상태 토글은 제거되었으며, 상태 안내는 README 또는 모드 설명으로 대체한다.

## 5. 적용 및 검증
1. `matrix.c`의 `matrix_init()`에서 `debounce_init()` 후 `debounce_profile_apply_current()`를 호출해 부팅 직후 현재 프로필을 반영한다.
2. QMK 초기화(`qmkInit`) 선행 단계에서 `debounce_profile_init()`을 호출해 EEPROM 값을 로드하고 부트 로그에 현재 모드/지연을 출력한다.
3. 빌드 검증: `cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/sirind/brick60'` → `cmake --build build -j10`.

## 6. 추가 확인 사항
- 펌웨어 버전 문자열 `_DEF_FIRMWARE_VERSION`를 작업 릴리스(`V251115R1`)에 맞게 유지한다.
- EEPROM 오프셋이 다른 사용자 데이터와 겹치지 않도록 `EECONFIG_USER_DATABLOCK` 맵을 계속 관리한다.
- 런타임 디바운스 엔진은 `sys_port.*`, `hw/driver/*`처럼 고위험 파일을 건드리지 않았지만, 이후 패치 시 동일 가이드를 준수한다.
