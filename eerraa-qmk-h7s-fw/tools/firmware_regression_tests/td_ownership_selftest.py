#!/usr/bin/env python3
"""Compile production TD paths; mutate ignored generated copies, never source."""
from pathlib import Path
import json
from datetime import datetime, timezone
import os
import subprocess
from rgb_input_cases import generate
from run import gcc

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
QMK = ROOT / "src/ap/modules/qmk"
BUILD = ROOT / "build-firmware-regression-tests" / "td-ownership-controls" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    source = generate(ROOT, BUILD).read_text(encoding="utf8")
    controls = [
        ("layer_override", "lifetime:layer_override",
         "    memset(td_layer_owners, 0, sizeof(td_layer_owners));\n    memset(td_layer_counts, 0, sizeof(td_layer_counts));\n    td_layer_union = 0;",
         "    /* mutation: preserve retired layer contributions */", 1),
        ("tri_recapture", "lifetime:tri_no_leak",
         "(regular_layer_state & ~mask) | (adjusted & mask)",
         "adjusted | (mask & 0)", 1),
        ("physical_owner", "same_slot", "record->event.key.row * MATRIX_COLS + record->event.key.col", "0", 2),
        ("release_owner", "remap", "return TD(state->index);", "return KC_NO;", 1),
        # The ordinary action path now resolves the record's keycode too, so
        # bypassing only td_owned_release still supplies ACTION_NO for a dance.
        # Release-owner and shared-output mutations exercise the remaining
        # TD lifetime duties without deliberately disabling that second guard.
        ("event_time", "queued", "return record->event.time;", "return timer_read32();", 1),
        ("shared_output", "shared", "--td_layer_counts[i] == 0", "--td_layer_counts[i] <= 1", 1),
        ("caps_feedback", "lifetime:caps_feedback", "return host_state.caps_lock;", "return false;", 1),
        ("mouse_owner", "lifetime:hid_shared", "if (!action_mouse_update(mouse_keycode, pressed)) return;",
         "(void)action_mouse_update(mouse_keycode, pressed);", 1),
        ("usage_owner", "lifetime:hid_shared", "return action_owned_usage(page, current);",
         "return (void)current, 0;", 1),
        ("extra_current", "lifetime:extra_current", "return action_owned_usage(page, current);",
         "return (void)current, 0;", 1),
        ("osl_owned_press", "lifetime:osl_hold", "&& !tap_dance_owns_press(record)", "&& true", 1),
        ("press_edge", "lifetime:rolling_edge", "            del_key_from_report(code);\n#else", "            del_key(code);\n#else", 1),
        ("oneshot_regular", "lifetime:osl_double_hold", "oneshot_layer_switch(get_oneshot_layer(), false);",
         "layer_off(get_oneshot_layer());", 1),
        ("queued_content", "lifetime:queued_layer", "IS_QK_TAP_DANCE(keycode) && tap_dance_remap_keycode(keymap_key_to_keycode(",
         "0 && tap_dance_remap_keycode(keymap_key_to_keycode(", 1),
        ("double_cancel", "lifetime:double_cancel", "        if (owner->cancelled) {\n            memset(owner, 0, sizeof(*owner));",
         "        if (owner->cancelled) {\n            if (owner->input_epoch == record->tap_dance_epoch) memset(owner, 0, sizeof(*owner));", 1),
        ("mod_replay", "lifetime:mod_replay", "if (!state->pressed || state->interrupted)", "if (0)", 1),
        ("hold_no_replay", "lifetime:capture", "if (!state->pressed || state->interrupted)", "if (1)", 1),
        ("stale_tombstone", "lifetime:stale_tombstone", "        memset(owner, 0, sizeof(*owner));\n        owner = NULL;",
         "        (void)0;", 1),
        ("retro_position", "lifetime:retro_td_position", "  record.event.key = state->key;\n", "", 1),
        ("retro_held_mods", "lifetime:retro_held_mods", "retro_tap_curr_mods & ~get_mods();", "retro_tap_curr_mods;", 1),
        ("retro_key_roll", "lifetime:retro_key_roll", "            retro_tap_primed = true;\n        }\n",
         "            retro_tap_primed = true;\n        } else {\n            retro_tap_curr_key = event_keycode;\n        }\n", 1),
        ("osl_layer_key", "lifetime:osl_layer_key", "            // don't release the key\n            do_release_oneshot = false;\n", "", 1),
        ("fallback_mods", "lifetime:fallback_mods", "          add_weak_mods(mods);\n          tapdance_tap_keycode(",
         "          (void)mods;\n          tapdance_tap_keycode(", 1),
        ("quantum_keycode", "lifetime:quantum_tap", "    tap_dance_run_quantum_keycode(&record, keycode);", "    (void)keycode;", 1),
        ("nested_dance", "lifetime:quantum_nested", "    if (IS_QK_TAP_DANCE(tap_dance_remap_keycode(keycode))) return;\n", "", 1),
        ("immediate_retire", "lifetime:own_clear_chord", "        if (state == exempt) continue;\n", "        if (exempt) continue;\n", 1),
        ("own_dance_exempt", "lifetime:own_clear_hold", "        if (state == exempt) continue;\n", "", 1),
        ("owned_newer_up", "lifetime:own_clear_hold", "(int32_t)(record->tap_dance_epoch - state->input_epoch) >= 0",
         "state->input_epoch == record->tap_dance_epoch", 1),
        ("boundary_epoch", "lifetime:dispatch_epoch", "        return (int32_t)(record->tap_dance_epoch - owner->input_epoch) < 0;\n",
         "        return true;\n", 1),
        ("tap_only_release", "lifetime:tap_only_release", "if (tap_count == 1U && has_double == false && has_tap_hold == false)", "if (0)", 1),
        ("storage_reset", "lifetime:storage_reset",
         "  /* A reset retires held dances while their executed actions still exist. */\n  tap_dance_cancel_all();\n", "", 1),
    ]
    layout = ["-mno-ms-bitfields"] if os.name == "nt" else []
    flags = ["-std=gnu11", "-O2", "-g", "-Wall", "-Wextra", "-Werror", *layout,
             f"-I{HERE}", f"-I{QMK / 'quantum'}", f"-I{QMK / 'port'}",
             "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-variable"]
    results = []
    for name, case, old, new, occurrences in [("baseline", None, None, None, 0), *controls]:
        text = source
        if old:
            assert text.count(old) == occurrences, (name, text.count(old))
            text = text.replace(old, new)
        c = BUILD / f"{name}.c"
        exe = BUILD / (name + (".exe" if os.name == "nt" else ""))
        c.write_text(text, encoding="utf8")
        compiled = subprocess.run([gcc(), *flags, str(c), "-o", str(exe)], capture_output=True, text=True)
        (BUILD / f"{name}.compile.log").write_text(compiled.stdout + compiled.stderr, encoding="utf8")
        assert compiled.returncode == 0, (name, compiled.stderr)
        for leg in ([case] if case else ["same_slot", "remap", "remap_shared", "queued", "scan_gap", "shared", "lifetime:caps_feedback", "lifetime:layer_override", "lifetime:layer_clear", "lifetime:tri_no_leak", "lifetime:tri_shared", "lifetime:tri_active",
                                          "lifetime:hid_shared", "lifetime:extra_current", "lifetime:osl_hold", "lifetime:tap_edge",
                                          "lifetime:osl_double_hold", "lifetime:rolling_edge", "lifetime:tap_code_edge",
                                          "lifetime:queued_layer", "lifetime:queued_remap", "lifetime:retro_release",
                                          "lifetime:retro_hold_lt", "lifetime:double_cancel", "lifetime:capture", "lifetime:mod_replay",
                                          "lifetime:mod_rolloff", "lifetime:retro_swallow", "lifetime:stale_tombstone",
                                          "lifetime:retro_td_position", "lifetime:retro_held_mods", "lifetime:retro_key_roll",
                                          "lifetime:osl_layer_key", "lifetime:fallback_mods", "lifetime:quantum_tap",
                                          "lifetime:quantum_nested", "lifetime:quantum_clear_hold", "lifetime:tap_only_release",
                                          "lifetime:storage_reset", "lifetime:own_clear_hold", "lifetime:dispatch_epoch", "lifetime:own_clear_chord"]):
            mode, selected = ("--lifetime", leg.split(":", 1)[1]) if leg.startswith("lifetime:") else ("--ownership", leg)
            result = subprocess.run([str(exe), mode, selected], capture_output=True, text=True, timeout=30)
            output = result.stdout + result.stderr
            (BUILD / f"{name}-{leg.replace(':', '-')}.log").write_text(output, encoding="utf8")
            caught = result.returncode != 0 and "assert" in output.lower()
            if name == "caps_feedback":
                caught = caught and "indicator_on && visible_frame==2" in output
            if name == "layer_override":
                caught = caught and "layer_state==(clear ? 0U : 2U)" in output
            if name == "tri_recapture":
                caught = caught and "layer_state==(shared ? 2U : (active ? 4U : 0U))" in output
            assert caught if case else result.returncode == 0, (name, leg, result.returncode, output)
            results.append(dict(control=name, case=leg, compile_exit_code=compiled.returncode, exit_code=result.returncode, expected_assertion=bool(case), detected_assertion=caught if case else False))
            print("PASS", name, leg, "negative assertion detected" if case else "production baseline", flush=True)
    (BUILD / "results.json").write_text(json.dumps(results, indent=2), encoding="utf8")
    print("Evidence:", BUILD, flush=True)


if __name__ == "__main__":
    main()
