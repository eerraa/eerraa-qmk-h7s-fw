"""Extract unchanged HAL/bridge Stop and LL disconnect/global-disable routines."""
from __future__ import annotations

import re
from pathlib import Path

from usb_ll_cases import function, macro, typedef


def generate(root: Path, build: Path, pcd_source: str | None = None) -> Path:
    hal = root / "src/lib/ST/STM32H7RSxx_HAL_Driver"
    pcd_path = hal / "Src/stm32h7rsxx_hal_pcd.c"
    pcd = pcd_source if pcd_source is not None else pcd_path.read_text(encoding="utf-8")
    ll_path = hal / "Src/stm32h7rsxx_ll_usb.c"
    ll = ll_path.read_text(encoding="utf-8")
    conf_path = root / "src/hw/driver/usb/usbd_conf.c"
    conf = conf_path.read_text(encoding="utf-8")
    device = (root / "src/lib/ST/CMSIS/Device/ST/STM32H7RSxx/Include/stm32h7s3xx.h").read_text(encoding="utf-8")
    hal_def = (hal / "Inc/stm32h7rsxx_hal_def.h").read_text(encoding="utf-8")
    pcd_header = (hal / "Inc/stm32h7rsxx_hal_pcd.h").read_text(encoding="utf-8")
    usbd = (root / "src/lib/ST/STM32_USB_Device_Library/Core/Inc/usbd_def.h").read_text(encoding="utf-8")
    routines = []
    for path, source, names in (
        (ll_path, ll, ("USB_DisableGlobalInt", "USB_DevDisconnect")),
        (pcd_path, pcd, ("HAL_PCD_Stop",)),
        (conf_path, conf, ("USBD_Get_USB_Status", "USBD_LL_Stop")),
    ):
        for name in names:
            body = function(source, name)
            line = source[:source.index(body)].count("\n") + 1
            routines.append(f'#line {line} "{path.as_posix()}"\n{body}')
    required = set(re.findall(r"\bUSB_OTG_\w+\b", "\n".join(routines))) - {"USB_OTG_GlobalTypeDef"}
    definitions: dict[str, str] = {}
    while required - definitions.keys():
        for name in sorted(required - definitions.keys()):
            definition = macro(device, name)
            definitions[name] = definition
            required.update(re.findall(r"\bUSB_OTG_\w+\b", definition))
    parts = [
        typedef(hal_def, "enum", "HAL_StatusTypeDef"),
        typedef(hal_def, "enum", "HAL_LockTypeDef"),
        typedef(usbd, "enum", "USBD_StatusTypeDef"),
        macro(hal_def, "__HAL_LOCK"), macro(hal_def, "__HAL_UNLOCK"),
        macro(pcd_header, "__HAL_PCD_DISABLE"),
        *[definitions[name] for name in sorted(definitions)],
    ]
    (build / "usb_stop_defs.inc").write_text("\n\n".join(parts) + "\n", encoding="utf-8")
    (build / "usb_stop_functions.inc").write_text("\n\n".join(routines) + "\n", encoding="utf-8")
    return root / "tools/firmware_regression_tests/test_usb_stop.c"
