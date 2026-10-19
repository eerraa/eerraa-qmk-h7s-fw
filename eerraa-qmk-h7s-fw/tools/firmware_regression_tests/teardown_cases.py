"""Compile production core teardown functions beside the actual HID class."""
import re
from pathlib import Path


def function(source: str, name: str) -> str:
    match = re.search(rf"USBD_StatusTypeDef\s+{name}\([^;]*?\)\s*\{{", source)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def generate(root: Path, build: Path, core_source: str | None = None) -> Path:
    source = core_source if core_source is not None else (
        root / "src/lib/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c"
    ).read_text(encoding="utf-8")
    names = ("USBD_SetClassConfig", "USBD_ClrClassConfig", "USBD_Stop", "USBD_DeInit")
    (build / "usb_teardown_core.inc").write_text(
        "\n\n".join(function(source, name) for name in names) + "\n", encoding="utf-8"
    )
    return root / "tools/firmware_regression_tests/test_usb_teardown.c"
