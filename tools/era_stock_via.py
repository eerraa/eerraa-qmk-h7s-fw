#!/usr/bin/env python3
"""Check every H7S STOCK VIA definition against its board config.h.

The commit hook runs this from its staged checkout-index snapshot. No compiler,
QMK installation, peer checkout or working-tree Git metadata is required.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BOARD_ROOT = "src/ap/modules/qmk/keyboards/era"
ENGINES = ("backlight", "rgblight", "rgb_matrix", "led_matrix")
MODULES = {
    frozenset(): None,
    frozenset(("backlight",)): "qmk_backlight_keycodes",
    frozenset(("rgblight",)): "qmk_rgblight_keycodes",
    frozenset(("backlight", "rgblight")): "qmk_backlight_rgblight_keycodes",
    frozenset(("rgb_matrix",)): "qmk_rgb_matrix_keycodes",
}
# Single indicator-only declaration: empty. Every current board config.h
# (comm/intigrity80, keynetix/may65, sirind/brick60, brick65, sculpturei)
# enables RGBLIGHT_ENABLE and no BACKLIGHT_ENABLE. Indicators sharing those
# RGB LEDs do not turn the independently controllable rgblight into an exception.
INDICATOR_ONLY = {}
FACT_MACROS = {"USB_VID", "USB_PID", "MATRIX_ROWS", "MATRIX_COLS", "SPLIT_KEYBOARD"}
FEATURE_MACROS = {engine.upper() + "_ENABLE": engine for engine in ENGINES}


class Tree:
    def __init__(self, root=REPO):
        self.root = Path(root)

    def read(self, rel):
        return (self.root / rel).read_text(encoding="utf-8")

    def definitions(self):
        return sorted(p.relative_to(self.root).as_posix()
                      for p in self.root.glob(BOARD_ROOT + "/**/json/*-VIA.JSON"))


def integer(value):
    if type(value) is int:
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise ValueError(f"not an integer USB id: {value!r}")


def load_json(text):
    def invalid(value):
        raise ValueError(f"invalid JSON constant {value}")
    value = json.loads(text, parse_constant=invalid)
    if not isinstance(value, dict):
        raise ValueError("JSON root must be an object")
    return value


def defines(text):
    """Board facts are unconditional literals; do not guess C expressions."""
    text = re.sub(r"/\*[\s\S]*?\*/|//[^\n]*", "", text)
    found, depth = {}, 0
    for line in text.splitlines():
        directive = re.match(r"\s*#\s*(\w+)\b(.*)", line)
        if not directive:
            continue
        op, rest = directive.groups()
        if op in ("if", "ifdef", "ifndef"):
            depth += 1
        elif op == "endif":
            depth -= 1
        elif op in ("define", "undef"):
            parts = rest.split(None, 1)
            if parts and parts[0] in FACT_MACROS | set(FEATURE_MACROS):
                if depth:
                    raise ValueError(f"conditional {parts[0]} needs an explicit checker rule")
                if op == "undef":
                    found.pop(parts[0], None)
                else:
                    found[parts[0]] = parts[1].strip() if len(parts) > 1 else ""
    return found


def lighting_module(board, features):
    engines = frozenset(features)
    if engines not in MODULES:
        raise ValueError(f"{board}: unsupported lighting feature combination: {', '.join(sorted(engines))}")
    return MODULES[engines]


def board_facts(tree, rel):
    folder = Path(rel).parent.parent.as_posix()
    board = folder.split("/keyboards/", 1)[1]
    macros = defines(tree.read(folder + "/config.h"))
    if "SPLIT_KEYBOARD" in macros:
        raise ValueError(f"{board}: H7S has no shipped split USB/matrix rule; define one before shipping")
    features = {engine for macro, engine in FEATURE_MACROS.items() if macro in macros}
    # usb_hid/usbd_desc.c and usb_cmp/usbd_desc.c use USB_VID and USB_PID
    # directly in idVendor/idProduct: H7S has no split-half PID transformation.
    return board, features, integer(macros["USB_VID"]), integer(macros["USB_PID"]), {
        "rows": integer(macros["MATRIX_ROWS"]), "cols": integer(macros["MATRIX_COLS"]),
    }


def validate(board, definition, features, vid, pid, matrix):
    keycodes = definition.get("keycodes", [])
    if not isinstance(keycodes, list) or any(not isinstance(k, str) for k in keycodes):
        raise ValueError(f"{board}: keycodes must be a string array")
    if "qmk_lighting" in keycodes:
        raise ValueError(f"{board}: forbidden qmk_lighting in keycodes")
    expected = lighting_module(board, features)
    lighting = [k for k in keycodes if re.match(r"qmk_(backlight|rgblight|rgb_matrix|led_matrix).*keycodes$", k)]
    if lighting != ([expected] if expected else []):
        raise ValueError(f"{board}: lighting module {lighting!r}; expected {expected!r}")
    for field, expected_id in (("vendorId", vid), ("productId", pid)):
        if integer(definition[field]) != expected_id:
            raise ValueError(f"{board}: {field} {definition[field]!r}; expected 0x{expected_id:04X}")
    actual = definition["matrix"]
    if any(type(actual.get(axis)) is not int or actual[axis] != matrix[axis] for axis in ("rows", "cols")):
        raise ValueError(f"{board}: matrix {actual!r}; expected {matrix!r}")


def check_all(tree):
    paths, errors = tree.definitions(), []
    if not paths:
        return 0, ["no STOCK VIA definitions found; refusing an empty scan"]
    for rel in paths:
        try:
            board, features, vid, pid, matrix = board_facts(tree, rel)
            validate(board, load_json(tree.read(rel)), features, vid, pid, matrix)
        except (OSError, ValueError, KeyError, TypeError) as exc:
            errors.append(f"{rel}: {exc}")
    return len(paths), errors


def main():
    count, errors = check_all(Tree())
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"STOCK VIA: {count} definitions match firmware lighting, USB and matrix")
    return 0


if __name__ == "__main__":
    sys.exit(main())
