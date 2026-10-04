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
        from test_i2c_timing import verify as verify_i2c_timing
        verify_i2c_timing(ROOT, BUILD/"i2c-timing", gcc())
        image = BUILD/"eeprom_image.c"
        shutil.copyfile(QMK/"port/platforms/eeprom.c", image)
        from storage_cases import macro_callers, initialization_callers, default_provider_callers
        initialization = initialization_callers(ROOT, BUILD)
        for layout_size in range(1, 5):
            execute("test_eeprom_initialization_" + str(layout_size), [initialization],
                    ["-DVIA_EEPROM_LAYOUT_OPTIONS_SIZE=" + str(layout_size)])
        execute("test_eeprom_chain", [HERE/'test_eeprom_chain.c', image, macro_callers(ROOT, BUILD),
                ROOT/'src/hw/driver/eeprom/zd24c128.c', ROOT/'src/hw/driver/i2c_async.c'],
                [*inc, "-D_USE_HW_I2C", "-D_USE_HW_EEPROM", "-DEEPROM_CHIP_ZD24C128"])
        execute("test_eeprom_default_provider", [default_provider_callers(ROOT, BUILD)],
                [f"-I{ROOT/'tools/era_via_host_tests/include'}", f"-I{QMK/'port'}", f"-I{QMK/'quantum'}",
                 f"-I{ROOT/'src/ap/modules'}", "-Wno-unused-function"])
        chain = BUILD / ("test_eeprom_chain.exe" if os.name == "nt" else "test_eeprom_chain")
        snapshot = BUILD / "power-cut-eeprom.bin"
        for boundary in range(1, 1000):
            result = subprocess.run([str(chain), "cut", str(boundary), str(snapshot)], timeout=30)
            if result.returncode == 2:
                print(f"PASS: physical EEPROM model, {boundary-1} torn-byte/bus cuts, cold mount and upload retry", flush=True)
                break
            result.check_returncode()
            subprocess.run([str(chain), "recover", str(snapshot)], check=True, timeout=30)
        else:
            raise AssertionError("power-cut boundary limit exceeded")
        from state_sync_cases import storage as generate_state_storage
        execute("test_state_sync_storage", [generate_state_storage(ROOT, BUILD), image,
                ROOT/'src/hw/driver/eeprom/zd24c128.c', ROOT/'src/hw/driver/i2c_async.c'],
                [f"-I{HERE}", *inc, "-D_USE_HW_I2C", "-D_USE_HW_EEPROM", "-DEEPROM_CHIP_ZD24C128"])
    if args.only in (None, "usb"):
        execute("test_usb_transport", [HERE/'test_usb_transport.c', HID/'usbd_hid.c', HID/'hid_tx_queue.c',
                ROOT/'src/hw/driver/usb/usb_class_pool.c'], [*inc, f"-I{HID}",
                f"-I{ROOT/'src/lib/ST/STM32_USB_Device_Library/Core/Inc'}",
                f"-I{ROOT/'src/hw/driver/usb/usb_cdc'}", "-DTEST_USB_TRANSPORT", "-Wno-unused-parameter"])
        from usb_pcd_cases import generate as generate_pcd
        pcd_source = generate_pcd(ROOT, BUILD)
        execute("test_usb_pcd", [pcd_source, HID/'usbd_hid.c', HID/'hid_tx_queue.c',
                ROOT/'src/hw/driver/usb/usb_class_pool.c'], [*inc, f"-I{HID}", f"-I{BUILD}",
                f"-I{ROOT/'src/lib/ST/STM32_USB_Device_Library/Core/Inc'}",
                f"-I{ROOT/'src/hw/driver/usb/usb_cdc'}", "-DTEST_USB_TRANSPORT", "-Wno-unused-parameter"])
        from usb_irq_cases import generate as generate_irq
        irq_source = generate_irq(ROOT, BUILD)
        for callbacks in (0, 1):
            execute("test_usb_irq_bridge_cb" + str(callbacks), [irq_source],
                    [f"-I{BUILD}", f"-DUSE_HAL_PCD_REGISTER_CALLBACKS={callbacks}", "-DTEST_USB_IRQ_BRIDGE",
                     "-Wno-pointer-to-int-cast", "-Wno-unused-function"])
        from usb_fifo_ready_cases import generate as generate_fifo_ready
        fifo_build = BUILD / "usb-fifo-ready"
        fifo_source = generate_fifo_ready(ROOT, fifo_build)
        for callbacks in (0, 1):
            execute("test_usb_fifo_ready_cb" + str(callbacks), [fifo_source, HID/'hid_tx_queue.c'],
                    [f"-I{fifo_build}", f"-I{HID}", f"-DUSE_HAL_PCD_REGISTER_CALLBACKS={callbacks}",
                     "-Wno-pointer-to-int-cast"])
        from usb_flush_cases import generate as generate_flush
        execute("test_usb_flush", [generate_flush(ROOT, BUILD)], [f"-I{BUILD}"])
        from teardown_cases import generate as generate_teardown
        execute("test_usb_teardown", [generate_teardown(ROOT, BUILD), HID/'usbd_hid.c', HID/'hid_tx_queue.c',
                ROOT/'src/hw/driver/usb/usb_class_pool.c'],
                [*inc, f"-I{HID}", f"-I{HERE}", f"-I{BUILD}",
                f"-I{ROOT/'src/lib/ST/STM32_USB_Device_Library/Core/Inc'}",
                f"-I{ROOT/'src/hw/driver/usb/usb_cdc'}", "-DTEST_USB_TRANSPORT", "-Wno-unused-parameter"])
        from usb_reset_barrier_cases import generate as generate_reset_barrier
        execute("test_usb_reset_barrier", [generate_reset_barrier(ROOT, BUILD), HID/'usbd_hid.c', HID/'hid_tx_queue.c',
                ROOT/'src/hw/driver/usb/usb_class_pool.c'],
                [*inc, f"-I{HID}", f"-I{HERE}", f"-I{BUILD}",
                f"-I{ROOT/'src/lib/ST/STM32_USB_Device_Library/Core/Inc'}",
                f"-I{ROOT/'src/hw/driver/usb/usb_cdc'}", "-DTEST_USB_TRANSPORT", "-Wno-unused-parameter"])
        from usb_cdc_control_cases import generate as generate_cdc_control
        execute("test_usb_cdc_control", [generate_cdc_control(ROOT, BUILD)],
                [*inc, f"-I{BUILD}", f"-I{ROOT/'src/lib/ST/STM32_USB_Device_Library/Core/Inc'}",
                f"-I{ROOT/'src/hw/driver/usb/usb_cdc'}"])
        from usb_cdc_lifecycle_cases import generate as generate_cdc_lifecycle
        cdc_sources = [generate_cdc_lifecycle(ROOT, BUILD), BUILD/'usb_cdc_lifecycle_class.c',
                       ROOT/'src/hw/driver/usb/usb_class_pool.c', ROOT/'src/common/core/qbuffer.c']
        cdc_flags = [*inc, f"-I{HERE}", f"-I{BUILD}", f"-I{ROOT/'src/common/core'}", f"-I{HID}",
                     f"-I{ROOT/'src/hw/driver/usb/usb_cdc'}", f"-I{ROOT/'src/hw/driver/usb/usb_cmp'}",
                     f"-I{ROOT/'src/lib/ST/STM32_USB_Device_Library/Core/Inc'}",
                     "-include", str(HERE/'usb_cdc_lifecycle_hardware.h'), "-Wno-unused-parameter"]
        execute("test_usb_cdc_lifecycle", cdc_sources, cdc_flags)
        execute("test_usb_cdc_lifecycle_composite", cdc_sources, [*cdc_flags, "-DUSE_USBD_COMPOSITE"])
        from usb_composite_cases import generate as generate_composite
        execute("test_usb_composite", [generate_composite(ROOT, BUILD)],
                [f"-I{BUILD}", f"-I{HERE/'usb_composite_include'}",
                 f"-I{ROOT/'src/lib/ST/STM32_USB_Device_Library/Core/Inc'}"])
        from usb_ll_cases import generate as generate_ll
        execute("test_usb_ll", [generate_ll(ROOT, BUILD)],
                [f"-I{BUILD}", f"-I{HERE/'usb_ll_include'}", "-Wno-pointer-to-int-cast"])
        from usb_close_cases import generate as generate_close
        execute("test_usb_close", [generate_close(ROOT, BUILD)],
                [f"-I{BUILD}", f"-I{HERE/'usb_ll_include'}", "-Wno-pointer-to-int-cast"])
        from usb_stop_cases import generate as generate_stop
        execute("test_usb_stop", [generate_stop(ROOT, BUILD)],
                [f"-I{BUILD}", "-Wno-pointer-to-int-cast"])
    if args.only in (None, "qbuffer"):
        execute("test_qbuffer", [HERE/'test_qbuffer.c', ROOT/'src/common/core/qbuffer.c'],
                [*inc, f"-I{ROOT/'src/common/core'}"])
    if args.only in (None, "input"):
        execute("test_input_features", [HERE/'test_input_features.c', QMK/'port/kkuk.c', QMK/'port/kill_switch.c'],
                [f"-I{HERE/'input_include'}", f"-I{QMK/'port'}", "-DKKUK_ENABLE", "-DKILL_SWITCH_ENABLE", "-Wno-unused-parameter"])
        from tapping_admission_cases import generate as generate_tapping
        layout = ["-mno-ms-bitfields"] if os.name == "nt" else []
        execute("test_tapping_admission", generate_tapping(ROOT, BUILD),
                [f"-I{BUILD/'tapping_admission'}", f"-I{QMK/'quantum'}", *layout,
                 "-DTAPPING_TERM_PER_KEY", "-DPERMISSIVE_HOLD_PER_KEY", "-DHOLD_ON_OTHER_KEY_PRESS_PER_KEY",
                 "-DTAPDANCE_ENABLE", "-Wno-unused-parameter", "-Wno-unused-function"])
        from layer_owner_cases import generate as generate_layer_owners
        owner_sources, owner_flags = generate_layer_owners(ROOT, BUILD)
        execute("test_layer_owner_no_td", owner_sources, owner_flags)
        from merge_frontend_cases import generate as generate_frontend
        execute("test_merge_frontend", [generate_frontend(ROOT, BUILD)],
                [f"-I{HERE}", f"-I{QMK/'quantum'}", f"-I{QMK/'port'}", *layout,
                 "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-variable"])
        from macro_executor_cases import generate as generate_macro
        execute("test_macro_executor", [generate_macro(ROOT, BUILD)], [f"-I{BUILD/'macro-executor'}"])
    if args.only in (None, "rgb"):
        from rgb_input_cases import generate as generate_rgb
        source = generate_rgb(ROOT, BUILD)
        # V260911R1: MinGW의 MS bitfield 대신 ARM GCC와 같은 QMK action_t 배치를 사용한다.
        layout = ["-mno-ms-bitfields"] if os.name == "nt" else []
        execute("test_rgb_physical_input", [source], [f"-I{HERE}", f"-I{QMK/'quantum'}", f"-I{QMK/'port'}", *layout,
                "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-variable"])
        from rgb_frame_cases import generate as generate_frames
        for board in ("brick60", "brick65"):
            frame_source = generate_frames(ROOT, BUILD, board)
            execute("test_rgb_frames_" + board, [frame_source], [f"-I{HERE}", f"-I{HERE/'ws2812_include'}",
                    f"-I{ROOT/'src/common/hw/include'}", f"-I{QMK/'quantum'}", *layout, "-Wno-unused-function"])
        from state_sync_cases import rgb as generate_state_rgb
        execute("test_state_sync_rgb", [generate_state_rgb(ROOT, BUILD), QMK/'port/era_state_sync.c'],
                [f"-I{HERE}", f"-I{HERE/'ws2812_include'}", f"-I{ROOT/'src/common/hw/include'}",
                 f"-I{ROOT/'src/ap/modules'}", "-Wno-unused-function", "-Wno-return-type"])
    if args.only in (None, "guards"):
        from polling_cases import generate as generate_polling
        execute("test_polling_wire", [generate_polling(ROOT, BUILD)], [])
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
