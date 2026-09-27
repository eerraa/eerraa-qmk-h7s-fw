#!/usr/bin/env python3
"""V260909R1: compile actual firmware sources against deterministic host hardware stubs."""
from __future__ import annotations
import argparse
import os
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
QMK = ROOT / "src/ap/modules/qmk"
HID = ROOT / "src/hw/driver/usb/usb_hid"
BUILD = ROOT / "build-firmware-regression-tests"

def gcc() -> str:
    override = os.environ.get("HOST_CC")
    if override:
        return override
    installed = Path(r"D:\baram-fw-tools_exe\arm_toolchain\mingw_gcc\bin\gcc.exe")
    return str(installed) if installed.is_file() else shutil.which("gcc") or "gcc"

def execute(name: str, sources: list[Path], flags: list[str]) -> None:
    out = BUILD / (name + (".exe" if os.name == "nt" else ""))
    command = [gcc(), "-std=gnu11", "-O2", "-g", "-Wall", "-Wextra", "-Werror", *flags, *map(str, sources), "-o", str(out)]
    print("BUILD", name, flush=True)
    subprocess.run(command, check=True, timeout=90)
    subprocess.run([str(out)], check=True, timeout=30)

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--only", choices=["queue", "debounce", "eeprom", "usb", "qbuffer", "input", "guards", "rgb"])
    args = parser.parse_args()
    BUILD.mkdir(exist_ok=True)
    inc = [f"-I{HERE/'include'}", f"-I{ROOT/'src/common/hw/include'}", f"-I{ROOT/'src/ap/modules'}"]
    if args.only in (None, "queue"):
        execute("test_hid_tx_queue", [HERE/'test_hid_tx_queue.c', HID/'hid_tx_queue.c'], [f"-I{HID}"])
    if args.only in (None, "debounce"):
        # Source text is copied without edits solely to let host matrix/timer headers replace target-only includes.
        runtime = BUILD/'debounce_runtime.c'
        shutil.copyfile(QMK/'quantum/debounce_runtime.c', runtime)
        algorithms = [QMK/'quantum/debounce'/f"{name}.c" for name in ("sym_defer_pk", "sym_eager_pk", "asym_eager_defer_pk")]
        execute("test_debounce", [HERE/'test_debounce.c', runtime, *algorithms], [*inc, f"-I{QMK/'quantum'}"])
    if args.only in (None, "eeprom"):
        image = BUILD/"eeprom_image.c"
        shutil.copyfile(QMK/"port/platforms/eeprom.c", image)
        execute("test_eeprom_chain", [HERE/'test_eeprom_chain.c', image,
                ROOT/'src/hw/driver/eeprom/zd24c128.c', ROOT/'src/hw/driver/i2c_async.c'],
                [*inc, "-D_USE_HW_I2C", "-D_USE_HW_EEPROM", "-DEEPROM_CHIP_ZD24C128"])
    if args.only in (None, "usb"):
        execute("test_usb_transport", [HERE/'test_usb_transport.c', HID/'usbd_hid.c', HID/'hid_tx_queue.c', HID/'usb_diagnostics.c',
                ROOT/'src/hw/driver/usb/usb_class_pool.c'], [*inc, f"-I{HID}",
                f"-I{ROOT/'src/lib/ST/STM32_USB_Device_Library/Core/Inc'}",
                f"-I{ROOT/'src/hw/driver/usb/usb_cdc'}", "-DTEST_USB_TRANSPORT", "-DUSB_DIAGNOSTICS_HOST_TEST", "-Wno-unused-parameter"])
    if args.only in (None, "qbuffer"):
        execute("test_qbuffer", [HERE/'test_qbuffer.c', ROOT/'src/common/core/qbuffer.c'],
                [*inc, f"-I{ROOT/'src/common/core'}"])
    if args.only in (None, "input"):
        execute("test_input_features", [HERE/'test_input_features.c', QMK/'port/kkuk.c', QMK/'port/kill_switch.c'],
                [f"-I{HERE/'input_include'}", f"-I{QMK/'port'}", "-DKKUK_ENABLE", "-DKILL_SWITCH_ENABLE", "-Wno-unused-parameter"])
    if args.only in (None, "rgb"):
        from rgb_input_cases import generate as generate_rgb
        source = generate_rgb(ROOT, BUILD)
        # V260911R1: MinGW의 MS bitfield 대신 ARM GCC와 같은 QMK action_t 배치를 사용한다.
        layout = ["-mno-ms-bitfields"] if os.name == "nt" else []
        execute("test_rgb_physical_input", [source], [f"-I{HERE}", f"-I{QMK/'quantum'}", *layout,
                "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-variable"])
        from rgb_frame_cases import generate as generate_frames
        for board in ("brick60", "brick65"):
            frame_source = generate_frames(ROOT, BUILD, board)
            execute("test_rgb_frames_" + board, [frame_source], [f"-I{HERE}", f"-I{HERE/'ws2812_include'}",
                    f"-I{ROOT/'src/common/hw/include'}", f"-I{QMK/'quantum'}", *layout, "-Wno-unused-function"])
    if args.only in (None, "guards"):
        from source_cases import generate
        via, reset, rgb_gate, ws2812, rgb_sat = generate(ROOT, BUILD)
        execute("test_via_guard", [via], [])
        execute("test_reset_barrier", [reset], [])
        execute("test_rgb_task_gate", [rgb_gate], [])
        for channels in (19, 27, 30, 32):
            execute("test_ws2812_transport_" + str(channels), [ws2812], [f"-I{HERE/'ws2812_include'}",
                    f"-I{ROOT/'src/common/hw/include'}", f"-DHW_WS2812_MAX_CH={channels}", "-Wno-unused-function"])
        execute("test_rgb_mode_transition", [rgb_sat], [])
    print("All selected firmware regression tests passed.", flush=True)

if __name__ == "__main__":
    main()
