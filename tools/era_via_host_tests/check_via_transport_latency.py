#!/usr/bin/env python3
"""H7S VIA transport must not reintroduce the retired 20 ms response throttle."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
USBD_HID = ROOT / "src/hw/driver/usb/usb_hid/usbd_hid.c"


def extract_function(source: str, name: str) -> str:
    match = re.search(rf"(?:static\s+)?(?:uint8_t|bool|void)\s+{name}\s*\([^)]*\)\s*\{{", source)
    if not match:
        raise SystemExit(f"missing {name}()")
    start = match.end() - 1
    depth = 0
    for i, ch in enumerate(source[start:], start):
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return source[start : i + 1]
    raise SystemExit(f"unbalanced {name}()")


def fail(message: str) -> int:
    print(f"FAIL {message}")
    return 1


def main() -> int:
    source = USBD_HID.read_text(encoding="utf-8")

    if "via_report_time" in source or "via_report_pre_time" in source:
        return fail("VIA response wall-clock throttle state must stay removed")

    data_out = extract_function(source, "USBD_HID_DataOut")
    if data_out.find("memcpy(") < 0 or data_out.find("usbHidRearmViaLocked(pdev)") < data_out.find("memcpy("):
        return fail("VIA RX must copy into owned queue storage before rearm")
    rearm = extract_function(source, "usbHidRearmViaLocked")
    if "via_rx_count < HID_VIA_RX_DEPTH" not in rearm or "USBD_LL_PrepareReceive(pdev, HID_VIA_EP_OUT" not in rearm:
        return fail("VIA OUT must rearm immediately only while a queue slot exists, otherwise NAK")
    sof = extract_function(source, "USBD_HID_SOF")
    completion = extract_function(source, "USBD_HID_DataIn")
    if "usbHidPumpLocked(pdev)" not in completion or "hidTxComplete(&via_tx)" not in completion:
        return fail("VIA completion must immediately pump the next response")
    if "usbHidPumpLocked(pdev)" not in sof:
        return fail("SOF must provide bounded retry after an arm failure")
    pump = extract_function(source, "usbHidPumpLocked")
    if "hidTxKick(&via_tx" not in pump:
        return fail("VIA must use the immutable-active-buffer FIFO")
    for body in (sof, completion, pump):
        if "millis(" in body or "delay(" in body:
            return fail("normal transport service must not impose a wall-clock throttle")
    if "generation == transport_generation" not in extract_function(source, "usbHidEnqueueViaResponse"):
        return fail("responses must not cross a bus generation")
    request = extract_function(source, "usbHidReadViaRequest")
    if "via_tx.count < via_tx.capacity" not in request:
        return fail("a request needs reserved response credit before dispatch")

    if "pdev->ep_out[HID_VIA_EP_OUT & 0xFU].is_used = 1U" not in source:
        return fail("VIA OUT ownership must be registered in ep_out")
    if "pdev->ep_in[HID_VIA_EP_OUT & 0xFU].is_used = 1U" in source:
        return fail("VIA OUT ownership must not be registered in ep_in")

    descriptor_start = source.find("__ALIGN_BEGIN static uint8_t USBD_HID_CfgDesc")
    descriptor_end = source.find("};", descriptor_start)
    if descriptor_start < 0 or descriptor_end < 0:
        return fail("HID configuration descriptor not found")
    descriptor = source[descriptor_start:descriptor_end]
    for endpoint in ("HID_VIA_EP_IN", "HID_VIA_EP_OUT"):
        pos = descriptor.find(endpoint)
        if pos < 0 or "HID_FS_BINTERVAL" not in descriptor[pos : pos + 500]:
            return fail(f"{endpoint} must advertise the 1 ms FS interval")

    print("PASS VIA transport is completion-driven with immediate RX rearm/NAK, response credit and bus generations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
