"""Run the production macro reader against bounded, in-memory EEPROM fixtures."""
from pathlib import Path

from source_cases import function


def generate(root: Path, build: Path) -> Path:
    quantum = root / "src/ap/modules/qmk/quantum"
    source = (quantum / "dynamic_keymap.c").read_text(encoding="utf-8")
    constants = (quantum / "send_string/send_string_keycodes.h").read_text(encoding="utf-8")
    names = ("SS_QMK_PREFIX", "SS_TAP_CODE", "SS_DOWN_CODE", "SS_UP_CODE", "SS_DELAY_CODE")
    definitions = [line for line in constants.splitlines()
                   if line.startswith("#define ") and line.split()[1] in names]
    assert len(definitions) == len(names)
    target = build / "dynamic_macro.inc"
    target.write_text("\n".join(definitions) + "\n" + function(source, "dynamic_keymap_macro_send") + "\n",
                      encoding="utf-8")
    return root / "tools/firmware_regression_tests/test_dynamic_macro.c"
