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

void apHeartbeatTouch(void)
{
  bspHeartbeatTouch();                                         // V251123R8: 메인 루프 헬스 체크 갱신
}

void apMain(void)
{
  uint32_t led_pre_time;
  uint32_t dbg_pre_time = 0U;                                    // V251123R7: USB 모니터 상태 주기 로그
  bool is_led_on = true;


  ledOn(_DEF_LED1);

  led_pre_time = millis();
  while(1)
  {
    uint32_t now = millis();
    apHeartbeatTouch();                                          // V251123R8: 루프 생존 갱신

    if (now - led_pre_time >= 500U)                               // V251123R7: CPU 생존 감시용 LED 토글
    {
      led_pre_time = now;
      is_led_on = !is_led_on;
      if (is_led_on)
      {
        ledOn(_DEF_LED1);
      }
      else
      {
        ledOff(_DEF_LED1);
      }
    }

    if (now - dbg_pre_time >= 1000U)                              // V251123R7: USB 모니터/리셋 상태 주기 출력
    {
      dbg_pre_time = now;
      usb_debug_state_t usb_dbg = {0};
      usbDebugGetState(&usb_dbg);
      logPrintf("[DBG] usb mon=%s stage=%u reset=%u\n",
                usb_dbg.monitor_enabled ? "ON" : "OFF",
                usb_dbg.boot_stage,
                usb_dbg.reset_pending ? 1U : 0U);
    }

    cliUpdate();
    usbProcess();                                               // V250924R2 USB 안정성 이벤트 처리
    usbHidMonitorBackgroundTick(micros());                      // V251108R9 SOF 누락 백그라운드 감시
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
