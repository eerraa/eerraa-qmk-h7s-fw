"""Extract unchanged H7RS LL routines for deterministic register fixtures.

Register addresses are fixture aliases. The firmware default FIFO polling bound
is kept in the generated header; tests use the header's supported override to
exercise timeout branches quickly without changing production source.
"""
from __future__ import annotations

import re
from pathlib import Path


def function(source: str, name: str) -> str:
    match = re.search(
        rf"(?:static\s+)?(?:HAL_StatusTypeDef|USBD_StatusTypeDef)\s+{name}\([^;]*?\)\s*\{{",
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
        "#ifndef HAL_USB_TIMEOUT\n" + macro(usb_header, "HAL_USB_TIMEOUT") + "\n#endif",
        macro(hal_header, "__HAL_LOCK"),
        macro(hal_header, "__HAL_UNLOCK"),
        *[macros[name] for name in sorted(macros)],
    ]
    (build / "usb_ll_defs.inc").write_text("\n\n".join(definitions) + "\n", encoding="utf-8")
    generated = []
    for name, routine in zip(names, routines):
        line = source[:source.index(routine)].count("\n") + 1
        generated.append(f'#line {line} "{ll_path.as_posix()}"\n{routine}')
    (build / "usb_ll_functions.inc").write_text("\n\n".join(generated) + "\n", encoding="utf-8")
    return root / "tools/firmware_regression_tests/test_usb_ll.c"
