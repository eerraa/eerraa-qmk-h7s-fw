"""Execute opt-in keyboard FIFO preparation using actual HAL/LL/bridge bodies.

Register bases are widened for the host, W1C/read-pop accesses use the existing
IRQ model adapters, and individual FIFO word stores are recorded explicitly.
No USB token/ACK timing, DMA memory access, PHY or reset quiescence is emulated.
"""
from __future__ import annotations

import argparse
from pathlib import Path

from usb_flush_cases import enum, macro
from usb_irq_cases import function, generate as generate_irq, host_register_access
from usb_ll_cases import function as bridge_function


def generate(root: Path, build: Path, pcd_source: str | None = None,
             conf_source: str | None = None) -> Path:
    # Reuse the canonical production register/type definitions, with all output
    # isolated under this fixture's own build directory.
    generate_irq(root, build, pcd_source=pcd_source, conf_source=conf_source)
    hal = root / "src/lib/ST/STM32H7RSxx_HAL_Driver"
    pcd_path = hal / "Src/stm32h7rsxx_hal_pcd.c"
    pcd = pcd_source if pcd_source is not None else pcd_path.read_text(encoding="utf-8")
    ll_path = hal / "Src/stm32h7rsxx_ll_usb.c"
    ll = ll_path.read_text(encoding="utf-8")
    conf_path = root / "src/hw/driver/usb/usbd_conf.c"
    conf = conf_source if conf_source is not None else conf_path.read_text(encoding="utf-8")
    usbd = (root / "src/lib/ST/STM32_USB_Device_Library/Core/Inc/usbd_def.h").read_text(encoding="utf-8")
    hid = (root / "src/hw/driver/usb/usb_hid/usbd_hid.h").read_text(encoding="utf-8")
    report = (root / "src/hw/driver/usb/usb_hid/usbd_hid_internal.h").read_text(encoding="utf-8")
    caps = (root / "src/hw/hw_caps_keys.h").read_text(encoding="utf-8")
    extra = [
        enum(usbd, "USBD_StatusTypeDef"),
        macro(caps, "HW_KEYS_PRESS_MAX"),
        macro(report, "HID_KEYBOARD_REPORT_SIZE"),
        macro(report, "HID_BOOT_KEYBOARD_REPORT_SIZE"),
        *[macro(hid, name) for name in ("HID_EPIN_ADDR", "HID_VIA_EP_IN", "HID_EXK_EP_IN")],
    ]
    (build / "usb_fifo_ready_defs.inc").write_text("\n\n".join(extra) + "\n", encoding="utf-8")

    generated = []
    for path, source, names, extractor in (
        (ll_path, ll, (
            "USB_WritePacket", "USB_ReadDevAllOutEpInterrupt", "USB_ReadDevAllInEpInterrupt",
            "USB_ReadDevOutEPInterrupt", "USB_ReadDevInEPInterrupt", "USB_GetDevSpeed",
            "USB_ActivateSetup", "USB_EP0_OutStart", "USB_InZlpAhbDelay",
            "USB_EnableInTransfer", "USB_EPStartXfer",
        ), function),
        (pcd_path, pcd, (
            "PCD_ReadRxFifo", "HAL_PCD_EP_Transmit", "PCD_WriteEmptyTxFifo", "HAL_PCD_EP_TransmitReady",
            "PCD_EP_OutXfrComplete_int", "PCD_EP_OutSetupPacket_int", "HAL_PCD_IRQHandler",
        ), function),
        (conf_path, conf, (
            "USBD_Get_USB_Status", "USBD_LL_Transmit", "USBD_LL_CloseEP",
        ), bridge_function),
    ):
        for name in names:
            routine = extractor(source, name)
            line = source[:source.index(routine)].count("\n") + 1
            generated.append(f'#line {line} "{path.as_posix()}"\n{routine}')
    extracted = host_register_access("\n\n".join(generated))
    extracted = extracted.replace("uint32_t USBx_BASE = (uint32_t)hpcd->Instance;",
                                  "uintptr_t USBx_BASE = (uintptr_t)hpcd->Instance;")
    fifo_store = "USBx_DFIFO((uint32_t)ch_ep_num) = __UNALIGNED_UINT32_READ(pSrc);"
    assert extracted.count(fifo_store) == 1, "production USB_WritePacket FIFO store"
    extracted = extracted.replace(fifo_store,
                                  "model_fifo_write(USBx_BASE, (uint32_t)ch_ep_num, __UNALIGNED_UINT32_READ(pSrc));")
    # Close's endpoint interrupt retirement is another W1C store; adapter only.
    for direction, register in (("IN", "DIEPINT"), ("OUT", "DOEPINT")):
        extracted = extracted.replace(
            f"USBx_{direction}EP(ep)->{register} = USBx_{direction}EP(ep)->{register};",
            f"model_clear_{direction.lower()}(hpcd->Instance, ep, USBx_{direction}EP(ep)->{register});",
        )
    (build / "usb_fifo_ready_functions.inc").write_text(extracted + "\n", encoding="utf-8")
    return root / "tools/firmware_regression_tests/test_usb_fifo_ready.c"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path)
    parser.add_argument("--source", type=Path,
                        help="Use an isolated HAL source for a negative control.")
    args = parser.parse_args()
    from run import ROOT, BUILD, HID, execute
    build = args.build or BUILD / "usb-fifo-ready"
    source = generate(ROOT, build, args.source.read_text(encoding="utf-8") if args.source else None)
    for callbacks in (0, 1):
        execute(f"test_usb_fifo_ready_cb{callbacks}", [source, HID / "hid_tx_queue.c"],
                [f"-I{build}", f"-I{HID}", f"-DUSE_HAL_PCD_REGISTER_CALLBACKS={callbacks}",
                 "-Wno-pointer-to-int-cast"])


if __name__ == "__main__":
    main()
