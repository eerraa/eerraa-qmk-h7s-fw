"""Pin the pre-alignment keycode ABI and compile checks against the real headers."""
from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
SNAPSHOT = HERE / "keycodes_before_0_0_8.json"
HEADERS = ("keycodes.h", "quantum_keycodes.h", "quantum_keycodes_legacy.h",
           "keymap_extras/keymap_us.h", "steno_keycodes.h")

# The only handler for these codes is quantum.c's BLUETOOTH_ENABLE block.
# No H7S board or build configuration defines BLUETOOTH_ENABLE. These six
# upstream moves are explicitly approved; no legacy H7S aliases are introduced.
MOVED = {
    "QK_OUTPUT_AUTO": (0x7C20, 0x7780),
    "QK_OUTPUT_USB": (0x7C21, 0x7784),
    "QK_OUTPUT_BLUETOOTH": (0x7C22, 0x7786),
    "OU_AUTO": (0x7C20, 0x7780),
    "OU_USB": (0x7C21, 0x7784),
    "OU_BT": (0x7C22, 0x7786),
}

RENAMED = {
    "KC_MS_UP": "QK_MOUSE_CURSOR_UP",
    "KC_MS_DOWN": "QK_MOUSE_CURSOR_DOWN",
    "KC_MS_LEFT": "QK_MOUSE_CURSOR_LEFT",
    "KC_MS_RIGHT": "QK_MOUSE_CURSOR_RIGHT",
    "KC_MS_BTN1": "QK_MOUSE_BUTTON_1",
    "KC_MS_BTN2": "QK_MOUSE_BUTTON_2",
    "KC_MS_BTN3": "QK_MOUSE_BUTTON_3",
    "KC_MS_BTN4": "QK_MOUSE_BUTTON_4",
    "KC_MS_BTN5": "QK_MOUSE_BUTTON_5",
    "KC_MS_BTN6": "QK_MOUSE_BUTTON_6",
    "KC_MS_BTN7": "QK_MOUSE_BUTTON_7",
    "KC_MS_BTN8": "QK_MOUSE_BUTTON_8",
    "KC_MS_WH_UP": "QK_MOUSE_WHEEL_UP",
    "KC_MS_WH_DOWN": "QK_MOUSE_WHEEL_DOWN",
    "KC_MS_WH_LEFT": "QK_MOUSE_WHEEL_LEFT",
    "KC_MS_WH_RIGHT": "QK_MOUSE_WHEEL_RIGHT",
    "KC_MS_ACCEL0": "QK_MOUSE_ACCELERATION_0",
    "KC_MS_ACCEL1": "QK_MOUSE_ACCELERATION_1",
    "KC_MS_ACCEL2": "QK_MOUSE_ACCELERATION_2",
    "KC_MS_U": "MS_UP",
    "KC_MS_D": "MS_DOWN",
    "KC_MS_L": "MS_LEFT",
    "KC_MS_R": "MS_RGHT",
    "KC_BTN1": "MS_BTN1",
    "KC_BTN2": "MS_BTN2",
    "KC_BTN3": "MS_BTN3",
    "KC_BTN4": "MS_BTN4",
    "KC_BTN5": "MS_BTN5",
    "KC_BTN6": "MS_BTN6",
    "KC_BTN7": "MS_BTN7",
    "KC_BTN8": "MS_BTN8",
    "KC_WH_U": "MS_WHLU",
    "KC_WH_D": "MS_WHLD",
    "KC_WH_L": "MS_WHLL",
    "KC_WH_R": "MS_WHLR",
    "KC_ACL0": "MS_ACL0",
    "KC_ACL1": "MS_ACL1",
    "KC_ACL2": "MS_ACL2",
    "RGB_TOG": "QK_UNDERGLOW_TOGGLE",
    "RGB_MODE_FORWARD": "QK_UNDERGLOW_MODE_NEXT",
    "RGB_MODE_REVERSE": "QK_UNDERGLOW_MODE_PREVIOUS",
    "RGB_HUI": "QK_UNDERGLOW_HUE_UP",
    "RGB_HUD": "QK_UNDERGLOW_HUE_DOWN",
    "RGB_SAI": "QK_UNDERGLOW_SATURATION_UP",
    "RGB_SAD": "QK_UNDERGLOW_SATURATION_DOWN",
    "RGB_VAI": "QK_UNDERGLOW_VALUE_UP",
    "RGB_VAD": "QK_UNDERGLOW_VALUE_DOWN",
    "RGB_SPI": "QK_UNDERGLOW_SPEED_UP",
    "RGB_SPD": "QK_UNDERGLOW_SPEED_DOWN",
    "RGB_MOD": "UG_NEXT",
    "RGB_RMOD": "UG_PREV",
    "SH_TG": "QK_SWAP_HANDS_TOGGLE",
    "SQ_TOG": "QK_SEQUENCER_TOGGLE",
    "MAGIC_SWAP_CONTROL_CAPSLOCK": "QK_MAGIC_SWAP_CONTROL_CAPS_LOCK",
    "MAGIC_UNSWAP_CONTROL_CAPSLOCK": "QK_MAGIC_UNSWAP_CONTROL_CAPS_LOCK",
    "MAGIC_TOGGLE_CONTROL_CAPSLOCK": "QK_MAGIC_TOGGLE_CONTROL_CAPS_LOCK",
    "MAGIC_UNCAPSLOCK_TO_CONTROL": "QK_MAGIC_CAPS_LOCK_AS_CONTROL_OFF",
    "MAGIC_CAPSLOCK_TO_CONTROL": "QK_MAGIC_CAPS_LOCK_AS_CONTROL_ON",
    "MAGIC_SWAP_LALT_LGUI": "QK_MAGIC_SWAP_LALT_LGUI",
    "MAGIC_UNSWAP_LALT_LGUI": "QK_MAGIC_UNSWAP_LALT_LGUI",
    "MAGIC_SWAP_RALT_RGUI": "QK_MAGIC_SWAP_RALT_RGUI",
    "MAGIC_UNSWAP_RALT_RGUI": "QK_MAGIC_UNSWAP_RALT_RGUI",
    "MAGIC_UNNO_GUI": "QK_MAGIC_GUI_ON",
    "MAGIC_NO_GUI": "QK_MAGIC_GUI_OFF",
    "MAGIC_TOGGLE_GUI": "QK_MAGIC_TOGGLE_GUI",
    "MAGIC_SWAP_GRAVE_ESC": "QK_MAGIC_SWAP_GRAVE_ESC",
    "MAGIC_UNSWAP_GRAVE_ESC": "QK_MAGIC_UNSWAP_GRAVE_ESC",
    "MAGIC_SWAP_BACKSLASH_BACKSPACE": "QK_MAGIC_SWAP_BACKSLASH_BACKSPACE",
    "MAGIC_UNSWAP_BACKSLASH_BACKSPACE": "QK_MAGIC_UNSWAP_BACKSLASH_BACKSPACE",
    "MAGIC_TOGGLE_BACKSLASH_BACKSPACE": "QK_MAGIC_TOGGLE_BACKSLASH_BACKSPACE",
    "MAGIC_HOST_NKRO": "QK_MAGIC_NKRO_ON",
    "MAGIC_UNHOST_NKRO": "QK_MAGIC_NKRO_OFF",
    "MAGIC_TOGGLE_NKRO": "QK_MAGIC_TOGGLE_NKRO",
    "MAGIC_SWAP_ALT_GUI": "QK_MAGIC_SWAP_ALT_GUI",
    "MAGIC_UNSWAP_ALT_GUI": "QK_MAGIC_UNSWAP_ALT_GUI",
    "MAGIC_TOGGLE_ALT_GUI": "QK_MAGIC_TOGGLE_ALT_GUI",
    "MAGIC_SWAP_LCTL_LGUI": "QK_MAGIC_SWAP_LCTL_LGUI",
    "MAGIC_UNSWAP_LCTL_LGUI": "QK_MAGIC_UNSWAP_LCTL_LGUI",
    "MAGIC_SWAP_RCTL_RGUI": "QK_MAGIC_SWAP_RCTL_RGUI",
    "MAGIC_UNSWAP_RCTL_RGUI": "QK_MAGIC_UNSWAP_RCTL_RGUI",
    "MAGIC_SWAP_CTL_GUI": "QK_MAGIC_SWAP_CTL_GUI",
    "MAGIC_UNSWAP_CTL_GUI": "QK_MAGIC_UNSWAP_CTL_GUI",
    "MAGIC_TOGGLE_CTL_GUI": "QK_MAGIC_TOGGLE_CTL_GUI",
    "MAGIC_EE_HANDS_LEFT": "QK_MAGIC_EE_HANDS_LEFT",
    "MAGIC_EE_HANDS_RIGHT": "QK_MAGIC_EE_HANDS_RIGHT",
    "MAGIC_SWAP_ESCAPE_CAPSLOCK": "QK_MAGIC_SWAP_ESCAPE_CAPS_LOCK",
    "MAGIC_UNSWAP_ESCAPE_CAPSLOCK": "QK_MAGIC_UNSWAP_ESCAPE_CAPS_LOCK",
    "MAGIC_TOGGLE_ESCAPE_CAPSLOCK": "QK_MAGIC_TOGGLE_ESCAPE_CAPS_LOCK",
    "LCG_SWP": "QK_MAGIC_SWAP_LCTL_LGUI",
    "LCG_NRM": "QK_MAGIC_UNSWAP_LCTL_LGUI",
    "RCG_SWP": "QK_MAGIC_SWAP_RCTL_RGUI",
    "RCG_NRM": "QK_MAGIC_UNSWAP_RCTL_RGUI",
    "LAG_SWP": "QK_MAGIC_SWAP_LALT_LGUI",
    "LAG_NRM": "QK_MAGIC_UNSWAP_LALT_LGUI",
    "RAG_SWP": "QK_MAGIC_SWAP_RALT_RGUI",
    "RAG_NRM": "QK_MAGIC_UNSWAP_RALT_RGUI",
    "GUI_ON": "QK_MAGIC_GUI_ON",
    "GUI_OFF": "QK_MAGIC_GUI_OFF",
    "GUI_TOG": "QK_MAGIC_TOGGLE_GUI",
}


def includes(qmk: Path) -> str:
    return "\n".join((
        "#define STENO_COMBINEDMAP",
        '#include "' + (qmk / "keycode.h").as_posix() + '"',
        '#include "' + (qmk / "quantum_keycodes.h").as_posix() + '"',
        '#include "' + (qmk / "steno_keycodes.h").as_posix() + '"',
    )) + "\n"


def keycode_names(qmk: Path) -> list[str]:
    """All enum entries and object-like keycode aliases, not helper functions."""
    names = set()
    for header in HEADERS:
        text = (qmk / header).read_text(encoding="utf-8")
        text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
        for body in re.findall(r"enum\s+\w+\s*\{(.*?)\}", text, re.S):
            for entry in body.split(","):
                match = re.match(r"\s*(\w+)", entry)
                if match:
                    names.add(match[1])
        for name, value in re.findall(r"^#define\s+(\w+)[ \t]+([^\n]+)", text, re.M):
            if not name.endswith("_KEYCODE_RANGE") and not name.startswith("QMK_KEYCODES_VERSION"):
                names.add(name)
    return sorted(names)


def snapshot(root: Path, build: Path, cc: str) -> None:
    """Generate once from pre-change headers; normal test runs never regenerate."""
    qmk = root / "src/ap/modules/qmk/quantum"
    names = keycode_names(qmk)
    build.mkdir(parents=True, exist_ok=True)
    source = build / "keycode_snapshot.c"
    source.write_text(includes(qmk) + "#include <stdio.h>\nint main(void) {\n" +
                      "".join(f'printf("{name} %u\\n", (unsigned)({name}));\n' for name in names) +
                      "return 0;\n}\n", encoding="utf-8")
    exe = build / "keycode_snapshot.exe"
    subprocess.run([cc, "-std=gnu11", f"-I{qmk}", f"-I{qmk/'keymap_extras'}", f"-I{qmk/'sequencer'}",
                    str(source), "-o", str(exe)], check=True, timeout=90)
    result = subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=30)
    values = {name: f"0x{int(value):04X}" for name, value in
              (line.split() for line in result.stdout.splitlines())}
    assert len(values) == len(names)
    data = {"source_commit": "72f47805beab57108411302777a0c72cf69a2e52",
            "headers": list(HEADERS), "keycodes": values}
    with SNAPSHOT.open("x", encoding="utf-8", newline="\n") as out:
        json.dump(data, out, indent=2)
        out.write("\n")
    print(f"Generated pre-change snapshot: {len(values)} keycode names")


def generate(root: Path, build: Path) -> Path:
    qmk = root / "src/ap/modules/qmk/quantum"
    data = json.loads(SNAPSHOT.read_text(encoding="utf-8"))
    values = data["keycodes"]
    assert set(RENAMED) | set(MOVED) <= values.keys()
    checks = ['_Static_assert(QMK_KEYCODES_VERSION_BCD == 0x00000008, "table version");']
    for old, value in values.items():
        name = RENAMED.get(old, old)
        if old in MOVED:
            before, after = MOVED[old]
            assert int(value, 16) == before
            value = hex(after)
        checks.append(f'_Static_assert(({name}) == {value}, "{old} -> {name}");')
    # Removed constructor aliases have no standalone value; cover their domains.
    checks.extend((
        '_Static_assert(UC(0x7FFF) == 0xFFFF, "Unicode boundary");',
        '_Static_assert(UM(0x3FFF) == 0xBFFF, "X -> UM boundary");',
        '_Static_assert(UP(0x7F, 0x7F) == 0xFFFF, "XP -> UP boundary");',
        '_Static_assert(UG_TOGG == 0x7820 && UG_SPDD == 0x782A, "underglow ABI");',
        '_Static_assert(RM_ON == 0x7840 && RM_FLGP == 0x784E, "matrix ABI");',
    ))
    source = build / "test_keycode_invariance.c"
    source.write_text(includes(qmk) + "\n".join(checks) +
                      f'\n#include <stdio.h>\nint main(void) {{ puts("PASS: {len(values)} pre-change keycode names; '
                      'explicit equal-value renames and six approved inactive-output moves"); return 0; }\n',
                      encoding="utf-8")
    return source


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", action="store_true", required=True,
                        help="Generate baseline from the untouched pre-alignment sources")
    args = parser.parse_args()
    from run import ROOT, BUILD, gcc
    snapshot(ROOT, BUILD, gcc())
