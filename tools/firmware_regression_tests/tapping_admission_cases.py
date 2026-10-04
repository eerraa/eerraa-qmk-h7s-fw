"""실제 tapping 상태 기계를 수용 경계와 함께 실행하는 독립 fixture."""
from __future__ import annotations

from pathlib import Path


def generate(root: Path, build: Path, source_root: Path | None = None) -> list[Path]:
    selected = (source_root or root) / "src/ap/modules/qmk/quantum"
    read = lambda name: (selected / name).read_bytes().decode("utf-8").replace("\r\n", "\n")
    target = build / "tapping_admission"
    target.mkdir(parents=True, exist_ok=True)
    keyboard = read("keyboard.h")
    action = read("action.h")
    event_types = keyboard[keyboard.index("/* key matrix position */"):keyboard.index("/* Common keypos_t object factory */")]
    record_types = action[action.index("/* tapping count and state */"):action.index("/* Execute action per keyevent */")]
    token_guard = "#define TAPPING_FIXTURE_HAS_SCAN_TOKEN\n" if "report_scan_token" in record_types else ""
    stats_guard = "#define TAPPING_FIXTURE_HAS_STATS\n" if "action_tapping_get_stats" in read("action_tapping.h") else ""
    action_header = """#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "action_code.h"
#include "keycode.h"
""" + event_types + record_types + token_guard + stats_guard + """
#define ac_dprintf(...) ((void)0)
#define debug_record(...) ((void)0)
#define debug_event(...) ((void)0)
bool is_tap_record(keyrecord_t *record);
void process_record(keyrecord_t *record);
void process_record_tap_hint(keyrecord_t *record);
void clear_keyboard(void);
"""
    (target / "action.h").write_text(action_header, encoding="utf-8")
    (target / "action_layer.h").write_text(
        '#pragma once\n#include "action.h"\naction_t layer_switch_get_action(keypos_t key);\naction_t action_for_keycode(uint16_t);\nvoid layer_clear_physical_momentary(void);\n', encoding="utf-8")
    (target / "timer.h").write_text(
        '#pragma once\n#include <stdint.h>\n#define TIMER_DIFF_32(a, b) ((uint32_t)((a) - (b)))\n', encoding="utf-8")
    for name in ("action_tapping.c", "action_tapping.h"):
        (target / name).write_bytes((selected / name).read_bytes())
    test = Path(__file__).with_name("test_tapping_admission.c")
    return [test, target / "action_tapping.c"]
