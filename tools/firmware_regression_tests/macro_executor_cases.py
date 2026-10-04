"""실제 매크로 executor/caller/owner/host adapter를 연결한 host fixture."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess

from source_cases import function

BASELINE = "7f2cdc27581f5bcdcfbe3d65a7a72d36b8da1785"


def generate(root: Path, build: Path, revision: str | None = None, remove_fix: bool = False,
             custom_layout: bool = False, remove_unmapped_fix: bool = False,
             nonkeyboard_layout: bool = False, positive_tap_delay: bool = False) -> Path:
    target = build / "macro-executor"
    target.mkdir(parents=True, exist_ok=True)
    inputs: dict[str, str] = {}

    def read(relative: str, selected: str | None = revision) -> str:
        raw = (subprocess.check_output(["git", "show", f"{selected}:{relative}"], cwd=root)
               if selected else (root / relative).read_bytes())
        inputs[f"{selected or 'working'}:{relative}"] = hashlib.sha256(raw).hexdigest()
        return raw.decode("utf-8").replace("\r\n", "\n")

    quantum = "src/ap/modules/qmk/quantum/"
    port = "src/ap/modules/qmk/port/"
    dynamic = read(quantum + "dynamic_keymap.c")
    action = read(quantum + "action.c")
    util = read(quantum + "action_util.c")
    string = read(quantum + "send_string/send_string.c")
    host = read(port + "protocol/host.c")
    via = read(quantum + "via.c")
    qmk = read("src/ap/modules/qmk/qmk.c")
    after = "era_macro_request(id)" in function(dynamic, "dynamic_keymap_macro_send")
    definitions = [line for line in read(quantum + "send_string/send_string_keycodes.h").splitlines()
                   if line.startswith("#define ") and line.split()[1] in
                   ("SS_QMK_PREFIX", "SS_TAP_CODE", "SS_DOWN_CODE", "SS_UP_CODE", "SS_DELAY_CODE")]
    assert len(definitions) == 5
    for name in ("keycodes.h", "keycode.h", "modifiers.h"):
        (target / name).write_text(read(quantum + name), encoding="utf-8")
    for name in ("report.h", "host_driver.h"):
        (target / name).write_text(read(port + "protocol/" + name), encoding="utf-8")
    (target / "hw_def.h").write_text("#pragma once\n#define HW_KEYS_PRESS_MAX 20\n", encoding="utf-8")
    (target / "util.h").write_text("#pragma once\n#define PACKED __attribute__((packed))\n", encoding="utf-8")
    (target / "process_keycode").mkdir(exist_ok=True)
    (target / "process_keycode/process_tap_dance.h").write_text(
        "#pragma once\n#define TAP_DANCE_MAX_SIMULTANEOUS (5U * 16U + 8U)\n", encoding="utf-8")
    for name in ("action.h", "action_util.h", "dynamic_keymap.h", "timer.h", "send_string.h"):
        (target / name).write_text("#pragma once\n", encoding="utf-8")
    macro_header = read(port + "era_macro.h") if after else ""
    (target / "era_macro.h").write_text(macro_header, encoding="utf-8")
    ownership = util[util.index("static uint8_t real_mods"):util.index("#ifdef KEY_OVERRIDE_ENABLE")]
    names = ("get_mods", "add_mods", "del_mods", "set_mods", "clear_mods", "get_weak_mods",
             "add_weak_mods", "del_weak_mods", "set_weak_mods", "clear_weak_mods")
    bodies = [function(read("src/bsp/bsp.c"), "delay"),
              function(read(port + "platforms/wait.c"), "wait_ms"),
              ownership, *(function(util, name) for name in names)]
    host_names = ["host_keyboard_delay", "host_keyboard_send", "host_mouse_send", "host_system_send",
                  "host_consumer_send", "host_last_system_usage", "host_last_consumer_usage"]
    if after:
        bodies += [function(host, "host_extra_snapshot")]
        host_names.append("host_extra_reconcile")
    bodies += [function(host, name) for name in host_names]
    bodies += [function(action, name) for name in ("register_mouse", "send_system_usage", "send_consumer_usage",
               "register_code", "unregister_code", "tap_code_wait", "tap_code_delay", "tap_code",
               "clear_keyboard", "clear_keyboard_but_mods", "clear_keyboard_but_mods_and_keys")]
    header = read(quantum + "send_string/send_string.h")
    bodies += [header[header.index("#define KCLUT_ENTRY"):header.index("// clang-format on")],
               string[string.index("/* Bit-Packed"):string.index("void send_string(")],
               function(string, "send_char_with_delay"), function(string, "send_string_with_delay")]
    bodies += [function(dynamic, name) for name in
               ("dynamic_keymap_macro_get_count", "dynamic_keymap_macro_get_buffer_size", "dynamic_keymap_macro_get_buffer")]
    if after:
        bodies.append(read(port + "era_macro.c", "d29e9d50e9b982458b2a039cfd5aafba8d4af54e"
                           if remove_unmapped_fix else revision))
    wrapper = function(dynamic, "dynamic_keymap_macro_send")
    if remove_fix:
        assert after
        wrapper = function(read(quantum + "dynamic_keymap.c", BASELINE), "dynamic_keymap_macro_send")
    bodies += [wrapper, function(via, "process_record_via"), function(qmk, "qmkUpdate")]
    mode = "#define ERA_MACRO_ENABLE\n" if after else "#define MACRO_TEST_BASELINE\n"
    if nonkeyboard_layout:
        assert after
        mode += "#define MACRO_NONKEYBOARD_LAYOUT\n"
        key_lut = re.sub(r"//[^\n]*", "", string[string.index("ascii_to_keycode_lut[128]"):])
        key_lut = key_lut.split("{", 1)[1].split("}", 1)[0]
        keycodes = [key.strip() for key in key_lut.split(",") if key.strip()]
        assert len(keycodes) == 128
        keycodes[13], keycodes[126], keycodes[94] = "KC_NO", "KC_AUDIO_VOL_UP", "KC_MS_BTN1"
        (target / "layout_override.c").write_text(
            '#include <stdint.h>\n#include "keycode.h"\n'
            'const uint8_t ascii_to_keycode_lut[128] = {' + ",".join(keycodes) + '};\n'
            'const uint8_t ascii_to_altgr_lut[16] = {[13/8] = 1U << (13%8), [126/8] = 1U << (126%8)};\n'
            'const uint8_t ascii_to_dead_lut[16] = {[13/8] = 1U << (13%8), [94/8] = 1U << (94%8)};\n', encoding="utf-8")
    if positive_tap_delay:
        mode += "#define MACRO_POSITIVE_TAP_DELAY\n"
    if custom_layout:
        assert after
        mode += "#define MACRO_CUSTOM_LAYOUT\n"
        (target / "layout_override.c").write_text(
            "#include <stdint.h>\nconst uint8_t ascii_to_altgr_lut[16] = {[126/8] = 1U << (126%8)};\n"
            "const uint8_t ascii_to_dead_lut[16] = {[94/8] = 1U << (94%8)};\n", encoding="utf-8")
    (target / "macro_mode.h").write_text(mode, encoding="utf-8")
    (target / "macro_production.inc").write_text("\n".join(definitions + bodies) + "\n", encoding="utf-8")
    fixture = root / "tools/firmware_regression_tests/test_macro_executor.c"
    fixture_bytes = fixture.read_bytes()
    frozen_fixture = target / fixture.name
    frozen_fixture.write_bytes(fixture_bytes)
    frozen_hashes = {str(path.relative_to(target)): hashlib.sha256(path.read_bytes()).hexdigest()
                     for path in target.rglob("*") if path.is_file() and path.name != "inputs.json"}
    (target / "inputs.json").write_text(json.dumps({"production_inputs_sha256": inputs,
        "frozen_compile_inputs_sha256": frozen_hashes,
        "mutation": ("restore pre-unmapped-fix executor" if remove_unmapped_fix else
                     "restore baseline synchronous wrapper" if remove_fix else None),
        "mocks": "matrix/task and transport/IRQ session boundaries; actual QMK caller, parser, LUT, owner and host adapter"},
        indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return frozen_fixture


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--revision")
    parser.add_argument("--remove-fix", action="store_true")
    parser.add_argument("--liveness-only", action="store_true")
    parser.add_argument("--custom-layout", action="store_true")
    parser.add_argument("--remove-unmapped-fix", action="store_true")
    parser.add_argument("--nonkeyboard-layout", action="store_true")
    parser.add_argument("--positive-tap-delay", action="store_true")
    args = parser.parse_args()
    from run import gcc
    root = Path(__file__).resolve().parents[2]
    source = generate(root, args.build, args.revision, args.remove_fix, args.custom_layout,
                      args.remove_unmapped_fix, args.nonkeyboard_layout, args.positive_tap_delay)
    sources = [source]
    if args.custom_layout or args.nonkeyboard_layout:
        sources.append(args.build / "macro-executor/layout_override.c")
    executable = args.build / ("test_macro_executor.exe" if os.name == "nt" else "test_macro_executor")
    subprocess.run([gcc(), "-std=gnu11", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
                    f"-I{args.build / 'macro-executor'}", *map(str, sources), "-o", str(executable)], check=True, timeout=90)
    result = subprocess.run([str(executable), *(["--liveness-only"] if args.liveness_only else [])], timeout=30)
    raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
