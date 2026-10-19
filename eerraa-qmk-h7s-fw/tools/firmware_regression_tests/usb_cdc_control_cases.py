"""Execute the production CDC SETUP handler with deferred EP0 consumption."""
from pathlib import Path

from usb_pcd_cases import function


def generate(root: Path, build: Path, source: str | None = None) -> Path:
    if source is None:
        source = (root / "src/hw/driver/usb/usb_cdc/usbd_cdc.c").read_text(encoding="utf-8")
    (build / "usb_cdc_setup.inc").write_text(function(source, "USBD_CDC_Setup") + "\n", encoding="utf-8")
    return root / "tools/firmware_regression_tests/test_usb_cdc_control.c"
