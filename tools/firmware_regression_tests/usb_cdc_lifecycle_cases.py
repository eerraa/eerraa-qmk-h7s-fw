"""Compile production core/CDC/interface with a controller adapter."""
from pathlib import Path

from usb_ll_cases import function


def generate(
    root: Path,
    build: Path,
    source: str | None = None,
    core_source: str | None = None,
) -> Path:
    cdc = root / "src/hw/driver/usb/usb_cdc"
    if source is None:
        source = (cdc / "usbd_cdc.c").read_text(encoding="utf-8")
    # Only relocate the source so target-local headers can be replaced. Keep
    # every class/interface statement intact, including the actual SOF pump.
    (build / "usb_cdc_lifecycle_class.c").write_text(source, encoding="utf-8")
    (build / "usb_cdc_lifecycle_interface.inc").write_text(
        (cdc / "usbd_cdc_if.c").read_text(encoding="utf-8"), encoding="utf-8"
    )
    core_path = root / "src/lib/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c"
    core = core_source if core_source is not None else core_path.read_text(encoding="utf-8")
    routines = []
    for name in ("USBD_SetClassConfig", "USBD_DeInit", "USBD_LL_Reset"):
        body = function(core, name)
        line = core[:core.index(body)].count("\n") + 1
        routines.append(f'#line {line} "{core_path.as_posix()}"\n{body}')
    (build / "usb_cdc_lifecycle_core.inc").write_text(
        "\n\n".join(routines) + "\n", encoding="utf-8"
    )
    return root / "tools/firmware_regression_tests/test_usb_cdc_lifecycle.c"
