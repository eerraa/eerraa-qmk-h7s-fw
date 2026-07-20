#include "ap.h"
#include "qmk/qmk.h"
#include "usb.h"                                             // V251123R7: USB 디버그 스냅샷 참조
#include "usbd_hid.h"                                      // V251108R9 USB SOF 모니터 백그라운드 훅


void cliUpdate(void);




void apInit(void)
{  
  cliOpen(HW_UART_CH_CLI, 115200);  
  qmkInit();

  logBoot(false);
}

void apMain(void)
{
  uint32_t pre_time   = millis();
  bool     is_led_on  = true;

  ledOn(_DEF_LED1);
  while(1)
  {
    if (is_led_on && millis()-pre_time >= 500U)                  // V251124R2: 부팅 후 0.5s 경과 시 LED 1회 소등
    {
      is_led_on = false;
      ledOff(_DEF_LED1);
    }

    cliUpdate();
    usbProcess();                                               // V250924R2 USB 안정성 이벤트 처리
    usbHidMonitorBackgroundService();                           // V251124R1: 모니터 OFF 시 타임스탬프 취득을 건너뛰는 래퍼
    qmkUpdate();
  }
}

void cliUpdate(void)
{
  static uint8_t cli_ch = HW_UART_CH_CLI; 

  if (usbIsOpen() && usbGetType() == USB_CON_CLI)
  {
    cli_ch = HW_UART_CH_USB;
  }
  else
  {
    cli_ch = HW_UART_CH_CLI;
  }
  if (cli_ch != cliGetPort())
  {
    if (cli_ch == HW_UART_CH_USB)
      logPrintf("\nCLI To USB\n");
    else
      logPrintf("\nCLI To UART\n");
    cliOpen(cli_ch, 0);
  }

  cliMain();
}

void cliLoopIdle(void)
{
  qmkUpdate();
}
