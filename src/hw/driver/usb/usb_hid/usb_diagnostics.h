#pragma once

#include <stdbool.h>
#include <stdint.h>


// V260823R2: 진단 세션은 고정 RAM만 사용하며 EEPROM과 자동 복구 경로를 갖지 않는다.
#define USB_DIAGNOSTICS_HISTOGRAM_BUCKETS  8U
#define USB_DIAGNOSTICS_TIMELINE_CAPACITY  8U
#define USB_DIAGNOSTICS_LOOP_STALL_US      1000U

typedef enum
{
  USB_DIAGNOSTICS_STATE_IDLE = 0,
  USB_DIAGNOSTICS_STATE_RUNNING,
  USB_DIAGNOSTICS_STATE_COMPLETE,
  USB_DIAGNOSTICS_STATE_STOPPED,
} usb_diagnostics_state_t;

typedef enum
{
  USB_DIAGNOSTICS_SPEED_UNKNOWN = 0,
  USB_DIAGNOSTICS_SPEED_FULL,
  USB_DIAGNOSTICS_SPEED_HIGH,
} usb_diagnostics_speed_t;

typedef enum
{
  USB_DIAGNOSTICS_EVENT_REPORT_DROP = 1,
  USB_DIAGNOSTICS_EVENT_USB_RESET,
  USB_DIAGNOSTICS_EVENT_CONFIGURED,
  USB_DIAGNOSTICS_EVENT_SUSPEND,
  USB_DIAGNOSTICS_EVENT_SPEED_CHANGE,
  USB_DIAGNOSTICS_EVENT_LOOP_STALL,
} usb_diagnostics_event_type_t;

typedef struct
{
  uint32_t report_drops;
  uint32_t usb_resets;
  uint32_t configurations;
  uint32_t suspends;
  uint32_t speed_changes;
} usb_diagnostics_hard_counters_t;

typedef struct
{
  uint32_t relative_ms;
  uint32_t value;
  uint8_t  type;
} usb_diagnostics_event_t;

typedef struct
{
  usb_diagnostics_state_t state;
  uint16_t                 session_id;
  uint8_t                  duration_seconds;
  uint8_t                  polling_mode;
  uint8_t                  speed;
  uint32_t                 elapsed_ms;
  uint32_t                 expected_interval_us;

  uint32_t report_samples;
  uint32_t latency_min_us;
  uint32_t latency_average_us;
  uint32_t latency_max_us;
  uint32_t interval_latency_max_us;
  uint16_t queue_depth_peak;
  uint32_t histogram[USB_DIAGNOSTICS_HISTOGRAM_BUCKETS];

  uint32_t loop_samples;
  uint32_t loop_gap_max_us;
  uint32_t interval_loop_gap_max_us;
  uint32_t loop_stall_count;

  usb_diagnostics_hard_counters_t boot_counters;
  usb_diagnostics_hard_counters_t session_counters;
  uint32_t                        timeline_overwrites;
  uint8_t                         event_count;
  usb_diagnostics_event_t         events[USB_DIAGNOSTICS_TIMELINE_CAPACITY];
} usb_diagnostics_snapshot_t;


void usbDiagnosticsInit(void);
bool usbDiagnosticsIsActive(void);
usb_diagnostics_state_t usbDiagnosticsGetState(void);
uint16_t usbDiagnosticsGetSessionId(void);

bool usbDiagnosticsStart(uint8_t  duration_seconds,
                         uint8_t  polling_mode,
                         uint32_t expected_interval_us,
                         uint32_t now_us);
bool usbDiagnosticsStop(uint32_t now_us);
bool usbDiagnosticsClear(void);
void usbDiagnosticsTask(uint32_t now_us);
void usbDiagnosticsCapture(usb_diagnostics_snapshot_t *snapshot, uint32_t now_us);

void usbDiagnosticsOnReportQueueDepth(uint16_t queued_reports);
void usbDiagnosticsOnReportTransferStarted(uint32_t request_us,
                                           uint16_t request_session_id,
                                           uint16_t queued_reports);
void usbDiagnosticsOnReportTransferCompleted(uint32_t now_us);
void usbDiagnosticsOnReportQueueDrop(uint32_t now_us);
void usbDiagnosticsOnUsbReset(uint32_t now_us, uint8_t speed);
void usbDiagnosticsOnUsbConfigured(uint32_t now_us, uint8_t speed);
void usbDiagnosticsOnUsbSuspend(uint32_t now_us);

#ifdef USB_DIAGNOSTICS_HOST_TEST
void usbDiagnosticsTestSetBootCounters(const usb_diagnostics_hard_counters_t *counters);
#endif
