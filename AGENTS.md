# EERRAA QMK H7S — 공통 작업 진입점

이 폴더가 통합 Git 루트다. Git 관리는 루트에서, 빌드·검사는 해당 컴포넌트에서 실행한다.
하위 AGENTS의 경로·명령은 컴포넌트 기준이며, 제품 계약·작업별 검증은 하위 AGENTS가 소유한다.

## 필수 시작 순서

1. 먼저 사용자 작업을 아래 표로 분류한다. 불명확하면 분류에 필요한 최소 경로만 확인한다.
2. 실제 작업 워크트리의 이 문서와 관련 하위 AGENTS를 반드시 읽은 뒤 조사·수정·검증한다. 범위가 넓어지면 새 범위의 AGENTS도 먼저 읽는다. 필수 문서를 읽을 수 없으면 차단 사유를 보고한다.

| 작업 | 필독 |
| --- | --- |
| 런타임·보드·QMK 포팅·VIA | [펌웨어 AGENTS](eerraa-qmk-h7s-fw/AGENTS.md) |
| UF2·복구·부트 인계 | [부트로더 AGENTS](eerraa-qmk-h7s-boot/AGENTS.md) |
| USB 종료·플래시 레이아웃·부트 호환 | 양쪽 AGENTS와 관련 계약; 사실 소유자에서 고치고 양쪽 소비를 검증 |
| 컴포넌트 문서·설정 | 대상 컴포넌트의 AGENTS |
| 루트 공통 정책·Git·워크트리·핸드오버 관리 | 이 문서; 컴포넌트에 영향이 있으면 해당 AGENTS도 필독 |

## 공통 정책

- 실제 root·branch·HEAD·status·관련 diff를 확인하고 사용자 변경과 ignored 입력을 보존한다.
- commit·push·merge·rebase·tag·release·배포·의존성 변경·실기기 조작은 사용자 명시 범위에서만 수행한다. commit은 검증된 관심사별로 자신의 변경만 포함한다.
- hook 설정은 `git config core.hooksPath eerraa-qmk-h7s-fw/hooks`다. 기존 폴더 경로와 컴포넌트의 ignore·개행 정책을 보존한다.
- 과거 커밋·태그는 이전 구조이므로 별도 워크트리에서 조회한다. `boot-history` 원격은 이력 참조용이며 통합 트리를 push하지 않는다.
- 구현 사실은 현재 소스·설정이 소유한다. 제품 요구·안전·호환 제약과 충돌하면 계약을 완화하지 말고 충돌로 다룬다.
- 하위 AGENTS의 영향별 최소 검사를 따른다. 문서만 바뀌면 관련 문서 검사와 `git diff --check`가 기본이며 전체 빌드·HIL은 요구하지 않는다.
- 실제 실행과 소스 추론을 구분하고 미실행·미측정을 PASS로 보고하지 않는다.
- 대화·commit·PR·공통 진입점은 한국어로 쓴다. 문서 규약은 [eerraa-agent-docs v2 고정본](https://github.com/eerraa/eerraa-agent-docs/blob/4bd84f45dd3e970bf25505059431058efbe7680a/AGENT_DOCS_CONVENTION.md)이며 문서체계 변경 때만 조회한다.

## 로컬 폴더

- `eerraa-qmk-h7s-fw-worktrees/`는 추가 워크트리 공간이다. 제거는 경로·등록·잠금·변경·ignored 자료를 확인하고, 실제 `main` 병합 또는 테스트 종료가 확인된 경우만 `git worktree remove`로 수행한다. 미보존 자료·용도 불명 자료는 보존하고 junction 재귀 삭제·무단 브랜치 삭제는 금지한다.
- `eerraa-qmk-h7s-fw-handover/`는 사용자 저장 지시가 있을 때만 쓰는 Git 비추적 폴더다. 복구 백업은 그 아래 `backups/`에 보존한다. 자동 기록·stage·활성 계약의 정본 취급은 금지한다. `.mixdog/`도 Git 비추적 로컬 도구 공간이다.
