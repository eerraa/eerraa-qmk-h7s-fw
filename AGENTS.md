# H7S 펌웨어 — 에이전트 진입점

STM32H7S3(내장 HS PHY) 키보드 펌웨어. 보드 5종이 한 소스를 공유하고 `-DKEYBOARD_PATH`로만
갈린다. USB는 HS 8/4/2 kHz와 FS 1 kHz를 지원하며 **기본은 FS 1 kHz**다. QMK 포팅층 위에
VIA/Vial을 얹었고, 커스텀 VIA 앱(`the-via-eerraa`)이 반대편 짝이다.

이 파일이 정본 지시 파일이고 `CLAUDE.md`는 여기로 보내는 포인터다. **답변·커밋 메시지·PR
본문은 한국어로 쓴다.** 문서 헤더·장르 다섯·거절 세 줄·최소 검사의 정본은 저장소
`eerraa-agent-docs` 태그 **v1**의 `eerraa-agent-docs/AGENT_DOCS_CONVENTION.md`다.
이 저장소는 그 규약을 따르고, 에이전트 문서가 여섯 편이라 색인을 따로 두지 않는다.

## 1. 시작 전에

기록된 상태를 믿지 말고 직접 확인한다. **현재 펌웨어 버전은 문서가 아니라 코드가 든다.**

```powershell
git status --short
git log --oneline -3
Select-String -Path src/hw/hw_def.h -Pattern "_DEF_FIRMWARE_VERSION"
```

전부 읽지 마라. 하려는 일에 따라 읽는다. 세 열은 이유가 다르다 — **Change**는 편집 전
필독, **Locate**는 조회, **Verify**는 빌드·캡처·판정할 때만.

| Change | Locate | Verify |
| --- | --- | --- |
| 정본·색인 — `docs/MAP.md` (**여기부터**) | 보드·채널·EEPROM 생성 표, 짝 저장소 | `python tools/era_doc_refs.py` |
| VIA·앱 계약 — `docs/contract_via.md` | 채널, exact-ms, `0x06`/`0x07`, MOUSE | `python tools/era_doc_refs.py`; `pwsh -NoProfile -File tools/era_via_host_tests/run.ps1` |
| USB 호스트 계약 — `docs/contract_usb.md` | 리포트, 부트 편차, 폴링, 부트로더 인계 | `python tools/era_doc_refs.py`; 소스면 `cmake --build build -j10` |
| EEPROM 계약 — `docs/contract_eeprom.md` | 슬롯, 버전 쿠키, 팩토리 리셋. 배치 표는 `docs/MAP.md` §5 | `python tools/era_doc_refs.py` |
| 검증 절차 — `docs/manual_verify.md` | 명령, 툴체인 전제, 증상별 확인 순서 | 그 문서가 적은 명령을 그대로 |
| 열린 항목 — `docs/state_open.md` | 아직 안 끝난 것, 되살리면 안 되는 것 | 해당 항목이 가리키는 실기 |
| 사용자 문구 — `docs/readme.txt` | 릴리스에 동봉하는 안내 | `python tools/era_doc_refs.py` |
| — | 코드가 어디 살고 무엇을 부르는가 — 소스 검색 (`git grep -n`, `rg`) | — |

## 2. 먼저 알아야 손해를 안 보는 것

- **USB instability monitor와 자동 폴링 다운그레이드는 없다.** 폐기한 것이지 빠진 것이 아니고,
  되살리면 안 되는 이유가 둘 있다 — `docs/contract_usb.md` §4. 검사기가 그 심볼들이 `src/`에
  다시 나타나면 실패한다.
- **문서가 코드와 어긋나면 코드가 이긴다.** 다만 그 계약이 앱 저장소와 짝을 이루는 것이면
  이쪽이 틀렸을 수도 있다 — 고치기 전에 양쪽을 대조한다(`docs/MAP.md` §7).
- **cwd는 이 저장소에 둔다.** 앱 저장소를 cwd로 열고 이쪽 규칙을 따르다가 앱에
  파생물 75,000줄을 잘못 커밋한 사고가 있다.
- **소스는 UTF-8(BOM 없음) + CRLF이고 주석이 한국어다.** 이 머신의 Python은 기본이 cp949라
  `PYTHONUTF8=1` 없이 스크립트로 소스를 다루면 한국어가 깨진다. 파일은 `rb`로 읽고 원래
  개행에 맞춰 쓴다. 백슬래시가 들어가는 편집은 heredoc으로 하지 말고 `.py` 파일을 만들어
  실행한다 — heredoc이 이스케이프를 한 단계 먹는다.
- **편집기에 열려 있던 파일이 저장되며 작업을 덮어쓴 사고가 있었다.** 커밋 전에
  `git status`와 `git diff --stat`을 본다.

> **REFUSED:** 의무 지식 그래프, 세션 시작 훅, 세션마다 생성하는 컨텍스트를 탐색 층으로 두는 것.
> **WHY:** 라우터와 검색이 같은 질문에 답하는 동안, 그 층은 로컬 상태와 세션마다 돌아가는 프로세스를 남기고 답을 에이전트가 다시 읽게 한다.
> **REOPENS:** 호출 비용이 셸 검색보다 낮고, 답을 에이전트가 읽는 것 이외의 수단이 검사하는 탐색 도구.

## Navigation

구조 질문은 라우터(`docs/MAP.md`)와 소스 검색(`git grep -n`, `rg`)이 답한다.
파생 인덱스를 조회하거나 재생성하지 않으며, 의무로 되살리지 않는다.

## 3. 작업 규칙

- **코드 변경마다** 사용자에게 받은 코드 버전으로 `// VYYMMDDRn ...` 형식의 한국어 변경 이력
  주석을 남기고, `src/hw/hw_def.h`의 `_DEF_FIRMWARE_VERSION`을 같은 값으로 올린다. 버전을
  받지 못했으면 하나를 임시 지정해 표기하고 **즉시 공식 버전 확인을 요청한다.**
  이 규칙은 **소스 변경에만** 적용된다 — 문서 전용 커밋은 버전을 올리지 않는다.
  버전 상승이 EEPROM 전체 초기화를 뜻한다는 점은 `docs/contract_eeprom.md` §2.
- 고위험 파일군은 변경 시 집중 리뷰한다: `src/ap/modules/qmk/port/sys_port.c`,
  `src/ap/modules/qmk/port/sys_port.h`, `src/hw/driver/`.
- 스타일: 공백 두 칸 들여쓰기, 중괄호는 새 줄, `if (cond) {`처럼 괄호 앞뒤 공백, 연산자 좌우
  공백(단 `millis()-pre_time`처럼 주변이 붙여 쓰면 그 관습을 따른다), `#define` 상수는
  대문자·값·주석 정렬, 주석은 `//`.
- QMK 업스트림을 병합할 때는 `src/ap/modules/qmk/quantum/`을 먼저 비교하고, 그다음
  `src/ap/modules/qmk/port/`에서 플랫폼 수정을 재적용한다.
- 커밋은 관심사별로 나눈다 — 문서, 소스.
  **지우는 커밋과 새로 쓰는 커밋을 분리한다. 문서를 지우는 커밋은 그 문서가 들고 있던 근거를
  커밋 메시지에 담는다** — 삭제만이 유일하게 복구 불가능한 수다.
- **명시적 요청 없이 push하지 않는다.**
- PR 제목·본문은 한국어로 쓰고 변경 요약, 검증 결과, 현재 펌웨어 버전 문자열을 담는다.

## 4. 검증

```powershell
python tools/era_doc_refs.py                                 # 문서-코드 정합 9종
pwsh -NoProfile -File tools/era_via_host_tests/run.ps1        # VIA 값 계층·진단·단일 생산자
cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/keynetix/may65' -G "MinGW Makefiles"
cmake --build build -j10
```

클론마다 한 번 `git config core.hooksPath hooks` 하면 `hooks/pre-commit`이 커밋 전에
`python tools/era_doc_refs.py`를 돌린다. 검사기 자신을 고쳤으면
`python tools/era_doc_refs_selftest.py`도 돌린다.

무엇이 무엇을 무는지, 어떤 변경이 무엇을 owe하는지, 툴체인 전제는 `docs/manual_verify.md`.
**소스를 건드리지 않은 변경은 빌드를 owe하지 않으며, 그렇게 말하는 것이 검증 진술이다.**
문서만 고쳤어도 첫 줄은 돌린다.

## 5. 하지 말 것

- instability monitor / 자동 폴링 다운그레이드 복원 — `docs/contract_usb.md` §4.
- EEPROM USER 슬롯 오프셋 이동 — `docs/contract_eeprom.md` §1.
- 같은 사실을 두 문서에 두기. 각 문서의 `Canonical for`가 그 문서의 범위를 선언한다.
- 검사기나 호스트 테스트가 물고 있는 계약을 문서에서 지우기. 문서에 없으면 그 검사가 왜
  있는지 아무도 모르게 된다.
- 문서에 날짜·세션 서사·진행 상태를 남기기. `git log`와 실행이 답한다.
  예외는 `docs/state_open.md` 하나다.
