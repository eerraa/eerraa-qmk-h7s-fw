#include "bootloader.h"
#include "eeprom.h"
#include "usb.h"        // V251109R4: VIA 리셋 유예 요청

void bootloader_jump(void)
{
  resetToBoot();
}

void mcu_reset(void)
{
  for (int i=0; i<32; i++)
  {
    eeprom_update();
  }
#ifdef _USE_HW_USB
  if (usbScheduleGraceReset(0U) == true)
  {
    return;             // V251109R4: VIA 응답 송신까지 리셋을 보류
  }
#endif
  resetToReset();
}
