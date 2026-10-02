"""Extract H7RS LL control flow with explicit MMIO W1C adapters for host fixtures.

Register addresses are fixture aliases. The firmware default FIFO polling bound
is kept in the generated header; tests use the header's supported override to
exercise timeout branches quickly without changing production source.
"""
from __future__ import annotations

import re
from pathlib import Path


def function(source: str, name: str) -> str:
    match = re.search(
        rf"(?:static\s+)?(?:void|HAL_StatusTypeDef|USBD_StatusTypeDef)\s+{name}\([^;]*?\)\s*\{{",
        source,
    )
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def typedef(source: str, kind: str, name: str) -> str:
    match = re.search(rf"typedef {kind}\s*\{{[^}}]*\}}\s*{name};", source)
    assert match, name
    return match.group()


def macro(source: str, name: str) -> str:
    match = re.search(rf"^#define\s+{name}\b", source, re.MULTILINE)
    assert match, name
    lines = source[match.start():].splitlines(keepends=True)
    end = 1
    while lines[end - 1].rstrip().endswith("\\"):
        end += 1
    return "".join(lines[:end]).rstrip()


def register_access(source: str) -> str:
    # Hardware W1C stores must not become ordinary RAM assignments on the host.
    for direction, register in (("IN", "DIEPINT"), ("OUT", "DOEPINT")):
        source = re.sub(rf"USBx_{direction}EP\(([^)]*)\)->{register} = ([^;]+);",
                        rf"usb_ll_clear_{direction.lower()}(\1, \2);", source)
    return source


def generate(root: Path, build: Path) -> Path:
    hal = root / "src/lib/ST/STM32H7RSxx_HAL_Driver"
    ll_path = hal / "Src/stm32h7rsxx_ll_usb.c"
    source = ll_path.read_text(encoding="utf-8")
    usb_header = (hal / "Inc/stm32h7rsxx_ll_usb.h").read_text(encoding="utf-8")
    hal_header = (hal / "Inc/stm32h7rsxx_hal_def.h").read_text(encoding="utf-8")
    device_header = (root / "src/lib/ST/CMSIS/Device/ST/STM32H7RSxx/Include/stm32h7s3xx.h").read_text(encoding="utf-8")
    names = ("USB_FlushTxFifo", "USB_FlushRxFifo", "USB_EPStopXfer", "USB_DeactivateEndpoint")
    routines = [function(source, name) for name in names]
    required = set(re.findall(r"\bUSB_OTG_\w+\b", "\n".join(routines)))
    required -= {"USB_OTG_GlobalTypeDef", "USB_OTG_EPTypeDef"}
    required |= {
        "USB_OTG_DIEPINT_EPDISD", "USB_OTG_DOEPINT_EPDISD",
        "USB_OTG_GINTMSK_GONAKEFFM", "USB_OTG_GINTMSK_RXFLVLM",
        "USB_OTG_GINTSTS_BOUTNAKEFF", "USB_OTG_GINTSTS_RXFLVL",
        "USB_OTG_GAHBCFG_DMAEN", "USB_OTG_DCTL_SGONAK", "USB_OTG_DCTL_CGONAK",
        "USB_OTG_GRXSTSP_EPNUM", "USB_OTG_GRXSTSP_BCNT", "USB_OTG_GRXSTSP_PKTSTS",
        "USB_OTG_DIEPINT_XFRC", "USB_OTG_DOEPINT_XFRC", "USB_OTG_GRSTCTL_TXFNUM",
    }
    macros: dict[str, str] = {}
    while required - macros.keys():
        for name in sorted(required - macros.keys()):
            definition = macro(device_header, name)
            macros[name] = definition
            required.update(re.findall(r"\bUSB_OTG_\w+\b", definition))
    definitions = [
        typedef(hal_header, "enum", "HAL_StatusTypeDef"),
        typedef(hal_header, "enum", "HAL_LockTypeDef"),
        typedef(usb_header, "struct", "USB_EPTypeDef"),
        "typedef USB_EPTypeDef USB_OTG_EPTypeDef;",
        "typedef USB_EPTypeDef PCD_EPTypeDef;",
        macro(usb_header, "EP_ADDR_MSK"),
        *[macro(usb_header, name) for name in ("STS_GOUT_NAK", "STS_DATA_UPDT", "STS_SETUP_UPDT", "EP_TYPE_ISOC", "USB_EP_STOP_MAX_POLLS")],
        "#ifndef HAL_USB_TIMEOUT\n" + macro(usb_header, "HAL_USB_TIMEOUT") + "\n#endif",
        macro(hal_header, "__HAL_LOCK"),
        macro(hal_header, "__HAL_UNLOCK"),
        *[macros[name] for name in sorted(macros)],
    ]
    (build / "usb_ll_defs.inc").write_text("\n\n".join(definitions) + "\n", encoding="utf-8")
    generated = []
    for name, routine in zip(names, routines):
        line = source[:source.index(routine)].count("\n") + 1
        generated.append(f'#line {line} "{ll_path.as_posix()}"\n{register_access(routine)}')
    (build / "usb_ll_functions.inc").write_text("\n\n".join(generated) + "\n", encoding="utf-8")
    return root / "tools/firmware_regression_tests/test_usb_ll.c"
