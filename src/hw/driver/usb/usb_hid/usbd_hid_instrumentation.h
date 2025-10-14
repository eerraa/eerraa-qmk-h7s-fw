#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cli.h"
#include "usbd_hid.h"
#include "usbd_hid_internal.h"

uint32_t usbHidInstrumentationNow(void);
void     usbHidInstrumentationOnSof(uint32_t now_us);
void     usbHidInstrumentationOnTimerPulse(void);
void     usbHidInstrumentationOnDataIn(void);
void     usbHidInstrumentationOnReportDequeued(uint32_t queued_reports);
void     usbHidInstrumentationOnImmediateSendSuccess(uint32_t queued_reports);
void     usbHidInstrumentationMarkReportStart(void);
void     usbHidInstrumentationHandleCli(cli_args_t *args);
void     usbHidMeasureRateTime(void);
