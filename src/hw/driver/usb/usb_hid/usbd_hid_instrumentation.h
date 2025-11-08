#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "def.h"  // V251108R1: USB_MONITOR_ENABLE/계측 매크로 확인
#include "cli.h"
#include "usbd_hid.h"
#include "usbd_hid_internal.h"

#if _DEF_ENABLE_USB_HID_TIMING_PROBE

uint32_t usbHidInstrumentationNow(void);
void     usbHidInstrumentationOnSof(uint32_t now_us);
void     usbHidInstrumentationOnTimerPulse(void);
void     usbHidInstrumentationOnDataIn(void);
void     usbHidInstrumentationOnReportDequeued(uint32_t queued_reports);
void     usbHidInstrumentationOnImmediateSendSuccess(uint32_t queued_reports);
void     usbHidInstrumentationMarkReportStart(void);
void     usbHidMeasureRateTime(void);

#else

#include "micros.h"  // V251010R1: 계측 비활성 시 릴리스 경로에서 호출 오버헤드 제거용 인라인 스텁 제공

static inline uint32_t usbHidInstrumentationNow(void)
{
#if defined(USB_MONITOR_ENABLE) || _DEF_ENABLE_USB_HID_TIMING_PROBE
  return micros();  // V251010R1: 모니터 활성 시 타임스탬프 유지
#else
  return 0U;
#endif
}

static inline void usbHidInstrumentationOnSof(uint32_t now_us)
{
  (void)now_us;  // V251010R1: 릴리스 빌드에서 호출 제거
}

static inline void usbHidInstrumentationOnTimerPulse(void)
{
  // V251010R1: 릴리스 빌드에서 계측 타이머 콜백 제거
}

static inline void usbHidInstrumentationOnDataIn(void)
{
  // V251010R1: 릴리스 빌드에서 데이터 계측 무효화
}

static inline void usbHidInstrumentationOnReportDequeued(uint32_t queued_reports)
{
  (void)queued_reports;  // V251010R1: 릴리스 빌드에서 큐 스냅샷 무효화
}

static inline void usbHidInstrumentationOnImmediateSendSuccess(uint32_t queued_reports)
{
  (void)queued_reports;  // V251010R1: 릴리스 빌드에서 즉시 전송 계측 무효화
}

static inline void usbHidInstrumentationMarkReportStart(void)
{
  // V251010R1: 릴리스 빌드에서 시작 타임스탬프 제거
}

static inline void usbHidMeasureRateTime(void)
{
  // V251010R1: 릴리스 빌드에서 폴링 간격 측정 비활성화
}

#endif

void     usbHidInstrumentationHandleCli(cli_args_t *args);
