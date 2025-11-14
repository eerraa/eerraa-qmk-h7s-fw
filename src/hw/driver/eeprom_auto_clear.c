#include "eeprom_auto_clear.h"

#include <stdint.h>

#include "hw_def.h"

#if AUTO_EEPROM_CLEAR_ENABLE
#include "log.h"
#include "eeprom.h"
#include "reset.h"
#include "qmk/quantum/eeconfig.h"
#include "qmk/port/port.h"
#include "qmk/port/platforms/eeprom.h"


static bool eepromReadU32(uint32_t addr, uint32_t *value)
{
  return eepromRead(addr, (uint8_t *)value, sizeof(uint32_t));
}

static bool eepromWriteU32(uint32_t addr, uint32_t value)
{
  return eepromWrite(addr, (uint8_t *)&value, sizeof(uint32_t));
}

static bool eepromWriteFlag(uint32_t addr, uint32_t value)
{
  if (eepromWriteU32(addr, value) != true)
  {
    logPrintf("[!] EEPROM auto clear : flag write 0x%08X fail\n", value);
    return false;
  }
  return true;
}
#endif


bool eepromAutoClearCheck(void)
{
#if AUTO_EEPROM_CLEAR_ENABLE
  const uint32_t flag_addr   = (uint32_t)((uintptr_t)EECONFIG_USER_EEPROM_CLEAR_FLAG);
  const uint32_t cookie_addr = (uint32_t)((uintptr_t)EECONFIG_USER_EEPROM_CLEAR_COOKIE);
  uint32_t flag_value        = 0;
  uint32_t cookie_value      = 0;

  if (eepromReadU32(flag_addr, &flag_value) != true || eepromReadU32(cookie_addr, &cookie_value) != true)
  {
    logPrintf("[!] EEPROM auto clear : sentinel read fail\n");
    return false;
  }

  const bool has_flag_magic   = (flag_value == AUTO_EEPROM_CLEAR_FLAG_MAGIC);
  const bool has_cookie_match = (cookie_value == AUTO_EEPROM_CLEAR_COOKIE);

  if (has_flag_magic && has_cookie_match)
  {
    return true;
  }

  if (has_flag_magic && has_cookie_match == false)
  {
    if (eepromWriteFlag(flag_addr, AUTO_EEPROM_CLEAR_FLAG_RESET) != true)
    {
      return false;
    }
  }

  logPrintf("[  ] EEPROM auto clear : begin (flag=0x%08X, cookie=0x%08X, target=0x%08X)\n",
            flag_value,
            cookie_value,
            AUTO_EEPROM_CLEAR_COOKIE);

  if (eepromFormat() != true)
  {
    logPrintf("[!] EEPROM auto clear : format fail\n");
    return false;
  }

  eeprom_init();  // V251112R2: 하드웨어/버퍼 재동기화

  if (eeprom_apply_factory_defaults(true) != true)              // V251112R3: VIA와 동일한 공용 초기화 경로
  {
    logPrintf("[!] EEPROM auto clear : factory defaults fail\n");
    return false;
  }

  logPrintf("[  ] EEPROM auto clear : success (cookie=0x%08X)\n", AUTO_EEPROM_CLEAR_COOKIE);
  logPrintf("[  ] EEPROM auto clear : scheduling reset\n");
  delay(10);
  resetToReset();
  return true;
#else
  return true;
#endif
}
