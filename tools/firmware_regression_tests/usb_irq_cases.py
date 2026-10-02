"""Execute the HAL IRQ and LL endpoint readers against an isolated register model.

Production branches and endpoint helpers are copied verbatim. The host copy only
widens register base pointers and redirects memory-mapped read-pop/W1C accesses
to the model; it does not emulate bus transactions, DMA or electrical timing.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

from usb_flush_cases import enum, macro


def function(source: str, name: str) -> str:
    match = re.search(
        rf"(?:static\s+)?(?:void|uint8_t|uint32_t|HAL_StatusTypeDef)\s+{name}\([^;]*?\)\s*\{{",
        source,
    )
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def structure(source: str, name: str) -> str:
    match = re.search(rf"typedef struct\s*\{{[^}}]*\}}\s*{name}\s*;", source)
    assert match, name
    return match.group()


def host_register_access(source: str) -> str:
    # Use the same register offsets with a host-width base address. DMA address
    # fields deliberately remain 32-bit and are never dereferenced in this model.
    source = source.replace("uint32_t USBx_BASE = (uint32_t)USBx;",
                            "uintptr_t USBx_BASE = (uintptr_t)USBx;")
    # These are the only bare endpoint W1C stores in the extracted IRQ. Named
    # CLEAR_* macros below cover every other endpoint/global acknowledgement.
    for direction, register in (("IN", "DIEPINT"), ("OUT", "DOEPINT")):
        pattern = rf"USBx_{direction}EP\(([^)]*)\)->{register} = (0xFB7FU);"
        source = re.sub(pattern, rf"model_clear_{direction.lower()}(USBx, \1, \2);", source)
    source = source.replace("RegVal = USBx->GRXSTSP;", "RegVal = model_rx_pop(USBx);")
    source = source.replace("hpcd->Instance->GOTGINT |= RegVal;",
                            "model_clear_otg(hpcd->Instance, RegVal);")
    return source


def generate(root: Path, build: Path, pcd_source: str | None = None,
             conf_source: str | None = None) -> Path:
    hal = root / "src/lib/ST/STM32H7RSxx_HAL_Driver"
    cmsis = (root / "src/lib/ST/CMSIS/Device/ST/STM32H7RSxx/Include/stm32h7s3xx.h").read_text(encoding="utf-8")
    definitions = (hal / "Inc/stm32h7rsxx_hal_def.h").read_text(encoding="utf-8")
    pcd_header = (hal / "Inc/stm32h7rsxx_hal_pcd.h").read_text(encoding="utf-8")
    ll_header = (hal / "Inc/stm32h7rsxx_ll_usb.h").read_text(encoding="utf-8")
    ll_source = (hal / "Src/stm32h7rsxx_ll_usb.c").read_text(encoding="utf-8")
    source = pcd_source if pcd_source is not None else (hal / "Src/stm32h7rsxx_hal_pcd.c").read_text(encoding="utf-8")
    parts = [enum(definitions, name) for name in ("HAL_StatusTypeDef", "HAL_LockTypeDef")]
    parts += [structure(cmsis, name) for name in (
        "USB_OTG_GlobalTypeDef", "USB_OTG_DeviceTypeDef",
        "USB_OTG_INEndpointTypeDef", "USB_OTG_OUTEndpointTypeDef",
    )]
    parts += [structure(ll_header, name) for name in ("USB_CfgTypeDef", "USB_EPTypeDef")]
    parts += [enum(pcd_header, name) for name in (
        "PCD_StateTypeDef", "PCD_LPM_StateTypeDef", "PCD_LPM_MsgTypeDef", "PCD_BCD_MsgTypeDef",
    )]
    parts += ["typedef USB_CfgTypeDef USB_OTG_CfgTypeDef;\ntypedef USB_EPTypeDef USB_OTG_EPTypeDef;",
              "typedef USB_OTG_GlobalTypeDef PCD_TypeDef;\ntypedef USB_OTG_CfgTypeDef PCD_InitTypeDef;\ntypedef USB_OTG_EPTypeDef PCD_EPTypeDef;"]
    handle = re.search(r"\{\s*PCD_TypeDef\s+\*Instance;.*?\}\s*PCD_HandleTypeDef;", pcd_header, re.DOTALL)
    assert handle, "PCD_HandleTypeDef"
    parts.append("typedef struct __PCD_HandleTypeDef\n" + handle.group())
    seen: set[str] = set()
    for header, pattern in (
        (cmsis, r"USB_OTG_\w+"),
        (ll_header, r"(?:USB_OTG_\w+|STS_\w+|DSTS_ENUMSPD_\w+|USBD_\w+|EP_TYPE_\w+|EP_ADDR_MSK|USBx_DEVICE|USBx_INEP|USBx_OUTEP|USB_MASK_INTERRUPT|USB_UNMASK_INTERRUPT)"),
        (pcd_header, r"(?:USB_OTG_\w+|__HAL_PCD_GET_FLAG|__HAL_PCD_IS_INVALID_INTERRUPT)"),
    ):
        names = re.findall(rf"^#define\s+({pattern})\b", header, re.MULTILINE)
        for name in dict.fromkeys(names):
            if name not in seen:
                parts.append(macro(header, name))
                seen.add(name)
    # Redirect W1C writes to the explicit fixture model. Reset's autonomous
    # register effects and the legality of injected flag combinations are not
    # inferred from these source-level accesses.
    parts += [
        "#define __HAL_PCD_CLEAR_FLAG(h, bits) model_clear_gint((h)->Instance, (bits))",
        "#define CLEAR_IN_EP_INTR(ep, bits) model_clear_in(USBx, (ep), (bits))",
        "#define CLEAR_OUT_EP_INTR(ep, bits) model_clear_out(USBx, (ep), (bits))",
    ]
    build.mkdir(parents=True, exist_ok=True)
    (build / "usb_irq_defs.inc").write_text("\n\n".join(parts) + "\n", encoding="utf-8")
    functions = [function(ll_source, name) for name in (
        "USB_ReadDevAllOutEpInterrupt", "USB_ReadDevAllInEpInterrupt",
        "USB_ReadDevOutEPInterrupt", "USB_ReadDevInEPInterrupt", "USB_GetDevSpeed",
        "USB_ActivateSetup", "USB_EP0_OutStart", "USB_InZlpAhbDelay",
        "USB_EnableInTransfer", "USB_EPStartXfer", "USB_ActivateRemoteWakeup", "USB_DeActivateRemoteWakeup",
    )]
    functions += [function(source, name) for name in (
        "PCD_ReadRxFifo", "HAL_PCD_EP_Transmit", "HAL_PCD_EP_Receive", "PCD_WriteEmptyTxFifo",
        "PCD_EP_OutXfrComplete_int", "PCD_EP_OutSetupPacket_int", "HAL_PCD_IRQHandler",
        "HAL_PCD_ActivateRemoteWakeup", "HAL_PCD_DeActivateRemoteWakeup",
    )]
    # Keep the HAL-only observation mode's notification at its production weak
    # default. Bridge mode uses the production override copied below instead.
    if "void HAL_PCD_ResetBeginCallback(" in source:
        functions.insert(0, "#ifndef TEST_USB_IRQ_BRIDGE\n" +
                         function(source, "HAL_PCD_ResetBeginCallback") + "\n#endif")
    (build / "usb_irq_functions.inc").write_text(
        host_register_access("\n\n".join(functions)) + "\n", encoding="utf-8"
    )
    from usb_pcd_cases import function as bridge_function
    hid = (root / "src/hw/driver/usb/usb_hid/usbd_hid.c").read_text(encoding="utf-8")
    (build / "usb_irq_wake_defs.inc").write_text(enum(hid, "usb_hid_wake_state_t") + "\n", encoding="utf-8")
    wake = [bridge_function(hid, name) for name in (
        "usbHidRemoteWakeSuspended", "usbHidRequestRemoteWakeFromInput",
        "usbHidOnSuspend", "usbHidOnResume", "usbHidConsumeWakeSof",
    )]
    (build / "usb_irq_wake.inc").write_text("\n\n".join(wake) + "\n", encoding="utf-8")
    conf = conf_source if conf_source is not None else (root / "src/hw/driver/usb/usbd_conf.c").read_text(encoding="utf-8")
    usb = (root / "src/hw/driver/usb/usb.c").read_text(encoding="utf-8")
    bridge = [bridge_function(conf, name) for name in (
        "usbDiagnosticsSpeedFromUsbd", "USBD_is_connected", "USBD_is_reset_pending",
        "usbPcdBeginReset", "usbPcdOnIrqEntry", "HAL_PCD_ResetBeginCallback",
        "usbPcdHardwareActive", "usbPcdResumeIfActive", "usbHidLogicalSuspendedSof",
        "SOFCallback", "SuspendCallback", "ResumeCallback", "DataInStageCallback",
        "DataOutStageCallback", "SetupStageCallback", "ResetCallback",
        "DisconnectCallback", "ConnectCallback", "ISOOUTIncompleteCallback", "ISOINIncompleteCallback",
    )]
    bridge += [bridge_function(usb, name) for name in ("usbIsResetPending", "usbIsConnect")]
    (build / "usb_irq_bridge.inc").write_text("\n\n".join(bridge) + "\n", encoding="utf-8")
    (build / "usb_irq_wrapper.inc").write_text(
        bridge_function(usb, "OTG_HS_IRQHandler") + "\n", encoding="utf-8"
    )
    return root / "tools/firmware_regression_tests/test_usb_irq.c"


def main() -> None:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--observe", action="store_true",
                      help="Record conditional reset ownership paths (the default).")
    mode.add_argument("--assert-reset-boundary", action="store_true",
                      help="Assert those paths are absent; intentionally fails on the current HAL.")
    mode.add_argument("--bridge", action="store_true",
                      help="Assert software Reset admission using the actual IRQ wrapper, HAL and PCD bridge.")
    parser.add_argument("--source", type=Path,
                        help="Compile a saved HAL source for an isolated negative control.")
    parser.add_argument("--build", type=Path)
    args = parser.parse_args()
    from run import ROOT, BUILD, execute
    build = args.build or BUILD / "usb-irq"
    source = generate(ROOT, build, args.source.read_text(encoding="utf-8") if args.source else None)
    for callbacks in (0, 1):
        flags = [f"-I{build}", f"-DUSE_HAL_PCD_REGISTER_CALLBACKS={callbacks}",
                 "-Wno-pointer-to-int-cast"]
        if args.bridge:
            flags += ["-DTEST_USB_IRQ_BRIDGE", "-Wno-unused-function"]
        elif not args.assert_reset_boundary:
            flags.append("-DTEST_USB_IRQ_OBSERVE")
        suffix = "_bridge" if args.bridge else "_assert" if args.assert_reset_boundary else ""
        execute(f"test_usb_irq{suffix}_cb{callbacks}", [source], flags)


if __name__ == "__main__":
    main()
