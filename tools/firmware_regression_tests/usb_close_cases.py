"""Compile unchanged bridge/HAL close routines against the H7RS LL fixture."""
from __future__ import annotations

from pathlib import Path

from usb_ll_cases import function, generate as generate_ll, typedef


def generate(root: Path, build: Path, conf_source: str | None = None) -> Path:
    generate_ll(root, build)
    hal_path = root / "src/lib/ST/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_pcd.c"
    hal = hal_path.read_text(encoding="utf-8")
    conf_path = root / "src/hw/driver/usb/usbd_conf.c"
    conf = conf_source if conf_source is not None else conf_path.read_text(encoding="utf-8")
    usbd = (root / "src/lib/ST/STM32_USB_Device_Library/Core/Inc/usbd_def.h").read_text(encoding="utf-8")
    (build / "usb_close_defs.inc").write_text(typedef(usbd, "enum", "USBD_StatusTypeDef") + "\n", encoding="utf-8")
    generated = []
    for path, source, names in (
        (hal_path, hal, ("HAL_PCD_EP_Abort", "HAL_PCD_EP_Close", "HAL_PCD_EP_Flush")),
        (conf_path, conf, ("USBD_Get_USB_Status", "USBD_LL_CloseEP")),
    ):
        for name in names:
            routine = function(source, name)
            line = source[:source.index(routine)].count("\n") + 1
            generated.append(f'#line {line} "{path.as_posix()}"\n{routine}')
    (build / "usb_close_functions.inc").write_text("\n\n".join(generated) + "\n", encoding="utf-8")
    return root / "tools/firmware_regression_tests/test_usb_close.c"
