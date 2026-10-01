"""Compile the unmodified production HAL endpoint-flush function and lock macros."""
import re
from pathlib import Path


def function(source: str) -> str:
    match = re.search(r"HAL_StatusTypeDef\s+HAL_PCD_EP_Flush\([^;]*?\)\s*\{", source)
    assert match, "HAL_PCD_EP_Flush"
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def enum(source: str, name: str) -> str:
    match = re.search(rf"typedef enum\s*\{{[^}}]*\}}\s*{name};", source)
    assert match, name
    return match.group()


def macro(source: str, name: str) -> str:
    match = re.search(rf"^#define\s+{name}\b", source, re.MULTILINE)
    assert match, name
    lines = source[match.start():].splitlines(keepends=True)
    end = 1
    while lines[end - 1].rstrip().endswith("\\"):
        end += 1
    return "".join(lines[:end])


def generate(root: Path, build: Path, pcd_source: str | None = None) -> Path:
    hal = root / "src/lib/ST/STM32H7RSxx_HAL_Driver"
    source = pcd_source if pcd_source is not None else (hal / "Src/stm32h7rsxx_hal_pcd.c").read_text(encoding="utf-8")
    definitions = (hal / "Inc/stm32h7rsxx_hal_def.h").read_text(encoding="utf-8")
    usb = (hal / "Inc/stm32h7rsxx_ll_usb.h").read_text(encoding="utf-8")
    parts = [enum(definitions, name) for name in ("HAL_StatusTypeDef", "HAL_LockTypeDef")]
    parts += [macro(definitions, name) for name in ("__HAL_LOCK", "__HAL_UNLOCK")]
    parts.append(macro(usb, "EP_ADDR_MSK"))
    (build / "usb_flush_defs.inc").write_text("\n\n".join(parts) + "\n", encoding="utf-8")
    (build / "usb_flush_function.inc").write_text(function(source) + "\n", encoding="utf-8")
    return root / "tools/firmware_regression_tests/test_usb_flush.c"
