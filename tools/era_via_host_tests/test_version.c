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

  // V260909R1: release version is the fixture input, not a hard-coded previous release.
  const char *cookie = _DEF_FIRMWARE_VERSION;
  expect_true("cookie VYYMMDDRn format", strlen(cookie) == 9U && cookie[0] == 'V' && cookie[7] == 'R');
  if (g_fails) return 1;
  uint8_t year = (uint8_t)((cookie[1] - '0') * 10 + cookie[2] - '0' - 24);
  uint8_t month = (uint8_t)((cookie[3] - '0') * 10 + cookie[4] - '0' - 1);
  uint8_t day = (uint8_t)((cookie[5] - '0') * 10 + cookie[6] - '0' - 1);
  uint8_t revision = (uint8_t)(cookie[8] - '0' - 1);
  expect_eq_u8("GET Year offset", via_get(1), year);
  expect_eq_u8("GET Month offset", via_get(2), month);
  expect_eq_u8("GET Day offset", via_get(3), day);
  expect_eq_u8("GET Revision offset", via_get(4), revision);
  via_get_ascii(version);
  expect_true("GET ASCII matches cookie without V plus NUL", memcmp(version, cookie + 1, 9U) == 0);
  expect_eq_u8("GET ASCII preserves report tail", version[9], 0xA5);

  memset(buf, 0, sizeof(buf));
  buf[0] = id_custom_set_value;
  buf[1] = 8;
  buf[2] = 1;
  buf[3] = 9;
  via_qmk_version(buf, 32);
  expect_eq_u8("SET does not change Year GET", via_get(1), year);

  if (g_fails != 0)
  {
    printf("FAILED %d\n", g_fails);
    return 1;
  }
  printf("All VERSION tests passed\n");
  return 0;
}
