#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "era_usb_diagnostics.h"
#include "usb.h"
#include "usb_diagnostics.h"

static int           s_failures;
static uint32_t      s_now_us;
static UsbBootMode_t s_mode = USB_BOOT_MODE_HS_8K;

uint32_t micros(void)
{
  return s_now_us;
}

UsbBootMode_t usbBootModeGet(void)
{
  return s_mode;
}

static void expect_true(const char *name, bool value)
{
  if (!value)
  {
    printf("FAIL %s\n", name);
    s_failures++;
  }
  else
  {
    printf("PASS %s\n", name);
  }
}

static uint16_t get_be16(const uint8_t *data)
{
  return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t get_be32(const uint8_t *data)
{
  return ((uint32_t)data[0] << 24) |
         ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8) |
         data[3];
}

static void prepare_request(uint8_t *data,
                            uint8_t command,
                            uint8_t operation,
                            uint16_t tag)
{
  memset(data, 0, ERA_USB_DIAGNOSTICS_PACKET_SIZE);
  data[0] = command;
  data[1] = ERA_USB_DIAGNOSTICS_KEYBOARD_VALUE;
  data[2] = ERA_USB_DIAGNOSTICS_PROTOCOL_VERSION;
  data[3] = operation;
  data[4] = (uint8_t)(tag >> 8);
  data[5] = (uint8_t)tag;
}

static uint16_t capture_chunk_zero(uint8_t *data, uint16_t tag)
{
  prepare_request(data, 0x02U, ERA_USB_DIAGNOSTICS_OP_SNAPSHOT, tag);
  expect_true("snapshot chunk0 handled", era_usb_diagnostics_via_command(data, 32U));
  expect_true("snapshot chunk0 ok", data[6] == ERA_USB_DIAGNOSTICS_STATUS_OK);
  expect_true("snapshot chunk0 tag", get_be16(&data[4]) == tag);
  return get_be16(&data[10]);
}

static void fetch_chunk(uint8_t *data, uint8_t chunk, uint16_t sequence, uint16_t tag)
{
  prepare_request(data, 0x02U, ERA_USB_DIAGNOSTICS_OP_SNAPSHOT, tag);
  data[6] = chunk;
  data[7] = (uint8_t)(sequence >> 8);
  data[8] = (uint8_t)sequence;
  expect_true("snapshot continuation handled", era_usb_diagnostics_via_command(data, 32U));
}

static void test_capability_and_validation(void)
{
  uint8_t data[32];

  prepare_request(data, 0x02U, ERA_USB_DIAGNOSTICS_OP_CAPABILITIES, 0x1234U);
  expect_true("capability handled", era_usb_diagnostics_via_command(data, 32U));
  expect_true("capability status", data[6] == ERA_USB_DIAGNOSTICS_STATUS_OK);
  expect_true("capability tag", get_be16(&data[4]) == 0x1234U);
  expect_true("capability histogram", data[16] == USB_DIAGNOSTICS_HISTOGRAM_BUCKETS);
  expect_true("capability firmware length", data[22] == 9U);
  expect_true("capability firmware", memcmp(&data[23], "V260823R2", 9U) == 0);

  prepare_request(data, 0x02U, ERA_USB_DIAGNOSTICS_OP_CAPABILITIES, 0x1122U);
  data[2] = 2U;
  expect_true("unsupported version handled", era_usb_diagnostics_via_command(data, 32U));
  expect_true("unsupported version status", data[6] == ERA_USB_DIAGNOSTICS_STATUS_UNSUPPORTED_VERSION);
  expect_true("unsupported version tag", get_be16(&data[4]) == 0x1122U);

  prepare_request(data, 0x02U, ERA_USB_DIAGNOSTICS_OP_CAPABILITIES, 1U);
  data[31] = 1U;
  expect_true("reserved byte handled", era_usb_diagnostics_via_command(data, 32U));
  expect_true("reserved byte invalid", data[6] == ERA_USB_DIAGNOSTICS_STATUS_INVALID);
}

static void test_session_and_chunks(void)
{
  uint8_t data[32];

  prepare_request(data, 0x03U, ERA_USB_DIAGNOSTICS_OP_START, 0x2001U);
  data[6] = 11U;
  expect_true("invalid duration handled", era_usb_diagnostics_via_command(data, 32U));
  expect_true("invalid duration rejected", data[6] == ERA_USB_DIAGNOSTICS_STATUS_INVALID);

  s_now_us = 1000000U;
  prepare_request(data, 0x03U, ERA_USB_DIAGNOSTICS_OP_START, 0x2002U);
  data[6] = 10U;
  expect_true("start handled", era_usb_diagnostics_via_command(data, 32U));
  expect_true("start ok", data[6] == ERA_USB_DIAGNOSTICS_STATUS_OK);
  expect_true("start running", data[7] == USB_DIAGNOSTICS_STATE_RUNNING);
  uint16_t session_id = get_be16(&data[8]);
  expect_true("session id nonzero", session_id != 0U);

  prepare_request(data, 0x03U, ERA_USB_DIAGNOSTICS_OP_START, 0x2003U);
  data[6] = 10U;
  era_usb_diagnostics_via_command(data, 32U);
  expect_true("concurrent start busy", data[6] == ERA_USB_DIAGNOSTICS_STATUS_BUSY);

  usbDiagnosticsTask(1000100U);
  usbDiagnosticsTask(1002300U);  // 2.2 ms gap -> disclosed firmware stall.
  usbDiagnosticsOnReportQueueDepth(3U);
  usbDiagnosticsOnReportTransferStarted(1002400U, session_id, 3U);
  usbDiagnosticsOnReportTransferCompleted(1002650U);  // 250 us -> HS8K 2x bucket.
  usbDiagnosticsOnReportQueueDrop(1002700U);
  usbDiagnosticsOnUsbReset(1002800U, USB_DIAGNOSTICS_SPEED_HIGH);
  usbDiagnosticsOnUsbConfigured(1002900U, USB_DIAGNOSTICS_SPEED_HIGH);
  usbDiagnosticsOnUsbSuspend(1003000U);
  usbDiagnosticsOnUsbReset(1003100U, USB_DIAGNOSTICS_SPEED_FULL);

  uint16_t sequence = capture_chunk_zero(data, 0x3001U);
  expect_true("snapshot sequence nonzero", sequence != 0U);
  expect_true("snapshot session", get_be16(&data[8]) == session_id);
  expect_true("snapshot samples", get_be32(&data[26]) == 1U);
  expect_true("snapshot event chunks", data[13] > ERA_USB_DIAGNOSTICS_BASE_CHUNKS);

  fetch_chunk(data, 1U, sequence, 0x3002U);
  expect_true("stats chunk ok", data[6] == ERA_USB_DIAGNOSTICS_STATUS_OK);
  expect_true("latency min", get_be32(&data[14]) == 250U);
  expect_true("latency average", get_be32(&data[18]) == 250U);
  expect_true("latency max", get_be32(&data[22]) == 250U);
  expect_true("queue peak", get_be16(&data[30]) == 3U);

  fetch_chunk(data, 3U, sequence, 0x3003U);
  expect_true("histogram chunk ok", data[6] == ERA_USB_DIAGNOSTICS_STATUS_OK);
  expect_true("2x histogram bucket", get_be32(&data[18]) == 1U);

  fetch_chunk(data, 4U, sequence, 0x3004U);
  expect_true("loop stall count", get_be32(&data[26]) == 1U);
  expect_true("stall threshold", get_be16(&data[30]) == USB_DIAGNOSTICS_LOOP_STALL_US);

  fetch_chunk(data, 6U, sequence, 0x3005U);
  expect_true("session report drop", get_be32(&data[18]) == 1U);
  expect_true("session reset", get_be32(&data[22]) == 2U);
  expect_true("session configured", get_be32(&data[26]) == 1U);

  fetch_chunk(data, 7U, sequence, 0x3006U);
  expect_true("session suspend", get_be32(&data[14]) == 1U);
  expect_true("session speed change", get_be32(&data[18]) == 1U);

  fetch_chunk(data, 1U, (uint16_t)(sequence + 1U), 0x3007U);
  expect_true("stale snapshot rejected", data[6] == ERA_USB_DIAGNOSTICS_STATUS_STALE_SNAPSHOT);

  usbDiagnosticsOnReportTransferStarted(1004000U, session_id, 0U);
  usbDiagnosticsOnReportTransferCompleted(1004500U);
  fetch_chunk(data, 1U, sequence, 0x3008U);
  expect_true("frozen snapshot unchanged", get_be32(&data[22]) == 250U);
  uint16_t next_sequence = capture_chunk_zero(data, 0x3009U);
  expect_true("new snapshot sequence", next_sequence != sequence);
  fetch_chunk(data, 1U, next_sequence, 0x3010U);
  expect_true("new snapshot sees latency", get_be32(&data[22]) == 500U);

  usbDiagnosticsTask(11000000U);
  capture_chunk_zero(data, 0x3011U);
  expect_true("duration auto complete", data[7] == USB_DIAGNOSTICS_STATE_COMPLETE);
  expect_true("duration capped", get_be32(&data[18]) == 10000U);

  prepare_request(data, 0x03U, ERA_USB_DIAGNOSTICS_OP_STOP, 0x3012U);
  era_usb_diagnostics_via_command(data, 32U);
  expect_true("stop completed reports no session", data[6] == ERA_USB_DIAGNOSTICS_STATUS_NO_SESSION);

  prepare_request(data, 0x03U, ERA_USB_DIAGNOSTICS_OP_CLEAR, 0x3013U);
  era_usb_diagnostics_via_command(data, 32U);
  expect_true("clear ok", data[6] == ERA_USB_DIAGNOSTICS_STATUS_OK);
  expect_true("clear idle", data[7] == USB_DIAGNOSTICS_STATE_IDLE);
}

static void test_wrap_saturation_and_bounds(void)
{
  usb_diagnostics_snapshot_t snapshot;
  usb_diagnostics_hard_counters_t near_max = {
    .report_drops = UINT32_MAX,
    .usb_resets = UINT32_MAX - 1U,
    .configurations = UINT32_MAX,
    .suspends = UINT32_MAX,
    .speed_changes = UINT32_MAX,
  };

  usbDiagnosticsInit();
  usbDiagnosticsTestSetBootCounters(&near_max);
  usbDiagnosticsOnReportQueueDrop(0U);
  usbDiagnosticsOnUsbReset(0U, USB_DIAGNOSTICS_SPEED_HIGH);
  usbDiagnosticsOnUsbReset(0U, USB_DIAGNOSTICS_SPEED_HIGH);
  usbDiagnosticsCapture(&snapshot, 0U);
  expect_true("report drop saturates", snapshot.boot_counters.report_drops == UINT32_MAX);
  expect_true("reset saturates", snapshot.boot_counters.usb_resets == UINT32_MAX);
  expect_true("snapshot RAM bounded", sizeof(snapshot) < 512U);

  uint32_t start = UINT32_MAX - 500000U;
  expect_true("wrap session starts", usbDiagnosticsStart(10U, USB_BOOT_MODE_HS_8K, 125U, start));
  usbDiagnosticsTask(start + 100U);
  usbDiagnosticsTask(start + 2000U);
  expect_true("wrap before deadline running", usbDiagnosticsIsActive());
  usbDiagnosticsTask(start + 10000000U);
  expect_true("wrap deadline completes", usbDiagnosticsGetState() == USB_DIAGNOSTICS_STATE_COMPLETE);
}

int main(void)
{
  usbDiagnosticsInit();
  test_capability_and_validation();
  test_session_and_chunks();
  test_wrap_saturation_and_bounds();

  if (s_failures != 0)
  {
    printf("%d diagnostic failure(s)\n", s_failures);
    return 1;
  }
  printf("all USB diagnostic host tests passed\n");
  return 0;
}
