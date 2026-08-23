@AGENTS.md

## Claude Code 전용

- 공용 규칙의 수정은 `AGENTS.md`에서만 한다. 이 파일에는 Claude 전용 지시만 둔다.
- 코드 구조 질문은 지식그래프 질의 우선 (AGENTS.md §3). `/graphify` 스킬로 그래프 재빌드·질의를 수행할 수 있다.
- `.claude/settings.json`의 SessionStart hook이 `tools/graphify/bootstrap.py`를 자동 실행하므로 git hook 설치·그래프 동기화는 수동으로 할 필요 없다.
- Claude의 auto-memory는 Codex CLI와 공유되지 않는다 — 공용으로 필요한 사실(세션 인수인계, 결정, 실측 결과)은 memory가 아니라 반드시 `docs/DECISIONS.md`에 기록한다.
