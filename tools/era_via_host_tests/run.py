#!/usr/bin/env python3
"""Portable host tests for VIA value layer, USB diagnostics, RGB SLEEP, CLEAN, VERSION, and TX producer.

Windows 문서 명령은 tools/era_via_host_tests/run.ps1 이다. 이 스크립트는 같은 검사를
PATH의 gcc로 돌린다 (mingw 경로가 없는 Linux 포함).
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
QMK = ROOT / "src" / "ap" / "modules" / "qmk"
SANDBOX = HERE / "sandbox"
INC = HERE / "include"


def gcc_bin() -> str:
    mingw = Path(r"D:\baram-fw-tools_exe\arm_toolchain\mingw_gcc\bin\gcc.exe")
    if mingw.is_file():
        return str(mingw)
    found = shutil.which("gcc")
    if found:
        return found
    raise SystemExit("gcc를 찾지 못했다")


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd))
    subprocess.check_call(cmd)


def compile_and_run(out: Path, sources: list[Path], cflags: list[str]) -> None:
    gcc = gcc_bin()
    cmd = [gcc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", *cflags, *[str(s) for s in sources], "-o", str(out)]
    run(cmd)
    run([str(out)])


def prepare_sandbox() -> None:
    if SANDBOX.exists():
        shutil.rmtree(SANDBOX)
    (SANDBOX / "process_keycode").mkdir(parents=True)
    (SANDBOX / "platforms").mkdir(parents=True)
    for name in (
        "tapping_term.c",
        "tapping_term.h",
        "tapdance.c",
        "tapdance.h",
        "era_state_sync.c",
        "era_state_sync.h",
        "mousekey_config.c",
        "mousekey_config.h",
        "rgb_sleep.c",
        "rgb_sleep.h",
    ):
        shutil.copy(QMK / "port" / name, SANDBOX / name)
    shutil.copy(INC / "port.h", SANDBOX / "port.h")
    shutil.copy(INC / "quantum.h", SANDBOX / "quantum.h")
    shutil.copy(INC / "wait.h", SANDBOX / "wait.h")
    shutil.copy(INC / "timer.h", SANDBOX / "timer.h")
    shutil.copy(INC / "platforms" / "eeprom.h", SANDBOX / "platforms" / "eeprom.h")
    shutil.copy(
        INC / "process_keycode" / "process_tap_dance.h",
        SANDBOX / "process_keycode" / "process_tap_dance.h",
    )


def main() -> int:
    os.chdir(ROOT)
    prepare_sandbox()
    gcc_exact = [
        "-Wno-unused-parameter",
        "-Wno-unused-function",
        "-include", str(INC / "force_host.h"),
        "-DEECONFIG_USER_DATA_SIZE=512",
        "-DMOUSEKEY_ENABLE",
        "-DERA_MOUSEKEY_RUNTIME_DELTA",
        f"-I{SANDBOX}",
        f"-I{INC}",
        f"-I{QMK / 'quantum'}",
        f"-I{QMK / 'quantum' / 'keymap_extras'}",
        f"-I{QMK / 'quantum' / 'process_keycode'}",
    ]
    compile_and_run(
        HERE / "test_era_via_exact_ms.exe",
        [
            HERE / "test_era_via_exact_ms.c",
            HERE / "host_stubs.c",
            SANDBOX / "era_state_sync.c",
            SANDBOX / "tapping_term.c",
            SANDBOX / "tapdance.c",
            SANDBOX / "mousekey_config.c",
        ],
        gcc_exact,
    )

    compile_and_run(
        HERE / "test_usb_diagnostics.exe",
        [
            HERE / "test_usb_diagnostics.c",
            ROOT / "src" / "hw" / "driver" / "usb" / "usb_hid" / "usb_diagnostics.c",
            QMK / "port" / "era_usb_diagnostics.c",
        ],
        [
            "-DUSB_DIAGNOSTICS_HOST_TEST",
            f"-I{HERE / 'diagnostics_include'}",
            f"-I{ROOT / 'src' / 'hw' / 'driver' / 'usb' / 'usb_hid'}",
            f"-I{QMK / 'port'}",
        ],
    )

    compile_and_run(
        HERE / "test_rgb_sleep.exe",
        [
            HERE / "test_rgb_sleep.c",
            SANDBOX / "rgb_sleep.c",
            SANDBOX / "era_state_sync.c",
        ],
        [
            "-Wno-unused-parameter",
            "-Wno-unused-function",
            "-include", str(INC / "force_host.h"),
            "-DEECONFIG_USER_DATA_SIZE=512",
            f"-I{SANDBOX}",
            f"-I{INC}",
            f"-I{QMK / 'quantum'}",
            f"-I{QMK / 'quantum' / 'keymap_extras'}",
        ],
    )

    compile_and_run(
        HERE / "test_sys_eeprom_clean.exe",
        [
            HERE / "test_sys_eeprom_clean.c",
            QMK / "port" / "sys_port.c",
        ],
        [
            "-Wno-unused-parameter",
            "-Wno-unused-function",
            "-include", str(INC / "force_host.h"),
            f"-I{INC}",
            f"-I{QMK / 'port'}",
            f"-I{QMK / 'quantum'}",
            f"-I{QMK / 'quantum' / 'keymap_extras'}",
        ],
    )

    hw_def = (ROOT / "src" / "hw" / "hw_def.h").read_text(encoding="utf-8")
    ver_match = re.search(r'_DEF_FIRMWARE_VERSION\s+"(V\d{6}R\d)"', hw_def)
    if ver_match is None:
        raise SystemExit("hw_def.h에서 _DEF_FIRMWARE_VERSION을 찾지 못했다")
    compile_and_run(
        HERE / "test_version.exe",
        [
            HERE / "test_version.c",
            QMK / "port" / "ver_port.c",
        ],
        [
            "-Wno-unused-parameter",
            "-Wno-unused-function",
            "-include", str(INC / "force_host.h"),
            "-include", "stdlib.h",
            f'-D_DEF_FIRMWARE_VERSION="{ver_match.group(1)}"',
            f"-I{INC}",
            f"-I{QMK / 'port'}",
            f"-I{QMK / 'quantum'}",
            f"-I{QMK / 'quantum' / 'keymap_extras'}",
        ],
    )

    run([sys.executable, str(HERE / "check_single_producer.py")])
    return 0


if __name__ == "__main__":
    sys.exit(main())
