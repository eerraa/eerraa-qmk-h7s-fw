#!/usr/bin/env python3
"""H7S pre-commit launcher conformance tests."""

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
    path.write_text("#!/bin/sh\n" + body, encoding="utf-8", newline="\n")
    path.chmod(0o755)


def run_hook(fake_bin: Path) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["PATH"] = str(fake_bin) + os.pathsep + env.get("PATH", "")
    return subprocess.run(
        ["sh", "hooks/pre-commit"],
        cwd=REPO,
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
        selected = marker.read_text(encoding="utf-8") if marker.exists() else ""
        check("fallback runs era_doc_refs.py", "tools/era_doc_refs.py" in selected)


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
    test_no_interpreter_fails_closed()
    print("all pre-commit launcher tests passed")
