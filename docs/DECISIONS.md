# 결정 기록

본 문서는 코드만으로는 알 수 없는 판단 근거를 남깁니다. 세션 간 인수인계용이며,
Claude auto-memory는 Codex CLI와 공유되지 않으므로 공용 사실은 반드시 여기에 기록합니다.

---

## 2026-08-24 — UF2 업로드 후 자동 시작 실패의 원인과 이원 대응 (V260824R2)

**증상**: UF2 업로드 자체는 항상 성공하나, 부트로더 → 펌웨어 자동 전환이 간헐적으로
실패해 무반응으로 남았다. USB 케이블을 재삽입하면 업로드한 펌웨어가 정상 부팅됐다.

**원인 (부트로더)**: `bootJumpFirm()`이 시스템 리셋이 아니라 직접 점프를 쓰는데,
직전의 `usbDeInit()`이 RCC 클럭만 차단하고 `DCTL.SDIS`를 세우지 않는다. 클럭 게이팅은
주변장치 레지스터를 리셋하지 않으므로 PHY의 D+ 풀업/HS 터미네이션이 유지되어 호스트가
디태치를 인지하지 못한다. 부수 원인으로 `uf2_flash_complete()`가 `tud_task()` 안에서
`bootUpdateFirm()`을 수행해 1.5~5초간 USB 스택이 정지한다(64KB 블록 소거 6회).
콜드부트·케이블 재삽입·펌웨어→부트로더 세 경로는 항상 정상인데 살아있는 USB 세션을
물려받는 점프 경로만 실패한다는 **비대칭**이 결정적 근거였다.

**결정 — 이원 대응**:
1. **부트로더** `19e0487` (V260824R1, eerraa-qmk-h7s-boot origin/main): UF2 완료 시
   점프 대신 `MODE_BIT_UPDATE` 예약(RTC DR3) → `tud_disconnect()` → 시스템 리셋.
   근본 해결이나 UF2로는 갱신 불가(내부 플래시, ST-LINK 필요)하므로
   **이후 출고분에만** 적용된다.
2. **펌웨어** (본 커밋, V260824R2): `USBD_LL_Start()`에서 `HAL_PCD_Init()`이 이미
   세워둔 SDIS를 100ms 유지한다. **기출하 보드용 보완책**이며 재열거 실패만 해결하고
   업데이트 중 USB 정지는 해결하지 못한다.

**펌웨어가 부트로더 문제를 보완할 수 있는 근거**: USB 분리/부착은 소프트웨어 인수인계가
아니라 D+/D- 선의 전기적 상태이고 `DCTL.SDIS` 비트 하나가 결정한다. USB 주변장치는
부트로더와 펌웨어가 공유하는 동일 하드웨어이며 점프 이후 소유자는 펌웨어다. 게다가
펌웨어는 이미 `HAL_PCD_Init()`에서 SDIS=1을 세우고 있었고 그 구간이 수백 µs로 너무
짧았을 뿐이라, 새 동작 추가가 아니라 **기존 구간의 유지 시간 연장**이다.

**판별 코드를 넣지 않은 이유**: `DCFG.DAD != 0`으로 점프 진입을 판별할 수 있으나
`HAL_PCD_Init()`이 클럭 인가(MspInit)와 `USB_CoreReset`(DAD를 0으로 리셋)을 한 호출에서
처리해 중복 초기화가 필요하다. 100ms를 아끼려고 하드웨어 의존 코드를 넣지 않기로 했다.

**브랜치 전략**: 이 수정은 진행 중인 production 작업과 독립적이므로 `main`에서 분기해
`main`에 직접 병합했다. production 브랜치(`goal/era-via-production*`)는 `main`을
병합해 가져간다. 버전은 production 라인이 같은 날 `V260824R1`을 이미 사용 중이라
충돌을 피해 `V260824R2`로 부여했다.

**미검증**: 양쪽 모두 실기 테스트 전이다. 빌드는 H7S 5종 전부 통과(경고 0건),
FLASH 증가 +8 B.

상세: `docs/features_usb_boot_handoff.md`, 부트로더 `docs/uf2_auto_start.md`

---

## 2026-07-20 — 에이전트 협업 재설정: graphify 지식그래프 도입 및 문서 체계 개편

**결정**:
1. graphify 지식그래프를 도입하고 산출물(`graphify-out/graph.json`, `GRAPH_REPORT.md`, `manifest.json`, `.graphify_labels.json`, 시맨틱 캐시)을 리포에 커밋한다. 코드 탐색은 그래프 질의를 1순위로 한다 (AGENTS.md §3).
2. 그래프 스코프는 `.graphifyignore`로 **"실제 컴파일되는 코드 + 프로젝트 문서"**로 한정한다. 전체 리포(1,046파일/247만 단어) 대신 360파일/19.7만 단어. 근거: 에이전트 이해 1순위 — 미컴파일 QMK 업스트림 원본(quantum 504→~100파일)과 벤더 HAL/CMSIS가 그래프에 있으면 커뮤니티/god node가 죽은 코드에 오염되고 "활성 기능"으로 오인할 위험이 있음. 토큰 절약은 2순위 부수 효과. 컴파일 목록의 기준은 `src/ap/modules/qmk/CMakeLists.txt`이며, 이 목록이 바뀌면 `.graphifyignore`를 함께 갱신해야 한다.
3. 세팅의 원격 유지: 커밋되는 `.claude/settings.json`의 SessionStart hook이 `tools/graphify/bootstrap.py`를 실행해 새 워크스페이스에서도 git hook 설치·그래프 동기화를 자동 복원한다. git post-commit/post-checkout hook과 graph.json merge driver는 graphify가 공용 git dir에 설치하므로 같은 리포의 모든 worktree(orca 워크스페이스)에 공유된다. Codex CLI는 AGENTS.md §3 체크리스트로 동일 부트스트랩을 실행한다.
4. 문서 감사(에이전트 39개, 전 문서 코드 대조) 결과: `docs/review.md`(V251124R6 리뷰 스냅샷 — 코드 변경 이력 주석과 커밋 9cbeb3b로 완전 복원 가능)와 `AGENTS.cursorrules`(AGENTS.md 구버전 사본, 고유 정보는 3308d68:AGENTS.md에서 복원 가능)를 폐기. 나머지 문서의 stale 항목 약 25건을 교정(자세한 내역은 이 커밋의 diff 참조).
5. 문서 역할 원칙 확정: 코드 구조·호출 관계는 그래프로 질의하고, `docs/`에는 코드에서 복원 불가능한 것(결정 기록·실측 회고·운영 가이드·릴리스 안내)만 둔다. 구조 설명만을 위한 신규 문서는 만들지 않는다.

**미해결(문서 공백, 감사에서 high 판정)**: ① 보드 추가/포팅 절차(board bring-up), ② USB 클래스/컴포지트(HID·CDC·CMP) 아키텍처. 필요해지는 시점에 운영 지식 중심으로 작성 검토.

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
