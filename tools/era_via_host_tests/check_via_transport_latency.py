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
    receive_pos = data_out.find("via_hid_receive_func(via_hid_usb_rx_report, rx_size)")
    rearm_pos = data_out.find("USBD_LL_PrepareReceive")
    if receive_pos < 0 or rearm_pos <= receive_pos:
        return fail("VIA OUT must be re-armed immediately after the RX callback copies the report")
    if "HID_VIA_EP_OUT" not in data_out[rearm_pos:]:
        return fail("VIA OUT re-arm must target HID_VIA_EP_OUT")
    if "via_hid_usb_rx_report" not in data_out[rearm_pos:]:
        return fail("VIA OUT re-arm must keep using the dedicated RX buffer")

    sof = extract_function(source, "USBD_HID_SOF")
    if "millis(" in sof or "delay(" in sof:
        return fail("VIA SOF drain must not use a wall-clock delay")
    if "qbufferAvailable(&via_report_q)" not in sof:
        return fail("VIA SOF drain must check the response queue")
    if "usbHidEpTryAcquire(hhid, HID_VIA_EP_IN)" not in sof:
        return fail("VIA SOF drain must obey the VIA IN busy fence")
    if "USBD_LL_Transmit(pdev, HID_VIA_EP_IN" not in sof:
        return fail("VIA responses must transmit on HID_VIA_EP_IN")
    if "via_hid_usb_tx_report" not in sof:
        return fail("VIA IN transmission must use the dedicated TX buffer")
    if "via_hid_usb_rx_report" in sof:
        return fail("VIA SOF drain must not share the armed RX buffer")
    if "USBD_LL_PrepareReceive" in sof:
        return fail("VIA OUT re-arm must not wait for response drain")

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

    print("PASS VIA transport has no 20ms throttle and rearms OUT immediately")
    return 0


if __name__ == "__main__":
    sys.exit(main())
