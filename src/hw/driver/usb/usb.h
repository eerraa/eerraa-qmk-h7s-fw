/*
 * usb.h
 *
 *  Created on: 2018. 3. 16.
 *      Author: HanCheol Cho
 */

#ifndef SRC_HW_USB_CDC_USB_H_
#define SRC_HW_USB_CDC_USB_H_




#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"

#ifdef _USE_HW_USB



#include "usbd_core.h"
#include "usbd_desc.h"

#if HW_USB_CDC == 1
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"
#endif

#if HW_USB_MSC == 1
#include "usbd_msc.h"
#endif

#if HW_USB_HID == 1
#include "usbd_hid.h"
// #include "usbd_hid_if.h"
#endif


typedef enum UsbMode
{
  USB_NON_MODE,
  USB_CDC_MODE,
  USB_MSC_MODE,
  USB_HID_MODE,
  USB_CMP_MODE,
} UsbMode_t;

typedef enum UsbType
{
  USB_CON_CDC = 0,
  USB_CON_CLI = 1,
  USB_CON_CAN = 2,
  USB_CON_ESP = 3,
  USB_CON_HID = 4,
} UsbType_t;

typedef enum UsbBootMode                               // V250923R1 Persisted USB polling profile
{
  USB_BOOT_MODE_HS_8K = 0,
  USB_BOOT_MODE_HS_4K,
  USB_BOOT_MODE_HS_2K,
  USB_BOOT_MODE_FS_1K,
  USB_BOOT_MODE_MAX,
} UsbBootMode_t;

#define USB_BOOT_MONITOR_CONFIRM_DELAY_MS (2000U)

typedef enum
{
  USB_BOOT_DOWNGRADE_REJECTED = 0,
  USB_BOOT_DOWNGRADE_ARMED,
  USB_BOOT_DOWNGRADE_CONFIRMED,
} usb_boot_downgrade_result_t;

bool         usbBootModeLoad(void);                    // V250923R1 Load stored boot mode selection
UsbBootMode_t usbBootModeGet(void);                    // V250923R1 Query active boot mode
bool         usbBootModeIsFullSpeed(void);             // V250923R1 Check if FS (1 kHz) mode is requested
uint8_t      usbBootModeGetHsInterval(void);           // V250923R1 Retrieve HS polling interval encoding
bool         usbBootModeSaveAndReset(UsbBootMode_t mode);
usb_boot_downgrade_result_t usbRequestBootModeDowngrade(UsbBootMode_t mode,
                                                        uint32_t      measured_delta_us,
                                                        uint16_t      expected_us,
                                                        uint16_t      missed_frames,
                                                        uint32_t      now_ms); // V251005R9 ISR 포화 값을 직접 전달하는 다운그레이드 요청 인터페이스
void         usbProcess(void);                         // V250924R2 USB 안정성 모니터 서비스 루프

static inline uint32_t usbCalcMissedFrames(uint32_t expected_us,
                                           uint32_t delta_us)        // V251005R6 속도별 상수 나눗셈 분기로 누락 프레임 산출 경량화
{
  switch (expected_us)
  {
    case 125U:
      return delta_us / 125U;
    case 250U:
      return delta_us / 250U;
    case 500U:
      return delta_us / 500U;
    case 1000U:
      return delta_us / 1000U;
    default:
      return (expected_us > 0U) ? (delta_us / expected_us) : 0U;
  }
}

bool usbInit(void);
bool usbBegin(UsbMode_t usb_mode);
void usbDeInit(void);
bool usbIsOpen(void);
bool usbIsConnect(void);
bool usbIsSuspended(void);

UsbMode_t usbGetMode(void);
UsbType_t usbGetType(void);


#endif


#ifdef __cplusplus
}
#endif




#endif /* SRC_HW_USB_CDC_USB_H_ */
