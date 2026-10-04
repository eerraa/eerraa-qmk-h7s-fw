"""실제 I2C 초기화/상수와 ST·EEPROM timing 제한을 독립 검증한다.

파형 실측 시험이 아니다. ST H7S BSP의 timing 식과 DS14359 Table 124,
ZD24C128A Table 8-3의 엄격한 제한을 적용한다. 클록 ±1%는 설계 여유다.
https://github.com/STMicroelectronics/stm32h7s78-dk-bsp/blob/356912a668692079bc31871c6ebfea0048a19c4f/stm32h7s78_discovery_bus.c
https://www.st.com/resource/en/datasheet/stm32h7s3a8.pdf
https://static.chipdip.ru/lib/578/DOC042578936.pdf
https://www.nxp.com/docs/en/user-guide/UM10204.pdf (Table 11, p44)
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
from fractions import Fraction
from pathlib import Path

from source_cases import function

ROOT = Path(__file__).resolve().parents[2]
KERNEL_HZ = 150_000_000
AF_MIN_NS, AF_MAX_NS, DNF = 50, 165, 0
# Sm tSU.STO=4700ns, Fm+ tHIGH=400ns/tSU.DAT=100ns는 EEPROM이 더 엄격하다.
LIMITS = {
    100: dict(low=4700, high=4000, setup=250, valid=3450, rise=1000, fall=300,
              stop=4700, start_hold=4000, start_setup=4700, bus_free=4700),
    400: dict(low=1300, high=600, setup=100, valid=900, rise=300, fall=300,
              stop=600, start_hold=600, start_setup=600, bus_free=1300),
    1000: dict(low=500, high=400, setup=100, valid=450, rise=100, fall=100,
               stop=260, start_hold=260, start_setup=260, bus_free=500),
}


def timing_bounds(word: int, khz: int) -> dict:
    """Register를 시간으로 환산한다. 생산 함수의 알고리즘 복제는 하지 않는다."""
    p, delay, hold = (word >> 28) + 1, ((word >> 20) & 15) + 1, (word >> 16) & 15
    high, low = ((word >> 8) & 255) + 1, (word & 255) + 1
    limits = LIMITS[khz]
    failures, corners = (["reserved_TIMINGR_bits"] if word & 0x0F000000 else []), []
    for clock in (KERNEL_HZ * 99 // 100, KERNEL_HZ, KERNEL_HZ * 101 // 100):
        tclk = Fraction(1_000_000_000, clock)
        tick = p * tclk
        sync_min = AF_MIN_NS + (DNF + 2) * tclk
        # Edges=0, 최소 filter/동기화에서 가장 빠르다. 보드 실측 edge를 가정하지 않는다.
        shortest_period = (low + high) * tick + 2 * sync_min
        longest_period = ((low + high) * tick + 2 * (AF_MAX_NS + (DNF + 3) * tclk)
                          + limits["rise"] + limits["fall"])
        low_ns, high_ns = low * tick + sync_min, high * tick + sync_min
        setup_ns = delay * tick - limits["rise"]
        hold_ns = hold * tick + AF_MIN_NS + (DNF + 3) * tclk - limits["fall"]
        valid_ns = hold * tick + AF_MAX_NS + (DNF + 4) * tclk + limits["rise"]
        checks = {
            "maximum_frequency": shortest_period >= Fraction(1_000_000, khz),
            "low": low_ns >= limits["low"], "high": high_ns >= limits["high"],
            "data_setup": setup_ns >= limits["setup"], "data_hold": hold_ns >= 0,
            "data_valid": valid_ns <= limits["valid"],
            "stop_setup": high * tick >= limits["stop"],
            "start_hold": high * tick >= limits["start_hold"],
            "start_setup": low * tick >= limits["start_setup"],
            "bus_free": low * tick >= limits["bus_free"],
            "kernel_low_clock": tclk < (low_ns - AF_MIN_NS - DNF * tclk) / 4,
            "kernel_high_clock": tclk < high_ns,
            # Controller의 SDADEL/SCLDEL stretch가 SCLL보다 길어지지 않는다.
            "data_stretch": (hold + delay) * tick + tclk <= low * tick,
        }
        failures.extend(f"{clock}Hz:{name}" for name, ok in checks.items() if not ok)
        corners.append(dict(clock_hz=clock, low_ns=float(low_ns), high_ns=float(high_ns),
                            setup_ns=float(setup_ns), hold_ns=float(hold_ns),
                            valid_ns=float(valid_ns), max_khz=float(1_000_000 / shortest_period),
                            shortest_period_ns=float(shortest_period),
                            longest_period_ns=float(longest_period),
                            min_khz_without_stretch=float(1_000_000 / longest_period)))
    return dict(word=f"0x{word:08X}", fields=dict(presc=p-1, scldel=delay-1, sdadel=hold,
                sclh=high-1, scll=low-1), limits_ns=limits, corners=corners, failures=failures)


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define I2C_MAX_CH 1
#define _DEF_I2C1 0
#define _DEF_I2C2 1
#define HAL_OK 0
#define I2C_ADDRESSINGMODE_7BIT 7U
#define I2C_DUALADDRESS_DISABLE 0U
#define I2C_GENERALCALL_DISABLE 0U
#define I2C_NOSTRETCH_DISABLE 0U
#define I2C_FASTMODEPLUS_ENABLE 1U
#define I2C_FASTMODEPLUS_DISABLE 0U
#define I2C_ANALOGFILTER_ENABLE 1U
#define RCC_PERIPHCLK_I2C23 23U
typedef struct {
  uint32_t Timing, OwnAddress1, AddressingMode, DualAddressMode, OwnAddress2;
  uint32_t GeneralCallMode, NoStretchMode;
} I2C_InitTypeDef;
typedef struct { void *Instance; I2C_InitTypeDef Init; } I2C_HandleTypeDef;
static I2C_HandleTypeDef handle;
static unsigned peripheral;
static struct { void *p_i2c; I2C_HandleTypeDef *p_hi2c; } i2c_tbl[] = {{&peripheral, &handle}};
static uint32_t i2c_freq[I2C_MAX_CH], i2c_errcount[I2C_MAX_CH];
static bool is_begin[I2C_MAX_CH], i2c_timing_logged[I2C_MAX_CH], owned, peripheral_active;
static unsigned resets, inits, deinits, clock_reads, plus, analog, digital;
static uint32_t kernel_clock;
static int init_status, plus_status, analog_status, digital_status;
static bool i2cAsyncOwned(uint8_t ch) { assert(ch==0U); return owned; }
static void i2cReset(uint8_t ch) { assert(ch==0U); resets++; }
static void logPrintf(const char *format, ...) { (void)format; }
static int HAL_I2C_DeInit(I2C_HandleTypeDef *h) {
  assert(h==&handle); deinits++; peripheral_active=false; return HAL_OK;
}
static int HAL_I2C_Init(I2C_HandleTypeDef *h) {
  assert(h==&handle); inits++; peripheral_active=true; return init_status;
}
static uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint32_t selector) {
  assert(selector==RCC_PERIPHCLK_I2C23 && inits==1U); clock_reads++; return kernel_clock;
}
static int HAL_I2CEx_ConfigFastModePlus(I2C_HandleTypeDef *h, uint32_t v) {
  assert(h==&handle); plus=v; return plus_status;
}
static int HAL_I2CEx_ConfigAnalogFilter(I2C_HandleTypeDef *h, uint32_t v) {
  assert(h==&handle); analog=v; return analog_status;
}
static int HAL_I2CEx_ConfigDigitalFilter(I2C_HandleTypeDef *h, uint32_t v) {
  assert(h==&handle); digital=v; return digital_status;
}
static void fixture_reset(void) {
  memset(&handle,0,sizeof(handle));
  resets=inits=deinits=clock_reads=0U; plus=analog=digital=99U;
  owned=is_begin[0]=peripheral_active=false;
  init_status=plus_status=analog_status=digital_status=HAL_OK; kernel_clock=150000000U;
}
'''

MAIN = r'''
int main(void) {
  const uint32_t modes[]={100U,400U,1000U};
  for (unsigned i=0; i<3U; i++) {
    fixture_reset();
    assert(i2cBegin(0U,modes[i]) && is_begin[0] && peripheral_active);
    assert(inits==1U && deinits==1U && resets==1U && clock_reads==1U);
    assert(handle.Init.Timing==i2cGetTimming(modes[i]));
    assert(analog==I2C_ANALOGFILTER_ENABLE && digital==0U && plus==(modes[i]==1000U));
    printf("%u %08X\n",(unsigned)modes[i],(unsigned)handle.Init.Timing);
  }
  fixture_reset();
  assert(i2cBegin(0U,0U) && handle.Init.Timing==i2cGetTimming(400U));
  printf("0 %08X\n",(unsigned)handle.Init.Timing);
  const uint32_t clocks[]={0U,64000000U,75000000U,149999999U,150000001U,300000000U};
  for (unsigned i=0; i<6U; i++) {
    fixture_reset(); kernel_clock=clocks[i]; is_begin[0]=true;
    assert(!i2cBegin(0U,1000U) && !is_begin[0] && !peripheral_active);
    assert(inits==1U && deinits==2U && clock_reads==1U);
    assert(analog==99U && digital==99U && plus==99U);
  }
  for (unsigned mode=0; mode<3U; mode++) {
    fixture_reset(); init_status=1; is_begin[0]=true;
    assert(!i2cBegin(0U,modes[mode]) && !is_begin[0] && !peripheral_active);
    assert(deinits==2U && clock_reads==0U && analog==99U);
  }
  for (unsigned i=0; i<3U; i++) for (unsigned mode=0; mode<3U; mode++) {
    fixture_reset(); is_begin[0]=true;
    if (i==0U) plus_status=1;
    if (i==1U) analog_status=1;
    if (i==2U) digital_status=1;
    assert(!i2cBegin(0U,modes[mode]) && !is_begin[0] && !peripheral_active);
    assert(deinits==2U && inits==1U && clock_reads==1U);
    if (i==0U) assert(analog==99U && digital==99U);
    if (i==1U) assert(digital==99U);
  }
  fixture_reset(); owned=true; is_begin[0]=true;
  assert(!i2cBegin(0U,1000U) && is_begin[0] && inits==0U && resets==0U);
  fixture_reset();
  assert(!i2cBegin(1U,1000U) && inits==0U && resets==0U);
  puts("PASS: actual i2cBegin/timing, three speeds, fallback, six wrong clocks, HAL init/filter/Fm+ failures, ownership and range");
}
'''


def verify(root: Path, build: Path, cc: str) -> None:
    build.mkdir(parents=True, exist_ok=True)
    source_path = root / "src/hw/driver/i2c.c"
    source = source_path.read_text(encoding="utf-8")
    clock_macro = re.search(r"(?m)^#define I2C_TIMING_KERNEL_CLOCK_HZ\s+\d+U$", source)
    assert clock_macro, "timing kernel clock prerequisite is missing"
    # 실제 생산 두 함수 전체를 statement 변경 없이 컴파일한다.
    production = function(source, "i2cGetTimming") + "\n" + function(source, "i2cBegin")
    fixture = build / "i2c_timing_fixture.c"
    fixture.write_text(PREFIX + clock_macro.group(0) + "\n" + production + MAIN, encoding="utf-8")
    exe = build / ("i2c_timing_fixture.exe" if os.name == "nt" else "i2c_timing_fixture")
    command = [cc, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", str(fixture), "-o", str(exe)]
    compiled = subprocess.run(command, capture_output=True, text=True, timeout=90)
    (build / "compile.log").write_text(compiled.stdout + compiled.stderr, encoding="utf-8")
    compiled.check_returncode()
    executed = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    (build / "production-fixture.log").write_text(executed.stdout + executed.stderr, encoding="utf-8")
    executed.check_returncode()
    timings = {int(k): int(v, 16) for k, v in re.findall(r"(?m)^(\d+) ([0-9A-F]{8})$", executed.stdout)}
    assert set(timings) == {0, 100, 400, 1000} and timings[0] == timings[400]
    checked = {str(k): timing_bounds(timings[k], k) for k in LIMITS}
    for k, values in checked.items():
        assert not values["failures"], f"{k}kHz: {values['failures']}"
    old = timing_bounds(0x00722425, 1000)
    assert any("maximum_frequency" in f for f in old["failures"])
    assert any("data_setup" in f for f in old["failures"])
    assert any(":low" in f for f in old["failures"])
    assert any(":high" in f for f in old["failures"])
    # Clock/filter 설정의 소스 연결도 확인한다. HSE override는 실행 clock guard가 거부한다.
    bsp = (root / "src/bsp/bsp.c").read_text(encoding="utf-8")
    hal_conf = (root / "src/bsp/device/stm32h7rsxx_hal_conf.h").read_text(encoding="utf-8")
    hse = int(re.search(r"#define HSE_VALUE\s+(\d+)UL", hal_conf).group(1))
    values = [int(re.search(rf"PLL1\.PLL{name}\s*=\s*(\d+);", bsp).group(1)) for name in ("M", "N", "P")]
    assert hse * values[1] // values[0] // values[2] // 1 // 2 // 2 == KERNEL_HZ
    for literal in ("SYSCLKDivider = RCC_SYSCLK_DIV1", "AHBCLKDivider = RCC_HCLK_DIV2",
                    "APB1CLKDivider = RCC_APB1_DIV2", "PWR_REGULATOR_VOLTAGE_SCALE0"):
        assert literal in bsp, literal
    assert "I2c23ClockSelection = RCC_I2C23CLKSOURCE_PCLK1" in source
    result = dict(status="PASS_SOFTWARE_ONLY", source_sha256=hashlib.sha256(source_path.read_bytes()).hexdigest(),
                  production_functions_sha256=hashlib.sha256(production.encode()).hexdigest(), command=command,
                  kernel_nominal_hz=KERNEL_HZ, design_clock_margin_percent=1,
                  analog_filter_ns=[AF_MIN_NS, AF_MAX_NS], digital_filter=DNF,
                  timings=checked, rejected_original_1mhz=old,
                  unverified=["actual SCL/SDA waveform", "board pull-up/capacitance and VCC", "installed EEPROM suffix"])
    (build / "timing-results.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(executed.stdout, end="")
    print("PASS: exact rational timing inequalities at 148.5/150/151.5MHz, max/min edges, 50..165ns filter; old 1MHz rejected")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build-firmware-regression-tests/i2c-timing")
    parser.add_argument("--cc")
    args = parser.parse_args()
    installed = Path(r"D:\baram-fw-tools_exe\arm_toolchain\mingw_gcc\bin\gcc.exe")
    cc = args.cc or os.environ.get("HOST_CC") or (str(installed) if installed.is_file() else shutil.which("gcc") or "gcc")
    verify(ROOT, args.build_dir, cc)


if __name__ == "__main__":
    main()
