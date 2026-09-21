# H7S 펌웨어 — 에이전트 진입점

STM32H7S3(내장 HS PHY) 키보드 펌웨어. 보드별 빌드는 `-DKEYBOARD_PATH`로 선택하며,
QMK 포팅층 위에 VIA/Vial을 얹고 커스텀 VIA 앱(`the-via-eerraa`)과 짝을 이룬다.

이 파일이 로컬 정본 지시 파일이고 `CLAUDE.md`는 여기로 연결하는 어댑터다. **답변·커밋
메시지·PR 본문은 한국어로 쓴다.** 문서 규약은 `eerraa-agent-docs` v2, commit
`4bd84f45dd3e970bf25505059431058efbe7680a`의 [AGENT_DOCS_CONVENTION.md](https://github.com/eerraa/eerraa-agent-docs/blob/4bd84f45dd3e970bf25505059431058efbe7680a/AGENT_DOCS_CONVENTION.md)에 고정한다.
일반 개발은 이 저장소의 로컬 지침만으로 시작하고 중앙 규약은 문서체계 변경 때만 조회한다.

## 1. 시작과 작업 라우팅

기록된 상태를 믿지 말고 실제 root/branch/HEAD/status와 관련 diff를 먼저 확인해 사용자 변경을
보존한다. 현재 펌웨어 버전은 `src/hw/hw_def.h`의 `_DEF_FIRMWARE_VERSION`이 소유한다.

| 작업 | 필요한 계약·문서 | 첫 소스·설정 앵커 | 관련 검증 |
| --- | --- | --- | --- |
| 일반 펌웨어 수정 | 요구·안전·호환 경계를 건드릴 때만 해당 계약 절 | 변경 소스와 호출자/소유 심볼을 소스 검색으로 확인 | `docs/manual_verify.md`의 영향별 host/regression/빌드 검사 |
| VIA wire/value/menu | `docs/contract_via.md`의 관련 절 | `src/ap/modules/qmk/quantum/via.h`, `src/ap/modules/qmk/keyboards/era/` | `python -X utf8 tools/era_doc_refs.py`; 영향 시 `pwsh -NoProfile -File tools/era_via_host_tests/run.ps1` |
| USB·부트·폴링 | `docs/contract_usb.md`의 관련 절 | `src/hw/driver/usb/`, `src/ap/modules/qmk/port/` | 문서 검사; 소스 영향은 `docs/manual_verify.md`가 지정한 host/regression/빌드 검사 |
| EEPROM·지속성·릴리스 버전 | `docs/contract_eeprom.md`의 관련 절 | `src/ap/modules/qmk/port/port.h`, `src/hw/hw_def.h` | 문서 검사; 소스 영향은 관련 host/regression/펌웨어 검사 |
| 문서·사용자 안내·문서 포인터 | `docs/MAP.md`, 필요하면 대상 계약/사용자 문서 | `AGENTS.md`, `CLAUDE.md`, `docs/`, `tools/era_doc_refs.py` | `python -X utf8 tools/era_doc_refs.py`; 검사기 변경 시 자기검사 |
| 릴리스·배포 준비 | `docs/contract_eeprom.md` §2, `docs/manual_verify.md`, 영향받는 사용자 문서 | `src/hw/hw_def.h`와 실제 배포 입력 | 문서 검사 + 영향받는 펌웨어 검사; 설치·배포는 별도 명시 범위 |
| 미구현·미판정 항목 | 해당될 때만 `docs/state_open.md` | 항목이 가리키는 소스/계약 | 항목의 다음 진입 조건이 요구하는 검증 |

코드 위치·호출 관계·현재 상수는 문서 표를 찾지 말고 소스 검색(`git grep -n`, `rg`)으로 확인한다.

## 2. 프로젝트 경계

- USB instability monitor와 자동 폴링 다운그레이드는 폐기 상태다. 이유와 재도입 경계는
  `docs/contract_usb.md` §4가 소유하며, 검사기는 관련 심볼의 복원을 거절한다.
- 구현 사실은 현재 소스·설정이 소유한다. 제품 요구·wire/storage 호환 의무와 소스가 충돌하면
  문구를 소스에 맞춰 완화하지 말고 해당 계약과 짝 저장소를 대조해 충돌로 다룬다.
- 짝 저장소는 `docs/MAP.md` §7의 인터페이스 포인터로만 조회한다. 상대를 수정해야 하는 작업은
  그 저장소의 `AGENTS.md`에서 별도 범위로 시작한다.
- 소스의 기존 인코딩·개행을 보존한다. Python 문서 도구는 UTF-8 모드로 실행한다.

## 3. 작업 규칙

- 일반 코드 변경마다 `// VYYMMDDRn ...` 이력 주석을 추가하거나 펌웨어 버전을 자동 상승시키지
  않는다. 현재 버전은 `src/hw/hw_def.h`가 소유한다.
- 릴리스/버전 변경이 실제 작업 범위일 때만 `_DEF_FIRMWARE_VERSION`을 바꾼다. 버전 문자열 상승은
  `docs/contract_eeprom.md` §2의 전체 EEPROM factory-reset 정책과 결합되어 있으므로 그 범위를
  수용한 변경인지 먼저 확인한다. JSON-only 변경은 그 계약에 따라 cookie를 올리지 않는다.
- 고위험 파일군은 변경 시 집중 리뷰한다: `src/ap/modules/qmk/port/sys_port.c`,
  `src/ap/modules/qmk/port/sys_port.h`, `src/hw/driver/`.
- 스타일은 주변 소스를 따른다. 비자명한 이유만 주석으로 남기고 패치·버전 이력을 주석에 누적하지
  않는다.
- QMK 업스트림을 병합할 때는 `src/ap/modules/qmk/quantum/`을 먼저 비교하고, 그다음
  `src/ap/modules/qmk/port/`에서 플랫폼 수정을 재적용한다.
- 커밋은 검증된 관심사별로 나누고 관련 없는 사용자 변경을 stage하지 않는다.
- **명시적 요청 없이 push하지 않는다.** PR 제목·본문은 한국어로 쓰고 변경 요약과 검증 결과를
  담는다.

## 4. 검증

문서·포인터 변경의 기본 검사는 다음이다.

```powershell
python -X utf8 tools/era_doc_refs.py
```

`tools/era_doc_refs.py` 또는 자기검사를 바꾸면 `python -X utf8 tools/era_doc_refs_selftest.py`도
실행한다. 펌웨어 소스 변경은 `docs/manual_verify.md`에서 영향받는 host/regression/ARM 검사를
고른다. 훅 실행기를 바꿨을 때는 `python hooks/test_pre_commit.py`도 실행한다.

검사별 범위·툴체인 전제·실기기만 판정할 수 있는 항목은 `docs/manual_verify.md`가 소유한다.
소스를 건드리지 않은 문서 변경은 ARM 전체 빌드나 HIL을 요구하지 않는다.

## 5. 하지 말 것

- instability monitor / 자동 폴링 다운그레이드 복원 — `docs/contract_usb.md` §4.
- EEPROM USER 슬롯 오프셋 이동 — `docs/contract_eeprom.md` §1.
- `_DEF_FIRMWARE_VERSION` 상승을 전체 EEPROM 초기화 결정과 분리 — `docs/contract_eeprom.md` §2.
