#include "usb.h"

#include "tusb.h"
#include "usb_desc.h"


static void usbInitPhy(void);


#define USB_DETACH_DELAY_MS       (100)                          // V260824R1: 호스트가 디태치를 확정할 최소 시간


static bool is_init = false;                                     // V260824R1: USB 클럭 인가 여부 (콜드부트에서는 미초기화 상태)




bool usbInit(void)
{
  usbInitPhy();

  tusb_init();

  is_init = true;                                                // V260824R1: 디태치 가능 상태 표시

  return true;
}

void usbDeInit(void)
{
  // V260824R1: 클럭을 끊기 전에 DCTL.SDIS 로 실제 전기적 디태치를 수행한다.
  //            클럭 게이팅만으로는 PHY 풀업/터미네이션이 유지되어
  //            호스트가 장치 분리를 인식하지 못한다.
  if (is_init == true)
  {
    tud_disconnect();
    delay(USB_DETACH_DELAY_MS);
    is_init = false;
  }

  __HAL_RCC_USB_OTG_HS_CLK_DISABLE();
  __HAL_RCC_USBPHYC_CLK_DISABLE();

  /* USB_OTG_HS interrupt Deinit */
  HAL_NVIC_DisableIRQ(OTG_HS_IRQn);  
}

void usbUpdate(void)
{
  tud_task();
}

void tud_mount_cb(void)
{
  logPrintf("tud_mount_cb()\n");
}

void tud_umount_cb(void)
{
  logPrintf("tud_umount_cb()\n");
}

void usbInitPhy(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};


  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USBPHYC;
  PeriphClkInit.UsbPhycClockSelection = RCC_USBPHYCCLKSOURCE_HSE;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_PWREx_EnableUSBVoltageDetector();

  /* USB_OTG_HS clock enable */
  __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
  __HAL_RCC_USBPHYC_CLK_ENABLE();

  // Disable VBUS sense (B device)
  USB_OTG_HS->GCCFG &= ~USB_OTG_GCCFG_VBDEN;

  // B-peripheral session valid override enable
  USB_OTG_HS->GCCFG |= USB_OTG_GCCFG_VBVALEXTOEN;
  USB_OTG_HS->GCCFG |= USB_OTG_GCCFG_VBVALOVAL;  
}

void OTG_HS_IRQHandler(void)
{
  tud_int_handler(BOARD_TUD_RHPORT);
}