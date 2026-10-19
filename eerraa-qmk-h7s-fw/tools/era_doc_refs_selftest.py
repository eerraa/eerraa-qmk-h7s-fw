#!/usr/bin/env python3
"""`era_doc_refs.py`가 비어 있지 않은지 확인한다.

검사기가 통과하는 것만으로는 그것이 무언가를 보고 있다는 뜻이 아니다. 여기서는 검사마다
결함을 하나씩 심고, 검사기가 그 결함을 그 이름으로 잡는지 보고, 원본을 되돌린다.

  python tools/era_doc_refs_selftest.py

중간에 죽어도 되돌리도록 전부 finally에서 복구한다. 시작 시 검사기가 이미 실패 상태면
아무것도 건드리지 않고 멈춘다.
"""

from __future__ import annotations

import re
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHECKER = ROOT / "tools/era_doc_refs.py"
MAP_DOC = "docs/MAP.md"
JSON_DOC = "src/ap/modules/qmk/keyboards/era/keynetix/may65/json/MAY65-H7S-VIA.JSON"
HW_DEF = "src/hw/hw_def.h"


def current_firmware_version() -> bytes:
    text = (ROOT / "src/hw/hw_def.h").read_bytes()
    match = re.search(rb'_DEF_FIRMWARE_VERSION\s+"(V\d{6}R\d)"', text)
    if match is None:
        raise SystemExit("hw_def.h에서 _DEF_FIRMWARE_VERSION을 찾지 못했다")
    return match.group(1)


def plant_readme_release_filename(body: bytes) -> bytes:
    current = current_firmware_version()
    planted = b"V000101R1" if current != b"V000101R1" else b"V000101R2"
    return body + b"\nselftest-" + planted + b".uf2\n"


def plant_td_advanced_alias(body: bytes) -> bytes:
    definition = json.loads(body)
    menu = next(menu for menu in definition["menus"] if isinstance(menu, dict) and menu.get("label") == "TAPDANCE")
    menu["content"][0]["content"].append({
        "label": "Hidden advanced time", "type": "range", "showIf": "0",
        "content": ["alternate_hold_time", 16, 57], "options": [0, 65535],
    })
    return json.dumps(definition).encode()


def plant_polling_text(body: bytes) -> bytes:
    definition = json.loads(body)
    definition["menus"].append({"label": "Observation", "content": [{
        "label": "Polling", "type": "label", "showIf": "0",
        "content": ["alternate_polling_name", 13, 4, 0],
    }]})
    return json.dumps(definition).encode()


def run_checker() -> tuple[int, str]:
    proc = subprocess.run(
        [sys.executable, "-X", "utf8", str(CHECKER)],
        cwd=ROOT, capture_output=True, text=True, encoding="utf-8",
    )
    return proc.returncode, (proc.stdout or "") + (proc.stderr or "")


def probe(name: str, tag: str, target: str, transform, create: bool = False) -> bool:
    path = ROOT / target
    original = None if create else path.read_bytes()
    try:
        path.write_bytes(transform(b"" if create else original))
        code, out = run_checker()
    finally:
        if create:
            path.unlink(missing_ok=True)
        else:
            path.write_bytes(original)
    caught = code == 1 and tag in out
    detail = next((line for line in out.splitlines() if tag in line), out.strip())
    print(f"{'PASS' if caught else 'FAIL'} {name:14s} {detail[:100]}")
    return caught


PROBES = (
    ("path", "[path]", MAP_DOC,
     lambda b: b + "\n`src/nope/missing.c`\n".encode(), False),
    ("comment", "[comment]", "src/hw/driver/usb/usbd_conf.c",
     lambda b: b.replace(b"docs/contract_usb.md", b"docs/gone.md", 1), False),
    ("index", "[index]", "docs/zz_selftest_orphan.md",
     lambda b: "# orphan\n\nGenre: map\nCanonical for: 자기검사\n".encode(), True),
    ("symbol", "[symbol]", MAP_DOC,
     lambda b: b + "\n`usbNoSuchThing`\n".encode(), False),
    ("retired", "[retired]", "src/hw/driver/usb/zz_selftest_tmp.h",
     lambda b: b"void usbInstabilitySelftest(void);\n", True),
    ("menu", "[menu]", JSON_DOC,
     lambda b: b.replace(b", 17,", b", 99,"), False),
    ("td-advanced", "[tapdance]", JSON_DOC,
     lambda b: b.replace(b'"{id_qmk_tapdance_1_mode} == 0"', b'"{id_qmk_tapdance_1_mode} != 255"', 1), False),
    ("td-hidden", "[tapdance]", JSON_DOC,
     lambda b: b.replace(b'"label": "TAPDANCE",', b'"showIf": "0", "label": "TAPDANCE",', 1), False),
    ("td-probe", "[tapdance]", JSON_DOC,
     lambda b: b.replace(b'"id_qmk_tapdance_1_mode", 16, 49', b'"id_qmk_tapdance_1_mode", 16, 57', 1), False),
    ("td-alias", "[tapdance]", JSON_DOC, plant_td_advanced_alias, False),
    ("polling-text", "[menu]", JSON_DOC, plant_polling_text, False),
    ("td-guidance", "[tapdance]", JSON_DOC,
     lambda b: b.replace(b'"Visit usekb.cc"', b'"Visit elsewhere"', 1), False),
    ("td-mode-status", "[tapdance]", JSON_DOC,
     lambda b: b.replace(b'"label": "Advanced settings"', b'"label": "Input mode"', 1), False),
    ("version", "릴리스 파일명이", "docs/readme.txt",
     plant_readme_release_filename, False),
    ("storage", "[storage]", "src/ap/modules/qmk/port/port.h",
     lambda b: b.replace(b"EECONFIG_USER_DATABLOCK + 152", b"EECONFIG_USER_DATABLOCK + 156", 1), False),
    ("storage-key", "[storage]", HW_DEF,
     lambda b: re.sub(rb"(ERA_EEPROM_RESET_KEY\s+0x[0-9A-Fa-f]{7})([0-9A-Fa-f])",
                      lambda m: m.group(1) + (b"1" if m.group(2) == b"0" else b"0"), b, count=1), False),
    ("storage-slot", "[storage]", "src/ap/modules/qmk/keyboards/era/sirind/brick65/port/indicator_port.c",
     lambda b: b.replace(b"EECONFIG_USER_INDICATOR + 4)", b"EECONFIG_USER_INDICATOR + 5)", 1), False),
    ("storage-ver", "[storage]", "src/hw/driver/eeprom_reset_guard.c",
     lambda b: b.replace(b"== ERA_EEPROM_RESET_KEY)", b"== (ERA_EEPROM_RESET_KEY ^ _DEF_FIRMWARE_VERSION[1]))", 1), False),
)


def probe_foreign_skip(name: str, prefix: str) -> bool:
    """FOREIGN_REPOS 접두사는 로컬 경로가 아니므로 없어도 [path]가 나면 안 된다."""
    path = ROOT / MAP_DOC
    original = path.read_bytes()
    planted = f"\n`{prefix}no-such-file.md`\n".encode()
    try:
        path.write_bytes(original + planted)
        code, out = run_checker()
    finally:
        path.write_bytes(original)
    leaked = "[path]" in out and prefix in out
    ok = code == 0 and not leaked
    detail = "skipped" if ok else next(
        (line for line in out.splitlines() if prefix in line), out.strip()
    )
    print(f"{'PASS' if ok else 'FAIL'} {name:14s} {detail[:100]}")
    return ok


def main() -> int:
    code, out = run_checker()
    if code != 0:
        print("검사기가 이미 실패 상태다. 먼저 그것을 고쳐라.\n" + out)
        return 1

    missed = [name for name, tag, target, transform, create in PROBES
              if not probe(name, tag, target, transform, create)]
    if not probe_foreign_skip("foreign", "eerraa-agent-docs/"):
        missed.append("foreign")

    code, out = run_checker()
    if code != 0:
        print("복구 실패 — 트리가 원래 상태로 돌아오지 않았다.\n" + out)
        return 1
    n = len(PROBES) + 1  # + FOREIGN_REPOS skip
    if missed:
        print(f"\nFAIL 심었는데 잡지 못한 결함: {missed}")
        return 1
    print(f"\nPASS 결함 {n}종을 전부 잡았고 트리는 원상 복구됐다")
    return 0


if __name__ == "__main__":
    sys.exit(main())
