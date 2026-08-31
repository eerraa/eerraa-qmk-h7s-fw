#include "ver_port.h"
#include "via.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>


static int g_fails = 0;


static void expect_true(const char *name, bool cond)
{
  if (cond)
  {
    printf("PASS %s\n", name);
  }
  else
  {
    printf("FAIL %s\n", name);
    g_fails++;
  }
}

static void expect_eq_u8(const char *name, uint8_t got, uint8_t want)
{
  if (got != want)
  {
    printf("FAIL %s got=%u want=%u\n", name, (unsigned)got, (unsigned)want);
    g_fails++;
  }
  else
  {
    printf("PASS %s\n", name);
  }
}

static uint8_t via_get(uint8_t value_id)
{
  uint8_t buf[32];

  memset(buf, 0, sizeof(buf));
  buf[0] = id_custom_get_value;
  buf[1] = 8;
  buf[2] = value_id;
  via_qmk_version(buf, 32);
  return buf[3];
}


static void via_get_ascii(uint8_t value[10])
{
  uint8_t buf[32];

  memset(buf, 0xA5, sizeof(buf));
  buf[0] = id_custom_get_value;
  buf[1] = 8;
  buf[2] = 5;
  via_qmk_version(buf, 32);
  memcpy(value, &buf[3], 10);
}


int main(void)
{
  uint8_t buf[32];
  uint8_t version[10];

  expect_true("cookie is V260901R1", strcmp(_DEF_FIRMWARE_VERSION, "V260901R1") == 0);

  // ver_port.c: Year = atoi(YY) - 24, Month/Day = atoi - 1, Rev = atoi(Rn) - 1
  expect_eq_u8("GET Year 26 -> 2", via_get(1), 2);
  expect_eq_u8("GET Month 09 -> 8", via_get(2), 8);
  expect_eq_u8("GET Day 01 -> 0", via_get(3), 0);
  expect_eq_u8("GET Rev R1 -> 0", via_get(4), 0);

  via_get_ascii(version);
  expect_true("GET ASCII -> 260901R1 plus NUL", memcmp(version, "260901R1", 9) == 0);
  expect_eq_u8("GET ASCII preserves report tail", version[9], 0xA5);

  memset(buf, 0, sizeof(buf));
  buf[0] = id_custom_set_value;
  buf[1] = 8;
  buf[2] = 1;
  buf[3] = 9;
  via_qmk_version(buf, 32);
  expect_eq_u8("SET does not change Year GET", via_get(1), 2);

  if (g_fails != 0)
  {
    printf("FAILED %d\n", g_fails);
    return 1;
  }
  printf("All VERSION tests passed\n");
  return 0;
}
