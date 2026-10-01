#!/usr/bin/env python3
"""로컬 문서/소스/계약 정합 검사기.

  path     백틱 안 로컬 저장소 경로가 실재하는가 (:line, brace/glob 지원)
  comment  소스 주석이 부르는 docs/ 경로가 실재하는가
  index    docs/ 아래 활성 문서가 docs/MAP.md에서 도달 가능한가
  symbol   백틱 안 식별자가 src/ 또는 tools/에 실재하는가
  retired  폐기된 USB 심볼이 src/에 되살아나지 않았는가
  menu     펌웨어가 라우팅하는 VIA 채널이 보드 JSON에서 도달 가능한가
  version  사용자 배포 파일명이 현재 펌웨어 버전과 일치하는가
  storage  저장 형식이 바뀌었는데 EEPROM 초기화 키 결정이 기록되지 않았는가

사용법:
  python tools/era_doc_refs.py                          검사. 발견 0건이면 exit 0
  python tools/era_doc_refs.py --record-storage "사유"  현재 초기화 키와 저장 형식을 기록
  python tools/era_doc_refs.py --record-storage "사유" --index
                                                    staged index 기준으로 기록 (훅이 보는 것)
"""

from __future__ import annotations

import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOC_DIR = ROOT / "docs"
MAP_DOC = DOC_DIR / "MAP.md"
ENTRY_DOCS = [ROOT / "AGENTS.md", ROOT / "CLAUDE.md"]
USER_DOC = DOC_DIR / "readme.txt"

# 외부 짝 저장소·규약 저장소 포인터. 로컬 경로 검사에서는 접두사만 식별하고 건너뛴다.
FOREIGN_REPOS = (
    "the-via-eerraa/",
    "qmk_firmware_eerraa/",
    "eerraa-qmk-h7s-boot/",
    "eerraa-54lm20-fw/",
    "eerraa-agent-docs/",
)

# 폐기된 서브시스템. src/에 0건이어야 하고, 이 목록이 곧 symbol 검사의 예외다 —
# 문서는 이 이름들을 "없는 것"으로 부를 수 있어야 한다.
RETIRED_SYMBOLS = (
    "usbMonitor",
    "usbInstability",
    "usbHidMonitor",
    "usbRequestBootModeDowngrade",
    "usbd_hid_instrumentation",
    "USB_MONITOR_ENABLE",
    "auto_downgrade",
)

BOARD_ROOT = ROOT / "src/ap/modules/qmk/keyboards/era"
VIA_H = ROOT / "src/ap/modules/qmk/quantum/via.h"
HW_DEF = ROOT / "src/hw/hw_def.h"

TICK = re.compile(r"`([^`\n]+)`")
FENCE = re.compile(r"^\s*```")
PATH_TOKEN = re.compile(
    r"^(?P<path>(?:<board>|[A-Za-z0-9_.-])[A-Za-z0-9_./{},*<>-]*"
    r"(?:\.(?:c|h|py|ps1|md|txt|json|JSON|cmake|uf2|html)|/))"
    r"(?::(?P<line>\d+))?$"
)
# `<board>/port/via_port.c`처럼 각 보드에 있는 파일을 가리키는 표기.
BOARD_PREFIX = "<board>/"
IDENT_TOKEN = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
DOCS_REF = re.compile(r"docs/[A-Za-z0-9_./-]+\.(?:md|txt)")
RELEASE_FILE = re.compile(r"-(V\d{6}R\d)\.(?:uf2|JSON)")

# 저장 형식의 원천. 여기서 뽑은 사실이 바뀌면 이미 저장된 바이트를 새 의미로 읽게 되므로,
# ERA_EEPROM_RESET_KEY를 올리거나(전체 초기화) 호환된다는 판단을 사유와 함께 기록해야 한다.
STORAGE_RECORD = ROOT / "tools/eeprom_reset_key.json"
STORAGE_DEFINES = (
    ("src/ap/modules/qmk/port/port.h", r"EECONFIG_USER_\w+"),
    ("src/ap/modules/qmk/quantum/eeconfig.h", r"EECONFIG_\w+"),
    ("src/ap/modules/qmk/quantum/via.h", r"VIA_EEPROM_\w+"),
    ("src/ap/modules/qmk/quantum/dynamic_keymap.c", r"DYNAMIC_KEYMAP_\w+"),
    ("src/ap/modules/qmk/port/version.h", r"QMK_BUILDDATE"),
    ("src/hw/hw_def.h", r"ERA_EEPROM_RESET_GUARD_\w+"),
    ("src/ap/modules/qmk/port/debounce_profile.c", r"\w+_(?:SIGNATURE|VERSION)"),
    ("src/ap/modules/qmk/port/mousekey_config.c", r"\w+_(?:SIGNATURE|VERSION)"),
    ("src/ap/modules/qmk/port/rgb_sleep.c", r"\w+_(?:SIGNATURE|VERSION)"),
    ("src/ap/modules/qmk/port/tapdance.c", r"\w+_(?:SIGNATURE|VERSION)"),
    ("src/ap/modules/qmk/port/tapdance.h", r"TAPDANCE_\w+_COUNT"),
    ("src/ap/modules/qmk/port/tapping_term.c", r"\w+_(?:SIGNATURE|VERSION)"),
)
# 보드 config.h에서 저장 위치·크기·RGB 모드 번호를 바꾸는 값.
STORAGE_BOARD_DEFINES = (
    r"EECONFIG_\w+|TOTAL_EEPROM_BYTE_COUNT|DYNAMIC_KEYMAP_\w+|MATRIX_ROWS|MATRIX_COLS"
    r"|VIA_EEPROM_\w+|ERA_EEPROM_RESET_\w+|RGBLIGHT_EFFECT_\w+|RGBLIGHT_ANIMATIONS"
    r"|NUM_ENCODERS|ENCODERS_\w+"
)
# EEPROM에 그대로 쓰이는 타입.
STORAGE_TYPES = (
    "debounce_profile_storage_t",
    "mousekey_config_storage_t",
    "rgb_sleep_storage_t",
    "tapdance_slot_storage_t",
    "tapdance_storage_t",
    "tapping_term_storage_t",
    "kill_switch_config_t",
    "kkuk_config_t",
    "rgblight_indicator_config_t",
    "rgblight_config_t",
    "keymap_config_t",
    "debug_config_t",
    "UsbBootMode_t",
)
# 순서가 곧 저장값인 파일 (RGB 모드 번호, 키맵에 저장되는 키코드 번호).
STORAGE_FILES = (
    "src/ap/modules/qmk/quantum/rgblight/rgblight_modes.h",
    "src/ap/modules/qmk/quantum/keycodes.h",
)
# 어떤 값을 어느 USER 슬롯 오프셋에 쓰는가. 보드 port 파일의 `+ 4` 같은 하위 오프셋도 여기서 보인다.
SLOT_WRITER = re.compile(r"EECONFIG_DEBOUNCE_HELPER(?:_CHECKED)?\s*\(([^;]*)\)\s*;")
# 부팅 reset guard가 키를 읽고 쓰는 소스. 어느 것도 펌웨어 버전을 키로 쓰면 안 된다.
RESET_GUARD_SOURCES = (
    "src/hw/hw_def.h",
    "src/hw/driver/eeprom_reset_guard.c",
    "src/ap/modules/qmk/port/platforms/eeprom.c",
    "src/ap/modules/qmk/port/eeconfig_port.c",
)
RESET_KEY = re.compile(r"^#define\s+ERA_EEPROM_RESET_KEY\s+(0x[0-9A-Fa-f]{8})U?\b", re.M)

findings: list[str] = []


def report(check: str, where: str, message: str) -> None:
    findings.append(f"[{check}] {where}: {message}")


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="strict")


def agent_docs() -> list[Path]:
    return sorted(DOC_DIR.glob("*.md")) + [p for p in ENTRY_DOCS if p.exists()]


def prose_lines(text: str):
    """펜스 코드 블록을 뺀 (줄번호, 줄)만 돌려준다. 인용된 남의 코드는 검사 대상이 아니다."""
    fenced = False
    for number, line in enumerate(text.splitlines(), 1):
        if FENCE.match(line):
            fenced = not fenced
            continue
        if not fenced:
            yield number, line


def source_blob() -> str:
    """symbol 검사가 "실재한다"고 인정하는 텍스트.

    문서 도구 자신(`era_doc_refs*.py`)은 뺀다 — 검사기나 자기검사가 어떤 이름을 문자열로
    들고 있다는 것은 그 심볼이 펌웨어에 있다는 뜻이 아니다.
    """
    parts = []
    for base in (ROOT / "src", ROOT / "tools"):
        for path in base.rglob("*"):
            if path.name.startswith("era_doc_refs"):
                continue
            if path.is_file() and path.suffix.lower() in {
                ".c", ".h", ".py", ".ps1", ".cmake", ".json", ".txt",
            }:
                parts.append(path.read_text(encoding="utf-8", errors="ignore"))
    for path in ROOT.rglob("CMakeLists.txt"):
        parts.append(path.read_text(encoding="utf-8", errors="ignore"))
    for path in (ROOT / "tools").rglob("*.cmake"):
        parts.append(path.read_text(encoding="utf-8", errors="ignore"))
    return "\n".join(parts)


def firmware_version() -> str:
    match = re.search(r'_DEF_FIRMWARE_VERSION\s+"(V\d{6}R\d)"', read(HW_DEF))
    if not match:
        raise SystemExit("hw_def.h에서 _DEF_FIRMWARE_VERSION을 찾지 못했다")
    return match.group(1)


def expand_braces(token: str) -> list[str]:
    match = re.match(r"^(.*)\{([^}]*)\}(.*)$", token)
    if not match:
        return [token]
    head, body, tail = match.groups()
    return [head + part.strip() + tail for part in body.split(",")]


# --------------------------------------------------------------------------
# 소스에서 다시 계산하는 사실
# --------------------------------------------------------------------------

def boards() -> list[dict]:
    rows = []
    for via_port in sorted(BOARD_ROOT.rglob("port/via_port.c")):
        board_dir = via_port.parent.parent
        definitions = sorted(board_dir.glob("json/*-VIA.JSON"))
        if not definitions:
            report("menu", str(board_dir.relative_to(ROOT)), "VIA JSON이 없다")
            continue
        data = json.loads(read(definitions[0]))
        rows.append(
            {
                "name": data.get("name", "?"),
                "dir": board_dir.relative_to(ROOT).as_posix(),
                "json": definitions[0].relative_to(ROOT).as_posix(),
                "via_port": via_port,
            }
        )
    return sorted(rows, key=lambda row: row["name"])


def channel_map() -> dict[str, int]:
    text = read(VIA_H)
    block = re.search(r"enum via_channel_id\s*\{(.*?)\}", text, re.S)
    if not block:
        raise SystemExit("via.h에서 via_channel_id enum을 찾지 못했다")
    return {
        name: int(value)
        for name, value in re.findall(r"(id_\w+)\s*=\s*(\d+)", block.group(1))
    }


def routed_channels(via_port: Path) -> set[str]:
    text = read(via_port)
    known = channel_map()
    return {name for name in known if re.search(rf"\b{name}\b", text)}


def exposed_channels(definition: Path) -> set[int]:
    text = read(definition)
    return {
        int(value)
        for value in re.findall(r'"content":\s*\[\s*"[^"]+",\s*(\d+),', text)
    }


# --------------------------------------------------------------------------
# 검사
# --------------------------------------------------------------------------

def check_paths_and_symbols() -> None:
    blob = source_blob()
    retired_lower = tuple(symbol for symbol in RETIRED_SYMBOLS)
    for doc in agent_docs():
        where_base = doc.relative_to(ROOT).as_posix()
        for number, line in prose_lines(read(doc)):
            where = f"{where_base}:{number}"
            for token in TICK.findall(line):
                token = token.strip()
                match = PATH_TOKEN.match(token)
                if match and ("/" in token):
                    if token.startswith(FOREIGN_REPOS):
                        continue
                    _check_one_path(where, match)
                    continue
                base = token[:-2] if token.endswith("()") else token
                if not IDENT_TOKEN.match(base) or len(base) < 4:
                    continue
                if "_" not in base and not re.search(r"[a-z][A-Z]", base):
                    continue
                if any(base.startswith(symbol) for symbol in retired_lower):
                    continue
                if base + "/" in FOREIGN_REPOS:  # 짝 저장소 이름은 심볼이 아니다
                    continue
                if base not in blob:
                    report("symbol", where, f"`{base}`가 src/·tools/에 없다")


def _check_one_path(where: str, match: re.Match) -> None:
    raw = match.group("path")
    line_no = match.group("line")
    if raw.startswith(BOARD_PREFIX):
        tail = raw[len(BOARD_PREFIX) :]
        for row in boards():
            if not (ROOT / row["dir"] / tail).exists():
                report("path", where, f"`{raw}` — {row['name']}에 `{tail}`이 없다")
        return
    for candidate in expand_braces(raw):
        if "*" in candidate:
            if not list(ROOT.glob(candidate)):
                report("path", where, f"`{candidate}`와 일치하는 파일이 없다")
            continue
        target = ROOT / candidate
        if not target.exists():
            report("path", where, f"`{candidate}`가 없다")
            continue
        if line_no and target.is_file():
            total = len(target.read_bytes().splitlines())
            if int(line_no) > total:
                report("path", where, f"`{candidate}:{line_no}` — 파일은 {total}줄이다")


def check_index() -> None:
    if not MAP_DOC.exists():
        report("index", "docs/MAP.md", "색인 문서가 없다")
        return
    linked = set()
    for target in re.findall(r"\[[^\]]+\]\(([^)]+)\)", read(MAP_DOC)):
        if target.startswith(("http", "#")):
            continue
        resolved = (DOC_DIR / target).resolve()
        if not resolved.exists():
            report("index", "docs/MAP.md", f"색인이 없는 파일을 가리킨다 — {target}")
            continue
        linked.add(resolved)
    for doc in sorted(DOC_DIR.iterdir()):
        if doc.name == MAP_DOC.name or doc.is_dir():
            continue
        if doc.resolve() not in linked:
            report("index", doc.relative_to(ROOT).as_posix(), "MAP.md 색인에서 도달할 수 없다")


def check_retired() -> None:
    for symbol in RETIRED_SYMBOLS:
        hits = [
            path.relative_to(ROOT).as_posix()
            for path in (ROOT / "src").rglob("*")
            if path.is_file()
            and path.suffix.lower() in {".c", ".h", ".json", ".txt"}
            and symbol in path.read_text(encoding="utf-8", errors="ignore")
        ]
        if hits:
            report("retired", hits[0], f"폐기된 `{symbol}`이 되살아났다 ({len(hits)}개 파일)")


def check_menu() -> None:
    known = channel_map()
    for row in boards():
        routed = routed_channels(row["via_port"])
        exposed = exposed_channels(ROOT / row["json"])
        missing = sorted(known[name] for name in routed if known[name] not in exposed)
        if missing:
            report(
                "menu",
                row["json"],
                f"펌웨어가 라우팅하는 채널 {missing}이 이 JSON에 없다 — 사용자가 도달할 수 없다",
            )


def check_source_comments() -> None:
    """소스 주석이 문서를 부르면 그 문서가 실재해야 한다.

    문서 쪽만 검사하면 문서를 지운 뒤 소스 주석이 계속 가리키는 반대 방향 드리프트가 남는다.
    """
    targets = sorted((ROOT / "src").rglob("*.c")) + sorted((ROOT / "src").rglob("*.h"))
    targets += [ROOT / "CMakeLists.txt"] + sorted((ROOT / "src").rglob("CMakeLists.txt"))
    for path in targets:
        if not path.exists():
            continue
        where_base = path.relative_to(ROOT).as_posix()
        for number, line in enumerate(
            path.read_text(encoding="utf-8", errors="ignore").splitlines(), 1
        ):
            for ref in DOCS_REF.findall(line):
                if not (ROOT / ref).exists():
                    report("comment", f"{where_base}:{number}", f"`{ref}`가 없다")


def check_release_version() -> None:
    current = firmware_version()
    for number, line in enumerate(read(USER_DOC).splitlines(), 1):
        for literal in RELEASE_FILE.findall(line):
            if literal != current:
                report(
                    "version",
                    f"docs/readme.txt:{number}",
                    f"릴리스 파일명이 {literal} — 현재 펌웨어는 {current}다",
                )


def strip_c(text: str) -> str:
    """주석을 지우고 줄 잇기를 합친다. 주석·정렬만 바뀐 것은 형식 변경이 아니다."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text.replace("\\\n", " ")


def c_defines(text: str, name_pattern: str) -> list[str]:
    wanted = re.compile(rf"(?:{name_pattern})")
    rows = []
    for match in re.finditer(r"^\s*#\s*define\s+(\w+)(\(?)([^\n]*)$", strip_c(text), re.M):
        name, function_like, value = match.groups()
        if function_like or not wanted.fullmatch(name):
            continue
        rows.append(f"{name} {' '.join(value.split())}")
    return rows


def c_typedef(text: str, name: str) -> str | None:
    text = strip_c(text)
    for match in re.finditer(r"typedef\s+(?:struct|union|enum)\b[^{;]*\{", text):
        depth, index = 1, match.end()
        while depth and index < len(text):
            depth += {"{": 1, "}": -1}.get(text[index], 0)
            index += 1
        tail = re.match(r"\s*(\w+)\s*;", text[index:])
        if tail and tail.group(1) == name:
            return " ".join(text[match.start():index].split())
    return None


def storage_groups(root: Path = ROOT) -> dict[str, str]:
    """저장 형식 사실을 묶음별 해시로 돌려준다. 기록 JSON의 diff가 어느 묶음이 바뀌었는지 보여 준다."""
    groups: dict[str, list[str]] = {}
    for relative, pattern in STORAGE_DEFINES:
        groups[f"define {relative}"] = c_defines(read(root / relative), pattern)
    for config in sorted((root / BOARD_ROOT.relative_to(ROOT)).rglob("config.h")):
        relative = config.relative_to(root).as_posix()
        groups[f"board {relative}"] = sorted(c_defines(read(config), STORAGE_BOARD_DEFINES))
    sources = sorted((root / "src/ap/modules/qmk").rglob("*.[ch]"))
    # 저장 enum 일부(BootMode)는 USB 드라이버 헤더가 소유한다.
    type_sources = sources + sorted((root / "src/hw").rglob("*.h"))
    for name in STORAGE_TYPES:
        bodies = [body for path in type_sources if name in path.read_text(encoding="utf-8", errors="ignore")
                  for body in [c_typedef(read(path), name)] if body]
        if len(bodies) != 1:
            report("storage", "tools/era_doc_refs.py", f"저장 타입 `{name}` 정의가 {len(bodies)}개다")
        groups[f"type {name}"] = bodies
    for relative in STORAGE_FILES:
        groups[f"file {relative}"] = [" ".join(strip_c(read(root / relative)).split())]
    groups["slots"] = sorted(
        f"{path.relative_to(root).as_posix()}: {' '.join(match.split())}"
        for path in sources for match in SLOT_WRITER.findall(strip_c(read(path))))
    return {
        name: hashlib.sha256("\n".join(rows).encode("utf-8")).hexdigest()[:16]
        for name, rows in groups.items()
    }


def reset_key(root: Path = ROOT) -> str | None:
    match = RESET_KEY.search(read(root / HW_DEF.relative_to(ROOT)))
    return match.group(1).upper().replace("0X", "0x") if match else None


def check_storage() -> None:
    key = reset_key()
    if key is None:
        report("storage", "src/hw/hw_def.h", "`ERA_EEPROM_RESET_KEY` 정의가 없다")
        return
    guard = read(ROOT / RESET_GUARD_SOURCES[1])
    for relative in RESET_GUARD_SOURCES:
        coupled = [
            number for number, line in enumerate(strip_c(read(ROOT / relative)).splitlines(), 1)
            if "_DEF_FIRMWARE_VERSION" in line and not re.match(r"\s*#define\s+_DEF_FIRMWARE_VERSION\b", line)
        ]
        if coupled:
            report("storage", f"{relative}:{coupled[0]}",
                   "reset guard는 `ERA_EEPROM_RESET_KEY`만 따라야 한다 — 펌웨어 버전에 다시 묶으면 "
                   "모든 업데이트가 전체 초기화가 된다 (docs/contract_eeprom.md §2)")
    if "== ERA_EEPROM_RESET_KEY" not in " ".join(strip_c(guard).split()):
        report("storage", RESET_GUARD_SOURCES[1], "부팅 reset guard가 `ERA_EEPROM_RESET_KEY`와 비교하지 않는다")
    if not STORAGE_RECORD.exists():
        report("storage", "tools/eeprom_reset_key.json", "기록이 없다 — `--record-storage \"사유\"`로 만든다")
        return
    record = json.loads(read(STORAGE_RECORD))
    if record.get("reset_key") != key:
        report("storage", "src/hw/hw_def.h",
               f"`ERA_EEPROM_RESET_KEY` {key}가 기록 {record.get('reset_key')}와 다르다 — "
               "전체 초기화를 수용했다면 `--record-storage \"사유\"`로 기록한다")
    current, recorded = storage_groups(), record.get("groups", {})
    changed = sorted(name for name in current.keys() | recorded.keys()
                     if current.get(name) != recorded.get(name))
    if changed:
        report("storage", "tools/eeprom_reset_key.json",
               f"저장 형식이 기록과 다르다 ({', '.join(changed[:4])}{' …' if len(changed) > 4 else ''}). "
               "기존 저장값이 새 형식에서 그대로 유효하지 않으면 `ERA_EEPROM_RESET_KEY`를 올리고, "
               "유효하면 키를 유지한 채 `--record-storage \"사유\"`로 다시 기록한다. 커밋에 넣지 않을 "
               "변경이 작업 트리에 있으면 `--index`로 staged 기준 기록 (docs/contract_eeprom.md §2)")


def record_storage(reason: str, staged: bool) -> int:
    """작업 트리, 또는 `staged`면 훅이 검사하는 staged index 스냅샷에서 기록한다."""
    snapshot = Path(tempfile.mkdtemp(prefix="h7s-storage-")) if staged else None
    try:
        if snapshot is not None:
            subprocess.run(["git", "checkout-index", "--all", f"--prefix={snapshot.as_posix()}/"],
                           cwd=ROOT, check=True)
        root = snapshot or ROOT
        key = reset_key(root)
        groups = storage_groups(root) if key else {}
    finally:
        if snapshot is not None:
            shutil.rmtree(snapshot, ignore_errors=True)
    if key is None:
        print("hw_def.h에서 ERA_EEPROM_RESET_KEY를 찾지 못했다")
        return 1
    if findings:
        print("\n".join(findings))
        return 1
    record = {"reset_key": key, "reason": reason, "groups": dict(sorted(groups.items()))}
    STORAGE_RECORD.write_text(json.dumps(record, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"기록했다: {STORAGE_RECORD.relative_to(ROOT).as_posix()} (키 {key}, 묶음 {len(groups)}개)")
    return 0


def main() -> int:
    if len(sys.argv) in (3, 4) and sys.argv[1] == "--record-storage" and sys.argv[2].strip() \
            and sys.argv[3:] in ([], ["--index"]):
        return record_storage(sys.argv[2].strip(), staged=sys.argv[3:] == ["--index"])
    if len(sys.argv) != 1:
        print(__doc__)
        return 2

    check_paths_and_symbols()
    check_source_comments()
    check_index()
    check_retired()
    check_menu()
    check_release_version()
    check_storage()

    if findings:
        for finding in findings:
            print(finding)
        print(f"\nFAIL {len(findings)}건")
        return 1
    print(f"PASS 로컬 문서/소스/계약 정합 (기준 펌웨어 {firmware_version()})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
