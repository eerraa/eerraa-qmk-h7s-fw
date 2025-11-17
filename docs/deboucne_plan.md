# 디바운스 런타임 설정 계획

## 1. 목표
- DEBOUNCE_TYPE과 디바운스 시간(단일/프리/포스트)을 VIA에서 런타임으로 제어하고 즉시 적용한다.
- 모든 설정은 EEPROM에 저장되며, 런타임 적용이 불가능한 시나리오에서도 재부팅 후 자동 반영을 보장한다.
- VIA JSON은 영어 UI 라벨을 사용하며 `sym_defer_pk`, `sym_eager_pk`, `asym_eager_defer_pk` 각각에 최적화된 구성을 제공한다.

## 2. 펌웨어 구조 변경
1. **디바운스 프로필 관리자**
   - `src/ap/modules/qmk/port`에 `debounce_profile.[ch]`(가칭) 추가.
   - 내용물: `type`, `pre_ms`, `post_ms`, `applied` 플래그, EEPROM 슬롯 포인터, 기본값(`sym_defer_pk`, 5ms).
   - API 예시: `debounce_profile_load()`, `debounce_profile_apply_current()`, `debounce_profile_save(bool force)`.
   - 오류 시 재부팅 필요 여부를 리턴/플래그로 노출.

2. **런타임 디바운스 엔진**
   - `src/ap/modules/qmk/quantum/debounce/` 하위에 `debounce_runtime.c` 추가.
   - 기존 알고리즘 파일을 빌드에 포함시키되, 내부 상수 `DEBOUNCE` 대신 `debounce_runtime_get_config()`로부터 `pre_ms/post_ms`를 취득하도록 수정.
   - `debounce_init()`이 호출될 때 현재 프로필에 맞는 상태 버퍼를 할당하고, VIA에서 변경 시 `debounce_runtime_reinit()`으로 재할당.

3. **EEPROM 통합**
   - `port.h`에 새로운 `EECONFIG_USER_DEBOUNCE` 공간(예: 8바이트) 정의.
   - `eeconfig_init_user_datablock()`에서 기본 프로필을 기록.
   - 저장 커맨드가 오면 `eeconfig_flush_debounce_profile()` 호출.

## 3. VIA 통신 설계
1. **채널/ID 정의**
   - `via.h`에 `id_qmk_key_response` 채널을 추가.
   - 값 ID 예시:
     - `id_qmk_debounce_mode`
     - `id_qmk_debounce_time_single`
     - `id_qmk_debounce_time_pre`
     - `id_qmk_debounce_time_post`
     - `id_qmk_debounce_status`
2. **명령 처리**
   - `port/via_port.c`에서 새 채널 분기 추가 → `debounce_port_via_command()`.
   - `get/set`: 값 검증(1~30ms) 후 `debounce_profile_update_pending()` 호출.
   - `save`: EEPROM 플러시, 필요 시 `command_id`에 성공/실패 코드 기록.

## 4. VIA UI 구성 (영문, dropdown 기반)
> 참고: [VIA Custom UI 문서](https://caniusevia.com/docs/custom_ui)는 `type`으로 `button`, `toggle`, `range`, `dropdown`, `color`, `keycode`만 허용하며 수치 표시는 별도 라벨/description을 지원하지 않음. `range` 컨트롤은 현재 값이 수치로 나타나지 않으므로 1ms 단위 조정에는 `dropdown`이 유일하게 직관적이다.

1. **FEATURE → “KEY RESPONSE” 섹션**
   - **Response Mode** (`type:"dropdown"`, `options`: `["Balanced (both press & release)", 0]`, `["Fast Press (release filtered)", 1]`, `["Advanced (press & release separate)", 2]`; 바인딩: `id_qmk_debounce_mode`).
   - **Balanced showIf (`mode==0`)**
     - `type:"dropdown"`, `label:"Overall Debounce (ms)"`, `options`: `["01 ms", 1]` … `["30 ms", 30]` (1ms 단위 표시).
   - **Fast Press showIf (`mode==1`)**
     - `type:"dropdown"`, `label:"Release Debounce (ms)"`, 동일한 1~30 옵션.
   - **Custom showIf (`mode==2`)**
     - `type:"dropdown"`, `label:"Press Debounce (ms)"`, 1~30 옵션.
     - `type:"dropdown"`, `label:"Release Debounce (ms)"`, 1~30 옵션.
2. **적용/저장 동작**
   - VIA가 `id_custom_set_value`를 보낼 때마다 즉시 완료 가능한 항목이면
     1) `debounce_runtime_reinit()`로 런타임 상태를 갱신하고,
     2) 성공 직후 `EECONFIG_USER_DEBOUNCE`에도 기록한다.
   - 만약 특정 디바운스 모드나 값이 런타임에 재초기화가 불가능(또는 불안정)하다면, 해당 경로 전체를 “저장 후 재부팅” 전략으로 통일한다: EEPROM에 새 값을 먼저 저장하고, 다음 리셋에서만 반영하도록 사용자에게 안내한다.
3. **상태 확인**
   - 런타임 변경이 항상 성공한다면 별도 표시가 필요 없으나, 재부팅 전략을 사용할 경우 `toggle`을 “Reboot Required” 표시용으로 사용해 사용자에게 안내한다 (`set` 요청은 무시).

## 5. 적용 및 검증
1. `matrix.c`에서 `debounce_init()` 호출 직후 현재 프로필을 적용하고, VIA 이벤트 시 `debounce_profile_apply_current()` 호출.
2. 빌드 검증: 지침의 CMake 명령 수행.
3. 변경 이력: 각 수정 파일에 `// VYYMMDDRn ...` 주석 삽입, `src/hw/hw_def.h`의 `_DEF_FIRMWARE_VERSION` 갱신(사용자 버전 번호 확인 필요).

## 6. 추가 확인 사항
- 공식 펌웨어 버전 문자열을 사용자에게 확인 후 반영.
- 기존 EEPROM 구조와 충돌하지 않도록 신규 슬롯 오프셋 재검토.
- 고위험 파일(`sys_port.*`, `hw/driver/*`) 수정 시 집중 리뷰.
