#include "usb_diagnostics.h"

#include <limits.h>
#include <string.h>

#ifndef USB_DIAGNOSTICS_HOST_TEST
#include "bsp.h"
#endif


// V260823R2: 실제 HID 전달과 펌웨어 루프 간격만 세션 중 계측한다. SOF 점수는 사용하지 않는다.
typedef struct
{
  usb_diagnostics_state_t state;
  uint16_t                 session_id;
  uint8_t                  duration_seconds;
  uint8_t                  polling_mode;
  uint8_t                  speed;
  uint32_t                 start_us;
  uint32_t                 deadline_us;
  uint32_t                 end_us;
  uint32_t                 expected_interval_us;

  bool     loop_timestamp_valid;
  uint32_t last_loop_us;
  uint32_t loop_samples;
  uint32_t loop_gap_max_us;
  uint32_t interval_loop_gap_max_us;
  uint32_t loop_stall_count;

  bool     report_in_flight;
  uint16_t report_session_id;
  uint32_t report_request_us;
  uint32_t report_samples;
  uint64_t latency_sum_us;
  uint32_t latency_min_us;
  uint32_t latency_max_us;
  uint32_t interval_latency_max_us;
  uint16_t queue_depth_peak;
  uint32_t histogram[USB_DIAGNOSTICS_HISTOGRAM_BUCKETS];
  uint32_t histogram_threshold_us[USB_DIAGNOSTICS_HISTOGRAM_BUCKETS - 1U];

  usb_diagnostics_hard_counters_t hard_counters;
  usb_diagnostics_event_t         timeline[USB_DIAGNOSTICS_TIMELINE_CAPACITY];
  uint32_t                        timeline_overwrites;
  uint8_t                         timeline_head;
  uint8_t                         timeline_count;
} usb_diagnostics_session_internal_t;

static volatile usb_diagnostics_session_internal_t s_session;
static volatile usb_diagnostics_hard_counters_t     s_boot_counters;
static volatile uint8_t                             s_current_speed;
static uint16_t                                     s_next_session_id;


static uint32_t usbDiagnosticsEnterCritical(void)
{
#ifdef USB_DIAGNOSTICS_HOST_TEST
  return 0U;
#else
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
#endif
}

static void usbDiagnosticsExitCritical(uint32_t primask)
{
#ifdef USB_DIAGNOSTICS_HOST_TEST
  (void)primask;
#else
  if (primask == 0U)
  {
    __enable_irq();
  }
#endif
}

static uint32_t usbDiagnosticsSaturatingIncrement(uint32_t value)
{
  return value == UINT32_MAX ? UINT32_MAX : value + 1U;
}

static void usbDiagnosticsRecordEvent(uint8_t type, uint32_t now_us, uint32_t value)
{
  uint8_t index = s_session.timeline_head;

  if (s_session.state != USB_DIAGNOSTICS_STATE_RUNNING)
  {
    return;
  }

  s_session.timeline[index].type        = type;
  s_session.timeline[index].relative_ms = (now_us - s_session.start_us) / 1000U;
  s_session.timeline[index].value       = value;
  s_session.timeline_head               = (uint8_t)((index + 1U) % USB_DIAGNOSTICS_TIMELINE_CAPACITY);
  if (s_session.timeline_count < USB_DIAGNOSTICS_TIMELINE_CAPACITY)
  {
    s_session.timeline_count++;
  }
  else
  {
    s_session.timeline_overwrites = usbDiagnosticsSaturatingIncrement(s_session.timeline_overwrites);
  }
}

static void usbDiagnosticsObserveSpeed(uint32_t now_us, uint8_t speed)
{
  if (speed == USB_DIAGNOSTICS_SPEED_UNKNOWN)
  {
    return;
  }

  if (s_current_speed != USB_DIAGNOSTICS_SPEED_UNKNOWN && s_current_speed != speed)
  {
    s_boot_counters.speed_changes = usbDiagnosticsSaturatingIncrement(s_boot_counters.speed_changes);
    if (s_session.state == USB_DIAGNOSTICS_STATE_RUNNING)
    {
      s_session.hard_counters.speed_changes = usbDiagnosticsSaturatingIncrement(s_session.hard_counters.speed_changes);
      usbDiagnosticsRecordEvent(USB_DIAGNOSTICS_EVENT_SPEED_CHANGE, now_us, speed);
    }
  }

  s_current_speed = speed;
  if (s_session.state == USB_DIAGNOSTICS_STATE_RUNNING)
  {
    s_session.speed = speed;
  }
}

static void usbDiagnosticsStopLocked(usb_diagnostics_state_t state, uint32_t now_us)
{
  s_session.state                 = state;
  s_session.end_us                = now_us;
  s_session.report_in_flight      = false;
  s_session.loop_timestamp_valid = false;
}

void usbDiagnosticsInit(void)
{
  uint32_t primask = usbDiagnosticsEnterCritical();

  memset((void *)&s_session, 0, sizeof(s_session));
  memset((void *)&s_boot_counters, 0, sizeof(s_boot_counters));
  s_current_speed  = USB_DIAGNOSTICS_SPEED_UNKNOWN;
  s_next_session_id = 0U;

  usbDiagnosticsExitCritical(primask);
}

bool usbDiagnosticsIsActive(void)
{
  return s_session.state == USB_DIAGNOSTICS_STATE_RUNNING;
}

usb_diagnostics_state_t usbDiagnosticsGetState(void)
{
  return s_session.state;
}

uint16_t usbDiagnosticsGetSessionId(void)
{
  return s_session.session_id;
}

uint8_t usbDiagnosticsGetCurrentSpeed(void)
{
  return s_current_speed;
}

bool usbDiagnosticsStart(uint8_t  duration_seconds,
                         uint8_t  polling_mode,
                         uint32_t expected_interval_us,
                         uint32_t now_us)
{
  usb_diagnostics_session_internal_t next = {0};

  if ((duration_seconds != 10U && duration_seconds != 30U && duration_seconds != 60U) ||
      expected_interval_us == 0U)
  {
    return false;
  }

  uint32_t primask = usbDiagnosticsEnterCritical();
  if (s_session.state == USB_DIAGNOSTICS_STATE_RUNNING)
  {
    usbDiagnosticsExitCritical(primask);
    return false;
  }

  s_next_session_id++;
  if (s_next_session_id == 0U)
  {
    s_next_session_id = 1U;
  }

  next.state                  = USB_DIAGNOSTICS_STATE_RUNNING;
  next.session_id             = s_next_session_id;
  next.duration_seconds       = duration_seconds;
  next.polling_mode           = polling_mode;
  next.speed                  = s_current_speed;
  next.start_us               = now_us;
  next.deadline_us            = now_us + ((uint32_t)duration_seconds * 1000000U);
  next.expected_interval_us   = expected_interval_us;
  next.latency_min_us         = UINT32_MAX;
  next.histogram_threshold_us[0] = expected_interval_us / 2U;
  next.histogram_threshold_us[1] = (expected_interval_us * 3U) / 4U;
  next.histogram_threshold_us[2] = expected_interval_us;
  next.histogram_threshold_us[3] = (expected_interval_us * 5U) / 4U;
  next.histogram_threshold_us[4] = (expected_interval_us * 3U) / 2U;
  next.histogram_threshold_us[5] = expected_interval_us * 2U;
  next.histogram_threshold_us[6] = expected_interval_us * 4U;
  memcpy((void *)&s_session, &next, sizeof(next));

  usbDiagnosticsExitCritical(primask);
  return true;
}

bool usbDiagnosticsStop(uint32_t now_us)
{
  uint32_t primask = usbDiagnosticsEnterCritical();
  if (s_session.state != USB_DIAGNOSTICS_STATE_RUNNING)
  {
    usbDiagnosticsExitCritical(primask);
    return false;
  }

  usbDiagnosticsStopLocked(USB_DIAGNOSTICS_STATE_STOPPED, now_us);
  usbDiagnosticsExitCritical(primask);
  return true;
}

bool usbDiagnosticsClear(void)
{
  uint32_t primask = usbDiagnosticsEnterCritical();
  if (s_session.state == USB_DIAGNOSTICS_STATE_RUNNING)
  {
    usbDiagnosticsExitCritical(primask);
    return false;
  }

  memset((void *)&s_session, 0, sizeof(s_session));
  usbDiagnosticsExitCritical(primask);
  return true;
}

void usbDiagnosticsTask(uint32_t now_us)
{
  if (s_session.state != USB_DIAGNOSTICS_STATE_RUNNING)
  {
    return;
  }

  if ((int32_t)(now_us - s_session.deadline_us) >= 0)
  {
    uint32_t primask = usbDiagnosticsEnterCritical();
    if (s_session.state == USB_DIAGNOSTICS_STATE_RUNNING)
    {
      usbDiagnosticsStopLocked(USB_DIAGNOSTICS_STATE_COMPLETE, s_session.deadline_us);
    }
    usbDiagnosticsExitCritical(primask);
    return;
  }

  if (s_session.loop_timestamp_valid)
  {
    uint32_t gap_us = now_us - s_session.last_loop_us;

    s_session.loop_samples = usbDiagnosticsSaturatingIncrement(s_session.loop_samples);
    if (gap_us > s_session.loop_gap_max_us)
    {
      s_session.loop_gap_max_us = gap_us;
    }
    if (gap_us > s_session.interval_loop_gap_max_us)
    {
      s_session.interval_loop_gap_max_us = gap_us;
    }
    if (gap_us > USB_DIAGNOSTICS_LOOP_STALL_US)
    {
      s_session.loop_stall_count = usbDiagnosticsSaturatingIncrement(s_session.loop_stall_count);
      uint32_t primask = usbDiagnosticsEnterCritical();
      usbDiagnosticsRecordEvent(USB_DIAGNOSTICS_EVENT_LOOP_STALL, now_us, gap_us);
      usbDiagnosticsExitCritical(primask);
    }
  }

  s_session.last_loop_us           = now_us;
  s_session.loop_timestamp_valid  = true;
}

void usbDiagnosticsOnReportQueueDepth(uint16_t queued_reports)
{
  uint32_t primask = usbDiagnosticsEnterCritical();
  if (s_session.state == USB_DIAGNOSTICS_STATE_RUNNING)
  {
    if (queued_reports > s_session.queue_depth_peak)
    {
      s_session.queue_depth_peak = queued_reports;
    }
  }
  usbDiagnosticsExitCritical(primask);
}

void usbDiagnosticsOnReportTransferStarted(uint32_t request_us,
                                           uint16_t request_session_id,
                                           uint16_t queued_reports)
{
  uint32_t primask = usbDiagnosticsEnterCritical();
  if (s_session.state == USB_DIAGNOSTICS_STATE_RUNNING &&
      request_session_id != 0U &&
      request_session_id == s_session.session_id)
  {
    s_session.report_in_flight = true;
    s_session.report_session_id = request_session_id;
    s_session.report_request_us = request_us;
    if (queued_reports > s_session.queue_depth_peak)
    {
      s_session.queue_depth_peak = queued_reports;
    }
  }
  usbDiagnosticsExitCritical(primask);
}

void usbDiagnosticsOnReportTransferCompleted(uint32_t now_us)
{
  uint32_t primask = usbDiagnosticsEnterCritical();
  if (s_session.state == USB_DIAGNOSTICS_STATE_RUNNING &&
      s_session.report_in_flight &&
      s_session.report_session_id == s_session.session_id)
  {
    uint32_t latency_us = now_us - s_session.report_request_us;
    uint8_t  bucket     = USB_DIAGNOSTICS_HISTOGRAM_BUCKETS - 1U;

    s_session.report_samples = usbDiagnosticsSaturatingIncrement(s_session.report_samples);
    if (UINT64_MAX - s_session.latency_sum_us < latency_us)
    {
      s_session.latency_sum_us = UINT64_MAX;
    }
    else
    {
      s_session.latency_sum_us += latency_us;
    }
    if (latency_us < s_session.latency_min_us)
    {
      s_session.latency_min_us = latency_us;
    }
    if (latency_us > s_session.latency_max_us)
    {
      s_session.latency_max_us = latency_us;
    }
    if (latency_us > s_session.interval_latency_max_us)
    {
      s_session.interval_latency_max_us = latency_us;
    }

    for (uint8_t i = 0U; i < USB_DIAGNOSTICS_HISTOGRAM_BUCKETS - 1U; i++)
    {
      if (latency_us <= s_session.histogram_threshold_us[i])
      {
        bucket = i;
        break;
      }
    }
    s_session.histogram[bucket] = usbDiagnosticsSaturatingIncrement(s_session.histogram[bucket]);
  }
  s_session.report_in_flight = false;
  usbDiagnosticsExitCritical(primask);
}

void usbDiagnosticsOnReportQueueDrop(uint32_t now_us)
{
  uint32_t primask = usbDiagnosticsEnterCritical();

  s_boot_counters.report_drops = usbDiagnosticsSaturatingIncrement(s_boot_counters.report_drops);
  if (s_session.state == USB_DIAGNOSTICS_STATE_RUNNING)
  {
    s_session.hard_counters.report_drops = usbDiagnosticsSaturatingIncrement(s_session.hard_counters.report_drops);
    usbDiagnosticsRecordEvent(USB_DIAGNOSTICS_EVENT_REPORT_DROP, now_us, 1U);
  }

  usbDiagnosticsExitCritical(primask);
}

void usbDiagnosticsOnUsbReset(uint32_t now_us, uint8_t speed)
{
  uint32_t primask = usbDiagnosticsEnterCritical();

  s_boot_counters.usb_resets = usbDiagnosticsSaturatingIncrement(s_boot_counters.usb_resets);
  if (s_session.state == USB_DIAGNOSTICS_STATE_RUNNING)
  {
    s_session.hard_counters.usb_resets = usbDiagnosticsSaturatingIncrement(s_session.hard_counters.usb_resets);
    usbDiagnosticsRecordEvent(USB_DIAGNOSTICS_EVENT_USB_RESET, now_us, speed);
  }
  usbDiagnosticsObserveSpeed(now_us, speed);

  usbDiagnosticsExitCritical(primask);
}

void usbDiagnosticsOnUsbConfigured(uint32_t now_us, uint8_t speed)
{
  uint32_t primask = usbDiagnosticsEnterCritical();

  s_boot_counters.configurations = usbDiagnosticsSaturatingIncrement(s_boot_counters.configurations);
  if (s_session.state == USB_DIAGNOSTICS_STATE_RUNNING)
  {
    s_session.hard_counters.configurations = usbDiagnosticsSaturatingIncrement(s_session.hard_counters.configurations);
    usbDiagnosticsRecordEvent(USB_DIAGNOSTICS_EVENT_CONFIGURED, now_us, speed);
  }
  usbDiagnosticsObserveSpeed(now_us, speed);

  usbDiagnosticsExitCritical(primask);
}

void usbDiagnosticsOnUsbSuspend(uint32_t now_us)
{
  uint32_t primask = usbDiagnosticsEnterCritical();

  s_boot_counters.suspends = usbDiagnosticsSaturatingIncrement(s_boot_counters.suspends);
  if (s_session.state == USB_DIAGNOSTICS_STATE_RUNNING)
  {
    s_session.hard_counters.suspends = usbDiagnosticsSaturatingIncrement(s_session.hard_counters.suspends);
    usbDiagnosticsRecordEvent(USB_DIAGNOSTICS_EVENT_SUSPEND, now_us, 0U);
  }

  usbDiagnosticsExitCritical(primask);
}

void usbDiagnosticsCapture(usb_diagnostics_snapshot_t *snapshot, uint32_t now_us)
{
  usb_diagnostics_session_internal_t session;
  usb_diagnostics_hard_counters_t     boot_counters;
  uint8_t                            current_speed;
  uint32_t                           primask;

  if (snapshot == NULL)
  {
    return;
  }

  primask = usbDiagnosticsEnterCritical();
  memcpy(&session, (const void *)&s_session, sizeof(session));
  memcpy(&boot_counters, (const void *)&s_boot_counters, sizeof(boot_counters));
  current_speed = s_current_speed;
  s_session.interval_latency_max_us = 0U;
  s_session.interval_loop_gap_max_us = 0U;
  usbDiagnosticsExitCritical(primask);

  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->state                    = session.state;
  snapshot->session_id               = session.session_id;
  snapshot->duration_seconds         = session.duration_seconds;
  snapshot->polling_mode             = session.polling_mode;
  snapshot->speed                    = session.state == USB_DIAGNOSTICS_STATE_IDLE ? current_speed : session.speed;
  snapshot->expected_interval_us     = session.expected_interval_us;
  snapshot->report_samples           = session.report_samples;
  snapshot->latency_min_us           = session.report_samples == 0U ? 0U : session.latency_min_us;
  snapshot->latency_max_us           = session.latency_max_us;
  snapshot->interval_latency_max_us  = session.interval_latency_max_us;
  snapshot->queue_depth_peak         = session.queue_depth_peak;
  snapshot->loop_samples             = session.loop_samples;
  snapshot->loop_gap_max_us          = session.loop_gap_max_us;
  snapshot->interval_loop_gap_max_us = session.interval_loop_gap_max_us;
  snapshot->loop_stall_count         = session.loop_stall_count;
  snapshot->boot_counters            = boot_counters;
  snapshot->session_counters         = session.hard_counters;
  snapshot->timeline_overwrites      = session.timeline_overwrites;
  snapshot->event_count              = session.timeline_count;
  memcpy(snapshot->histogram, session.histogram, sizeof(snapshot->histogram));

  if (session.report_samples > 0U)
  {
    uint64_t average = session.latency_sum_us / session.report_samples;
    snapshot->latency_average_us = average > UINT32_MAX ? UINT32_MAX : (uint32_t)average;
  }

  if (session.state != USB_DIAGNOSTICS_STATE_IDLE)
  {
    uint32_t end_us = session.state == USB_DIAGNOSTICS_STATE_RUNNING ? now_us : session.end_us;
    uint32_t elapsed_us = end_us - session.start_us;
    uint32_t duration_us = (uint32_t)session.duration_seconds * 1000000U;
    if (elapsed_us > duration_us)
    {
      elapsed_us = duration_us;
    }
    snapshot->elapsed_ms = elapsed_us / 1000U;
  }

  uint8_t first = (uint8_t)((session.timeline_head + USB_DIAGNOSTICS_TIMELINE_CAPACITY - session.timeline_count) %
                            USB_DIAGNOSTICS_TIMELINE_CAPACITY);
  for (uint8_t i = 0U; i < session.timeline_count; i++)
  {
    snapshot->events[i] = session.timeline[(first + i) % USB_DIAGNOSTICS_TIMELINE_CAPACITY];
  }
}

#ifdef USB_DIAGNOSTICS_HOST_TEST
void usbDiagnosticsTestSetBootCounters(const usb_diagnostics_hard_counters_t *counters)
{
  if (counters != NULL)
  {
    s_boot_counters = *counters;
  }
}
#endif
