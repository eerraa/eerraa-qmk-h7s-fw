#include "era_usb_diagnostics.h"

#include <stddef.h>
#include <string.h>

#include "hw_def.h"
#include "micros.h"
#include "usb.h"
#include "usb_diagnostics.h"


#define ERA_USB_DIAGNOSTICS_CMD_GET_KEYBOARD_VALUE  0x02U
#define ERA_USB_DIAGNOSTICS_CMD_SET_KEYBOARD_VALUE  0x03U
#define ERA_USB_DIAGNOSTICS_RECOMMENDED_SNAPSHOT_MS 1000U
#define ERA_USB_DIAGNOSTICS_TIMELINE_EVENTS_PER_CHUNK 2U

static usb_diagnostics_snapshot_t s_frozen_snapshot;
static uint16_t                   s_snapshot_sequence;
static bool                       s_snapshot_valid;


static void eraUsbDiagnosticsPutBe16(uint8_t *out, uint16_t value)
{
  out[0] = (uint8_t)(value >> 8);
  out[1] = (uint8_t)value;
}

static void eraUsbDiagnosticsPutBe32(uint8_t *out, uint32_t value)
{
  out[0] = (uint8_t)(value >> 24);
  out[1] = (uint8_t)(value >> 16);
  out[2] = (uint8_t)(value >> 8);
  out[3] = (uint8_t)value;
}

static uint16_t eraUsbDiagnosticsGetBe16(const uint8_t *in)
{
  return (uint16_t)(((uint16_t)in[0] << 8) | in[1]);
}

static bool eraUsbDiagnosticsBytesAreZero(const uint8_t *data, uint8_t first)
{
  for (uint8_t i = first; i < ERA_USB_DIAGNOSTICS_PACKET_SIZE; i++)
  {
    if (data[i] != 0U)
    {
      return false;
    }
  }
  return true;
}

static uint32_t eraUsbDiagnosticsExpectedIntervalUs(UsbBootMode_t mode)
{
  switch (mode)
  {
    case USB_BOOT_MODE_FS_1K:
      return 1000U;
    case USB_BOOT_MODE_HS_2K:
      return 500U;
    case USB_BOOT_MODE_HS_4K:
      return 250U;
    case USB_BOOT_MODE_HS_8K:
      return 125U;
    default:
      return 0U;
  }
}

static uint8_t eraUsbDiagnosticsChunkCount(const usb_diagnostics_snapshot_t *snapshot)
{
  return (uint8_t)(ERA_USB_DIAGNOSTICS_BASE_CHUNKS +
                   ((snapshot->event_count + ERA_USB_DIAGNOSTICS_TIMELINE_EVENTS_PER_CHUNK - 1U) /
                    ERA_USB_DIAGNOSTICS_TIMELINE_EVENTS_PER_CHUNK));
}

static void eraUsbDiagnosticsPrepareResponse(uint8_t *data,
                                             uint8_t  command,
                                             uint8_t  operation,
                                             uint8_t  tag_hi,
                                             uint8_t  tag_lo,
                                             uint8_t  status)
{
  memset(&data[2], 0, ERA_USB_DIAGNOSTICS_PACKET_SIZE - 2U);
  data[0] = command;
  data[1] = ERA_USB_DIAGNOSTICS_KEYBOARD_VALUE;
  data[2] = ERA_USB_DIAGNOSTICS_PROTOCOL_VERSION;
  data[3] = operation;
  data[4] = tag_hi;
  data[5] = tag_lo;
  data[6] = status;
  data[7] = (uint8_t)usbDiagnosticsGetState();
  eraUsbDiagnosticsPutBe16(&data[8], usbDiagnosticsGetSessionId());
}

static void eraUsbDiagnosticsEncodeCapabilities(uint8_t *payload)
{
  const char *version = _DEF_FIRMWARE_VERSION;
  size_t      length  = strlen(version);

  if (length > 9U)
  {
    length = 9U;
  }

  payload[0] = ERA_USB_DIAGNOSTICS_CAP_REPORT_TIMING |
               ERA_USB_DIAGNOSTICS_CAP_HISTOGRAM |
               ERA_USB_DIAGNOSTICS_CAP_FIRMWARE_TIMING |
               ERA_USB_DIAGNOSTICS_CAP_EVENT_TIMELINE |
               ERA_USB_DIAGNOSTICS_CAP_BOOT_COUNTERS;
  payload[1] = 0x07U;  // V260823R2: 10/30/60초 지원 비트.
  payload[2] = USB_DIAGNOSTICS_HISTOGRAM_BUCKETS;
  payload[3] = USB_DIAGNOSTICS_TIMELINE_CAPACITY;
  eraUsbDiagnosticsPutBe16(&payload[4], ERA_USB_DIAGNOSTICS_RECOMMENDED_SNAPSHOT_MS);
  payload[6] = 1U;  // big-endian
  payload[7] = 1U;  // 시간 단위: microsecond
  payload[8] = (uint8_t)length;
  memcpy(&payload[9], version, length);
}

static void eraUsbDiagnosticsEncodeSnapshotChunk(uint8_t *payload,
                                                 uint8_t chunk,
                                                 const usb_diagnostics_snapshot_t *snapshot)
{
  switch (chunk)
  {
    case 0U:
      payload[0] = snapshot->polling_mode;
      payload[1] = snapshot->speed;
      payload[2] = snapshot->duration_seconds;
      payload[3] = snapshot->event_count;
      eraUsbDiagnosticsPutBe32(&payload[4], snapshot->elapsed_ms);
      eraUsbDiagnosticsPutBe32(&payload[8], snapshot->expected_interval_us);
      eraUsbDiagnosticsPutBe32(&payload[12], snapshot->report_samples);
      payload[16] = USB_DIAGNOSTICS_HISTOGRAM_BUCKETS;
      payload[17] = USB_DIAGNOSTICS_TIMELINE_CAPACITY;
      break;

    case 1U:
      eraUsbDiagnosticsPutBe32(&payload[0], snapshot->latency_min_us);
      eraUsbDiagnosticsPutBe32(&payload[4], snapshot->latency_average_us);
      eraUsbDiagnosticsPutBe32(&payload[8], snapshot->latency_max_us);
      eraUsbDiagnosticsPutBe32(&payload[12], snapshot->interval_latency_max_us);
      eraUsbDiagnosticsPutBe16(&payload[16], snapshot->queue_depth_peak);
      break;

    case 2U:
    case 3U:
    {
      uint8_t first = (uint8_t)((chunk - 2U) * 4U);
      for (uint8_t i = 0U; i < 4U; i++)
      {
        eraUsbDiagnosticsPutBe32(&payload[i * 4U], snapshot->histogram[first + i]);
      }
      break;
    }

    case 4U:
      eraUsbDiagnosticsPutBe32(&payload[0], snapshot->loop_samples);
      eraUsbDiagnosticsPutBe32(&payload[4], snapshot->loop_gap_max_us);
      eraUsbDiagnosticsPutBe32(&payload[8], snapshot->interval_loop_gap_max_us);
      eraUsbDiagnosticsPutBe32(&payload[12], snapshot->loop_stall_count);
      eraUsbDiagnosticsPutBe16(&payload[16], USB_DIAGNOSTICS_LOOP_STALL_US);
      break;

    case 5U:
      eraUsbDiagnosticsPutBe32(&payload[0], snapshot->boot_counters.report_drops);
      eraUsbDiagnosticsPutBe32(&payload[4], snapshot->boot_counters.usb_resets);
      eraUsbDiagnosticsPutBe32(&payload[8], snapshot->boot_counters.configurations);
      eraUsbDiagnosticsPutBe32(&payload[12], snapshot->boot_counters.suspends);
      break;

    case 6U:
      eraUsbDiagnosticsPutBe32(&payload[0], snapshot->boot_counters.speed_changes);
      eraUsbDiagnosticsPutBe32(&payload[4], snapshot->session_counters.report_drops);
      eraUsbDiagnosticsPutBe32(&payload[8], snapshot->session_counters.usb_resets);
      eraUsbDiagnosticsPutBe32(&payload[12], snapshot->session_counters.configurations);
      break;

    case 7U:
      eraUsbDiagnosticsPutBe32(&payload[0], snapshot->session_counters.suspends);
      eraUsbDiagnosticsPutBe32(&payload[4], snapshot->session_counters.speed_changes);
      eraUsbDiagnosticsPutBe32(&payload[8], snapshot->timeline_overwrites);
      break;

    default:
    {
      uint8_t first = (uint8_t)((chunk - ERA_USB_DIAGNOSTICS_BASE_CHUNKS) *
                                ERA_USB_DIAGNOSTICS_TIMELINE_EVENTS_PER_CHUNK);
      for (uint8_t i = 0U; i < ERA_USB_DIAGNOSTICS_TIMELINE_EVENTS_PER_CHUNK; i++)
      {
        uint8_t event_index = (uint8_t)(first + i);
        uint8_t offset      = (uint8_t)(i * 9U);
        if (event_index < snapshot->event_count)
        {
          payload[offset] = snapshot->events[event_index].type;
          eraUsbDiagnosticsPutBe32(&payload[offset + 1U], snapshot->events[event_index].relative_ms);
          eraUsbDiagnosticsPutBe32(&payload[offset + 5U], snapshot->events[event_index].value);
        }
      }
      break;
    }
  }
}

bool era_usb_diagnostics_via_command(uint8_t *data, uint8_t length)
{
  uint8_t  command;
  uint8_t  version;
  uint8_t  operation;
  uint8_t  tag_hi;
  uint8_t  tag_lo;
  uint8_t  argument;
  uint16_t requested_sequence;
  bool     valid = false;

  if (data == NULL || length < ERA_USB_DIAGNOSTICS_PACKET_SIZE)
  {
    return false;
  }
  if ((data[0] != ERA_USB_DIAGNOSTICS_CMD_GET_KEYBOARD_VALUE &&
       data[0] != ERA_USB_DIAGNOSTICS_CMD_SET_KEYBOARD_VALUE) ||
      data[1] != ERA_USB_DIAGNOSTICS_KEYBOARD_VALUE)
  {
    return false;
  }

  command            = data[0];
  version            = data[2];
  operation          = data[3];
  tag_hi             = data[4];
  tag_lo             = data[5];
  argument           = data[6];
  requested_sequence = eraUsbDiagnosticsGetBe16(&data[7]);

  switch (operation)
  {
    case ERA_USB_DIAGNOSTICS_OP_CAPABILITIES:
      valid = command == ERA_USB_DIAGNOSTICS_CMD_GET_KEYBOARD_VALUE &&
              argument == 0U && requested_sequence == 0U && eraUsbDiagnosticsBytesAreZero(data, 9U);
      break;
    case ERA_USB_DIAGNOSTICS_OP_SNAPSHOT:
      valid = command == ERA_USB_DIAGNOSTICS_CMD_GET_KEYBOARD_VALUE &&
              eraUsbDiagnosticsBytesAreZero(data, 9U) &&
              (argument != 0U || requested_sequence == 0U);
      break;
    case ERA_USB_DIAGNOSTICS_OP_START:
      valid = command == ERA_USB_DIAGNOSTICS_CMD_SET_KEYBOARD_VALUE &&
              requested_sequence == 0U && eraUsbDiagnosticsBytesAreZero(data, 9U);
      break;
    case ERA_USB_DIAGNOSTICS_OP_STOP:
    case ERA_USB_DIAGNOSTICS_OP_CLEAR:
      valid = command == ERA_USB_DIAGNOSTICS_CMD_SET_KEYBOARD_VALUE &&
              argument == 0U && requested_sequence == 0U && eraUsbDiagnosticsBytesAreZero(data, 9U);
      break;
    default:
      valid = false;
      break;
  }

  eraUsbDiagnosticsPrepareResponse(data,
                                   command,
                                   operation,
                                   tag_hi,
                                   tag_lo,
                                   ERA_USB_DIAGNOSTICS_STATUS_OK);

  if (version != ERA_USB_DIAGNOSTICS_PROTOCOL_VERSION)
  {
    data[6] = ERA_USB_DIAGNOSTICS_STATUS_UNSUPPORTED_VERSION;
    return true;
  }
  if (!valid)
  {
    data[6] = ERA_USB_DIAGNOSTICS_STATUS_INVALID;
    return true;
  }

  if (operation == ERA_USB_DIAGNOSTICS_OP_CAPABILITIES)
  {
    eraUsbDiagnosticsEncodeCapabilities(&data[ERA_USB_DIAGNOSTICS_PAYLOAD_OFFSET]);
    return true;
  }

  if (operation == ERA_USB_DIAGNOSTICS_OP_START)
  {
    UsbBootMode_t mode = usbBootModeGet();
    uint32_t expected_interval_us = eraUsbDiagnosticsExpectedIntervalUs(mode);

    if (usbDiagnosticsIsActive())
    {
      data[6] = ERA_USB_DIAGNOSTICS_STATUS_BUSY;
      return true;
    }
    if (!usbDiagnosticsStart(argument, (uint8_t)mode, expected_interval_us, micros()))
    {
      data[6] = ERA_USB_DIAGNOSTICS_STATUS_INVALID;
      return true;
    }

    s_snapshot_valid = false;
    data[7] = (uint8_t)usbDiagnosticsGetState();
    eraUsbDiagnosticsPutBe16(&data[8], usbDiagnosticsGetSessionId());
    data[ERA_USB_DIAGNOSTICS_PAYLOAD_OFFSET] = argument;
    data[ERA_USB_DIAGNOSTICS_PAYLOAD_OFFSET + 1U] = (uint8_t)mode;
    eraUsbDiagnosticsPutBe32(&data[ERA_USB_DIAGNOSTICS_PAYLOAD_OFFSET + 2U], expected_interval_us);
    return true;
  }

  if (operation == ERA_USB_DIAGNOSTICS_OP_STOP)
  {
    if (!usbDiagnosticsStop(micros()))
    {
      data[6] = ERA_USB_DIAGNOSTICS_STATUS_NO_SESSION;
      return true;
    }
    data[7] = (uint8_t)usbDiagnosticsGetState();
    return true;
  }

  if (operation == ERA_USB_DIAGNOSTICS_OP_CLEAR)
  {
    if (!usbDiagnosticsClear())
    {
      data[6] = ERA_USB_DIAGNOSTICS_STATUS_BUSY;
      return true;
    }
    s_snapshot_valid = false;
    data[7] = (uint8_t)usbDiagnosticsGetState();
    eraUsbDiagnosticsPutBe16(&data[8], usbDiagnosticsGetSessionId());
    return true;
  }

  if (argument == 0U)
  {
    usbDiagnosticsCapture(&s_frozen_snapshot, micros());
    s_snapshot_sequence++;
    if (s_snapshot_sequence == 0U)
    {
      s_snapshot_sequence = 1U;
    }
    s_snapshot_valid = true;
  }
  else if (!s_snapshot_valid || requested_sequence != s_snapshot_sequence)
  {
    data[6] = ERA_USB_DIAGNOSTICS_STATUS_STALE_SNAPSHOT;
    eraUsbDiagnosticsPutBe16(&data[10], s_snapshot_valid ? s_snapshot_sequence : 0U);
    return true;
  }

  uint8_t chunk_count = eraUsbDiagnosticsChunkCount(&s_frozen_snapshot);
  if (argument >= chunk_count)
  {
    data[6] = ERA_USB_DIAGNOSTICS_STATUS_INVALID;
    return true;
  }

  data[7] = (uint8_t)s_frozen_snapshot.state;
  eraUsbDiagnosticsPutBe16(&data[8], s_frozen_snapshot.session_id);
  eraUsbDiagnosticsPutBe16(&data[10], s_snapshot_sequence);
  data[12] = argument;
  data[13] = chunk_count;
  eraUsbDiagnosticsEncodeSnapshotChunk(&data[ERA_USB_DIAGNOSTICS_PAYLOAD_OFFSET],
                                       argument,
                                       &s_frozen_snapshot);
  return true;
}
