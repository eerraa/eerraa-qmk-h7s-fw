# H7S 펌웨어 — 에이전트 진입점

STM32H7S3(내장 HS PHY) 키보드 펌웨어. 보드별 빌드는 `-DKEYBOARD_PATH`로 선택하며,
QMK 포팅층 위에 VIA/Vial을 얹고 커스텀 VIA 앱(`the-via-eerraa`)과 짝을 이룬다.

이 파일은 펌웨어 범위의 정본 지시 파일이다.

## 1. 시작과 작업 라우팅

공통 Git·승인·문서 규약·검증 원칙은 루트 AGENTS.md를 따른다. 이 문서의 상대 경로와
빌드·검사 명령은 현재 펌웨어 컴포넌트 디렉터리 기준이다.

현재 펌웨어 버전은 `src/hw/hw_def.h`의 `_DEF_FIRMWARE_VERSION`이 소유한다.

| 작업 | 필요한 계약·문서 | 첫 소스·설정 앵커 | 관련 검증 |
| --- | --- | --- | --- |
| 일반 펌웨어 수정 | 요구·안전·호환 경계를 건드릴 때만 해당 계약 절 | 변경 소스와 호출자/소유 심볼을 소스 검색으로 확인 | `docs/manual_verify.md`의 영향별 host/regression/빌드 검사 |
| VIA wire/value/menu | `docs/contract_via.md`의 관련 절 | `src/ap/modules/qmk/quantum/via.h`, `src/ap/modules/qmk/keyboards/era/` | `python -X utf8 tools/era_doc_refs.py`; 영향 시 `pwsh -NoProfile -File tools/era_via_host_tests/run.ps1` |
| USB·부트·폴링 | `docs/contract_usb.md`의 관련 절 | `src/hw/driver/usb/`, `src/ap/modules/qmk/port/` | 문서 검사; 소스 영향은 `docs/manual_verify.md`가 지정한 host/regression/빌드 검사 |
| EEPROM·지속성·릴리스 버전 | `docs/contract_eeprom.md`의 관련 절 | `src/ap/modules/qmk/port/port.h`, `src/hw/hw_def.h` | 문서 검사; 소스 영향은 관련 host/regression/펌웨어 검사 |
| 문서·사용자 안내·문서 포인터 | `docs/MAP.md`, 필요하면 대상 계약/사용자 문서 | `AGENTS.md`, `docs/`, `tools/era_doc_refs.py` | `python -X utf8 tools/era_doc_refs.py`; 검사기 변경 시 자기검사 |
| 릴리스·배포 준비 | `docs/contract_eeprom.md` §2, `docs/manual_verify.md`, 영향받는 사용자 문서 | `src/hw/hw_def.h`와 실제 배포 입력 | 문서 검사 + 영향받는 펌웨어 검사; 설치·배포는 별도 명시 범위 |
| 미구현·미판정 항목 | 해당될 때만 `docs/state_open.md` | 항목이 가리키는 소스/계약 | 항목의 다음 진입 조건이 요구하는 검증 |

코드 위치·호출 관계·현재 상수는 문서 표를 찾지 말고 소스 검색(`git grep -n`, `rg`)으로 확인한다.

## 2. 프로젝트 경계

- USB instability monitor와 자동 폴링 다운그레이드는 폐기 상태다. 이유와 재도입 경계는
  `docs/contract_usb.md` §4가 소유하며, 검사기는 관련 심볼의 복원을 거절한다.
- 부트로더 컴포넌트·외부 VIA 앱과의 인터페이스는 `docs/MAP.md` §7로 조회한다.
  관련 범위의 AGENTS를 먼저 읽으며, 외부 저장소 수정은 별도 승인 범위로 확인한다.
- 소스의 기존 인코딩·개행을 보존한다. Python 문서 도구는 UTF-8 모드로 실행한다.

## 3. 작업 규칙

- 일반 코드 변경마다 `// VYYMMDDRn ...` 이력 주석을 추가하거나 펌웨어 버전을 자동 상승시키지
  않는다. 현재 버전은 `src/hw/hw_def.h`가 소유한다.
- 릴리스/버전 변경이 실제 작업 범위일 때만 `_DEF_FIRMWARE_VERSION`을 바꾼다. 버전은 식별용이고
  EEPROM 초기화와 무관하다. 저장 형식(USER 슬롯, 저장 타입, eeconfig·VIA·키맵 주소, RGB 모드
  번호)을 바꾸면 `docs/contract_eeprom.md` §2에 따라 `ERA_EEPROM_RESET_KEY`를 올리거나(전체
  초기화 수용) 호환 사유를 기록한다. `tools/era_doc_refs.py`의 `storage` 검사가 이를 강제한다.
- 고위험 파일군은 변경 시 집중 리뷰한다: `src/ap/modules/qmk/port/sys_port.c`,
  `src/ap/modules/qmk/port/sys_port.h`, `src/hw/driver/`.
- 스타일은 주변 소스를 따른다. 비자명한 이유만 주석으로 남기고 패치·버전 이력을 주석에 누적하지
  않는다.
- QMK 업스트림을 병합할 때는 `src/ap/modules/qmk/quantum/`을 먼저 비교하고, 그다음
  `src/ap/modules/qmk/port/`에서 플랫폼 수정을 재적용한다.
- PR 본문은 변경 요약과 검증 결과를 담는다.

## 4. 검증

문서·포인터 변경의 기본 검사는 다음이다.

```powershell
python -X utf8 tools/era_doc_refs.py
```

`tools/era_doc_refs.py` 또는 자기검사를 바꾸면 `python -X utf8 tools/era_doc_refs_selftest.py`도
실행한다. 펌웨어 소스 변경은 `docs/manual_verify.md`에서 영향받는 host/regression/ARM 검사를
고른다. 훅 실행기를 바꿨을 때는 `python hooks/test_pre_commit.py`도 실행한다.

검사별 범위·툴체인 전제·실기기만 판정할 수 있는 항목은 `docs/manual_verify.md`가 소유한다.

## 5. 하지 말 것

- instability monitor / 자동 폴링 다운그레이드 복원 — `docs/contract_usb.md` §4.
- EEPROM USER 슬롯 오프셋 이동 — `docs/contract_eeprom.md` §1.
- EEPROM 초기화 키를 펌웨어 버전에서 다시 계산하거나 부팅 reset guard를 보드별로 끌 수 있게 함 — `docs/contract_eeprom.md` §2.
