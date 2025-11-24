# Tap Dance (VIA/Vial 호환) 가이드

## 1. 목적과 범위
- Brick60-H7S에서 VIA UI를 통해 8개 Tap Dance 슬롯의 액션과 슬롯별 Term을 설정하고, EEPROM에 영구 저장하는 방법을 설명합니다.
- 대상 모듈: `src/ap/modules/qmk/port/tapdance.{c,h}`, `src/ap/modules/qmk/qmk.c`, `src/ap/modules/qmk/quantum/process_keycode/process_tap_dance.c`, `src/ap/modules/qmk/keyboards/era/sirind/brick60/json/BRICK60-H7S-VIA.JSON`

## 2. 구성 파일 & 빌드 매크로
| 경로 | 심볼/설정 | 설명 |
| --- | --- | --- |
| `src/ap/modules/qmk/keyboards/era/sirind/brick60/config.h` | `TAPDANCE_ENABLE`, `TAP_DANCE_ENABLE` | Tap Dance 런타임 설정/VIA 연동 활성화. |
| `src/ap/modules/qmk/CMakeLists.txt` | `TAPDANCE_ENABLE` 감지 시 `process_tap_dance.c` 포함 | 빌드 타임에 Tap Dance 소스 포함. |
| `src/ap/modules/qmk/port/tapdance.{c,h}` | `tapdance_init()`, `tapdance_handle_via_command()` | VIA 채널 16 처리, EEPROM 저장/로드, 상태머신(Vial 호환) 구현. |
| `src/ap/modules/qmk/qmk.c` | `process_record_kb()` | `QK_KB_0~7` 커스텀 키코드를 `TD(0)~TD(7)`으로 치환. |
| `src/hw/hw_def.h` | `_DEF_FIRMWARE_VERSION` | 현재 버전 문자열: `V251124R8`. |

## 3. EEPROM 슬롯 & 데이터 구조
| 심볼/크기 | 오프셋 (`EECONFIG_USER_DATABLOCK` 기준) | 필드 | 설명 |
| --- | --- | --- | --- |
| `EECONFIG_USER_TAPDANCE` (88B) | +64 | `slots[8]`: 각 슬롯 `actions[4]`(uint16, tap/hold/dtap/tap+hold), `term_ms`(uint16), `version`(1B), `signature`(uint32) | 시그니처 `0x4E414454`("TDAN"), 버전 1. 기본값: 액션 KC_NO, term 200ms. |

- `EECONFIG_DEBOUNCE_HELPER`로 래핑되어 `eeconfig_init_tapdance()`, `eeconfig_flush_tapdance()` 등이 생성됩니다.
- USER EEPROM 초기화 시 `tapdance_storage_apply_defaults()` → `tapdance_storage_flush(true)`로 기본값이 기록됩니다.

## 4. VIA UI·명령 매핑
- 채널 ID: `id_qmk_tapdance`(16).
- Value ID: 슬롯당 5개, `(16, base+0..4)` = tap, hold, double tap, tap+hold, term. 슬롯 1의 base=1 → 1~5, 슬롯 8의 base=36 → 36~40.
- Term 단위: VIA dropdown 10~50 → 실제 ms = 값×10 (100~500ms). 내부에서 최소/최대/10ms 단위로 정규화.
- `id_custom_set_value`: 값 적용 후 즉시 에코(`tapdance_get_value`)로 응답. `id_custom_save`: EEPROM flush.

### VIA JSON (VIA3 `customKeycodes`)
- 경로: `src/ap/modules/qmk/keyboards/era/sirind/brick60/json/BRICK60-H7S-VIA.JSON`
- `customKeycodes`에 TD0~TD7 정의. VIA는 배열 순서대로 `QK_KB_0~7`을 전송하며, 펌웨어에서 `TD(0)~TD(7)`으로 치환합니다.
- 키맵에 TD0~TD7을 배치하면 슬롯 1~8 설정과 연동됩니다(슬롯 번호는 UI 표기, 인덱스는 0~7).

## 5. 런타임 흐름
```
qmkInit()
  ↳ eeprom_init()
  ↳ via_hid_init()
  ↳ tapdance_init() [TAPDANCE_ENABLE]
      ↳ eeconfig_init_tapdance()
      ↳ 시그니처/버전/범위 검사 → 손상 시 기본값 기록 후 flush
      ↳ 저장소 → 런타임 상태 동기화

via_custom_value_command_kb() (채널 16)
  ↳ tapdance_handle_via_command()
      set_value → 저장소 갱신 → 상태 정규화 → 에코
      get_value → 현재 값 응답
      save     → eeconfig_flush_tapdance(true)

process_record_kb()
  ↳ QK_KB_0~7 → TD(0)~TD(7) 치환 → 이후 Tap Dance 표준 경로

process_tap_dance.c
  ↳ tapdance_get_term_ms()로 슬롯 term 적용 (글로벌 TAPPING_TERM과 독립)
  ↳ tap_dance_task()에서 슬롯 term 기반 타임아웃 처리
```

## 6. 상태머신(Vial 호환)
- 카운트/인터럽트/pressed 상태를 조합해 아래 단계로 분기:
  - `SINGLE_TAP`: tap 전송
  - `SINGLE_HOLD`: hold 없을 때 tap로 대체 후 press 유지
  - `DOUBLE_TAP`: double tap 지정 없으면 tap 두 번 + press 유지
  - `DOUBLE_HOLD`: tap+hold 없으면 tap 전송 후 hold(없으면 tap) press
  - `DOUBLE_SINGLE_TAP`: tap 전송 후 press 유지
  - `MORE_TAPS`: 추가 처리 없음
- on_each_tap: 3연타 시 tap×3, 4회 이상은 tap 반복 전송.
- reset 시 TAP_CODE_DELAY 만큼 지연 후 press된 코드 unregister.

## 7. Term 동작
- 슬롯별 term만 사용하며 글로벌 `TAPPING_TERM`/`g_tapping_term`과 완전히 독립.
- 범위: 100~500ms, 20ms 스텝 정규화. 범위 밖 값/손상 데이터는 200ms로 복원.

## 8. 키코드/매핑 요약
- VIA TD0~TD7 → `QK_KB_0`~`QK_KB_7` → `TD(0)`~`TD(7)` (process_record_kb 치환).
- 실제 Tap Dance 실행은 `tap_dance_actions[0..7]`가 담당하며, 슬롯별 액션/term은 EEPROM에서 로드.

## 9. 초기화·리셋 시나리오
- EEPROM 공장 초기화(`eeconfig_init_user_datablock`) 시 Tap Dance 슬롯을 기본값(액션 없음/term 200ms)으로 채우고 즉시 flush.
- 시그니처/버전 불일치나 범위 밖 term 발견 시 init 단계에서 기본값으로 복원 후 dirty 플래그를 남겨 저장소를 정규화.

## 10. 운영 체크 포인트
- VIA Save 전에는 RAM만 갱신되므로, 영구 저장이 필요하면 반드시 VIA의 Save(또는 id_custom_save)를 실행.
- UI에서 term을 10~50 범위 밖으로 보내더라도 내부 정규화로 100~500ms 범위에 맞춰 저장/반영.
- Tap Dance 동작은 Term별 슬롯 타이머를 사용하므로 글로벌 tapping term 변경과 무관.

## 11. 확장/포팅 시 유의사항
- TAPDANCE_ENABLE 매크로를 키보드별로 명시해야 기능과 VIA 라우팅이 활성화됨.
- `customKeycodes` 순서를 바꾸면 QK_KB_n 매핑이 달라지므로, 순서 유지 또는 `process_record_kb` 치환 로직을 함께 조정할 것.
- 타 보드로 이식 시 `config.h`에 TAPDANCE_ENABLE 추가, VIA JSON에 TD 항목 삽입, `EECONFIG_USER_TAPDANCE` 오프셋 확보가 필요.
