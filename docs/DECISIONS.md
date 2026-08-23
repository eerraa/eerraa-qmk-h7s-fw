# 결정 기록

본 문서는 코드만으로는 알 수 없는 판단 근거를 남깁니다. 세션 간 인수인계용이며,
Claude auto-memory는 Codex CLI와 공유되지 않으므로 공용 사실은 반드시 여기에 기록합니다.

---

## 2026-08-23 — MOUSE VIA 페이지 · exact-ms JSON 노출 · state-sync 보강 (V260823R1)

**배경**: custom VIA 앱(`the-via-eerraa`)과 custom QMK(`qmk_firmware_eerraa`)의 Non-Split ERA 기능을 이 H7S 코드베이스에 맞추는 작업. 검증 + 이식을 함께 수행했다.

**검증 결과 (V260821R1 exact-ms / state-sync)**:
1. exact-ms 배치(channel 15 ID 5, channel 16 ID 41~48, 2-byte BE, 100~500)는 앱이 번들한 정의(`the-via-eerraa/public/definitions/era/v3/1163001890.json`)와 **정확히 일치**했다. 앱의 `ExactMillisecondControl`은 `type: "range"` + `id_*_term_exact` 이름 + `options: [100, 500]`으로 트리거되고, 2바이트 BE로 읽고 쓴다.
2. **공식 VIA 호환 vs Custom VIA JSON 관리 분리**:
   - **이 리포(`eerraa-qmk-h7s-fw-via`)**: 공식 웹 VIA(`usevia.app`) 호환용 드롭다운(dropdown) 방식을 유지한다.
   - **Custom VIA 앱(`the-via-eerraa`)**: `era-definitions/custom/v3/`에서 exact-ms `range` 슬라이더([100, 500]) 및 `exactMsFamily: "h7s"` 계약을 적용한 JSON을 별도로 관리한다.
   - 펌웨어 C 계층은 공식 VIA의 legacy dropdown GET/SET과 Custom VIA의 exact-ms 2-byte BE GET/SET을 모두 완벽하게 지원하는 듀얼 호환 구조로 동작한다.
3. **결함 ②**: `era_state_sync_via_command()`에 INVALID 판정 팔이 없어 `ERA_STATE_SYNC_STATUS_INVALID` 상수가 죽어 있었고, 길이 검사도 6바이트였다. 참조 구현과 앱 파서(`parseStateSyncEnvelope`)가 모두 32바이트 봉투 + 전 구간 0을 요구하므로 그에 맞췄다.
4. **결함 ③**: CONFIG revision이 tapping/tapdance/layout options에서만 올라갔다. 참조는 `nvm_eeprom_changed_kb` 캐치올로 모든 EEPROM 설정 쓰기에서 올린다. 이 포팅층에는 그 훅이 없으므로 채널별로 **값이 실제로 바뀐 SET에서만** 올리도록 추가했다: 디바운스(14), KKUK(12), SOCD(10/11), 인디케이터(0), BootMode(13), USB 모니터(13), 그리고 `id_eeprom_reset`(세 도메인 전부).
5. **결함 ④ — 기존 이미지의 스택 오버플로**: `report_exk_q`가 원소 크기를 `sizeof(report_info_t)`(=`HW_KEYS_PRESS_MAX`+2 = **22**)로 생성하고 있었는데, 실제 원소는 `exk_report_info_t`(변경 전 **9바이트**)였다. `qbuffer`는 생성 시 받은 size만큼 복사하므로:
   - `qbufferRead(q, (uint8_t *)&report_info, 1)`이 스택의 9바이트 지역변수에 **22바이트를 쓴다 → 13바이트 스택 오버플로**. EXK 재시도 큐를 드레인할 때마다 발생한다.
   - `qbufferWrite`는 반대로 9바이트 지역변수에서 22바이트를 읽는다.
   - 백킹 배열(`exk_report_info_t[128]` = 1152B)도 큐가 2816B로 가정하므로 인덱스 52부터 배열 밖을 침범한다.

   발화 조건은 "EXK 전송이 BUSY로 실패해 큐에 적재된 뒤 드레인될 때"다. 미디어/시스템 키를 누른 순간 HID EP가 바쁜 경우에만 생기므로 드물어 현장에서 드러나지 않았다. `sizeof(exk_report_info_t)`로 교정했다. **NKRO 때문에 생긴 문제가 아니라 이번 조사에서 발견한 기존 결함**이며, EP를 32B로 키우면(원소 33B > 22B) 이번에는 반대로 절단이 되므로 어느 쪽이든 수정이 필요했다.

**신규 결정 (이식)**:
1. **마우스는 EXK 인터페이스의 리포트 ID 2로 내보낸다.** 리포트가 6바이트라 **기존 8바이트 엔드포인트에 그대로 들어가므로 엔드포인트 크기·FIFO 배분·인터페이스 개수가 모두 그대로다.** 인터페이스 0(키보드)은 한 바이트도 건드리지 않았다.
2. **NKRO는 구현했다가 되돌렸다 (오너 결정 2026-08-23).** 기본 상태가 이미 전환 없이 20키(`HW_KEYS_PRESS_MAX` = 20)이므로 240키로 늘리는 기능적 실익이 없는 반면, NKRO 리포트(32B)를 실으려면 EXK EP를 8B → 32B로 키워야 해서 **NKRO를 켜지 않는 사용자까지 재열거와 RAM +3KB(재시도 큐 1152B → 4224B)를 부담**한다. 6KRO↔NKRO 토글은 "부트 호환이냐 동시입력이냐"의 교환을 사용자에게 떠넘기는 장치인데, 현재 구성은 그 교환 자체가 없다. 상세와 재도입 시 설계는 `docs/features_usb_hid_reports.md` §5.
3. **VIA 채널 번호는 참조 QMK와 다르다.** 참조는 MOUSE=13을 쓰지만 이 코드베이스에서 13은 USB POLLING이 점유하므로 **MOUSE=17**을 새로 할당했다. value id 1~6 배치는 참조와 동일하게 유지했다. VIA는 정의를 (vendorId, productId)별로 캐시하므로 H7S 정의가 별도인 이상 번호 일치는 요구되지 않는다.
4. MOUSE 페이지는 참조 `era_mousekey.c`의 의미론을 그대로 옮겼다: 최고 속도와 가속 시간은 저장하지 않고 파생하며, 가속 off는 최고 속도가 아니라 **첫 스텝 속도**로 고정한다. 실측 기본값(Start 4px / Top 16px / 램프 1.0초 / 100 이벤트/초)도 참조의 2026-08-18 측정치를 그대로 쓴다. 근거는 `docs/features_mousekey.md` §6.
5. `MOUSEKEY_MOVE_DELTA` / `MOUSEKEY_WHEEL_DELTA`를 `ERA_MOUSEKEY_RUNTIME_DELTA` 아래에서만 변수로 승격했다(참조와 동일한 fork). 이 페이지가 유일한 쓰기 주체다.
6. `_DEF_FIRMWARE_VERSION`을 `V260823R1`로 올렸다. **5개 보드 모두 `AUTO_FACTORY_RESET_ENABLE 1`이므로 이 이미지 첫 부팅에서 EEPROM이 공장 초기화된다.** 신규 `EECONFIG_USER_MOUSEKEY` 슬롯(+152, 16B)은 그 경로에서 채워지고, 초기화가 없더라도 시그니처 불일치로 기본값이 기록된다.

**인터페이스 0의 부트 규격 편차 (조사 결과 + 유지 결정, 오너 2026-08-23)**: 이번 작업 중 확인한 사실이며 **V260823R1 이전부터 있던 상태**다. 전문은 `docs/features_usb_hid_reports.md` §3~4.
- `HW_KEYS_PRESS_MAX`가 20이라 인터페이스 0의 IN 리포트는 **22바이트/20키 배열**이다. 빌드 이미지에서 추출한 디스크립터가 `95 14 75 08`(REPORT_COUNT 20)로 이를 확증한다. **6KRO가 아니다.**
- 그럼에도 BIOS에서 동작해 온 이유는 22바이트의 **앞 8바이트가 부트 포맷과 바이트 단위로 같기** 때문이다.
- 규격 편차 두 가지: ① `SET_PROTOCOL(boot)` 이후에도 8바이트로 줄이지 않는다. ② 인터페이스 0의 `GET_REPORT`에 핸들러가 없어 STALL된다.
- **결정: 둘 다 고치지 않는다.** 고치려면 재시도 큐와 SOF 드레인이 얽힌 8000Hz 송신 경로를 프로토콜 상태에 따라 가변 길이로 바꿔야 하고, 현장 실패 보고가 없다.
- 이 사실이 NKRO를 되돌린 판단의 근거이기도 하다(위 신규 결정 2번).

**알려진 한계(수용)**: `mk_time_to_max`가 1바이트라 Cursor Acceleration의 도달 범위가 갱신 주기에 따라 좁아진다. 200 /s(5ms)에서 상한은 1.275초이므로 JSON이 함께 제시하는 1.5s·2.0s는 조용히 잘린다. 참조 QMK도 같은 옵션 목록·같은 동작이고, 주기별로 옵션 목록을 바꾸는 것은 VIA 정의 형식으로 표현할 수 없다. **되읽기가 실제 값을 정직하게 보고하는 것**으로 계약을 맞췄고, 호스트 테스트가 이를 검증한다(`docs/features_mousekey.md` §6-2).

**검증**: 5개 보드 전부 빌드 성공(경고 0). `tools/era_via_host_tests/run.ps1` 통과 — 기존 exact-ms/state-sync 케이스에 MOUSE 단위 환산(최고속도 반올림, 램프 시간 보존, 가속 off의 첫 스텝 고정, 클램프 정직성)과 state-sync INVALID 팔을 추가했다. EXK 리포트 디스크립터는 파서로 검증(129바이트, 컬렉션 균형, 리포트별 크기 MOUSE 6B / SYSTEM 3B / CONSUMER 3B)했고 같은 계약을 `usbd_hid.c`의 `_Static_assert` 3개로 고정했다. **실기 검증은 아직 하지 않았다** — USB 재열거, 마우스 키 동작, 미디어 키, 8kHz 유지 여부는 실기에서 확인이 필요하다.

**남은 항목(이 리포 밖)**: `the-via-eerraa`의 `public/definitions/era_advanced.json`에는 H7S 보드 중 `brick60-h7s`(0x45520022) 하나만 등록되어 있다. MAY65(0x0030)·INTIGRITY80(0x0028)·BRICK65(0x0023)·SCULPTUREI(0x0034)는 `stateSync`/`exactMsFamily` 미등록이라 앱이 state-sync 폴링을 하지 않는다. exact-ms 컨트롤은 JSON의 `options: [100, 500]`이 fallback을 이기므로 정상 동작한다.

---

## 2026-08-21 — VIA GET 0x06 리비전과 exact-ms term (V260821R1)

**결정**:
1. GET_KEYBOARD_VALUE selector `0x06` 리비전 봉투는 vendored `via.c` GET switch에서 버퍼만 채운다. `raw_hid_send` stub는 비워 두고, 실제 TX는 기존 `via_hid_task` → `usbHidEnqueueViaResponse` 단일 생산자를 유지한다.
2. Global exact-ms는 channel 15 ID 5, TD0–TD7 exact-ms는 channel 16 ID 41–48, wire는 2-byte big-endian uint16 ms. 기존 ID는 삭제/재할당하지 않는다.
3. exact SET은 100–500만 허용하고 범위 밖은 거절한다. load/sync는 유효한 uint16을 20ms 격자로 되돌리지 않는다. legacy GET만 floor-to-20ms 투영한다.
4. CONFIG revision은 값이 실제로 바뀐 tapping/tapdance SET 뒤에 올린다. KEYMAP/MACRO revision은 VIA keymap/macro write 명령 뒤에 올린다.
5. `_DEF_FIRMWARE_VERSION`을 `V260821R1`로 올려 AUTO_FACTORY_RESET 쿠키를 갱신한다. Brick60은 `AUTO_FACTORY_RESET_ENABLE 1`이므로 이 이미지 첫 부팅에서 EEPROM이 공장 초기화된다.

**검증**: `tools/era_via_host_tests/run.ps1` (exact/legacy vector + GET 0x06 + single-producer source check).

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
