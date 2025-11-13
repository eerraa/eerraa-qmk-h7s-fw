#include "eeconfig.h"
#include "eeprom.h"
#include "qmk/port/port.h"
#include "hw_def.h"
#include "usb.h"
#include "qmk/port/usb_monitor.h"


#if (EECONFIG_USER_DATA_SIZE) > 0
void eeconfig_init_user_datablock(void)
{
  uint8_t dummy_user[(EECONFIG_USER_DATA_SIZE)] = {0};
  eeconfig_update_user_datablock(dummy_user);
#ifdef BOOTMODE_ENABLE
  usbBootModeApplyDefaults();                                 // V251113R2: USER 초기화 시 BootMode 슬롯 기본값 기록
#endif
#ifdef USB_MONITOR_ENABLE
  usb_monitor_storage_apply_defaults();                       // V251113R2: USB 모니터 슬롯 기본값 기록
#endif
#if defined(AUTO_EEPROM_CLEAR_FLAG_MAGIC) && defined(AUTO_EEPROM_CLEAR_COOKIE)
  eeprom_update_dword((uint32_t *)EECONFIG_USER_EEPROM_CLEAR_FLAG, AUTO_EEPROM_CLEAR_FLAG_MAGIC);
  eeprom_update_dword((uint32_t *)EECONFIG_USER_EEPROM_CLEAR_COOKIE, AUTO_EEPROM_CLEAR_COOKIE);
#endif
}
#endif
