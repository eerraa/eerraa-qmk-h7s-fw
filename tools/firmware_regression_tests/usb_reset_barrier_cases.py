"""Connect production user-reset service and core teardown to the real HID queues."""
import re
from pathlib import Path

from source_cases import function
from teardown_cases import generate as generate_teardown


def generate(root: Path, build: Path) -> Path:
    source = (root / "src/hw/driver/usb/usb.c").read_text(encoding="utf-8")
    constants = []
    for name in ("USB_RESET_RESPONSE_GRACE_MS", "USB_RESET_DETACH_DELAY_MS"):
        match = re.search(rf"^#define\s+{name}\s+[^\n]+", source, re.M)
        assert match, name
        constants.append(match.group(0))
    # The public scheduling definition has a trailing line comment. Preserve its
    # body exactly rather than inventing a second request/deadline policy here.
    match = re.search(r"bool\s+usbScheduleGraceReset\([^;]*?\)\s*//[^\n]*\n\{", source)
    assert match, "usbScheduleGraceReset"
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    (build / "usb_reset_barrier_service.inc").write_text(
        "\n\n".join([*constants, source[match.start():end],
                     function(source, "usbProcessDeferredReset")]) + "\n",
        encoding="utf-8",
    )
    generate_teardown(root, build)
    return root / "tools/firmware_regression_tests/test_usb_reset_barrier.c"
