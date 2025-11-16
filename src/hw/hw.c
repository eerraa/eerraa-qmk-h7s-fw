#include "hw.h"
#include "qmk/port/platforms/eeprom.h"  // V250923R1 Preload QMK EEPROM services
#include "qmk/port/usb_monitor.h"    // V251112R6: USB 모니터 초기화 진입점



extern uint32_t _fw_flash_begin;

volatile const firm_ver_t firm_ver __attribute__((section(".version"))) = 
{
  .magic_number = VERSION_MAGIC_NUMBER,
  .version_str  = _DEF_FIRMWATRE_VERSION,
  .name_str     = _DEF_BOARD_NAME,
  .firm_addr    = (uint32_t)&_fw_flash_begin
};



bool hwInit(void)
{  
  #ifdef _USE_HW_CACHE
  SCB_EnableICache();
  SCB_EnableDCache();
  #endif  

  cliInit();
  logInit();  
  ledInit();
  microsInit();

  uartInit();
  for (int i=0; i<HW_UART_MAX_CH; i++)
  {
    uartOpen(i, 115200);
  }

  logOpen(HW_LOG_CH, 115200);
  logPrintf("\r\n[ Firmware Begin... ]\r\n");
  logPrintf("Booting..Name \t\t: %s\r\n", _DEF_BOARD_NAME);
  logPrintf("Booting..KBD  \t\t: %s\r\n", KBD_NAME);  
  logPrintf("Booting..Ver  \t\t: %s\r\n", _DEF_FIRMWATRE_VERSION);  
  logPrintf("Booting..Clock\t\t: %d Mhz\r\n", (int)HAL_RCC_GetSysClockFreq()/1000000);
  logPrintf("Booting..Date \t\t: %s\r\n", __DATE__); 
  logPrintf("Booting..Time \t\t: %s\r\n", __TIME__); 
  logPrintf("Booting..Addr \t\t: 0x%X\r\n", (uint32_t)&_fw_flash_begin); 

  logPrintf("\n");
  logPrintf("[  ] ICache  %s\n", (SCB->CCR & SCB_CCR_IC_Msk) ? "ON":"OFF");
  logPrintf("[  ] DCache  %s\n", (SCB->CCR & SCB_CCR_DC_Msk) ? "ON":"OFF");
  
  rtcInit();
  resetInit();    
  i2cInit();
  eepromInit();
  eeprom_init();                                              // V250923R1 Sync QMK EEPROM image before USB init
#ifdef BOOTMODE_ENABLE
  bootmode_init();                                            // V251112R6: BootMode 기본값 초기화
#endif
#ifdef USB_MONITOR_ENABLE
  usb_monitor_init();                                         // V251112R6: USB 모니터 기본값 초기화
#endif
  bool factory_reset_ok = eepromAutoFactoryResetCheck();      // V251112R3: AUTO_FACTORY_RESET_ENABLE 빌드에서 강제 초기화
#ifdef BOOTMODE_ENABLE
  if (usbBootModeLoad() != true)                              // V250923R1 Apply stored USB boot mode preference
  {
    logPrintf("[!] usbBootModeLoad Fail\n");
  }
#endif
#ifdef USB_MONITOR_ENABLE
  if (usbInstabilityLoad() != true)                           // V251108R1: VIA 토글과 USB 모니터 상태 동기화
  {
    logPrintf("[!] usbInstabilityLoad Fail\n");
  }
#endif
  (void)factory_reset_ok;
  #ifdef _USE_HW_QSPI
  qspiInit();
  #endif
  flashInit();
  keysInit();
  #ifdef _USE_HW_WS2812
  ws2812Init();
  #endif
  
  cdcInit();
  usbInit();
#if HW_USB_CMP
  usbBegin(USB_CMP_MODE);
#else
  usbBegin(USB_HID_MODE);
#endif

#if defined(_USE_HW_I2C)
  {
    i2c_ready_wait_stats_t ready_stats;
    i2cGetReadyWaitStats(_DEF_I2C1, &ready_stats);
    logPrintf("[I2C] ready wait summary max=%lums count=%lu last=0x%02X\n",
              (unsigned long)ready_stats.wait_max_ms,
              (unsigned long)ready_stats.wait_count,
              ready_stats.wait_last_addr);                       // V251112R9: 부팅 완료 후 한 줄 요약
  }
#endif

  return true;
}
