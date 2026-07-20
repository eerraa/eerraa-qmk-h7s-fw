@AGENTS.md

## Claude Code 전용

- 공용 규칙의 수정은 `AGENTS.md`에서만 한다. 이 파일에는 Claude 전용 지시만 둔다.
- Claude의 auto-memory는 Codex CLI와 공유되지 않는다 — 공용으로 필요한 사실(세션 인수인계, 결정, 실측 결과)은 memory가 아니라 반드시 리포 문서(`docs/STATUS.md`, `docs/DECISIONS.md`, `docs/evidence/`)에 기록한다.