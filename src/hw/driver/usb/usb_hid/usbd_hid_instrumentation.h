#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "def.h"  // V251010R1: 인라인 스텁에서 _USE_USB_MONITOR 플래그를 참조
#include "cli.h"
#include "usbd_hid.h"
#include "usbd_hid_internal.h"

#if _DEF_ENABLE_USB_HID_TIMING_PROBE

uint32_t usbHidInstrumentationNow(void);
void     usbHidInstrumentationOnSof(uint32_t now_us);
void     usbHidInstrumentationOnTimerPulse(uint32_t delay_us, uint16_t compare_ticks);
void     usbHidInstrumentationOnTimerResidual(uint32_t residual_us, uint32_t delay_us);
void     usbHidInstrumentationOnDataIn(void);
void     usbHidInstrumentationOnReportDequeued(uint32_t queued_reports);
void     usbHidInstrumentationOnImmediateSendSuccess(uint32_t queued_reports);
void     usbHidInstrumentationMarkReportStart(void);
void     usbHidMeasureRateTime(void);

#else

#include "micros.h"  // V251010R1: 계측 비활성 시 릴리스 경로에서 호출 오버헤드 제거용 인라인 스텁 제공

static inline uint32_t usbHidInstrumentationNow(void)
{
#if _USE_USB_MONITOR || _DEF_ENABLE_USB_HID_TIMING_PROBE
  return micros();  // V251010R1: 모니터 활성 시 타임스탬프 유지
#else
  return 0U;
#endif
}

static inline void usbHidInstrumentationOnSof(uint32_t now_us)
{
  (void)now_us;  // V251010R1: 릴리스 빌드에서 호출 제거
}

static inline void usbHidInstrumentationOnTimerPulse(uint32_t delay_us, uint16_t compare_ticks)
{
  (void)delay_us;    // V251010R9: 릴리스 빌드에서 타이머 보정 정보 전달을 무효화
  (void)compare_ticks;
}

static inline void usbHidInstrumentationOnTimerResidual(uint32_t residual_us, uint32_t delay_us)
{
  (void)residual_us; // V251011R1: 릴리스 빌드에서는 잔차 계측 미사용
  (void)delay_us;
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
bool     usbHidTimerSyncGetState(usb_hid_timer_sync_state_t *p_state);  // V251010R9: 타이머 보정 상태를 CLI에서 조회
