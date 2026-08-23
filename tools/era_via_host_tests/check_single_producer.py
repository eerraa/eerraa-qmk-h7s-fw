#!/usr/bin/env python3
"""H7S VIA raw-HID TX must have one producer: via_hid_task enqueue."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
VIA_HID = ROOT / "src/ap/modules/qmk/port/via_hid.c"
VIA_C = ROOT / "src/ap/modules/qmk/quantum/via.c"


def extract_function(source: str, name: str) -> str:
    match = re.search(rf"void\s+{name}\s*\([^)]*\)\s*\{{", source)
    if not match:
        raise SystemExit(f"missing {name}()")
    start = match.end() - 1
    depth = 0
    for i, ch in enumerate(source[start:], start):
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return source[start : i + 1]
    raise SystemExit(f"unbalanced {name}()")


def main() -> int:
    hid = VIA_HID.read_text(encoding="utf-8")
    via = VIA_C.read_text(encoding="utf-8")

    send_body = extract_function(hid, "raw_hid_send")
    if "usbHidEnqueueViaResponse" in send_body or "USBD_" in send_body:
        print("FAIL raw_hid_send must stay an empty stub")
        return 1
    if "usbHidEnqueueViaResponse" not in extract_function(hid, "via_hid_task"):
        print("FAIL via_hid_task must call usbHidEnqueueViaResponse")
        return 1
    if "era_state_sync_via_command" not in via or "id_era_state_sync" not in via:
        print("FAIL via.c GET switch must fill 0x06 without owning TX")
        return 1
    print("PASS single raw-HID producer")
    return 0


if __name__ == "__main__":
    sys.exit(main())
