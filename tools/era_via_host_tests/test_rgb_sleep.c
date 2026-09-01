#include "rgb_sleep.h"
#include "via.h"
#include "port.h"
#include "era_state_sync.h"
#include "platforms/eeprom.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>


#define HOST_EEPROM_SIZE 4096U

static int g_fails = 0;
static uint32_t g_now_ms = 0;
static uint32_t g_matrix_idle_ms = 0;
static bool g_usb_suspended = false;
static bool g_host_seen = false;
static uint32_t g_sof_count = 0;
static bool g_rgb_enabled = true;
static bool g_rgb_suspended = false;
static int g_suspend_calls = 0;
static int g_wakeup_calls = 0;
static int g_disable_noeeprom_calls = 0;
static int g_eeprom_writes = 0;
static uint8_t s_eeprom[HOST_EEPROM_SIZE];


static uintptr_t host_eeprom_index(const void *addr)
{
  return (uintptr_t)addr;
}

void eeprom_read_block(void *buf, const void *addr, uint32_t len)
{
  uintptr_t off = host_eeprom_index(addr);
  if (off + len > HOST_EEPROM_SIZE)
  {
    memset(buf, 0, len);
    return;
  }
  memcpy(buf, &s_eeprom[off], len);
}

void eeprom_update_block(const void *buf, void *addr, size_t len)
{
  uintptr_t off = host_eeprom_index(addr);
  if (off + len > HOST_EEPROM_SIZE)
  {
    return;
  }
  memcpy(&s_eeprom[off], buf, len);
  g_eeprom_writes++;
}

uint16_t timer_read(void)
{
  return (uint16_t)g_now_ms;
}

uint16_t timer_elapsed(uint16_t last)
{
  return (uint16_t)g_now_ms - last;
}


uint32_t last_matrix_activity_elapsed(void)
{
  return g_matrix_idle_ms;
}

bool usbIsSuspended(void)
{
  return g_usb_suspended;
}

bool usbHostSeen(void)
{
  return g_host_seen;
}

uint32_t usbSofCount(void)
{
  return g_sof_count;
}

uint32_t timer_read32(void)
{
  return g_now_ms;
}

uint32_t timer_elapsed32(uint32_t last)
{
  return g_now_ms - last;
}

void rgblight_suspend(void)
{
  g_suspend_calls++;
  if (!g_rgb_suspended)
  {
    g_rgb_suspended = true;
    g_rgb_enabled = false;
  }
}

void rgblight_wakeup(void)
{
  g_wakeup_calls++;
  g_rgb_suspended = false;
  g_rgb_enabled = true;
}

void rgblight_disable_noeeprom(void)
{
  g_disable_noeeprom_calls++;
  g_rgb_enabled = false;
}

bool rgblight_is_enabled(void)
{
  return g_rgb_enabled;
}

static void fail(const char *name)
{
  printf("FAIL %s\n", name);
  g_fails++;
}

static void pass(const char *name)
{
  printf("PASS %s\n", name);
}

static void expect_true(const char *name, bool cond)
{
  if (cond)
  {
    pass(name);
  }
  else
  {
    fail(name);
  }
}

static void via_packet(uint8_t cmd, uint8_t value_id, uint8_t value, uint8_t *buf)
{
  memset(buf, 0, 32);
  buf[0] = cmd;
  buf[1] = id_qmk_rgb_sleep;
  buf[2] = value_id;
  buf[3] = value;
}

static uint8_t via_get_timeout(void)
{
  uint8_t buf[32];

  via_packet(id_custom_get_value, id_qmk_rgb_sleep_timeout, 0, buf);
  rgb_sleep_handle_via_command(buf, 32);
  return buf[3];
}

static uint32_t via_set_timeout(uint8_t minutes)
{
  uint8_t buf[32];
  uint32_t before = era_state_sync_config_revision();

  via_packet(id_custom_set_value, id_qmk_rgb_sleep_timeout, minutes, buf);
  rgb_sleep_handle_via_command(buf, 32);
  return era_state_sync_config_revision() - before;
}

static uint16_t via_get_timeout_exact(void)
{
  uint8_t buf[32];

  via_packet(id_custom_get_value, id_qmk_rgb_sleep_timeout_exact, 0, buf);
  rgb_sleep_handle_via_command(buf, 32);
  return ((uint16_t)buf[3] << 8) | (uint16_t)buf[4];
}

static uint32_t via_set_timeout_exact(uint16_t seconds)
{
  uint8_t buf[32];
  uint32_t before = era_state_sync_config_revision();

  via_packet(id_custom_set_value, id_qmk_rgb_sleep_timeout_exact, (uint8_t)(seconds >> 8), buf);
  buf[4] = (uint8_t)(seconds & 0xFFU);
  rgb_sleep_handle_via_command(buf, 32);
  return era_state_sync_config_revision() - before;
}

static bool via_get_enable(void)
{
  uint8_t buf[32];

  via_packet(id_custom_get_value, id_qmk_rgb_sleep_enable, 0, buf);
  rgb_sleep_handle_via_command(buf, 32);
  return buf[3] != 0U;
}

static uint32_t via_set_enable(bool enabled)
{
  uint8_t buf[32];
  uint32_t before = era_state_sync_config_revision();

  via_packet(id_custom_set_value, id_qmk_rgb_sleep_enable, enabled ? 1U : 0U, buf);
  rgb_sleep_handle_via_command(buf, 32);
  return era_state_sync_config_revision() - before;
}

static void via_save(void)
{
  uint8_t buf[32];

  via_packet(id_custom_save, 0, 0, buf);
  rgb_sleep_handle_via_command(buf, 32);
}

static void reset_rgb_counts(void)
{
  g_suspend_calls = 0;
  g_wakeup_calls = 0;
  g_disable_noeeprom_calls = 0;
}

static void poke_stored_seconds(uint16_t seconds)
{
  uint8_t rec[4];

  rec[0] = 0x53;
  rec[1] = 1;
  rec[2] = (uint8_t)(seconds & 0xFFU);
  rec[3] = (uint8_t)(seconds >> 8);
  eeprom_update_block(rec, EECONFIG_USER_RGB_SLEEP, sizeof(rec));
}

int main(void)
{
  uint32_t ten_min = 10U * 60U * 1000U;

  expect_true("three reasons stay lit below idle",
              !rgb_sleep_policy_local_requested(false, false, true, 60, 59999));
  expect_true("explicit suspend is an independent reason",
              rgb_sleep_policy_local_requested(true, false, true, 60, 0));
  expect_true("frame loss is an independent reason",
              rgb_sleep_policy_local_requested(false, true, true, 60, 0));
  expect_true("idle timeout is an independent reason",
              rgb_sleep_policy_local_requested(false, false, true, 60, 60000));
  expect_true("user disable gates idle sleep",
              !rgb_sleep_policy_local_requested(false, false, false, 60, UINT32_MAX));
  expect_true("user disable gates explicit suspend sleep",
              !rgb_sleep_policy_local_requested(true, false, false, 60, 0));
  expect_true("user disable gates frame-loss sleep",
              !rgb_sleep_policy_local_requested(false, true, false, 60, 0));
  expect_true("zero timeout does not idle-sleep",
              !rgb_sleep_policy_local_requested(false, false, true, 0, UINT32_MAX));
  expect_true("wraparound elapsed still darkens",
              rgb_sleep_policy_local_requested(false, false, true, 600, UINT32_MAX));

  expect_true("preset 10", rgb_sleep_timeout_is_preset(10));
  expect_true("preset 1/3/5/30/60",
              rgb_sleep_timeout_is_preset(1) && rgb_sleep_timeout_is_preset(3) &&
              rgb_sleep_timeout_is_preset(5) && rgb_sleep_timeout_is_preset(30) &&
              rgb_sleep_timeout_is_preset(60));
  expect_true("reject 0/2/7/255",
              !rgb_sleep_timeout_is_preset(0) && !rgb_sleep_timeout_is_preset(2) &&
              !rgb_sleep_timeout_is_preset(7) && !rgb_sleep_timeout_is_preset(255));

  expect_true("project 1s floors to 1 min", rgb_sleep_policy_preset_minutes(1) == 1);
  expect_true("project 179s stays 1 min", rgb_sleep_policy_preset_minutes(179) == 1);
  expect_true("project 180s is 3 min", rgb_sleep_policy_preset_minutes(180) == 3);
  expect_true("project 599s is 5 min", rgb_sleep_policy_preset_minutes(599) == 5);
  expect_true("project 600s is 10 min", rgb_sleep_policy_preset_minutes(600) == 10);
  expect_true("project 3599s is 30 min", rgb_sleep_policy_preset_minutes(3599) == 30);
  expect_true("project 3600s is 60 min", rgb_sleep_policy_preset_minutes(3600) == 60);
  expect_true("project UINT16_MAX clamps to 60", rgb_sleep_policy_preset_minutes(UINT16_MAX) == 60);

  rgb_sleep_init();
  expect_true("RGB Sleep defaults enabled", via_get_enable());
  expect_true("GET default 10", via_get_timeout() == 10);
  expect_true("default store is 600 s", rgb_sleep_timeout_seconds() == 600);

  expect_true("disable bumps once", via_set_enable(false) == 1);
  expect_true("GET disabled", !via_get_enable());
  expect_true("disable preserves timeout", rgb_sleep_timeout_seconds() == 600);
  expect_true("same disable is no-op bump", via_set_enable(false) == 0);
  expect_true("enable bumps once", via_set_enable(true) == 1);
  expect_true("GET enabled", via_get_enable());

  expect_true("SET 5 bumps", via_set_timeout(5) == 1);
  expect_true("GET 5 after SET", via_get_timeout() == 5);
  expect_true("SET 5 stores 300 s", rgb_sleep_timeout_seconds() == 300);
  expect_true("same SET is no-op bump", via_set_timeout(5) == 0);
  expect_true("invalid SET refused", via_set_timeout(7) == 0 && via_get_timeout() == 5);

  expect_true("exact SET 137 s bumps", via_set_timeout_exact(137) == 1);
  expect_true("exact GET 137 s", via_get_timeout_exact() == 137);
  expect_true("preset GET projects 137 s to 1 min without mutation",
              via_get_timeout() == 1 && rgb_sleep_timeout_seconds() == 137);
  expect_true("same exact SET is no-op bump", via_set_timeout_exact(137) == 0);
  expect_true("exact SET 1 s", via_set_timeout_exact(1) == 1 && via_get_timeout_exact() == 1);
  expect_true("exact SET 65535 s", via_set_timeout_exact(UINT16_MAX) == 1 &&
              via_get_timeout_exact() == UINT16_MAX);
  expect_true("exact SET 0 rejected", via_set_timeout_exact(0) == 0 &&
              via_get_timeout_exact() == UINT16_MAX);

  {
    uint8_t buf[32];
    via_packet(id_custom_set_value, id_qmk_rgb_sleep_timeout_exact, 0, buf);
    buf[4] = 137;
    expect_true("short exact SET is unhandled",
                rgb_sleep_handle_via_command(buf, 4) && buf[0] == id_unhandled &&
                via_get_timeout_exact() == UINT16_MAX);
    via_packet(id_custom_get_value, id_qmk_rgb_sleep_timeout_exact, 0, buf);
    expect_true("short exact GET is unhandled",
                rgb_sleep_handle_via_command(buf, 4) && buf[0] == id_unhandled);
    via_packet(id_custom_get_value, 99, 0, buf);
    expect_true("unknown RGB SLEEP value keeps legacy inert handling",
                rgb_sleep_handle_via_command(buf, 32) && buf[0] == id_custom_get_value);
  }

  rgb_sleep_init();
  expect_true("reboot without SAVE restores 10", via_get_timeout() == 10);

  expect_true("SET 30 then SAVE", via_set_timeout(30) == 1);
  via_save();
  rgb_sleep_init();
  expect_true("reboot after SAVE keeps 30", via_get_timeout() == 30);
  expect_true("saved store is 1800 s", rgb_sleep_timeout_seconds() == 1800);

  expect_true("exact SET 137 then SAVE", via_set_timeout_exact(137) == 1);
  via_save();
  rgb_sleep_init();
  expect_true("reboot after exact SAVE keeps 137 s", via_get_timeout_exact() == 137);
  expect_true("official GET after exact SAVE projects 137 s without mutation",
              via_get_timeout() == 1 && rgb_sleep_timeout_seconds() == 137);

  expect_true("disable then SAVE", via_set_enable(false) == 1);
  via_save();
  rgb_sleep_init();
  expect_true("reboot after SAVE keeps disabled", !via_get_enable());
  expect_true("disabled reboot preserves exact timeout", via_get_timeout_exact() == 137);
  expect_true("re-enable then SAVE", via_set_enable(true) == 1);
  via_save();
  rgb_sleep_init();
  expect_true("reboot after re-enable keeps enabled", via_get_enable());
  expect_true("re-enable restores preserved exact timeout", via_get_timeout_exact() == 137);

  rgb_sleep_storage_apply_defaults();
  rgb_sleep_storage_flush(true);
  rgb_sleep_init();
  expect_true("CLEAN restores RGB Sleep enabled", via_get_enable());
  expect_true("CLEAN restores 10", via_get_timeout() == 10);

  poke_stored_seconds(599);
  rgb_sleep_init();
  expect_true("legacy v1 slot migrates enabled", via_get_enable());
  expect_true("GET projects 599s to 5 without mutation",
              via_get_timeout() == 5 && rgb_sleep_timeout_seconds() == 599);

  g_now_ms = 1000;
  g_sof_count = 1;
  g_host_seen = true;
  g_usb_suspended = false;
  g_matrix_idle_ms = 0;
  g_rgb_enabled = true;
  g_rgb_suspended = false;
  rgb_sleep_init();
  rgb_sleep_task();
  expect_true("fresh host stays lit", !rgb_sleep_is_dark());

  g_matrix_idle_ms = ten_min;
  reset_rgb_counts();
  g_eeprom_writes = 0;
  rgb_sleep_task();
  expect_true("idle timeout suspends RGB", rgb_sleep_is_dark() && g_suspend_calls == 1);
  expect_true("sleep does not write EEPROM", g_eeprom_writes == 0);

  reset_rgb_counts();
  rgb_sleep_task();
  expect_true("already-dark task is not a second suspend",
              rgb_sleep_is_dark() && g_suspend_calls == 0);

  g_rgb_enabled = true;
  reset_rgb_counts();
  rgb_sleep_task();
  expect_true("owner reasserts if RGB is turned on while dark",
              rgb_sleep_is_dark() && g_suspend_calls == 1);

  g_matrix_idle_ms = 0;
  reset_rgb_counts();
  g_eeprom_writes = 0;
  rgb_sleep_task();
  expect_true("keypress wakes RGB", !rgb_sleep_is_dark() && g_wakeup_calls == 1);
  expect_true("wake does not write EEPROM", g_eeprom_writes == 0);

  g_usb_suspended = true;
  reset_rgb_counts();
  rgb_sleep_task();
  expect_true("usb suspend darkens", rgb_sleep_is_dark() && g_suspend_calls == 1);

  g_usb_suspended = false;
  g_matrix_idle_ms = 0;
  reset_rgb_counts();
  rgb_sleep_task();
  expect_true("resume with recent key wakes", !rgb_sleep_is_dark() && g_wakeup_calls == 1);

  expect_true("master disable changes state", via_set_enable(false) == 1);
  g_usb_suspended = true;
  reset_rgb_counts();
  rgb_sleep_task();
  expect_true("master disable blocks USB suspend RGB sleep",
              !rgb_sleep_is_dark() && g_suspend_calls == 0);
  g_usb_suspended = false;
  g_host_seen = true;
  g_sof_count = 30;
  g_now_ms = 6000;
  rgb_sleep_task();
  g_sof_count = 31;
  g_now_ms = 6001;
  rgb_sleep_task();
  g_now_ms = 6000 + RGB_SLEEP_SOF_STALE_MS;
  reset_rgb_counts();
  rgb_sleep_task();
  expect_true("master disable blocks host-loss RGB sleep",
              !rgb_sleep_is_dark() && g_suspend_calls == 0);
  expect_true("master re-enable changes state", via_set_enable(true) == 1);

  g_usb_suspended = true;
  rgb_sleep_task();
  g_usb_suspended = false;
  g_matrix_idle_ms = ten_min;
  reset_rgb_counts();
  rgb_sleep_task();
  expect_true("resume while idle stays dark (single owner)",
              rgb_sleep_is_dark() && g_wakeup_calls == 0);

  g_usb_suspended = false;
  g_matrix_idle_ms = 0;
  g_host_seen = true;
  g_sof_count = 10;
  g_now_ms = 5000;
  rgb_sleep_init();
  g_sof_count = 11;
  rgb_sleep_task();
  expect_true("SOF seen stays lit", !rgb_sleep_is_dark());
  g_now_ms = 5000 + RGB_SLEEP_SOF_STALE_MS;
  reset_rgb_counts();
  rgb_sleep_task();
  expect_true("SOF stale with host seen darkens", rgb_sleep_is_dark() && g_suspend_calls == 1);

  g_sof_count = 12;
  g_now_ms = 5000 + RGB_SLEEP_SOF_STALE_MS + 10;
  g_matrix_idle_ms = 0;
  reset_rgb_counts();
  rgb_sleep_task();
  expect_true("SOF return wakes", !rgb_sleep_is_dark() && g_wakeup_calls == 1);

  g_host_seen = false;
  g_sof_count = 20;
  g_now_ms = 0;
  rgb_sleep_init();
  g_now_ms = RGB_SLEEP_SOF_STALE_MS + 1000;
  g_matrix_idle_ms = 0;
  reset_rgb_counts();
  rgb_sleep_task();
  expect_true("never-enumerated power stays lit", !rgb_sleep_is_dark() && g_suspend_calls == 0);

  if (g_fails != 0)
  {
    printf("%d failed\n", g_fails);
    return 1;
  }
  printf("ok\n");
  return 0;
}
