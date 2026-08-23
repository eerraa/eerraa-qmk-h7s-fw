# 마우스 키(MOUSE) VIA 튜닝 가이드 (V260823R1)

## 1. 목적과 범위
- QMK 마우스 키 엔진(`quantum/mousekey.c`)을 켜고, 그 런타임 변수를 VIA 페이지에서 조정·보존한다.
- 전 보드 공통. `MOUSEKEY_ENABLE` / `MOUSE_ENABLE` / `MOUSE_SHARED_EP` / `ERA_MOUSEKEY_RUNTIME_DELTA`는 `src/ap/modules/qmk/CMakeLists.txt`에서 전역 정의한다.
- 참조 구현: custom QMK `keyboards/era/common/features/era_mousekey.c` (실측 기본값의 출처도 동일).

## 2. 구성 파일
| 경로 | 심볼/함수 | 설명 |
| --- | --- | --- |
| `src/ap/modules/qmk/port/mousekey_config.{c,h}` | `mousekey_config_init()`, `mousekey_config_handle_via_command()` | EEPROM 이미지 보관, 단위 환산, VIA 명령 처리. |
| `src/ap/modules/qmk/port/port.h` | `EECONFIG_USER_MOUSEKEY` | USER 데이터 블록 오프셋 +152, 16B. |
| `src/ap/modules/qmk/qmk.c` | `mousekey_config_init()` | `keyboard_init()` **이전**에 호출해 `mk_*` 변수를 먼저 세운다. |
| `src/ap/modules/qmk/port/eeconfig_port.c` | `mousekey_config_storage_apply_defaults()` | EEPROM 초기화 경로에서 기본값 기록. |
| `src/ap/modules/qmk/quantum/mousekey.{c,h}` | `mk_move_delta`, `mk_wheel_delta` | 업스트림에서 매크로인 두 값을 `ERA_MOUSEKEY_RUNTIME_DELTA` 아래에서만 변수로 승격. |
| 각 보드 `port/via_port.c` | `via_custom_value_command_kb()` | 채널 17을 `mousekey_config_handle_via_command()`로 라우팅. |
| 각 보드 VIA JSON | `FEATURE → MOUSE` | 6개 컨트롤 노출. |

마우스 보고는 EXK 인터페이스의 리포트 ID 2(6바이트)로 나간다. 리포트가 6바이트라 **기존 8바이트 엔드포인트에 그대로 들어가므로 엔드포인트 크기와 FIFO 배분은 변경되지 않았다.** 인터페이스 구성 전반은 `docs/features_usb_hid_reports.md`.

## 3. 이 유닛은 런타임 상태를 소유하지 않는다
QMK가 이 페이지가 만지는 값을 전부 쓰기 가능한 전역 변수로 이미 들고 있다. 그래서 기능 전체가 **보존된 이미지 하나 + 그 변수들로의 대입**이다. `tapping_term.c`가 `get_*` 훅 오버라이드를 통해 엔진에 값을 전달하느라 별도 상태 구조체를 드는 것과 다르다. 여기에는 걸 훅이 없다.

`mousekey_config_apply_runtime()`은 init·EEPROM 초기화·VIA set 세 시점에서만 실행되며, 마우스 키 처리 패스 안에서는 아무것도 읽지 않는다. 즉 **스캔 비용을 늘리지 않는다.**

## 4. EEPROM 슬롯
| 오프셋 | 크기 | 필드 |
| --- | --- | --- |
| +152 | 16B | `delay`, `interval`, `max_speed`, `time_to_max`, `wheel_delay`, `wheel_interval`, `wheel_max_speed`, `wheel_time_to_max`, `move_delta`, `wheel_delta`, `version`, `reserved`, `signature`(uint32) |

- 시그니처 `0x4B53554D`("MUSK"), 버전 1. `_Static_assert`로 16B 고정.
- 페이지가 6개만 보여도 raw 10개를 전부 저장한다. 나중에 나머지를 여는 것이 마이그레이션이 아니라 정의 변경이 되도록 하기 위해서다.
- 유효성 판정은 **시그니처/버전만** 본다. 범위는 `mousekey_config_normalize()`가 필드별로 클램프한다. 열 개의 독립된 손잡이라 한 필드가 범위를 벗어나도 나머지 아홉을 버릴 이유가 없다.

## 5. VIA 매핑 (채널 17)
참조 QMK는 채널 13을 쓰지만, 이 코드베이스에서 13은 USB POLLING(BootMode/모니터)이 이미 점유하고 있다. value id 1~6 배치는 참조와 동일하다.

| Value ID | 라벨 | 단위 | 엔진 값 |
| --- | --- | --- | --- |
| 1 | Cursor Start Speed | px (1~127) | `mk_move_delta` |
| 2 | Cursor Top Speed | px (1~127) | `mk_move_delta × mk_max_speed` (파생) |
| 3 | Cursor Acceleration | 50ms 단위 (0 = off) | `mk_time_to_max` (파생) |
| 4 | Cursor Steps Per Second | ms (1~255) | `mk_interval` |
| 5 | Wheel Rate | ms (1~255) | `mk_wheel_interval` |
| 6 | Wheel Acceleration | 0/1/2 | `mk_wheel_max_speed` + `mk_wheel_time_to_max` (쌍) |

`id_custom_set_value`는 **클램프된 실제 값**을 곧바로 에코한다. set이 값을 자를 수 있고 VIA는 되돌아온 값을 그리므로, 에코를 빼면 사용자가 요청한 값이 화면에 남고 펌웨어가 든 값과 어긋난다.

## 6. 단위 환산에서 실제로 어려운 세 곳
### 6-1. 최고 속도는 저장되지 않는다
엔진이 드는 것은 (스텝, 배수) 쌍이다. 한 최고 속도를 만드는 (스텝, 배수) 조합이 여러 개라 raw 쌍은 서로 다투는 컨트롤이 된다. 그래서 페이지는 사용자가 볼 수 있는 두 속도(첫 스텝, 최고 스텝)를 px로 제시하고, 배수는 파생값으로 둔다.
- Start Speed를 바꿀 때 최고 속도를 붙잡아 둔다(`era_mousekey`와 동일). 그러지 않으면 첫 스텝을 바꿀 때 최고 속도가 끌려간다.
- 비율 계산은 **내림이 아니라 반올림**이다. 스텝 8에서 상한 127을 내림하면 비율 15 → 120으로 되읽히지만, 반올림하면 16이 되고 엔진의 자체 클램프가 정확히 127로 되돌려준다.

### 6-2. 가속은 페이지에서 시간, 엔진에서 이벤트 수
`mk_time_to_max`는 이동 이벤트 수를 센다. 그대로 통과시키면 갱신 주기를 올릴 때 건드리지 않은 램프가 절반으로 줄어든다. 그래서 지속시간으로 들고, `time_to_max × interval`로 파생시킨다. Steps Per Second를 바꿀 때는 램프를 먼저 읽고 뒤에 다시 세운다.

표시 단위 50ms는 해상도가 아니라 **왕복 정확성** 때문이다. 이벤트 수로 내렸다가 다시 시간으로 올릴 때 반올림 오차로 값이 어긋나지 않는 단위가 50ms다(33ms는 어긋나고, 20ms·10ms도 일부 조합에서 어긋난다). 아래의 클램프는 이것과 별개의 제약이다.

도달 가능한 범위는 `mk_time_to_max`가 1바이트라는 사실이 정하고, **갱신 주기가 빨라질수록 좁아진다.**

| Steps Per Second | 주기 | 램프 상한(255 이벤트) | JSON이 제시하지만 닿지 않는 값 |
| --- | --- | --- | --- |
| 200 /s | 5 ms | 1.275 s | 1.5 s, 2.0 s |
| 125 /s | 8 ms | 2.04 s | 없음 |
| 100 /s | 10 ms | 2.55 s | 없음 |
| 63 /s | 16 ms | 4.08 s | 없음 |
| 50 /s | 20 ms | 5.1 s | 없음 |

**200 /s에서 1.5 s·2.0 s를 고르면 조용히 1.275 s로 잘린다.** 참조 QMK도 같은 옵션 목록을 쓰고 같은 동작을 하며(그쪽 `era_mousekey.c` 주석에 기록되어 있다), 옵션 목록을 주기별로 바꾸는 것은 VIA 정의 형식으로 표현할 수 없다. 그래서 두 목록을 그대로 두고 **되읽기가 정직한 것**으로 계약을 맞췄다 — VIA를 새로고침하면 실제로 든 짧은 램프가 표시된다. 호스트 테스트가 이 두 성질(담기는 조합은 정확히 왕복, 담기지 않는 조합은 요청보다 짧은 값을 반환)을 검증한다.

### 6-3. 가속 Off는 "최고 속도 고정"이 아니라 "첫 스텝 고정"
`mk_time_to_max`가 0이면 엔진의 `mousekey_repeat >= mk_time_to_max` 판정이 첫 반복부터 참이라, `mk_max_speed`가 키다운 이후 모든 이벤트의 스텝 크기가 된다. 여기에 **최고** 속도를 넣으면 쓸 수 없다 — 기본 50 이벤트/초에서 32px는 초당 1600px이고, 호스트의 포인터 가속까지 곱해지면 한 번에 화면 절반을 건넌다.

그래서 런타임 배수는 저장되지 않고 파생된다. off일 때 1을 넣으면 `move_unit()`의 모든 분기가 `mk_move_delta` 하나로 접힌다. 저장된 `max_speed`는 건드리지 않으므로 램프를 다시 고르면 사용자가 고른 비율이 그대로 돌아온다. 휠은 원래 이렇게 동작했다(off 단계가 `{1, 0}`). 커서만 예외였다.

## 7. 기본값
| 항목 | 값 | 근거 |
| --- | --- | --- |
| Steps Per Second | 100 /s (10ms) | 참조 QMK 2026-08-18 실측 |
| Start Speed | 4 px (초당 400 카운트) | 동일 |
| Top Speed | 16 px (초당 1600 카운트) | 동일. 5120폭에서 1.6초, 1920폭에서 1.2초에 횡단 |
| Acceleration | 1.0초 | 0.6초는 빠르게, 2.0초는 느리게 읽혔다 |
| Wheel Rate / Acceleration | `MOUSEKEY_WHEEL_INTERVAL` / Strong | 업스트림 값 |

업스트림 기본값(스텝 8px, 50 이벤트/초, 30 이벤트만에 80px 도달)은 당시 디스플레이 기준이라 현대 해상도에서 너무 급하고 빠르다. 최고 속도는 목표 범위의 중간에 맞췄다 — 조금 느린 기본값은 조준으로 만회되지만 조금 빠른 기본값은 만회되지 않는다.

## 8. 트러블슈팅
| 증상 | 확인 |
| --- | --- |
| 커서가 전혀 안 움직인다 | 키맵에 Mouse 키코드를 배치했는지. VIA KEYMAP 탭의 기본 키코드 목록에 있다(별도 정의 옵트인 불필요). |
| 가속 Off인데 너무 빠르다 | `mousekey_config_apply_runtime()`이 `time_to_max == 0`에서 `mk_max_speed = 1`을 넣는지 확인. |
| Top Speed가 고른 값으로 되읽히지 않는다 | Start Speed가 12px 같은 값이면 일부 목표가 도달 불가다. 되읽기는 이때 정직하게 실제 값을 보고한다. |
| 램프가 주기를 바꿀 때 같이 변한다 | `id_qmk_mousekey_cursor_interval` set 경로에서 램프 읽기/재설정이 빠졌다. |
| 가속을 2.0초로 골랐는데 1.3초로 표시된다 | Steps Per Second가 200 /s(5ms)면 램프 상한이 1.275초다. §6-2 표 참조. 주기를 낮추면 도달한다. |
| 값이 재부팅 후 사라진다 | VIA에서 SAVE를 눌러야 `id_custom_save` → `eeconfig_flush_mousekey_cfg(true)`가 실행된다. |
