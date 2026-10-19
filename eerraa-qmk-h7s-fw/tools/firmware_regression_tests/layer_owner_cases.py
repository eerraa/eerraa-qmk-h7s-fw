"""TD를 끈 production layer owner와 tri-layer helper의 합성 경계를 링크한다."""
from pathlib import Path
import os
import shutil


def generate(root: Path, build: Path) -> tuple[list[Path], list[str]]:
    qmk = root / "src/ap/modules/qmk"
    target = build / "no_td_layers"
    target.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(qmk / "quantum/action_layer.c", target / "action_layer.c")
    shutil.copyfile(qmk / "port/platforms/progmem.h", target / "progmem.h")
    (target / "timer.h").write_text("#pragma once\n#include <stdint.h>\nuint32_t timer_read32(void);\n", encoding="utf8")
    (target / "gpio.h").write_text("#pragma once\n#include <stdint.h>\ntypedef uint16_t pin_t;\n", encoding="utf8")
    (target / "report.h").write_text("""#pragma once
#include <stdint.h>
typedef struct { uint8_t mods, reserved, keys[6]; } report_keyboard_t;
void add_key_to_report(uint8_t);
void del_key_from_report(uint8_t);
void clear_keys_from_report(void);
""", encoding="utf8")
    flags = ["-DMATRIX_ROWS=1", "-DMATRIX_COLS=3", "-DDYNAMIC_KEYMAP_ENABLE", "-DDYNAMIC_KEYMAP_LAYER_COUNT=8",
             "-I" + str(target), "-I" + str(qmk / "quantum")]
    if os.name == "nt": flags.append("-mno-ms-bitfields")
    return [Path(__file__).with_name("test_layer_owner_no_td.c"), target / "action_layer.c"], flags
