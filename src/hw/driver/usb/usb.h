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

#ifdef BOOTMODE_ENABLE
bool          usbBootModeLoad(void);                    // V250923R1 Load stored boot mode selection
UsbBootMode_t usbBootModeGet(void);                     // V250923R1 Query active boot mode
bool          usbBootModeIsFullSpeed(void);             // V250923R1 Check if FS (1 kHz) mode is requested
uint8_t       usbBootModeGetHsInterval(void);           // V250923R1 Retrieve HS polling interval encoding
bool          usbBootModeStore(UsbBootMode_t mode);     // V251108R1 VIA BootMode 저장 공개
bool          usbBootModeSaveAndReset(UsbBootMode_t mode);
bool          usbBootModeScheduleApply(UsbBootMode_t mode);  // V251108R3: 인터럽트 문맥에서 리셋을 defer
usb_boot_downgrade_result_t usbRequestBootModeDowngrade(UsbBootMode_t mode,
                                                        uint32_t      measured_delta_us,
                                                        uint32_t      expected_us,
                                                        uint32_t      now_ms); // V250924R2 USB 다운그레이드 요청 인터페이스
#else
static inline bool usbBootModeLoad(void)
{
  return true;
}

static inline UsbBootMode_t usbBootModeGet(void)
{
  return USB_BOOT_MODE_HS_8K;
}

static inline bool usbBootModeIsFullSpeed(void)
{
  return false;
}

static inline uint8_t usbBootModeGetHsInterval(void)
{
  return 0x01;
}

static inline bool usbBootModeStore(UsbBootMode_t mode)
{
  (void)mode;
  return false;
}

static inline bool usbBootModeSaveAndReset(UsbBootMode_t mode)
{
  (void)mode;
  return false;
}

static inline bool usbBootModeScheduleApply(UsbBootMode_t mode)
{
  (void)mode;
  return false;
}

static inline usb_boot_downgrade_result_t usbRequestBootModeDowngrade(UsbBootMode_t mode,
                                                                      uint32_t      measured_delta_us,
                                                                      uint32_t      expected_us,
                                                                      uint32_t      now_ms)
{
  (void)mode;
  (void)measured_delta_us;
  (void)expected_us;
  (void)now_ms;
  return USB_BOOT_DOWNGRADE_REJECTED;
}
#endif

#ifdef USB_MONITOR_ENABLE
bool usbInstabilityLoad(void);                          // V251108R1 VIA USB 모니터 토글 로드
bool usbInstabilityStore(bool enable);                  // V251108R1 VIA USB 모니터 토글 저장
bool usbInstabilityIsEnabled(void);                     // V251108R1 USB 모니터 런타임 상태
#else
static inline bool usbInstabilityLoad(void)
{
  return true;
}

static inline bool usbInstabilityStore(bool enable)
{
  (void)enable;
  return false;
}

static inline bool usbInstabilityIsEnabled(void)
{
  return false;
}
#endif

void usbProcess(void);                                  // V250924R2 USB 안정성 모니터 서비스 루프

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
