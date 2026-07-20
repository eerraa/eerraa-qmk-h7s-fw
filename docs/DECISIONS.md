# 결정 기록

본 문서는 코드만으로는 알 수 없는 판단 근거를 남깁니다. 세션 간 인수인계용이며,
Claude auto-memory는 Codex CLI와 공유되지 않으므로 공용 사실은 반드시 여기에 기록합니다.

---

## 2026-07-20 — MAY65 underglow 기본값 OFF 및 버전 상향 (V260720R1)

**결정**: `era/keynetix/may65`의 `RGBLIGHT_DEFAULT_ON`을 `false`로 변경하고,
`_DEF_FIRMWARE_VERSION`을 `V260701R2` → `V260720R1`로 상향한다.

**버전 상향이 필수인 이유**: `RGBLIGHT_DEFAULT_ON`은 펌웨어 전체에서
`eeconfig_update_rgblight_default()` 한 곳에서만 소비되고
(`quantum/rgblight/rgblight.c:885`), 이 함수는 `rgblight_init()`의
`if (!rgblight_config.mode)` 가드 안에서만 호출된다(`rgblight.c:918-921`).
즉 **EEPROM 팩토리 초기화 시 1회만** 기록되는 공장 초기값이지 매 부팅 정책이 아니다.
`AUTO_FACTORY_RESET_ENABLE 1`(may65 `config.h:21`)의 리셋 쿠키가
`_DEF_FIRMWARE_VERSION`에서 파생되므로(`hw_def.h:50-57`), 버전을 올리지 않으면
기존 기기의 EEPROM이 유지되어 저장된 `enable=1`이 계속 이긴다.
상수만 바꾸면 체감상 아무 변화가 없다.

**팩토리 리셋 수용 근거**: 버전 쿠키 상향은 동적 키맵·VIA 설정까지 전부 초기화한다
(`quantum/eeconfig.c`의 `eeconfig_init_quantum`). MAY65는 2026-07-20 기준
**아직 판매가 시작되지 않아** 잃을 사용자 설정이 없으므로 사용자가 명시적으로 허용했다.
단 이 면제는 MAY65 한정이며, 이미 출시된 보드(brick60/brick65/intigrity80/sculpturei)는
전역 버전 쿠키를 공유하므로 해당 보드 릴리스 시 영향을 별도로 판단해야 한다.

**인디케이터 무영향 근거**: underglow OFF는 인디케이터(물리 WS2812 0번)에 영향을 주지 않는다.
채널이 분리되어 있고(`HW_WS2812_CAPS 0` vs `HW_WS2812_RGB 1` / `RGB_CNT 26`),
`rgblight_render_frame()`의 비활성 분기는 underglow 버퍼
(`effect_start_pos..effect_end_pos`)만 0으로 밀며, 인디케이터 렌더 콜백은
`rgblight_config.enable`을 참조하지 않고 항상 실행된다(`rgblight.c:1632-1678`).

**검증**: `cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/keynetix/may65'` 빌드 성공,
`MAY65-V260720R1.uf2` 생성 확인(239,616 bytes).
