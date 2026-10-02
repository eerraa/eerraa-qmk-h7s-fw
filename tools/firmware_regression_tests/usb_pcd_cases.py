"""Compile the production PCD bridge and USBD lifecycle/completion functions."""
import re
from pathlib import Path


def function(source: str, name: str) -> str:
    # Callback declarations have a conditional static/extern name. Keep that
    # conditional declaration together with the unmodified shared body.
    callback = re.search(
        rf"#if \(USE_HAL_PCD_REGISTER_CALLBACKS == 1U\)\s+static void PCD_{name}\([^;]*?"
        rf"#else\s+void HAL_PCD_{name}\([^;]*?#endif[^\n]*\n\{{", source
    )
    match = callback or re.search(
        rf"(?:static\s+)?(?:void|bool|uint\d+_t|USBD_StatusTypeDef)\s+{name}\([^;]*?\)\s*\{{", source
    )
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def generate(root: Path, build: Path, conf_source: str | None = None) -> Path:
    conf = conf_source if conf_source is not None else (root / "src/hw/driver/usb/usbd_conf.c").read_text(encoding="utf-8")
    core = (root / "src/lib/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c").read_text(encoding="utf-8")
    names = []
    for name in ("USBD_is_connected", "USBD_is_reset_pending", "usbPcdBeginReset", "usbPcdOnIrqEntry"):
        if re.search(rf"(?:void|bool) {name}\(", conf):
            names.append(name)
    names.append("usbPcdHardwareActive")
    if "static void usbPcdResumeIfActive" in conf:
        names.append("usbPcdResumeIfActive")
    functions = [function(conf, name) for name in (*names,
        "usbHidLogicalSuspendedSof", "SOFCallback",
        "SuspendCallback", "ResumeCallback", "DataInStageCallback", "DataOutStageCallback",
        "SetupStageCallback", "ResetCallback", "DisconnectCallback",
    )]
    usb = (root / "src/hw/driver/usb/usb.c").read_text(encoding="utf-8")
    functions += [function(usb, name) for name in ("usbIsResetPending", "usbIsConnect")]
    functions += [function(core, name) for name in (
        "USBD_CoreFindIF", "USBD_CoreFindEP", "USBD_LL_Suspend", "USBD_LL_Resume",
        "USBD_LL_SOF", "USBD_LL_DataInStage", "USBD_LL_DataOutStage",
        "USBD_LL_Reset", "USBD_LL_SetupStage", "USBD_SetClassConfig", "USBD_ClrClassConfig",
    )]
    # Keep the standard request dispatcher and EP0 IO state machine intact.
    for name in ("usbd_ctlreq.c", "usbd_ioreq.c"):
        functions.append((root / "src/lib/ST/STM32_USB_Device_Library/Core/Src" / name).read_text(encoding="utf-8"))
    target = build / "usb_pcd_callbacks.inc"
    target.write_text("\n\n".join(functions) + "\n", encoding="utf-8")
    return root / "tools/firmware_regression_tests/test_usb_pcd.c"
