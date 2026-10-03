#include "sys_port.h"
#include "via.h"
#include "platforms/eeprom.h"
#include "bootloader.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>


static int g_fails = 0;
static uint32_t g_now_ms = 0;
static int g_clean_calls = 0;
static int g_boot_calls = 0;


uint32_t timer_read32(void)
{
  return g_now_ms;
}

uint32_t timer_elapsed32(uint32_t last)
{
  return g_now_ms - last;
}

void eeprom_req_clean(void)
{
  g_clean_calls++;
}

bool bootloader_jump_deferred(void)
{
  g_boot_calls++;
  return true;
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
  buf[1] = id_qmk_system;
  buf[2] = value_id;
  buf[3] = value;
}

static uint8_t via_get(uint8_t value_id)
{
  uint8_t buf[32];

  via_packet(id_custom_get_value, value_id, 0, buf);
  via_qmk_system(buf, 32);
  return buf[3];
}

static void via_set(uint8_t value_id, uint8_t value)
{
  uint8_t buf[32];

  via_packet(id_custom_set_value, value_id, value, buf);
  via_qmk_system(buf, 32);
}

static void via_save(void)
{
  uint8_t buf[32];

  via_packet(id_custom_save, 0, 0, buf);
  via_qmk_system(buf, 32);
}

static void reset_session(void)
{
  g_now_ms = 0;
  g_clean_calls = 0;
  g_boot_calls = 0;
  via_set(2, 0);
  via_set(3, 0);
  via_set(4, 0);
  g_clean_calls = 0;
  g_boot_calls = 0;
}


int main(void)
{
  uint8_t short_buf[2];

  reset_session();
  expect_true("GET confirm bits default 0",
              via_get(2) == 0 && via_get(3) == 0 && via_get(4) == 0);
  expect_true("GET Jump to Boot is 0", via_get(1) == 0);

  via_set(2, 1);
  expect_true("SET confirm 1 GET 1", via_get(2) == 1);
  via_set(2, 0);
  expect_true("SET 0 clears confirm 1", via_get(2) == 0);
  expect_true("clear does not CLEAN", g_clean_calls == 0);

  reset_session();
  via_set(2, 1);
  via_set(3, 1);
  via_set(4, 1);
  expect_true("three confirms CLEAN once", g_clean_calls == 1);
  expect_true("CLEAN clears confirm bits",
              via_get(2) == 0 && via_get(3) == 0 && via_get(4) == 0);

  reset_session();
  via_set(2, 1);
  via_set(3, 1);
  via_save();
  expect_true("SAVE is not CLEAN", g_clean_calls == 0);

  reset_session();
  via_set(2, 1);
  g_now_ms = 10000;
  expect_true("confirmation survives ten seconds", via_get(2) == 1);
  via_set(3, 1);
  g_now_ms = UINT32_MAX;
  expect_true("confirmations survive long delay", via_get(2) == 1 && via_get(3) == 1);
  g_now_ms = 20;
  via_save();
  expect_true("timer wrap and SAVE do not clear or CLEAN",
              via_get(2) == 1 && via_get(3) == 1 && g_clean_calls == 0);
  via_set(4, 1);
  expect_true("delayed third toggle CLEANs once", g_clean_calls == 1);
  expect_true("delayed CLEAN consumes all bits",
              via_get(2) == 0 && via_get(3) == 0 && via_get(4) == 0);
  via_set(4, 1);
  via_save();
  expect_true("repeated final toggle cannot repeat CLEAN", g_clean_calls == 1);

  reset_session();
  via_set(4, 1);
  via_set(2, 1);
  g_now_ms = 60000;
  via_set(2, 0);
  via_set(3, 1);
  expect_true("OFF cancels only its own confirmation",
              via_get(2) == 0 && via_get(3) == 1 && via_get(4) == 1 && g_clean_calls == 0);
  via_set(2, 1);
  expect_true("confirmation order is unrestricted", g_clean_calls == 1);

  reset_session();
  via_set(1, 0);
  expect_true("Jump to Boot SET 0 does not jump", g_boot_calls == 0);
  via_set(1, 1);
  expect_true("Jump to Boot SET 1 jumps once", g_boot_calls == 1);
  expect_true("Jump to Boot GET stays 0", via_get(1) == 0);

  short_buf[0] = id_custom_set_value;
  short_buf[1] = id_qmk_system;
  via_qmk_system(short_buf, 2);
  expect_true("short SET is unhandled", short_buf[0] == id_unhandled);

  if (g_fails != 0)
  {
    printf("%d failed\n", g_fails);
    return 1;
  }
  printf("ok\n");
  return 0;
}
