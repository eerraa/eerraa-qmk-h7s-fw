"""Extract unchanged composite core/config routines for explicit class adapters.

The fixture includes the real USBD type header with an isolated host config.
Its MAX_NUM_CONFIGURATION=2 exercises the configuration-switch branch; shipped
usbd_conf.h still owns MAX_NUM_CONFIGURATION=1. No HID/CDC class is linked here.
"""
from __future__ import annotations

from pathlib import Path

from usb_ll_cases import function


def generate(
    root: Path,
    build: Path,
    core_source: str | None = None,
    ctlreq_source: str | None = None,
) -> Path:
    core_path = root / "src/lib/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c"
    ctlreq_path = root / "src/lib/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c"
    core = core_source if core_source is not None else core_path.read_text(encoding="utf-8")
    ctlreq = ctlreq_source if ctlreq_source is not None else ctlreq_path.read_text(encoding="utf-8")
    routines = []
    for name in ("USBD_SetClassConfig", "USBD_ClrClassConfig", "USBD_Stop", "USBD_DeInit", "USBD_LL_Reset"):
        body = function(core, name)
        line = core[:core.index(body)].count("\n") + 1
        routines.append(f'#line {line} "{core_path.as_posix()}"\n{body}')
    (build / "usb_composite_core.inc").write_text("\n\n".join(routines) + "\n", encoding="utf-8")
    body = function(ctlreq, "USBD_SetConfig")
    line = ctlreq[:ctlreq.index(body)].count("\n") + 1
    (build / "usb_composite_ctlreq.inc").write_text(
        f'#line {line} "{ctlreq_path.as_posix()}"\n{body}\n', encoding="utf-8"
    )
    return root / "tools/firmware_regression_tests/test_usb_composite.c"
