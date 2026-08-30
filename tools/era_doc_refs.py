#!/usr/bin/env python3
"""문서-코드 정합 검사기.

문서가 코드에서 어긋나면 여기서 빨개진다. 아홉 가지를 본다.

  path     백틱 안 저장소 경로가 실재하는가 (`:line`, `{a,b}`, `*` 지원)
  comment  소스 주석이 부르는 docs/ 경로가 실재하는가
  header   docs/*.md가 Genre / Canonical for 두 줄을 선언하는가.
           Status: / Read when: 은 이 저장소에 값이 바뀌는 ADR이 없어 금지
  index    docs/ 아래 모든 문서가 docs/MAP.md 색인에서 도달 가능한가
  symbol   백틱 안 식별자가 src/ 또는 tools/에 실재하는가
  retired  폐기된 서브시스템의 심볼이 src/에 되살아나지 않았는가
  table    문서의 생성 표가 소스에서 다시 계산한 값과 같은가
  menu     펌웨어가 라우팅하는 VIA 채널이 보드 JSON에서 도달 가능한가
  version  문서의 버전 리터럴이 현재 펌웨어 버전을 넘지 않는가

사용법:
  python tools/era_doc_refs.py            검사. 발견 0건이면 exit 0
  python tools/era_doc_refs.py --tables   생성 표를 찍는다 (문서에 붙여넣기)
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOC_DIR = ROOT / "docs"
MAP_DOC = DOC_DIR / "MAP.md"
ENTRY_DOCS = [ROOT / "AGENTS.md", ROOT / "CLAUDE.md"]
USER_DOC = DOC_DIR / "readme.txt"

GENRES = ("contract", "map", "manual", "state", "entry")
# 값이 바뀌는 ADR이 생기면 Status: 만 그 파일에서 허용한다. 지금은 없다.
FORBIDDEN_HEADERS = ("Status:", "Read when:")

# 같은 PC의 짝 저장소·규약 저장소. 경로 검사는 접두사만 보고 통과시킨다.
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
PORT_H = ROOT / "src/ap/modules/qmk/port/port.h"
HW_DEF = ROOT / "src/hw/hw_def.h"

TABLE_BEGIN = re.compile(r"^<!-- era-doc-refs: (?P<name>[a-z-]+) -->$")
TABLE_END = "<!-- era-doc-refs: end -->"

TICK = re.compile(r"`([^`\n]+)`")
FENCE = re.compile(r"^\s*```")
PATH_TOKEN = re.compile(
    r"^(?P<path>(?:<board>|[A-Za-z0-9_.-])[A-Za-z0-9_./{},*<>-]*"
    r"(?:\.(?:c|h|py|ps1|md|txt|json|JSON|cmake|uf2|html)|/))"
    r"(?::(?P<line>\d+))?$"
)
# `<board>/port/via_port.c`처럼 다섯 보드에 각각 있는 파일을 가리키는 표기.
BOARD_PREFIX = "<board>/"
IDENT_TOKEN = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
DOCS_REF = re.compile(r"docs/[A-Za-z0-9_./-]+\.(?:md|txt)")
VERSION_LITERAL = re.compile(r"\bV\d{6}R\d\b")
RELEASE_FILE = re.compile(r"-(V\d{6}R\d)\.(?:uf2|JSON)")

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
            report("table", str(board_dir.relative_to(ROOT)), "VIA JSON이 없다")
            continue
        data = json.loads(read(definitions[0]))
        rows.append(
            {
                "name": data.get("name", "?"),
                "dir": board_dir.relative_to(ROOT).as_posix(),
                "json": definitions[0].relative_to(ROOT).as_posix(),
                "pid": data.get("productId", "?"),
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


def eeprom_slots() -> list[tuple[str, int, str]]:
    rows = []
    for name, offset, size in re.findall(
        r"#define\s+(EECONFIG_USER_\w+)\s+.*?DATABLOCK\s*\+\s*(\d+)\)\)\s*//\s*(\d+B)",
        read(PORT_H),
    ):
        rows.append((name, int(offset), size))
    return sorted(rows, key=lambda row: row[1])


# --------------------------------------------------------------------------
# 생성 표
# --------------------------------------------------------------------------

def table_boards() -> list[str]:
    lines = ["| Board | Source | Official VIA JSON (under json/) | PID |", "| --- | --- | --- | --- |"]
    for row in boards():
        lines.append(
            f"| {row['name']} | `{row['dir']}/` |"
            f" `{row['json'].rsplit('/', 1)[1]}` | `{row['pid']}` |"
        )
    return lines


def table_channels() -> list[str]:
    known = channel_map()
    board_rows = boards()
    routed: set[str] = set()
    for row in board_rows:
        routed |= routed_channels(row["via_port"])
    exposed: set[int] = set()
    for row in board_rows:
        exposed |= exposed_channels(ROOT / row["json"])
    lines = ["| Channel | Name | Firmware routing | Board JSON exposure |", "| --- | --- | --- | --- |"]
    for name, number in sorted(known.items(), key=lambda item: item[1]):
        lines.append(
            f"| {number} | `{name}` |"
            f" {'O' if name in routed else '-'} |"
            f" {'O' if number in exposed else '-'} |"
        )
    return lines


def table_eeprom() -> list[str]:
    lines = ["| Offset | Size | Symbol |", "| --- | --- | --- |"]
    for name, offset, size in eeprom_slots():
        lines.append(f"| +{offset} | {size} | `{name}` |")
    return lines


def _enum_values(enum_name: str, pattern: str) -> list[tuple[str, int]]:
    text = read(VIA_H)
    block = re.search(rf"enum {enum_name}\s*\{{(.*?)\}}", text, re.S)
    if not block:
        raise SystemExit(f"via.h에서 {enum_name} enum을 찾지 못했다")
    return [
        (name, int(value))
        for name, value in re.findall(r"(id_\w+)\s*=\s*(\d+)", block.group(1))
        if re.search(pattern, name)
    ]


def _span(values: list[tuple[str, int]]) -> str:
    numbers = sorted(number for _, number in values)
    if not numbers:
        raise SystemExit("value id를 찾지 못했다")
    if numbers[-1] - numbers[0] + 1 != len(numbers):
        raise SystemExit(f"value id가 연속이 아니다 — {numbers}")
    return str(numbers[0]) if len(numbers) == 1 else f"{numbers[0]}–{numbers[-1]}"


def table_wire_values() -> list[str]:
    channels = channel_map()
    rows = [
        ("Global TAPPING term (exact)", channels["id_qmk_tapping"],
         _span(_enum_values("via_qmk_tapping_value", r"_term_exact$"))),
        ("TD0–TD7 term (exact)", channels["id_qmk_tapdance"],
         _span(_enum_values("via_qmk_tapdance_value", r"_term_exact$"))),
        ("MOUSE six controls", channels["id_qmk_mousekey"],
         _span(_enum_values("via_qmk_mousekey_value", r"^id_qmk_mousekey_"))),
    ]
    lines = ["| Control | Channel | value id |", "| --- | --- | --- |"]
    for label, channel, span in rows:
        lines.append(f"| {label} | {channel} | {span} |")
    return lines


TABLES = {
    "boards": table_boards,
    "via-channels": table_channels,
    "eeprom-slots": table_eeprom,
    "wire-values": table_wire_values,
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


def _forbidden_header_lines(path: Path) -> None:
    where_base = path.relative_to(ROOT).as_posix()
    for number, line in enumerate(read(path).splitlines(), 1):
        for header in FORBIDDEN_HEADERS:
            if line.startswith(header):
                report(
                    "header",
                    f"{where_base}:{number}",
                    f"`{header}` 필드는 이 저장소에서 쓰지 않는다",
                )


def check_headers() -> None:
    for doc in sorted(DOC_DIR.glob("*.md")):
        head = read(doc).splitlines()[:8]
        where = doc.relative_to(ROOT).as_posix()
        genre = next((l for l in head if l.startswith("Genre:")), None)
        canonical = next((l for l in head if l.startswith("Canonical for:")), None)
        if genre is None:
            report("header", where, "`Genre:` 줄이 없다")
        elif genre.split(":", 1)[1].strip() not in GENRES:
            report("header", where, f"Genre 값이 {GENRES} 밖이다 — {genre}")
        if canonical is None:
            report("header", where, "`Canonical for:` 줄이 없다")
        elif not canonical.split(":", 1)[1].strip():
            report("header", where, "`Canonical for:`가 비어 있다")
        _forbidden_header_lines(doc)
    for doc in ENTRY_DOCS:
        if doc.exists():
            _forbidden_header_lines(doc)


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


def check_tables() -> None:
    for doc in sorted(DOC_DIR.glob("*.md")):
        lines = read(doc).splitlines()
        where_base = doc.relative_to(ROOT).as_posix()
        index = 0
        while index < len(lines):
            begin = TABLE_BEGIN.match(lines[index].strip())
            if not begin:
                index += 1
                continue
            name = begin.group("name")
            try:
                end = lines.index(TABLE_END, index)
            except ValueError:
                report("table", f"{where_base}:{index + 1}", f"`{name}` 블록이 닫히지 않았다")
                return
            if name not in TABLES:
                report("table", f"{where_base}:{index + 1}", f"모르는 표 이름 — {name}")
                index = end + 1
                continue
            actual = [line.rstrip() for line in lines[index + 1 : end] if line.strip()]
            expected = TABLES[name]()
            if actual != expected:
                report(
                    "table",
                    f"{where_base}:{index + 1}",
                    f"`{name}` 표가 소스와 다르다 — `python tools/era_doc_refs.py --tables`로 다시 받아라",
                )
            index = end + 1


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

    문서 쪽만 검사하면 반대 방향 드리프트가 남는다 — 문서를 지웠는데 소스 주석이 계속
    그것을 가리키는 경우다. 실제로 이번 문서 재편이 그런 주석을 둘 남겼었다.
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


def check_versions() -> None:
    current = firmware_version()
    for doc in agent_docs() + [USER_DOC]:
        where_base = doc.relative_to(ROOT).as_posix()
        for number, line in enumerate(read(doc).splitlines(), 1):
            for literal in VERSION_LITERAL.findall(line):
                if literal > current:
                    report(
                        "version",
                        f"{where_base}:{number}",
                        f"{literal}은 현재 펌웨어 {current}보다 높다",
                    )
    for number, line in enumerate(read(USER_DOC).splitlines(), 1):
        for literal in RELEASE_FILE.findall(line):
            if literal != current:
                report(
                    "version",
                    f"docs/readme.txt:{number}",
                    f"릴리스 파일명이 {literal} — 현재 펌웨어는 {current}다",
                )


def main() -> int:
    if "--tables" in sys.argv:
        for name, builder in TABLES.items():
            print(f"<!-- era-doc-refs: {name} -->")
            for line in builder():
                print(line)
            print(TABLE_END)
            print()
        return 0

    check_paths_and_symbols()
    check_source_comments()
    check_headers()
    check_index()
    check_retired()
    check_tables()
    check_menu()
    check_versions()

    if findings:
        for finding in findings:
            print(finding)
        print(f"\nFAIL {len(findings)}건")
        return 1
    print(f"PASS 문서-코드 정합 9종 (기준 펌웨어 {firmware_version()})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
