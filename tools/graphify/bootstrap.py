#!/usr/bin/env python3
"""graphify 세션 부트스트랩.

Claude Code SessionStart hook과 Codex 세션 시작 체크리스트에서 실행된다.
새 워크스페이스(클론 직후)에서도 graphify 세팅이 자동 복원되도록 한다.

하는 일:
  1. graphify 모듈 확인 (없으면 설치 안내만 출력하고 정상 종료)
  2. graphify-out/.graphify_python / .graphify_root 재생성 (로컬 상태, gitignore 대상)
  3. git post-commit/post-checkout hook 미설치 시 자동 설치
  4. HEAD가 마지막 갱신 시점과 다르면 graphify update 실행 (AST-only, LLM 불필요)
  5. 그래프 상태 한 줄 요약 출력 (SessionStart hook stdout은 세션 컨텍스트에 주입됨)

어떤 실패도 세션을 막지 않는다: 항상 exit 0.
"""

import subprocess
import sys
from pathlib import Path

# Windows 콘솔(cp949)에서 한글/특수문자 출력이 깨지지 않도록 UTF-8 강제
for stream in (sys.stdout, sys.stderr):
    if hasattr(stream, "reconfigure"):
        stream.reconfigure(encoding="utf-8", errors="replace")

REPO_ROOT = Path(__file__).resolve().parents[2]
OUT = REPO_ROOT / "graphify-out"


def sh(args, timeout=300):
    """명령 실행, (returncode, stdout) 반환. 실패해도 예외를 올리지 않는다."""
    try:
        r = subprocess.run(
            args, cwd=REPO_ROOT, capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=timeout,
        )
        return r.returncode, (r.stdout or "") + (r.stderr or "")
    except Exception as e:
        return 1, str(e)


def main():
    try:
        import graphify  # noqa: F401
    except ImportError:
        print("[graphify] 모듈 미설치 — `pip install graphifyy` 후 이 스크립트를 다시 실행하면 "
              "그래프 질의(graphify query)를 쓸 수 있습니다. 커밋된 graphify-out/graph.json은 "
              "설치 없이도 직접 열람 가능합니다.")
        return

    OUT.mkdir(exist_ok=True)
    (OUT / ".graphify_python").write_text(sys.executable, encoding="utf-8")
    (OUT / ".graphify_root").write_text(str(REPO_ROOT), encoding="utf-8")

    # git hook 자동 설치 (이미 있으면 no-op). git worktree에서도 동작하도록
    # 파일 경로 검사 대신 graphify hook status 출력으로 판정한다.
    rc, out = sh([sys.executable, "-m", "graphify", "hook", "status"], timeout=60)
    if "post-commit: installed" not in out:
        rc, out = sh([sys.executable, "-m", "graphify", "hook", "install"])
        print("[graphify] git hook 설치:", "완료" if rc == 0 else f"실패 — {out.strip()[:200]}")

    # HEAD 이동 감지 시에만 그래프 증분 갱신 (post-commit hook이 있으면 평소엔 no-op)
    head_file = OUT / ".graphify_last_head"
    rc, head = sh(["git", "rev-parse", "HEAD"], timeout=30)
    head = head.strip()
    if rc == 0 and head:
        last = head_file.read_text(encoding="utf-8").strip() if head_file.exists() else ""
        if head != last or not (OUT / "graph.json").exists():
            rc, out = sh([sys.executable, "-m", "graphify", "update", "."])
            if rc == 0:
                head_file.write_text(head, encoding="utf-8")
            else:
                print("[graphify] update 실패:", out.strip()[:300])

    # 세션 컨텍스트용 상태 요약
    graph = OUT / "graph.json"
    if graph.exists():
        try:
            import json
            g = json.loads(graph.read_text(encoding="utf-8"))
            n_nodes = len(g.get("nodes", []))
            n_edges = len(g.get("edges", []) or g.get("links", []))
            print(f"[graphify] 지식그래프 준비됨: {n_nodes} nodes / {n_edges} edges — "
                  f'코드 질문은 `graphify query "<질문>"` 우선, 관계는 `graphify path`, '
                  f"코드 수정 후엔 `graphify update .` (AST-only, LLM 불필요).")
        except Exception:
            print("[graphify] graph.json 존재 (파싱 생략)")
    else:
        print("[graphify] graph.json 없음 — /graphify 로 최초 빌드 필요")


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"[graphify] bootstrap 오류(세션은 계속): {e}")
    sys.exit(0)
