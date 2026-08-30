# H7S 펌웨어 — 데이터 맵

Genre: map
Canonical for: 이 저장소의 정본 규칙 — 어떤 사실이 어디 살고, 두 곳이 어긋났을 때 어느 쪽이 이기며,
그것을 무는 것이 무엇인가. 문서 색인, 보드·채널·EEPROM 사실 표, 짝 저장소 대응, 문서 규칙

이 문서는 **무엇이 어디 있고 어긋나면 어느 쪽이 정본인가**만 답한다. 결정의 근거는
`docs/contract_via.md`·`docs/contract_usb.md`·`docs/contract_eeprom.md`, 아직 끝나지 않은 것은
`docs/state_open.md`, 이력은 `git log`에 있다. 날짜·세션 서사·진행 상태는 여기 쓰지 않는다.

## 1. 정본 규칙

| 사실 | 정본 | 어긋나면 무엇이 잡는가 |
| --- | --- | --- |
| 현재 펌웨어 버전 | `src/hw/hw_def.h`의 `_DEF_FIRMWARE_VERSION` | `version` 검사 — 문서가 현재보다 높은 버전을 적으면 실패 |
| VIA 채널·value id | `src/ap/modules/qmk/quantum/via.h` | `table`·`menu` 검사 |
| EEPROM USER 배치 | `src/ap/modules/qmk/port/port.h` | `table` 검사 |
| 보드 목록·PID | `src/ap/modules/qmk/keyboards/era/` 아래 트리 | `table` 검사 |
| VIA wire 봉투(`0x06`·`0x07`)와 값 계층 | 소스, 그리고 반대편은 앱 ADR(§7) | `tools/era_via_host_tests/run.ps1` |
| raw-HID TX 생산자 | `src/ap/modules/qmk/port/via_hid.c` | `tools/era_via_host_tests/check_single_producer.py` |
| 사용자에게 나가는 문구 | `docs/readme.txt` | `version` 검사(릴리스 파일명) |
| 코드가 어디서 무엇을 부르는가 | 소스. 조회는 소스 검색 | 없음 — 파생 인덱스는 정본이 아니다 |

문서와 코드가 어긋나면 **코드가 이긴다.** 문서를 고치고, 왜 어긋났는지 커밋 메시지에 남긴다.
반대로 코드가 계약을 어긴 경우에는 계약이 이긴다 — 그 계약은 §7의 앱 저장소와 짝을 이루므로
이쪽만 고치면 앱이 깨진다.

## 2. 문서 색인

| 문서 | Genre | 무엇의 원본인가 |
| --- | --- | --- |
| [contract_via.md](contract_via.md) | contract | VIA·앱과의 wire 계약. 채널 주소, exact-ms, `0x06`/`0x07` 봉투, 단일 TX 생산자, MOUSE 단위 환산 |
| [contract_usb.md](contract_usb.md) | contract | USB 호스트와의 계약. 인터페이스·리포트 구성, 부트 규격 편차, 폴링 모드 소유권, 폐기한 자동 복구, 주기 작업의 타이머 규칙 |
| [contract_eeprom.md](contract_eeprom.md) | contract | 영속 상태 계약. USER 슬롯 소유권, 버전 쿠키와 팩토리 리셋, 쓰기 경로의 8 kHz 예산 |
| [manual_verify.md](manual_verify.md) | manual | 실기 없이 돌릴 수 있는 검증과 그 명령, 실기에서만 판정되는 것, 증상별 확인 순서 |
| [state_open.md](state_open.md) | state | 아직 판정되지 않은 것과 착수 조건. 이 문서만 시간이 지나면 지워진다 |
| [readme.txt](readme.txt) | (사용자 문서) | 릴리스에 동봉하는 사용자 안내. 에이전트 문서 규격의 예외 — §8 |

`AGENTS.md`와 `CLAUDE.md`는 진입 사슬이라 헤더 규약의 예외다. 그 둘과 이 색인 사이에
문서 목록을 두 벌 두지 않는다 — `AGENTS.md`는 Change / Locate / Verify로 라우팅하고,
여기는 **무엇의 원본인가**로 라우팅한다.

## 3. 보드

아래 세 표는 `python tools/era_doc_refs.py --tables`가 소스에서 다시 계산해 찍는다. 손으로
고치지 마라 — 검사기가 소스와 대조한다.

<!-- era-doc-refs: boards -->
| 보드 | 소스 | 공식 VIA JSON (json/ 아래) | PID |
| --- | --- | --- | --- |
| BRICK60 | `src/ap/modules/qmk/keyboards/era/sirind/brick60/` | `BRICK60-H7S-VIA.JSON` | `0x0022` |
| BRICK65 | `src/ap/modules/qmk/keyboards/era/sirind/brick65/` | `BRICK65-H7S-VIA.JSON` | `0x0023` |
| INTIGRITY80 | `src/ap/modules/qmk/keyboards/era/intigrity80/` | `INTIGRITY80-VIA.JSON` | `0x0028` |
| MAY65 | `src/ap/modules/qmk/keyboards/era/keynetix/may65/` | `MAY65-H7S-VIA.JSON` | `0x0030` |
| SCULPTUREI | `src/ap/modules/qmk/keyboards/era/sirind/sculpturei/` | `SCULPTUREI-VIA.JSON` | `0x0034` |
<!-- era-doc-refs: end -->

vendorId는 5종 모두 `0x4552`다. 다섯 보드는 같은 소스를 공유하고 `-DKEYBOARD_PATH`로만 갈린다.

## 4. VIA 채널

`펌웨어 라우팅`은 보드의 `<board>/port/via_port.c`가 그 채널을 받는다는 뜻이고, `보드 JSON 노출`은
사용자가 공식 VIA에서 그 화면에 **도달할 수 있다**는 뜻이다. 라우팅만 있고 노출이 없으면
펌웨어에 있는 기능에 손이 닿지 않는다 — `menu` 검사가 그것을 막는다(§8).

<!-- era-doc-refs: via-channels -->
| 채널 | 이름 | 펌웨어 라우팅 | 보드 JSON 노출 |
| --- | --- | --- | --- |
| 0 | `id_custom_channel` | O | O |
| 1 | `id_qmk_backlight_channel` | - | - |
| 2 | `id_qmk_rgblight_channel` | - | O |
| 3 | `id_qmk_rgb_matrix_channel` | - | - |
| 4 | `id_qmk_audio_channel` | - | - |
| 5 | `id_qmk_led_matrix_channel` | - | - |
| 8 | `id_qmk_version` | O | O |
| 9 | `id_qmk_system` | O | O |
| 10 | `id_qmk_kill_switch_lr` | O | O |
| 11 | `id_qmk_kill_switch_ud` | O | O |
| 12 | `id_qmk_kkuk` | O | O |
| 13 | `id_qmk_usb_polling` | O | O |
| 14 | `id_qmk_key_response` | O | O |
| 15 | `id_qmk_tapping` | O | O |
| 16 | `id_qmk_tapdance` | O | O |
| 17 | `id_qmk_mousekey` | O | O |
<!-- era-doc-refs: end -->

채널 2는 VIA 코어(`src/ap/modules/qmk/quantum/via.c`)가 직접 처리하므로 보드 라우팅이 없다.
1·3·4·5는 VIA가 예약한 번호이며 이 펌웨어는 쓰지 않는다 — 새 채널을 할당할 때 피해야 하는
번호다. value id 배치는 `contract_via.md`가 소유한다.

## 5. EEPROM USER 슬롯

`EECONFIG_USER_DATABLOCK` 기준 오프셋이다. USER 블록은 512 B이며, 슬롯 주소는 한 번 배포되면
움직이지 않는다(`contract_eeprom.md` §1).

<!-- era-doc-refs: eeprom-slots -->
| 오프셋 | 크기 | 심볼 |
| --- | --- | --- |
| +0 | 8B | `EECONFIG_USER_INDICATOR` |
| +8 | 8B | `EECONFIG_USER_KILL_SWITCH_LR` |
| +16 | 8B | `EECONFIG_USER_KILL_SWITCH_UD` |
| +24 | 4B | `EECONFIG_USER_KKUK` |
| +28 | 4B | `EECONFIG_USER_BOOTMODE` |
| +32 | 4B | `EECONFIG_USER_RESERVED_32` |
| +36 | 4B | `EECONFIG_USER_EEPROM_CLEAR_FLAG` |
| +40 | 4B | `EECONFIG_USER_EEPROM_CLEAR_COOKIE` |
| +44 | 8B | `EECONFIG_USER_DEBOUNCE` |
| +52 | 12B | `EECONFIG_USER_TAPPING_TERM` |
| +64 | 88B | `EECONFIG_USER_TAPDANCE` |
| +152 | 16B | `EECONFIG_USER_MOUSEKEY` |
<!-- era-doc-refs: end -->

## 6. 코드 구조는 소스가 답한다

구조 질문은 소스 검색(`git grep -n`, `rg`)이 답한다. 파생 인덱스는 정본이 아니다.
값과 계약의 위치만 이 문서가 가리킨다.

| 질문 | 어디서 답하는가 |
| --- | --- |
| 이 함수는 어디 살고 무엇을 부르는가 | 소스 검색 (`git grep -n`, `rg`) |
| 값 — 오프셋·채널 번호·상수 | 이 문서 §3~§5 (소스에서 다시 계산됨) |
| 왜 이렇게 되어 있는가 | `contract_*.md` |
| **없는 것** — 폐기된 서브시스템과 그 이유 | `contract_usb.md` §4 |
| 저장소 밖의 반대편 계약 | §7, 그리고 앱 저장소의 ADR |
| 무엇을 어떻게 돌려 보는가 | `manual_verify.md` |

## 7. 짝 저장소

전부 이 PC에만 있는 경로다. **cwd는 이 저장소에 둔다** — 앱 저장소를 cwd로 열고 이쪽 규칙을
따르다가 앱에 파생물 75,000줄을 잘못 커밋한 사고가 있다. 반대편을 고쳐야 하면
그 저장소에서 그쪽 `AGENTS.md`를 읽고 시작한다.

| 저장소 | 이 펌웨어의 무엇과 짝인가 |
| --- | --- |
| `the-via-eerraa/docs/adr/0001-state-sync-protocol.md` | selector `0x06` 봉투, 3 도메인 revision, exact-ms 규칙 |
| `the-via-eerraa/docs/adr/0002-h7s-usb-diagnostics.md` | selector `0x07` wire·계측 경계·비교 유효성 |
| `the-via-eerraa/docs/adr/0003-era-menu-help-ui.md` | 메뉴 라벨(KKUK 등)과 설명 문구 |
| `the-via-eerraa/docs/MAP.md` §3 | 채널·value id 표 (H7S TD는 채널 16 / 41–48, MOUSE는 17) |
| `qmk_firmware_eerraa/keyboards/era/` | 참조 구현. 채널 번호는 다르다(§4 · `contract_via.md` §2) |
| `eerraa-qmk-h7s-boot` | UF2 부트로더. 인계 계약은 `contract_usb.md` §5 |

**기능을 추가하면 앱 커스텀 정의와 이 저장소의 공식 JSON 양쪽에 넣는다.** 커스텀 앱에서만
말할 수 있는 경로는 오류다. 한쪽만 바뀐 채로 배포된 사고가 양쪽 저장소에서 각각 있었다 —
이쪽은 MOUSE 채널 17을 라우팅하면서 공식 JSON 5개에 메뉴가 없었고, RP2040 쪽은 커스텀 정의
하나에서 세 메뉴가 빠진 채 수명 내내 배포됐다. 둘 다 **양쪽 다 문서를 갖고 있는 상태에서**
일어났다. 저장소 간 정합은 아직 아무 검사기도 보지 않는다(§8).

## 8. 문서 규칙

헤더·장르 다섯·거절 세 줄·은퇴·최소 검사의 정본은 저장소 `eerraa-agent-docs` 태그 **v1**의
`eerraa-agent-docs/AGENT_DOCS_CONVENTION.md`다. 이 절은 그 규약이 저장소마다 고르라고 남겨
둔 것만 적는다.

- 에이전트 문서는 여섯 편뿐이라 **flat**이다 (`docs/` 바로 아래). 장르 디렉터리와 별도 색인을
  두지 않는다. `AGENTS.md`가 Change / Locate / Verify로 라우팅하고, 이 문서 §2가 원본을
  색인한다.
- 문서에 저장소 경로를 쓸 때는 **저장소 상대 경로 전체**를 백틱으로 적는다
  (`src/ap/modules/qmk/port/via_hid.c`). 짝 저장소·규약 저장소 파일은 저장소 이름을 앞에
  붙인다 (`the-via-eerraa/docs/MAP.md`, `eerraa-agent-docs/AGENT_DOCS_CONVENTION.md`).
  `path` 검사가 해소한다. 경로가 `파일:줄`이면 그 줄이 파일 안인지도 본다.
- 보드·채널·EEPROM·wire 표는 `<!-- era-doc-refs: … -->` 마커 안에 두고
  `python tools/era_doc_refs.py --tables`가 소스에서 다시 계산한다. 손으로 고치지 마라 —
  `table` 검사가 소스와 대조한다.
- 이 저장소에는 값이 바뀌는 ADR이 없으므로 `Status:`와 `Read when:`을 쓰지 않는다.
  `header` 검사가 부재를 본다.

검사기는 `python tools/era_doc_refs.py` 하나이며 아홉 가지를 본다: `path` `comment` `header`
`index` `symbol` `retired` `table` `menu` `version`. `comment`는 반대 방향을 본다 — 소스
주석이 부르는 `docs/` 경로가 실재하는지다. 문서 쪽만 검사하면 문서를 지웠는데 주석이 계속
그것을 가리키는 드리프트가 남는다. 그 검사기가 비어 있지 않은지는
`python tools/era_doc_refs_selftest.py`가 결함을 심어 확인한다. 커밋마다 돌리려면 클론당
`git config core.hooksPath hooks` — `hooks/pre-commit`이 검사기를 부른다. CI 워크플로는
없다.

**아직 기계가 보지 못하는 것**은 세 가지다 —
문장이 선언한 장르에 맞는지, `Canonical for`가 참인지(비어 있지 않은지만 본다), 그리고
짝 저장소와의 정합. 세 번째는 §7의 사고 두 건이 바로 그 구멍에서 나왔다.
