@AGENTS.md

## Claude Code 전용

- 공용 규칙은 `AGENTS.md`에서만 고친다. 이 파일에는 Claude 전용 배선만 둔다.
- `.claude/settings.json`의 SessionStart hook이 `tools/graphify/bootstrap.py`를 자동 실행하므로
  git hook 설치와 그래프 동기화를 수동으로 할 필요가 없다.
- **Claude의 auto-memory는 Codex CLI와 공유되지 않는다.** 두 도구가 함께 알아야 하는 사실은
  memory가 아니라 문서에 남긴다 — 그 사실의 원본을 선언한 문서가 어디인지는 `docs/MAP.md` §2가
  답한다. 아직 판정되지 않은 것은 `docs/state_open.md`로 간다.
