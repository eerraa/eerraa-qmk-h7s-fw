#!/usr/bin/env python3
"""H7S pre-commit 실행기 정합 테스트."""

from __future__ import annotations

import os
import shlex
import subprocess
import tempfile
from pathlib import Path


REPO = Path(__file__).resolve().parent.parent
HOOK = REPO / "hooks" / "pre-commit"


def check(name: str, condition: bool) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {name}")
    print(f"PASS {name}")


def sh_path(path: Path) -> str:
    return path.resolve().as_posix()


def write_command(path: Path, body: str) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("#!/bin/sh\n" + body)
    path.chmod(0o755)


def run_hook(fake_bin: Path, repo: Path = REPO) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["PATH"] = str(fake_bin) + os.pathsep + env.get("PATH", "")
    return subprocess.run(
        ["sh", "hooks/pre-commit"],
        cwd=repo,
        env=env,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )


def test_wiring() -> None:
    mode = subprocess.run(
        ["git", "ls-files", "-s", "hooks/pre-commit"],
        cwd=REPO,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    check("pre-commit mode 100755", mode.stdout.startswith("100755 "))

    attr = subprocess.run(
        ["git", "check-attr", "eol", "--", "hooks/pre-commit"],
        cwd=REPO,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    check("pre-commit checkout stays LF", attr.stdout.strip().endswith(": lf"))


def test_python_fallback() -> None:
    with tempfile.TemporaryDirectory() as temp:
        fake_bin = Path(temp)
        marker = fake_bin / "selected.txt"
        marker_arg = shlex.quote(sh_path(marker))

        write_command(fake_bin / "python3", "exit 1\n")
        write_command(
            fake_bin / "python",
            "if [ \"$1\" = \"-c\" ]; then exit 0; fi\n"
            f"printf 'python:%s\\n' \"$*\" > {marker_arg}\n"
            "exit 0\n",
        )

        result = run_hook(fake_bin)
        if result.returncode != 0:
            print(result.stdout)
            print(result.stderr)
        check("broken python3 falls back to python", result.returncode == 0)
        check("trap syntax is accepted", "invalid signal specification" not in result.stderr)
        selected = marker.read_text(encoding="utf-8") if marker.exists() else ""
        check("fallback runs era_doc_refs.py", "tools/era_doc_refs.py" in selected)


def test_staged_snapshot() -> None:
    with tempfile.TemporaryDirectory() as temp:
        base = Path(temp)
        repo = base / "repo"
        fake_bin = base / "bin"
        (repo / "hooks").mkdir(parents=True)
        (repo / "tools").mkdir()
        fake_bin.mkdir()

        (repo / "hooks" / "pre-commit").write_text(
            HOOK.read_text(encoding="utf-8"),
            encoding="utf-8",
            newline="\n",
        )
        (repo / "tools" / "era_doc_refs.py").write_text(
            "# staged checker fixture\n",
            encoding="utf-8",
        )
        (repo / "proof.txt").write_text("staged\n", encoding="utf-8")

        subprocess.run(["git", "init", "-q"], cwd=repo, check=True)
        subprocess.run(
            ["git", "add", "hooks/pre-commit", "tools/era_doc_refs.py", "proof.txt"],
            cwd=repo,
            check=True,
        )
        (repo / "proof.txt").write_text("unstaged\n", encoding="utf-8")

        write_command(
            fake_bin / "python3",
            'if [ "$1" = "-c" ]; then exit 0; fi\n'
            'root=$(dirname "$(dirname "$1")")\n'
            'if [ "$(cat "$root/proof.txt" 2>/dev/null)" = "staged" ]; then exit 0; fi\n'
            'echo "checker did not receive staged snapshot" >&2\n'
            "exit 9\n",
        )
        write_command(fake_bin / "python", "exit 1\n")

        result = run_hook(fake_bin, repo)
        if result.returncode != 0:
            print(result.stdout)
            print(result.stderr)
        check("hook validates staged index, not unstaged worktree", result.returncode == 0)


def test_no_interpreter_fails_closed() -> None:
    with tempfile.TemporaryDirectory() as temp:
        fake_bin = Path(temp)
        write_command(fake_bin / "python3", "exit 1\n")
        write_command(fake_bin / "python", "exit 1\n")

        result = run_hook(fake_bin)
        check("missing interpreter is refused", result.returncode != 0)
        check("refusal explains Python requirement", "Python 3.8+" in result.stderr)


if __name__ == "__main__":
    test_wiring()
    test_python_fallback()
    test_staged_snapshot()
    test_no_interpreter_fails_closed()
    print("all pre-commit launcher tests passed")
