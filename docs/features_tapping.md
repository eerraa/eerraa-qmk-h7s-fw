# TAPPING_TERM 런타임 변경 가이드

## 1. 목적과 범위
- Brick60-H7S에서 VIA를 통해 TAPPING_TERM(탭-홀드 분기 시간)과 관련 옵션(Per­missive Hold, Hold on Other Key Press, Retro Tapping)을 런타임으로 조정하는 기능을 설명합니다.
- 대상 모듈: `src/ap/modules/qmk/port/tapping_term.{c,h}`, `src/ap/modules/qmk/keyboards/era/sirind/brick60/port/via_port.c`, `src/ap/modules/qmk/keyboards/era/sirind/brick60/json/BRICK60-H7S-VIA.JSON`, `src/ap/modules/qmk/port/eeconfig_port.c`, `src/ap/modules/qmk/quantum/action_tapping.c`, `src/ap/modules/qmk/quantum/action.c`, `src/ap/modules/qmk/quantum/process_keycode/*`의 TAPPING_TERM 소비 경로.

## 2. 구성 파일 & 빌드 매크로
| 경로 | 심볼/함수 | 설명 |
| --- | --- | --- |
| `src/ap/modules/qmk/keyboards/era/sirind/brick60/config.h` | `G_TERM_ENABLE`, `TAPPING_TERM_PER_KEY`, `PERMISSIVE_HOLD_PER_KEY`, `HOLD_ON_OTHER_KEY_PRESS_PER_KEY`, `RETRO_TAPPING_PER_KEY` | Brick60 빌드에서 TAPPING_TERM 런타임 제어를 활성화하고 QMK의 per-key 훅을 사용하도록 강제합니다. |
| `src/ap/modules/qmk/qmk.c` | `tapping_term_init()` | QMK 초기화 시 EEPROM을 읽어 런타임 상태로 반영합니다. |
| `src/ap/modules/qmk/port/tapping_term.{c,h}` | `tapping_term_handle_via_command()`, `tapping_term_sync_state_from_storage()` | VIA 명령 처리, 값 정규화, 런타임 상태/EEPROM 저장소 동기화 핵심 로직. |
| `src/ap/modules/qmk/port/port.h` | `EECONFIG_USER_TAPPING_TERM` | USER 데이터 블록 내 TAPPING_TERM 저장 슬롯(오프셋 +52, 12B). |
| `src/ap/modules/qmk/port/eeconfig_port.c` | `tapping_term_storage_apply_defaults()`, `tapping_term_storage_flush()` | EEPROM 초기화 경로(수동/자동 초기화 포함)에서 기본값을 기록하고 즉시 저장합니다. |
| `src/ap/modules/qmk/keyboards/era/sirind/brick60/port/via_port.c` | `via_custom_value_command_kb()` | VIA 커스텀 채널 분기. `id_qmk_tapping`(채널 15)으로 유입된 명령을 `tapping_term_handle_via_command()`로 라우팅합니다. |
| `src/ap/modules/qmk/keyboards/era/sirind/brick60/json/BRICK60-H7S-VIA.JSON` | `TAPPING` 섹션 | VIA UI에 TAPPING_TERM dropdown(100~500ms, 20ms 스텝)과 옵션 토글을 노출합니다. |

## 3. EEPROM 슬롯 & 데이터 구조
| 심볼/크기 | 오프셋 (`EECONFIG_USER_DATABLOCK` 기준) | 필드 | 설명 |
| --- | --- | --- | --- |
| `EECONFIG_USER_TAPPING_TERM` (12B) | +52 | `tapping_term_ms` (uint16), `permissive_hold`, `hold_on_other_key_press`, `retro_tapping` (각 1B), `version` (1B), `signature` (uint32) | 시그니처 `0x50415447`("GTAP"), 버전 1. `_Static_assert`로 크기 12B를 고정합니다. 기본값은 200ms/옵션 OFF. |

- `EECONFIG_DEBOUNCE_HELPER` 매크로로 래핑되어 `eeconfig_init_tapping_term()`, `eeconfig_flush_tapping_term()`, `eeconfig_flag_tapping_term()` 등의 헬퍼를 생성합니다.
- `eeconfig_init_user_datablock()`에서 기본값을 기록 후 즉시 `tapping_term_storage_flush(true)`로 저장하므로 EEPROM 공장 초기화·자동 초기화 시에도 슬롯이 항상 채워집니다.

## 4. VIA UI·명령 매핑
- 채널 ID: `id_qmk_tapping`(15). 커스텀 명령 포맷은 `[command_id, channel_id, value_id, value_data...]`.
- `src/ap/modules/qmk/quantum/via.h` 값 매핑 및 UI 단위:

| Value ID | VIA 단위/범위 | 설명 |
| --- | --- | --- |
| `id_qmk_tapping_global_term` | `value_data[0] × 10ms` (VIA dropdown 10~50 → 100~500ms) | `tapping_term_normalize()`로 100~500ms, 20ms 스텝으로 정규화 후 저장. |
| `id_qmk_tapping_permissive_hold` | 0/1 토글 | Mod-Tap에서 인터럽트 시에도 HOLD로 전환할지 여부. |
| `id_qmk_tapping_hold_on_other_key_press` | 0/1 토글 | 다른 키가 눌리면 즉시 HOLD로 강제할지 여부. |
| `id_qmk_tapping_retro_tapping` | 0/1 토글 | 탭-홀드 릴리즈 시점이 지났을 때 TAP으로 판정할지 여부. |

- `id_custom_set_value` 수신 시 값을 적용하고 곧바로 `tapping_term_get_value()`로 에코 응답합니다. 지속 저장은 `id_custom_save`(VIA Save 버튼)에서 `tapping_term_storage_flush(true)`가 호출될 때 수행됩니다.

## 5. 런타임 흐름
```
qmkInit()
  ↳ eeprom_init()
  ↳ via_hid_init()
  ↳ debounce_profile_init()
  ↳ tapping_term_init() [G_TERM_ENABLE]
      ↳ eeconfig_init_tapping_term()
      ↳ tapping_term_is_storage_valid() 검사
          ↳ 시그니처/버전/범위 불일치 시 기본값 기록 + eeconfig_flush_tapping_term(true)
      ↳ tapping_term_sync_state_from_storage()로 런타임 상태 반영
  ↳ keyboard_setup()/keyboard_init()

VIA 커스텀 명령 수신 (`via_custom_value_command_kb`)
  ↳ channel id_qmk_tapping → tapping_term_handle_via_command()
      ↳ set_value → tapping_term_set_value() → tapping_term_sync_state_from_storage() → 에코
      ↳ get_value → tapping_term_get_value()
      ↳ save     → tapping_term_storage_flush(true)
```
- EEPROM이 지워졌을 때(`eeconfig_init_user_datablock()` 호출)도 `tapping_term_storage_apply_defaults()` → `tapping_term_storage_flush(true)`가 실행되어 동일 흐름으로 재초기화됩니다.

## 6. 정규화·유효성 규칙
- 범위: 100~500ms(`TAPPING_TERM_MIN/MAX_MS`), 스텝: 20ms(`TAPPING_TERM_STEP_MS`). VIA는 10ms 단위 값을 보내지만 `tapping_term_normalize()`가 스텝에 맞게 내림 정규화합니다.
- 불리언 필드는 0/1만 허용. 그 외 값, 시그니처 불일치(`0x50415447`), 버전 불일치(1)가 감지되면 기본값으로 덮어쓴 뒤 dirty 플래그를 세팅합니다.
- 기본값 적용 시 `eeconfig_flag_tapping_term(true)`로 dirty 마크를 남겨 즉시/차후 flush 대상에 포함시킵니다.

## 7. 적용 범위 (TAPPING_TERM이 영향을 주는 경로)
- `src/ap/modules/qmk/port/tapping_term.c`: `get_tapping_term()`, `get_permissive_hold()`, `get_hold_on_other_key_press()`, `get_retro_tapping()`을 재정의해 QMK per-key 훅을 모두 런타임 상태로 반환합니다(현재는 키 구분 없이 글로벌 값).
- `src/ap/modules/qmk/quantum/action_tapping.c`: `GET_TAPPING_TERM`과 위 훅들을 사용해 Mod-Tap/Layer-Tap의 탭·홀드 판정을 수행합니다.
- `src/ap/modules/qmk/quantum/action.c`: `get_hold_on_other_key_press()`로 인터럽트 시 HOLD 강제, `get_retro_tapping()`로 Retro Tapping 처리에 관여합니다.
- `src/ap/modules/qmk/quantum/process_keycode/process_auto_shift.c`: Retro Shift 처리에서 `get_hold_on_other_key_press()`/`get_retro_tapping()`을 사용해 롤/인터럽트 시나리오를 분기합니다.
- `src/ap/modules/qmk/quantum/process_keycode/process_tap_dance.c`, `process_caps_word.c`, `process_space_cadet.c` 등: `GET_TAPPING_TERM`을 공통 타이머로 사용합니다.
- `src/ap/modules/qmk/quantum/process_keycode/process_combo.h`: `COMBO_HOLD_TERM`이 `TAPPING_TERM`에 묶여 있어 콤보 홀드 임계값도 함께 변합니다.
- `src/ap/modules/qmk/quantum/pointing_device/pointing_device_auto_mouse.h`: AUTO_MOUSE_DELAY가 `GET_TAPPING_TERM` 기반이므로 활성화 시 포인팅 지연에도 영향을 줍니다.
- `DYNAMIC_TAPPING_TERM_ENABLE`는 현재 비활성화되어 QMK 기본 동적 증감 키코드는 사용하지 않으며, 모든 변화는 VIA 채널을 통해서만 주입됩니다.

## 8. 초기화·리셋 시나리오
- `eeconfig_init()`, VIA EEPROM 리셋, `AUTO_FACTORY_RESET` 후 재부팅 시 `eeconfig_init_user_datablock()`이 호출되어 TAPPING_TERM 슬롯을 기본값(200ms/옵션 OFF)으로 다시 채우고 바로 flush합니다.
- 시그니처/버전이 바뀌거나 범위를 벗어난 값이 저장되어도 `tapping_term_init()` 단계에서 기본값으로 복원하고 저장소를 dirty 표시하므로, 다음 `id_custom_save` 시점에 정상화된 값이 기록됩니다.

## 9. 사용 방법
1. VIA `TAPPING` 섹션에서 Global Tapping Term(100~500ms, 20ms 스텝)과 각 옵션 토글을 변경합니다. 설정 즉시 런타임에 반영됩니다.
2. 값을 유지하려면 VIA의 Save(또는 `id_custom_save`)를 눌러 EEPROM에 기록합니다. Save를 누르지 않으면 재부팅 시 이전 저장 값으로 롤백됩니다.
3. EEPROM 초기화/펌웨어 업데이트 후에는 기본값(200ms)으로 돌아가므로 필요 시 다시 설정합니다.

## 10. 운영 체크 포인트
- 정규화 로직이 20ms 스텝으로 내림 적용되므로 130ms와 같이 스텝에 맞지 않는 값은 120ms로 저장됩니다. VIA 에코(SET 후 GET)로 수신 값을 확인하십시오.
- `id_custom_set_value`/`get_value` 패킷 길이가 4바이트 미만이면 `id_unhandled`로 응답하므로 클라이언트는 표준 32바이트 RAW HID 패킷을 유지해야 합니다.
- 별도 로그가 없으므로 값 확인은 VIA UI 재조회 또는 RAW HID 응답을 통해 수행합니다. EEPROM dirty 플래그는 Save 이후에만 클리어됩니다.
