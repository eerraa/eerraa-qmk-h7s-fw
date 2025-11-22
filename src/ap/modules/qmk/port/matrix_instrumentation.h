#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "micros.h"
#include "usb.h"

#if _DEF_ENABLE_MATRIX_TIMING_PROBE
extern uint32_t g_matrixInstrumentationScanTime;  // V251009R9: 매트릭스 계측 결과 저장소
#endif

static inline uint32_t matrixInstrumentationCaptureStart(void)
{
#if _DEF_ENABLE_MATRIX_TIMING_PROBE || _DEF_ENABLE_USB_HID_TIMING_PROBE
  return micros();  // V251009R9: 활성 계측이 존재할 때만 타이머 접근
#else
  return 0U;
#endif
}

static inline void matrixInstrumentationLogScan(uint32_t pre_time_us, bool info_enabled)
{
#if _DEF_ENABLE_MATRIX_TIMING_PROBE
  if (info_enabled)
  {
    g_matrixInstrumentationScanTime = micros() - pre_time_us;
  }
#else
  (void)pre_time_us;
  (void)info_enabled;
#endif
}

static inline void matrixInstrumentationPropagate(bool changed, uint32_t pre_time_us)
{
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
  if (changed)
  {
    usbHidSetTimeLog(0, pre_time_us);  // V251009R9: HID 계측 활성 시에만 타임스탬프 전달
  }
#else
  (void)changed;
  (void)pre_time_us;
#endif
}

static inline void matrixInstrumentationReset(void)
{
#if _DEF_ENABLE_MATRIX_TIMING_PROBE
  g_matrixInstrumentationScanTime = 0U;
#endif
}

static inline uint32_t matrixInstrumentationGetScanTime(void)
{
#if _DEF_ENABLE_MATRIX_TIMING_PROBE
  return g_matrixInstrumentationScanTime;
#else
  return 0U;
#endif
}

static inline bool matrixInstrumentationIsCompileEnabled(void)
{
#if _DEF_ENABLE_MATRIX_TIMING_PROBE
  return true;
#else
  return false;
#endif
}
