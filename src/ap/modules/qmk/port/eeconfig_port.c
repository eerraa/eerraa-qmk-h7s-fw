#include "eeconfig.h"
#include "eeprom.h"
#include "qmk/port/port.h"
#include "hw_def.h"


#if (EECONFIG_USER_DATA_SIZE) > 0
void eeconfig_init_user_datablock(void)
{
  uint8_t dummy_user[(EECONFIG_USER_DATA_SIZE)] = {0};
  eeconfig_update_user_datablock(dummy_user);
#if defined(AUTO_EEPROM_CLEAR_FLAG_MAGIC) && defined(AUTO_EEPROM_CLEAR_COOKIE)
  eeprom_update_dword((uint32_t *)EECONFIG_USER_EEPROM_CLEAR_FLAG, AUTO_EEPROM_CLEAR_FLAG_MAGIC);
  eeprom_update_dword((uint32_t *)EECONFIG_USER_EEPROM_CLEAR_COOKIE, AUTO_EEPROM_CLEAR_COOKIE);
#endif
}
#endif
