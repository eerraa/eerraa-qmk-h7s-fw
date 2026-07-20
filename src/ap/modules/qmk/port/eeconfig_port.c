#include "eeconfig.h"
#include "eeprom.h"
#include "qmk/port/port.h"
#include "hw_def.h"
#include "usb.h"
#include "qmk/port/usb_monitor.h"
#include "qmk/port/debounce_profile.h"


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
  debounce_profile_storage_apply_defaults();                   // V251115R1: VIA 디바운스 프로필 기본값 기록
  debounce_profile_save(true);
#ifdef G_TERM_ENABLE
  tapping_term_storage_apply_defaults();                       // V251123R4: VIA TAPPING 슬롯 기본값 기록
  tapping_term_storage_flush(true);
#endif
#ifdef TAPDANCE_ENABLE
  tapdance_storage_apply_defaults();                           // V251124R8: VIA TAPDANCE 슬롯 기본값 기록
  tapdance_storage_flush(true);
#endif
#if defined(AUTO_FACTORY_RESET_FLAG_MAGIC) && defined(AUTO_FACTORY_RESET_COOKIE)
  eeprom_update_dword((uint32_t *)EECONFIG_USER_EEPROM_CLEAR_FLAG, AUTO_FACTORY_RESET_FLAG_MAGIC);
  eeprom_update_dword((uint32_t *)EECONFIG_USER_EEPROM_CLEAR_COOKIE, AUTO_FACTORY_RESET_COOKIE);
#endif
}
#endif
