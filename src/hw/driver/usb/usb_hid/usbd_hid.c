/**
  ******************************************************************************
  * @file    usbd_hid.c
  * @author  MCD Application Team
  * @brief   This file provides the HID core functions.
  *
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  * @verbatim
  *
  *          ===================================================================
  *                                HID Class  Description
  *          ===================================================================
  *           This module manages the HID class V1.11 following the "Device Class Definition
  *           for Human Interface Devices (HID) Version 1.11 Jun 27, 2001".
  *           This driver implements the following aspects of the specification:
  *             - The Boot Interface Subclass
  *             - The Mouse protocol
  *             - Usage Page : Generic Desktop
  *             - Usage : Joystick
  *             - Collection : Application
  *
  * @note     In HS mode and when the DMA is used, all variables and data structures
  *           dealing with the DMA during the transaction process should be 32-bit aligned.
  *
  *
  *  @endverbatim
  *
  ******************************************************************************
  */


#include "usbd_hid.h"
#include "usbd_ctlreq.h"
#include "usbd_desc.h"
#include "usb.h"                                                // V250923R1 Boot mode aware intervals
#include <string.h>                                             // V251108R8: VIA 큐 헬퍼에서 memset 사용

#include "log.h"
#include "qbuffer.h"
#include "report.h"
#include "micros.h"
#include "usbd_hid_internal.h"
#include "usb_diagnostics.h"             // V260823R2: 실제 HID 전달/하드 이벤트 진단 코어


#if HW_USB_LOG == 1
#define logDebug(...)                              \
  {                                                \
    if (HW_LOG_CH == HW_UART_CH_USB) logDisable(); \
    logPrintf(__VA_ARGS__);                        \
    if (HW_LOG_CH == HW_UART_CH_USB) logEnable();  \
  }
#else
#define logDebug(...) 
#endif

static uint8_t USBD_HID_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_HID_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_HID_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t USBD_HID_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_HID_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_HID_EP0_RxReady(USBD_HandleTypeDef *pdev);
static uint8_t USBD_HID_SOF(USBD_HandleTypeDef *pdev);

#ifndef USE_USBD_COMPOSITE
static uint8_t *USBD_HID_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_HID_GetHSCfgDesc(uint16_t *length);
static uint8_t *USBD_HID_GetOtherSpeedCfgDesc(uint16_t *length);
static uint8_t *USBD_HID_GetDeviceQualifierDesc(uint16_t *length);
#endif /* USE_USBD_COMPOSITE  */

#if (USBD_SUPPORT_USER_STRING_DESC == 1U)
static uint8_t *USBD_HID_GetUsrStrDescriptor(struct _USBD_HandleTypeDef *pdev, uint8_t index,  uint16_t *length);
#endif


static bool usbHidUpdateWakeUp(USBD_HandleTypeDef *pdev);
static void usbHidInitTimer(void);
static uint32_t usbHidBackupTimerOffsetUs(void);                       // V251012R1 FS 백업 전송 지연 재조정


// V260824R1: 전송 시작은 메인 루프와 TIM2 ISR 양쪽에서 들어오므로 검사와 점유를
//            분리하면 두 컨텍스트가 같은 엔드포인트를 이중으로 무장할 수 있다.
//            PRIMASK 임계구역으로 test-and-set을 원자화한다(약 10 cycle).
static bool usbHidEpTryAcquire(USBD_HID_HandleTypeDef *hhid, uint8_t ep_addr)
{
  uint8_t  index = (uint8_t)(ep_addr & 0x0FU);
  uint32_t primask;
  bool     acquired = false;

  if (hhid == NULL)
  {
    return false;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  if (hhid->in_ep_busy[index] == 0U)
  {
    hhid->in_ep_busy[index] = 1U;
    acquired = true;
  }
  if (primask == 0U)
  {
    __enable_irq();
  }

  return acquired;
}

static void usbHidEpRelease(USBD_HID_HandleTypeDef *hhid, uint8_t ep_addr)
{
  if (hhid != NULL)
  {
    hhid->in_ep_busy[ep_addr & 0x0FU] = 0U;                            // V260824R1: 해당 EP만 해제
  }
}


typedef struct
{
  uint32_t diagnostic_request_us;
  uint16_t diagnostic_session_id;
  uint8_t  buf[HID_KEYBOARD_REPORT_SIZE];
} report_info_t;  // V260823R2: 큐 대기까지 포함한 요청→DataIn 지연을 보존

_Static_assert(sizeof(report_info_t) == (HID_KEYBOARD_REPORT_SIZE + 6U),
               "진단 메타데이터가 키보드 리포트 큐를 불필요하게 패딩한다.");  // V260823R2

typedef struct
{
  uint8_t  buf[32];
} via_report_info_t;

typedef struct
{
  uint8_t len;
  uint8_t buf[HID_EXK_EP_SIZE];
} exk_report_info_t;

static USBD_SetupReqTypedef ep0_req;
static uint8_t ep0_req_buf[USB_MAX_EP0_SIZE];

static qbuffer_t             via_report_q;
static via_report_info_t     via_report_q_buf[128];
__ALIGN_BEGIN static uint8_t via_hid_usb_rx_report[32] __ALIGN_END;
__ALIGN_BEGIN static uint8_t via_hid_usb_tx_report[32] __ALIGN_END;   // V260901R1: 즉시 OUT 재무장과 IN 전송이 같은 버퍼를 공유하지 않게 분리
static void (*via_hid_receive_func)(uint8_t *data, uint8_t length) = NULL;


static qbuffer_t              report_q;
static report_info_t          report_buf[128];
__ALIGN_BEGIN  static uint8_t hid_buf[HID_KEYBOARD_REPORT_SIZE] __ALIGN_END = {0,};

static qbuffer_t              report_exk_q;
static exk_report_info_t      report_exk_buf[128];
__ALIGN_BEGIN  static uint8_t hid_buf_exk[HID_EXK_EP_SIZE] __ALIGN_END = {0,};



USBD_ClassTypeDef USBD_HID =
{
  USBD_HID_Init,
  USBD_HID_DeInit,
  USBD_HID_Setup,
  NULL,                 /* EP0_TxSent */
  USBD_HID_EP0_RxReady, /* EP0_RxReady */
  USBD_HID_DataIn,      /* DataIn */
  USBD_HID_DataOut,     /* DataOut */
  USBD_HID_SOF,         /* SOF */
  NULL,
  NULL,
#ifdef USE_USBD_COMPOSITE
  NULL,
  NULL,
  NULL,
  NULL,
#else
  USBD_HID_GetHSCfgDesc,
  USBD_HID_GetFSCfgDesc,
  USBD_HID_GetOtherSpeedCfgDesc,
  USBD_HID_GetDeviceQualifierDesc,
#endif /* USE_USBD_COMPOSITE  */

#if (USBD_SUPPORT_USER_STRING_DESC == 1U)
  USBD_HID_GetUsrStrDescriptor,
#endif 
};

#ifndef USE_USBD_COMPOSITE
/* USB HID device FS Configuration Descriptor */
__ALIGN_BEGIN static uint8_t USBD_HID_CfgDesc[USB_HID_CONFIG_DESC_SIZ] __ALIGN_END =
{
  0x09,                                               /* bLength: Configuration Descriptor size */
  USB_DESC_TYPE_CONFIGURATION,                        /* bDescriptorType: Configuration */
  USB_HID_CONFIG_DESC_SIZ,                            /* wTotalLength: Bytes returned */
  0x00,
  0x03,                                               /* bNumInterfaces: 3 interface */
  0x01,                                               /* bConfigurationValue: Configuration value */
  0x00,                                               /* iConfiguration: Index of string descriptor
                                                         describing the configuration */
#if (USBD_SELF_POWERED == 1U)
  0xE0,                                               /* bmAttributes: Bus Powered according to user configuration */
#else
  0xA0,                                               /* bmAttributes: Bus Powered according to user configuration */
#endif /* USBD_SELF_POWERED */
  USBD_MAX_POWER,                                     /* MaxPower (mA) */

  /************** Descriptor of Keyboard interface ****************/
  /* 09 */
  0x09,                                               /* bLength: Interface Descriptor size */
  USB_DESC_TYPE_INTERFACE,                            /* bDescriptorType: Interface descriptor type */
  0x00,                                               /* bInterfaceNumber: Number of Interface */
  0x00,                                               /* bAlternateSetting: Alternate setting */
  0x01,                                               /* bNumEndpoints */
  0x03,                                               /* bInterfaceClass: HID */
  0x01,                                               /* bInterfaceSubClass : 1=BOOT, 0=no boot */
  0x01,                                               /* nInterfaceProtocol : 0=none, 1=keyboard, 2=mouse */
  0,                                                  /* iInterface: Index of string descriptor */
  /******************** Descriptor of Keyboard HID ********************/
  /* 18 */
  0x09,                                               /* bLength: HID Descriptor size */
  HID_DESCRIPTOR_TYPE,                                /* bDescriptorType: HID */
  0x11,                                               /* bcdHID: HID Class Spec release number */
  0x01,
  0x00,                                               /* bCountryCode: Hardware target country */
  0x01,                                               /* bNumDescriptors: Number of HID class descriptors to follow */
  0x22,                                               /* bDescriptorType */
  HID_KEYBOARD_REPORT_DESC_SIZE,                      /* wItemLength: Total length of Report descriptor */
  0x00,
  /******************** Descriptor of Keyboard endpoint ********************/
  /* 27 */
  0x07,                                               /* bLength: Endpoint Descriptor size */
  USB_DESC_TYPE_ENDPOINT,                             /* bDescriptorType:*/

  HID_EPIN_ADDR,                                      /* bEndpointAddress: Endpoint Address (IN) */
  0x03,                                               /* bmAttributes: Interrupt endpoint */
  HID_EPIN_SIZE,                                      /* wMaxPacketSize: */
  0x00,
  HID_HS_BINTERVAL,                                   /* bInterval: Polling Interval */
  /* 34 */


  /*---------------------------------------------------------------------------*/
  /* VIA interface descriptor */
  0x09,                                               /* bLength: Endpoint Descriptor size */
  USB_DESC_TYPE_INTERFACE,                            /* bDescriptorType: */
  0x01,                                               /* bInterfaceNumber: Number of Interface */
  0x00,                                               /* bAlternateSetting: Alternate setting */
  0x02,                                               /* bNumEndpoints: Two endpoints used */
  0x03,                                               /* bInterfaceClass: HID */
  0x00,                                               /* bInterfaceSubClass : 1=BOOT, 0=no boot */
  0x00,                                               /* nInterfaceProtocol : 0=none, 1=keyboard, 2=mouse */
  0x00,                                               /* iInterface */

  /******************** Descriptor of VIA ********************/
  /* 43 */
  0x09,                                               /* bLength: HID Descriptor size */
  HID_DESCRIPTOR_TYPE,                                /* bDescriptorType: HID */
  0x11,                                               /* bcdHID: HID Class Spec release number */
  0x01,
  0x00,                                               /* bCountryCode: Hardware target country */
  0x01,                                               /* bNumDescriptors: Number of HID class descriptors to follow */
  0x22,                                               /* bDescriptorType */
  HID_KEYBOARD_VIA_REPORT_DESC_SIZE,                  /* wItemLength: Total length of Report descriptor */
  0x00,

  /******************** Descriptor of VIA endpoint ********************/
  /* 52 */
  0x07,                                               /* bLength: Endpoint Descriptor size */
  USB_DESC_TYPE_ENDPOINT,                             /* bDescriptorType:*/
  HID_VIA_EP_IN,                                      /* bEndpointAddress: Endpoint Address (IN) */
  USBD_EP_TYPE_INTR,                                  /* bmAttributes: Interrupt endpoint */
  HID_VIA_EP_SIZE,                                    /* wMaxPacketSize: */
  0x00,
  HID_FS_BINTERVAL,                                   /* V260901R1: FS VIA IN도 1ms polling */

  /* 59 */
  0x07,                                               /* bLength: Endpoint Descriptor size */
  USB_DESC_TYPE_ENDPOINT,                             /* bDescriptorType:*/
  HID_VIA_EP_OUT,                                     /* bEndpointAddress: Endpoint Address (OUT) */
  USBD_EP_TYPE_INTR,                                  /* bmAttributes: Interrupt endpoint */
  HID_VIA_EP_SIZE,                                    /* wMaxPacketSize: */
  0x00,
  HID_FS_BINTERVAL,                                   /* V260901R1: FS VIA OUT도 1ms polling */
  /* 66 */


  /*---------------------------------------------------------------------------*/
  /* EXK interface descriptor */
  0x09,                                               /* bLength: Endpoint Descriptor size */
  USB_DESC_TYPE_INTERFACE,                            /* bDescriptorType: */
  0x02,                                               /* bInterfaceNumber: Number of Interface */
  0x00,                                               /* bAlternateSetting: Alternate setting */
  0x01,                                               /* bNumEndpoints: One endpoint used */
  0x03,                                               /* bInterfaceClass: HID */
  0x01,                                               /* bInterfaceSubClass : 1=BOOT, 0=no boot */
  0x00,                                               /* nInterfaceProtocol : 0=none, 1=keyboard, 2=mouse */
  0x00,                                               /* iInterface */

  /******************** Descriptor of EXK ********************/
  /* 75 */
  0x09,                                               /* bLength: HID Descriptor size */
  HID_DESCRIPTOR_TYPE,                                /* bDescriptorType: HID */
  0x11,                                               /* bcdHID: HID Class Spec release number */
  0x01,
  0x00,                                               /* bCountryCode: Hardware target country */
  0x01,                                               /* bNumDescriptors: Number of HID class descriptors to follow */
  0x22,                                               /* bDescriptorType */
  HID_EXK_REPORT_DESC_SIZE,                           /* wItemLength: Total length of Report descriptor */
  0x00,

  /******************** Descriptor of EXK endpoint ********************/
  /* 84 */
  0x07,                                               /* bLength: Endpoint Descriptor size */
  USB_DESC_TYPE_ENDPOINT,                             /* bDescriptorType:*/
  HID_EXK_EP_IN,                                      /* bEndpointAddress: Endpoint Address (IN) */
  USBD_EP_TYPE_INTR,                                  /* bmAttributes: Interrupt endpoint */
  HID_EXK_EP_SIZE,                                    /* wMaxPacketSize: */
  0x00,
  HID_HS_BINTERVAL,                                   /* bInterval: Polling Interval */
  /* 91 */

};
#endif /* USE_USBD_COMPOSITE  */

/* USB HID device Configuration Descriptor */
__ALIGN_BEGIN static uint8_t USBD_HID_Desc[USB_HID_DESC_SIZ] __ALIGN_END =
{
  /* 18 */
  0x09,                                               /* bLength: HID Descriptor size */
  HID_DESCRIPTOR_TYPE,                                /* bDescriptorType: HID */
  0x11,                                               /* bcdHID: HID Class Spec release number */
  0x01,
  0x00,                                               /* bCountryCode: Hardware target country */
  0x01,                                               /* bNumDescriptors: Number of HID class descriptors to follow */
  0x22,                                               /* bDescriptorType */
  HID_KEYBOARD_REPORT_DESC_SIZE,                      /* wItemLength: Total length of Report descriptor */
  0x00,
};

#ifndef USE_USBD_COMPOSITE
/* USB Standard Device Descriptor */
__ALIGN_BEGIN static uint8_t USBD_HID_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
{
  USB_LEN_DEV_QUALIFIER_DESC,
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00,
  0x02,
  0x00,
  0x00,
  0x00,
  0x40,
  0x01,
  0x00,
};
#endif /* USE_USBD_COMPOSITE  */

__ALIGN_BEGIN static uint8_t HID_KEYBOARD_ReportDesc[HID_KEYBOARD_REPORT_DESC_SIZE] __ALIGN_END =
{
  0x05, 0x01,                         // USAGE_PAGE (Generic Desktop)
  0x09, 0x06,                         // USAGE (Keyboard)
  0xa1, 0x01,                         // COLLECTION (Application)
  0x05, 0x07,                         //   USAGE_PAGE (Keyboard)
  0x19, 0xe0,                         //   USAGE_MINIMUM (Keyboard LeftControl)
  0x29, 0xe7,                         //   USAGE_MAXIMUM (Keyboard Right GUI)
  0x15, 0x00,                         //   LOGICAL_MINIMUM (0)
  0x25, 0x01,                         //   LOGICAL_MAXIMUM (1)
  0x75, 0x01,                         //   REPORT_SIZE (1)
  0x95, 0x08,                         //   REPORT_COUNT (8)
  0x81, 0x02,                         //   INPUT (Data,Var,Abs)
  0x95, 0x01,                         //   REPORT_COUNT (1)
  0x75, 0x08,                         //   REPORT_SIZE (8)
  0x81, 0x03,                         //   INPUT (Cnst,Var,Abs)
  0x95, 0x05,                         //   REPORT_COUNT (5)
  0x75, 0x01,                         //   REPORT_SIZE (1)
  0x05, 0x08,                         //   USAGE_PAGE (LEDs)
  0x19, 0x01,                         //   USAGE_MINIMUM (Num Lock)
  0x29, 0x05,                         //   USAGE_MAXIMUM (Kana)
  0x91, 0x02,                         //   OUTPUT (Data,Var,Abs)
  0x95, 0x01,                         //   REPORT_COUNT (1)
  0x75, 0x03,                         //   REPORT_SIZE (3)
  0x91, 0x03,                         //   OUTPUT (Cnst,Var,Abs)
  0x95, HW_KEYS_PRESS_MAX,            //   REPORT_COUNT (6)
  0x75, 0x08,                         //   REPORT_SIZE (8)
  0x15, 0x00,                         //   LOGICAL_MINIMUM (0)
  0x26, 0xFF, 0x00,                   //   LOGICAL_MAXIMUM (255)
  0x05, 0x07,                         //   USAGE_PAGE (Keyboard)
  0x19, 0x00,                         //   USAGE_MINIMUM (Reserved (no event indicated))
  0x29, 0xFF,                         //   USAGE_MAXIMUM (Keyboard Application)
  0x81, 0x00,                         //   INPUT (Data,Ary,Abs)
  0xc0                                // END_COLLECTION
};

__ALIGN_BEGIN static uint8_t HID_VIA_ReportDesc[HID_KEYBOARD_VIA_REPORT_DESC_SIZE] __ALIGN_END = 
{
  //
  0x06, 0x60, 0xFF, // Usage Page (Vendor Defined)
  0x09, 0x61,       // Usage (Vendor Defined)
  0xA1, 0x01,       // Collection (Application)
  // Data to host
  0x09, 0x62,       //   Usage (Vendor Defined)
  0x15, 0x00,       //   Logical Minimum (0)
  0x26, 0xFF, 0x00, //   Logical Maximum (255)
  0x95, 32,         //   Report Count
  0x75, 0x08,       //   Report Size (8)
  0x81, 0x02,       //   Input (Data, Variable, Absolute)
  // Data from host
  0x09, 0x63,       //   Usage (Vendor Defined)
  0x15, 0x00,       //   Logical Minimum (0)
  0x26, 0xFF, 0x00, //   Logical Maximum (255)
  0x95, 32,         //   Report Count
  0x75, 0x08,       //   Report Size (8)
  0x91, 0x02,       //   Output (Data, Variable, Absolute)
  0xC0              // End Collection
};

__ALIGN_BEGIN static uint8_t HID_EXK_ReportDesc[HID_EXK_REPORT_DESC_SIZE] __ALIGN_END =
{
  //
  0x05, 0x01,               // Usage Page (Generic Desktop)
  0x09, 0x80,               // Usage (System Control)
  0xA1, 0x01,               // Collection (Application)
  0x85, REPORT_ID_SYSTEM,   //   Report ID
  0x19, 0x01,               //   Usage Minimum (Pointer)
  0x2A, 0xB7, 0x00,         //   Usage Maximum (System Display LCD Autoscale)
  0x15, 0x01,               //   Logical Minimum
  0x26, 0xB7, 0x00,         //   Logical Maximum
  0x95, 0x01,               //   Report Count (1)
  0x75, 0x10,               //   Report Size (16)
  0x81, 0x00,               //   Input (Data, Array, Absolute)
  0xC0,                     // End Collection

  0x05, 0x0C,               // Usage Page (Consumer)
  0x09, 0x01,               // Usage (Consumer Control)
  0xA1, 0x01,               // Collection (Application)
  0x85, REPORT_ID_CONSUMER, //   Report ID
  0x19, 0x01,               //   Usage Minimum (Consumer Control)
  0x2A, 0xA0, 0x02,         //   Usage Maximum (AC Desktop Show All Applications)
  0x15, 0x01,               //   Logical Minimum
  0x26, 0xA0, 0x02,         //   Logical Maximum
  0x95, 0x01,               //   Report Count (1)
  0x75, 0x10,               //   Report Size (16)
  0x81, 0x00,               //   Input (Data, Array, Absolute)
  0xC0,                     // End Collection

  // V260823R1: MOUSE 리포트 (report_mouse_t = report_id + buttons + x + y + v + h = 6B)
  0x05, 0x01,               // Usage Page (Generic Desktop)
  0x09, 0x02,               // Usage (Mouse)
  0xA1, 0x01,               // Collection (Application)
  0x85, REPORT_ID_MOUSE,    //   Report ID
  0x09, 0x01,               //   Usage (Pointer)
  0xA1, 0x00,               //   Collection (Physical)
  0x05, 0x09,               //     Usage Page (Button)
  0x19, 0x01,               //     Usage Minimum (Button 1)
  0x29, 0x05,               //     Usage Maximum (Button 5)
  0x15, 0x00,               //     Logical Minimum (0)
  0x25, 0x01,               //     Logical Maximum (1)
  0x95, 0x05,               //     Report Count (5)
  0x75, 0x01,               //     Report Size (1)
  0x81, 0x02,               //     Input (Data, Variable, Absolute)
  0x95, 0x01,               //     Report Count (1)
  0x75, 0x03,               //     Report Size (3)
  0x81, 0x03,               //     Input (Constant)
  0x05, 0x01,               //     Usage Page (Generic Desktop)
  0x09, 0x30,               //     Usage (X)
  0x09, 0x31,               //     Usage (Y)
  0x15, 0x81,               //     Logical Minimum (-127)
  0x25, 0x7F,               //     Logical Maximum (127)
  0x95, 0x02,               //     Report Count (2)
  0x75, 0x08,               //     Report Size (8)
  0x81, 0x06,               //     Input (Data, Variable, Relative)
  0x09, 0x38,               //     Usage (Wheel)
  0x15, 0x81,               //     Logical Minimum (-127)
  0x25, 0x7F,               //     Logical Maximum (127)
  0x95, 0x01,               //     Report Count (1)
  0x75, 0x08,               //     Report Size (8)
  0x81, 0x06,               //     Input (Data, Variable, Relative)
  0x05, 0x0C,               //     Usage Page (Consumer)
  0x0A, 0x38, 0x02,         //     Usage (AC Pan)
  0x15, 0x81,               //     Logical Minimum (-127)
  0x25, 0x7F,               //     Logical Maximum (127)
  0x95, 0x01,               //     Report Count (1)
  0x75, 0x08,               //     Report Size (8)
  0x81, 0x06,               //     Input (Data, Variable, Relative)
  0xC0,                     //   End Collection
  0xC0                      // End Collection
};

// V260823R1: 리포트 디스크립터가 선언한 크기와 QMK 구조체 크기는 반드시 같아야 한다.
//            어긋나면 호스트가 리포트를 잘못 해석하므로 링크가 아니라 컴파일에서 막는다.
#ifdef MOUSE_SHARED_EP
_Static_assert(sizeof(report_mouse_t) == 6U, "MOUSE 리포트 디스크립터(6B)와 report_mouse_t 크기가 다르다.");
_Static_assert(sizeof(report_mouse_t) <= HID_EXK_EP_SIZE, "MOUSE 리포트가 EXK 엔드포인트 크기를 넘는다.");
#endif
_Static_assert(sizeof(report_extra_t) == 3U, "SYSTEM/CONSUMER 리포트 디스크립터(3B)와 report_extra_t 크기가 다르다.");

static USBD_HID_HandleTypeDef *p_hhid = NULL;
static uint8_t HIDInEpAdd = HID_EPIN_ADDR;
extern USBD_HandleTypeDef USBD_Device;
static TIM_HandleTypeDef htim2;

// V260823R2: ST USB 속도 값을 진단 계약의 안정된 값으로 정규화한다.
static uint8_t usbHidDiagnosticsSpeedCode(uint8_t speed)
{
  if (speed == USBD_SPEED_HIGH)
  {
    return USB_DIAGNOSTICS_SPEED_HIGH;
  }
  if (speed == USBD_SPEED_FULL)
  {
    return USB_DIAGNOSTICS_SPEED_FULL;
  }
  return USB_DIAGNOSTICS_SPEED_UNKNOWN;
}


/**
  * @brief  USBD_HID_Init
  *         Initialize the HID interface
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t USBD_HID_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);

  USBD_HID_HandleTypeDef *hhid;

  hhid = (USBD_HID_HandleTypeDef *)USBD_malloc(sizeof(USBD_HID_HandleTypeDef));

  if (hhid == NULL)
  {
    pdev->pClassDataCmsit[pdev->classId] = NULL;
    return (uint8_t)USBD_EMEM;
  }

  p_hhid = hhid;

  pdev->pClassDataCmsit[pdev->classId] = (void *)hhid;
  pdev->pClassData = pdev->pClassDataCmsit[pdev->classId];

  uint8_t hs_interval = usbBootModeGetHsInterval();                      // V250923R1 Dynamic HS polling interval


#ifdef USE_USBD_COMPOSITE
  /* Get the Endpoints addresses allocated for this class instance */
  HIDInEpAdd  = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_INTR, (uint8_t)pdev->classId);
#endif /* USE_USBD_COMPOSITE */
  pdev->ep_in[HIDInEpAdd & 0xFU].bInterval = pdev->dev_speed == USBD_SPEED_HIGH ? hs_interval:HID_FS_BINTERVAL;

  /* Open EP IN */
  (void)USBD_LL_OpenEP(pdev, HIDInEpAdd, USBD_EP_TYPE_INTR, HID_EPIN_SIZE);
  pdev->ep_in[HIDInEpAdd & 0xFU].is_used = 1U;


  // VIA EP
  //
  pdev->ep_in[HID_VIA_EP_IN & 0xFU].bInterval = pdev->dev_speed == USBD_SPEED_HIGH ? hs_interval:HID_FS_BINTERVAL;
  (void)USBD_LL_OpenEP(pdev, HID_VIA_EP_IN, USBD_EP_TYPE_INTR, HID_VIA_EP_SIZE);
  pdev->ep_in[HID_VIA_EP_IN & 0xFU].is_used = 1U;

  pdev->ep_out[HID_VIA_EP_OUT & 0xFU].bInterval = pdev->dev_speed == USBD_SPEED_HIGH ? hs_interval:HID_FS_BINTERVAL;  // V260901R1: OUT 메타데이터는 ep_out 소유
  (void)USBD_LL_OpenEP(pdev, HID_VIA_EP_OUT, USBD_EP_TYPE_INTR, HID_VIA_EP_SIZE);
  pdev->ep_out[HID_VIA_EP_OUT & 0xFU].is_used = 1U;                    // V260901R1: ST core의 OUT endpoint 소유권과 일치

  // EXK EP
  //
  pdev->ep_in[HID_EXK_EP_IN & 0xFU].bInterval = pdev->dev_speed == USBD_SPEED_HIGH ? hs_interval:HID_FS_BINTERVAL;
  (void)USBD_LL_OpenEP(pdev, HID_EXK_EP_IN, USBD_EP_TYPE_INTR, HID_EXK_EP_SIZE);
  pdev->ep_in[HID_EXK_EP_IN & 0xFU].is_used = 1U;


  memset((void *)hhid->in_ep_busy, 0, sizeof(hhid->in_ep_busy));       // V260824R1: 구성 시 전 EP 해제

  /* Prepare Out endpoint to receive next packet */
  (void)USBD_LL_PrepareReceive(pdev, HID_VIA_EP_OUT, via_hid_usb_rx_report, sizeof(via_hid_usb_rx_report));


  static bool is_first = true;
  if (is_first)
  {
    is_first = false;

    qbufferCreateBySize(&report_q, (uint8_t *)report_buf, sizeof(report_info_t), 128); 
    qbufferCreateBySize(&via_report_q, (uint8_t *)via_report_q_buf, sizeof(via_report_info_t), 128); 
    // V260823R1: 원소 크기를 exk_report_info_t로 교정. 기존의 report_info_t는 HW_KEYS_PRESS_MAX+2(22)라
    //            실제 원소 9바이트보다 커서, qbufferRead가 스택의 9바이트 지역변수에 22바이트를 썼다.
    qbufferCreateBySize(&report_exk_q, (uint8_t *)report_exk_buf, sizeof(exk_report_info_t), 128); 

    logPrintf("[OK] USB Hid\n");
    logPrintf("     Keyboard\n");

    usbHidInitTimer();
  }

  // V260823R2: 최초 구성과 재구성을 항상 카운트하되 타임스탬프는 세션 중에만 읽는다.
  usbDiagnosticsOnUsbConfigured(usbDiagnosticsIsActive() ? micros() : 0U,
                                usbHidDiagnosticsSpeedCode(pdev->dev_speed));

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_HID_DeInit
  *         DeInitialize the HID layer
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t USBD_HID_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);

#ifdef USE_USBD_COMPOSITE
  /* Get the Endpoints addresses allocated for this class instance */
  HIDInEpAdd  = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_INTR, (uint8_t)pdev->classId);
#endif /* USE_USBD_COMPOSITE */

  /* Close HID EPs */
  (void)USBD_LL_CloseEP(pdev, HIDInEpAdd);
  pdev->ep_in[HIDInEpAdd & 0xFU].is_used = 0U;
  pdev->ep_in[HIDInEpAdd & 0xFU].bInterval = 0U;

  /* Free allocated memory */
  if (pdev->pClassDataCmsit[pdev->classId] != NULL)
  {
    (void)USBD_free(pdev->pClassDataCmsit[pdev->classId]);
    pdev->pClassDataCmsit[pdev->classId] = NULL;
  }

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_HID_Setup
  *         Handle the HID specific requests
  * @param  pdev: instance
  * @param  req: usb requests
  * @retval status
  */
static uint8_t USBD_HID_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_HID_HandleTypeDef *hhid = (USBD_HID_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
  USBD_StatusTypeDef ret = USBD_OK;
  uint16_t len;
  uint8_t *pbuf;
  uint16_t status_info = 0U;

  if (hhid == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  logDebug("HID_SETUP %d\n", pdev->classId);
  logDebug("  req->bmRequest : 0x%X\n", req->bmRequest);
  logDebug("  req->bRequest  : 0x%X\n", req->bRequest);
  logDebug("       wIndex    : 0x%X\n", req->wIndex);
  logDebug("       wLength   : 0x%X %d\n", req->wLength, req->wLength);

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS :
      switch (req->bRequest)
      {
        case USBD_HID_REQ_SET_PROTOCOL:
          logDebug("  USBD_HID_REQ_SET_PROTOCOL  : 0x%X, 0x%d\n", req->wValue, req->wLength);      
          hhid->Protocol = (uint8_t)(req->wValue);
          break;

        case USBD_HID_REQ_GET_PROTOCOL:
          logDebug("  USBD_HID_REQ_GET_PROTOCOL  : 0x%X, 0x%d\n", req->wValue, req->wLength);      
          (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->Protocol, 1U);
          break;

        case USBD_HID_REQ_SET_IDLE:
          logDebug("  USBD_HID_REQ_SET_IDLE  : 0x%X, 0x%d\n", req->wValue, req->wLength);      
          hhid->IdleState = (uint8_t)(req->wValue >> 8);
          break;

        case USBD_HID_REQ_GET_IDLE:
          logDebug("  USBD_HID_REQ_GET_IDLE  : 0x%X, 0x%d\n", req->wValue, req->wLength);          
          (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->IdleState, 1U);
          break;

        case USBD_HID_REQ_SET_REPORT:  
          logDebug("  USBD_HID_REQ_SET_REPORT  : 0x%X, 0x%d\n", req->wValue, req->wLength);     
          ep0_req = *req;
          USBD_CtlPrepareRx(pdev, ep0_req_buf, req->wLength);
          break;

        default:
          logDebug("  ERROR  : 0x%X\n", req->wValue); 
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;
    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest)
      {
        case USB_REQ_GET_STATUS:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            (void)USBD_CtlSendData(pdev, (uint8_t *)&status_info, 2U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_GET_DESCRIPTOR:
          logDebug("  USB_REQ_GET_DESCRIPTOR  : 0x%X\n", req->wValue); 
          if ((req->wValue >> 8) == HID_REPORT_DESC)
          {
            switch(req->wIndex)
            {
              case 1:
                len = MIN(HID_KEYBOARD_VIA_REPORT_DESC_SIZE, req->wLength);
                pbuf = HID_VIA_ReportDesc;
                break;

              case 2:
                len = MIN(HID_EXK_REPORT_DESC_SIZE, req->wLength);
                pbuf = HID_EXK_ReportDesc;
                break;

              default:
                len = MIN(HID_KEYBOARD_REPORT_DESC_SIZE, req->wLength);
                pbuf = HID_KEYBOARD_ReportDesc;
              break;
            }
          }
          else if ((req->wValue >> 8) == HID_DESCRIPTOR_TYPE)
          {
            pbuf = USBD_HID_Desc;
            len = MIN(USB_HID_DESC_SIZ, req->wLength);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
            break;
          }
          (void)USBD_CtlSendData(pdev, pbuf, len);
          break;

        case USB_REQ_GET_INTERFACE :
          logDebug("  USB_REQ_GET_INTERFACE  : 0x%X\n", req->wValue); 
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->AltSetting, 1U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_SET_INTERFACE:
          logDebug("  USB_REQ_SET_INTERFACE  : 0x%X\n", req->wValue); 
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            hhid->AltSetting = (uint8_t)(req->wValue);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_CLEAR_FEATURE:
          logDebug("  USB_REQ_CLEAR_FEATURE  : 0x%X\n", req->wValue); 
          break;

        default:
          logDebug("  ERROR  : 0x%X\n", req->wValue); 
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;

    default:
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;
  }

  return (uint8_t)ret;
}

/**
  * @brief  USBD_HID_EP0_RxReady
  *         handle EP0 Rx Ready event
  * @param  pdev: device instance
  * @retval status
  */
uint8_t USBD_HID_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  logDebug("USBD_HID_EP0_RxReady()\n");
  logDebug("  req->bmRequest : 0x%X\n", ep0_req.bmRequest);
  logDebug("  req->bRequest  : 0x%X\n", ep0_req.bRequest);
  logDebug("  %d \n", ep0_req.wLength);
  for (int i=0; i<ep0_req.wLength; i++)
  {
    logDebug("  %d : 0x%02X\n", i, ep0_req_buf[i]);
  }

  if (ep0_req.bRequest == USBD_HID_REQ_SET_REPORT)
  {
    uint8_t led_bits = ep0_req_buf[0];

    usbHidSetStatusLed(led_bits);
  }
  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_HID_SendReport
  *         Send HID Report
  * @param  buff: pointer to report
  * @retval status
  */
bool USBD_HID_SendReport(uint8_t *report,
                         uint16_t len,
                         uint32_t diagnostic_request_us,
                         uint16_t diagnostic_session_id,
                         uint16_t queued_reports)
{
  USBD_HandleTypeDef *pdev = &USBD_Device;
  bool ret = false;

  if (p_hhid == NULL)
  {
    return false;
  }

  if (pdev->dev_state == USBD_STATE_CONFIGURED)
  {
    if (usbHidEpTryAcquire(p_hhid, HID_EPIN_ADDR))                     // V260824R1: 키보드 EP만 점유
    {
      ret = true;
      if (diagnostic_session_id != 0U)
      {
        // V260823R2: 활성 세션만 원 요청 시각을 연결해 비활성 8k 경로의 임계구역 비용을 없앤다.
        // 표식은 전송보다 먼저 세운다. USB ISR이 그 사이에 완료를 보고할 수 있다.
        usbDiagnosticsOnReportTransferStarted(diagnostic_request_us,
                                              diagnostic_session_id,
                                              queued_reports);
      }
      if (USBD_LL_Transmit(pdev, HID_EPIN_ADDR, report, len) != USBD_OK)
      {
        usbHidEpRelease(p_hhid, HID_EPIN_ADDR);                        // V260824R1: 무장 실패 시 즉시 해제
        ret = false;
      }
    }
  }

  return ret;
}

/**
  * @brief  USBD_HID_SendReportEXK
  *         Send HID Report
  * @param  buff: pointer to report
  * @retval status
  */
bool USBD_HID_SendReportEXK(uint8_t *report, uint16_t len)
{
  USBD_HandleTypeDef *pdev = &USBD_Device;
  bool ret = false;

  if (p_hhid == NULL)
  {
    return false;
  }

  if (pdev->dev_state == USBD_STATE_CONFIGURED)
  {
    if (usbHidEpTryAcquire(p_hhid, HID_EXK_EP_IN))                     // V260824R1: EXK EP만 점유
    {
      ret = true;
      if (USBD_LL_Transmit(pdev, HID_EXK_EP_IN, report, len) != USBD_OK)
      {
        usbHidEpRelease(p_hhid, HID_EXK_EP_IN);                        // V260824R1: 무장 실패 시 즉시 해제
        ret = false;
      }
    }
  }

  return ret;
}

/**
  * @brief  USBD_HID_GetPollingInterval
  *         return polling interval from endpoint descriptor
  * @param  pdev: device instance
  * @retval polling interval
  */
uint32_t USBD_HID_GetPollingInterval(USBD_HandleTypeDef *pdev)
{
  uint32_t polling_interval;

  /* HIGH-speed endpoints */
  if (pdev->dev_speed == USBD_SPEED_HIGH)
  {
    /* Sets the data transfer polling interval for high speed transfers.
     Values between 1..16 are allowed. Values correspond to interval
     of 2 ^ (bInterval-1). */
    uint8_t hs_interval = usbBootModeGetHsInterval();
    polling_interval    = (((1U << (hs_interval - 1U))) / 8U);           // V250923R1 Reflect dynamic HS interval
  }
  else   /* LOW and FULL-speed endpoints */
  {
    /* Sets the data transfer polling interval for low and full
    speed transfers */
    polling_interval =  HID_FS_BINTERVAL;
  }

  return ((uint32_t)(polling_interval));
}

#if (USBD_SUPPORT_USER_STRING_DESC == 1U)
uint8_t *USBD_HID_GetUsrStrDescriptor(struct _USBD_HandleTypeDef *pdev, uint8_t index,  uint16_t *length)
{
  logPrintf("USBD_HID_GetUsrStrDescriptor() %d\n", index);
  return USBD_HID_ProductStrDescriptor(pdev->dev_speed, length);
}
#endif

#ifndef USE_USBD_COMPOSITE
/**
  * @brief  USBD_HID_GetCfgFSDesc
  *         return FS configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_GetFSCfgDesc(uint16_t *length)
{
  USBD_EpDescTypeDef *pEpDesc = USBD_GetEpDesc(USBD_HID_CfgDesc, HID_EPIN_ADDR);

  if (pEpDesc != NULL)
  {
    pEpDesc->bInterval = HID_FS_BINTERVAL;
  }

  *length = (uint16_t)sizeof(USBD_HID_CfgDesc);
  return USBD_HID_CfgDesc;
}

/**
  * @brief  USBD_HID_GetCfgHSDesc
  *         return HS configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_GetHSCfgDesc(uint16_t *length)
{
  uint8_t             hs_interval = usbBootModeGetHsInterval();
  USBD_EpDescTypeDef *pEpDesc    = USBD_GetEpDesc(USBD_HID_CfgDesc, HID_EPIN_ADDR);

  if (pEpDesc != NULL)
  {
    pEpDesc->bInterval = hs_interval;                                 // V250923R1 Keyboard HS polling interval
  }

  pEpDesc = USBD_GetEpDesc(USBD_HID_CfgDesc, HID_VIA_EP_IN);
  if (pEpDesc != NULL)
  {
    pEpDesc->bInterval = hs_interval;                                 // V250923R1 VIA IN polling interval
  }

  pEpDesc = USBD_GetEpDesc(USBD_HID_CfgDesc, HID_VIA_EP_OUT);
  if (pEpDesc != NULL)
  {
    pEpDesc->bInterval = hs_interval;                                 // V250923R1 VIA OUT polling interval
  }

  pEpDesc = USBD_GetEpDesc(USBD_HID_CfgDesc, HID_EXK_EP_IN);
  if (pEpDesc != NULL)
  {
    pEpDesc->bInterval = hs_interval;                                 // V250923R1 EXK polling interval
  }

  *length = (uint16_t)sizeof(USBD_HID_CfgDesc);
  return USBD_HID_CfgDesc;
}

/**
  * @brief  USBD_HID_GetOtherSpeedCfgDesc
  *         return other speed configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_GetOtherSpeedCfgDesc(uint16_t *length)
{
  USBD_EpDescTypeDef *pEpDesc = USBD_GetEpDesc(USBD_HID_CfgDesc, HID_EPIN_ADDR);

  if (pEpDesc != NULL)
  {
    pEpDesc->bInterval = HID_FS_BINTERVAL;
  }

  *length = (uint16_t)sizeof(USBD_HID_CfgDesc);
  return USBD_HID_CfgDesc;
}
#endif /* USE_USBD_COMPOSITE  */


/**
  * @brief  USBD_HID_DataIn
  *         handle data IN Stage
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t USBD_HID_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  // V260824R1: 완료된 엔드포인트만 해제한다. 이전에는 epnum과 무관하게 공용 state를
  //            IDLE로 되돌려 VIA/EXK 완료가 진행 중인 키보드 전송을 풀어 버렸다.
  usbHidEpRelease((USBD_HID_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId], epnum);

  if (epnum != (HID_EPIN_ADDR & 0x0F))
  {
    return (uint8_t)USBD_OK;
  }
  
  if (usbDiagnosticsIsActive())
  {
    usbDiagnosticsOnReportTransferCompleted(micros());                // V260823R2: 실제 키보드 IN 완료 시각
  }

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_HID_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  USBD_HID_HandleTypeDef *hhid = (USBD_HID_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (hhid == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  /* Get the received data length */
  uint32_t rx_size;
  rx_size = USBD_LL_GetRxDataSize(pdev, epnum);

  if (via_hid_receive_func != NULL)
  {
    via_hid_receive_func(via_hid_usb_rx_report, rx_size);
  }

  // V260901R1: RX 콜백이 32B를 자체 큐에 복사한 직후 OUT을 재무장해 응답 대기와 다음 수신을 분리
  (void)USBD_LL_PrepareReceive(pdev, HID_VIA_EP_OUT, via_hid_usb_rx_report, sizeof(via_hid_usb_rx_report));

  return (uint8_t)USBD_OK;
}

uint8_t USBD_HID_SOF(USBD_HandleTypeDef *pdev)
{
  if (qbufferAvailable(&via_report_q))
  {
    // V260901R1: VIA 응답의 20ms wall-clock throttle을 제거하고 IN EP가 비는 첫 SOF에 바로 전송
    // V260824R1: VIA 응답도 동일한 EP busy 규약에 편입한다. 이전에는 이 경로가 규약을
    //            우회해 "IN 전송 진행 중"이라는 불변식 자체가 성립하지 않았다.
    USBD_HID_HandleTypeDef *hhid = (USBD_HID_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

    if (usbHidEpTryAcquire(hhid, HID_VIA_EP_IN))
    {
      qbufferRead(&via_report_q, (uint8_t *)via_hid_usb_tx_report, 1);
      if (USBD_LL_Transmit(pdev, HID_VIA_EP_IN, via_hid_usb_tx_report, sizeof(via_hid_usb_tx_report)) != USBD_OK)
      {
        usbHidEpRelease(hhid, HID_VIA_EP_IN);                          // V260824R1: 무장 실패 시 즉시 해제
      }
    }
  }
  return (uint8_t)USBD_OK;
}

#ifndef USE_USBD_COMPOSITE
/**
  * @brief  DeviceQualifierDescriptor
  *         return Device Qualifier descriptor
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_GetDeviceQualifierDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_HID_DeviceQualifierDesc);

  return USBD_HID_DeviceQualifierDesc;
}
#endif /* USE_USBD_COMPOSITE  */

bool usbHidUpdateWakeUp(USBD_HandleTypeDef *pdev)
{
  PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef *)pdev->pData;
  bool ret = false;
  
  if (pdev->dev_state == USBD_STATE_SUSPENDED)
  {
    logPrintf("[  ] USB WakeUp\n");

    __HAL_PCD_UNGATE_PHYCLOCK((hpcd));
    HAL_PCD_ActivateRemoteWakeup(hpcd);
    delay(10);
    HAL_PCD_DeActivateRemoteWakeup(hpcd);
    ret = true;
  }

  return ret;
}

bool usbHidSetViaReceiveFunc(void (*func)(uint8_t *, uint8_t))
{
  via_hid_receive_func = func;
  return true;
}

bool usbHidEnqueueViaResponse(const uint8_t *p_data, uint8_t length)
{
  via_report_info_t info;

  if (p_data == NULL)
  {
    return false;
  }

  if (length > sizeof(info.buf))
  {
    length = sizeof(info.buf);
  }

  memset(info.buf, 0, sizeof(info.buf));
  memcpy(info.buf, p_data, length);

  if (qbufferWrite(&via_report_q, (uint8_t *)&info, 1) != true)
  {
    logPrintf("[!] VIA TX queue overflow\n");                         // V251108R8: 메인 루프 큐 적재 실패 감시
    return false;
  }

  return true;
}

bool usbHidSendReport(uint8_t *p_data, uint16_t length)
{
  report_info_t report_info = {0};
  uint16_t      diagnostic_session_id = 0U;
  uint32_t      diagnostic_request_us = 0U;

  if (length > HID_KEYBOARD_REPORT_SIZE)
  {
    return false;
  }

  if (!USBD_is_suspended())
  {
    if (usbDiagnosticsIsActive())
    {
      diagnostic_session_id = usbDiagnosticsGetSessionId();
      diagnostic_request_us = micros();                            // V260823R2: 실제 리포트 요청 경계
    }

    memcpy(hid_buf, p_data, length);
    if (!USBD_HID_SendReport((uint8_t *)hid_buf,
                             HID_KEYBOARD_REPORT_SIZE,
                             diagnostic_request_us,
                             diagnostic_session_id,
                             (uint16_t)qbufferAvailable(&report_q)))
    {
      memcpy(report_info.buf, p_data, length);
      report_info.diagnostic_request_us = diagnostic_request_us;
      report_info.diagnostic_session_id = diagnostic_session_id;
      if (qbufferWrite(&report_q, (uint8_t *)&report_info, 1) != true)
      {
        usbDiagnosticsOnReportQueueDrop(usbDiagnosticsIsActive() ? micros() : 0U);  // V260823R2
      }
      else if (diagnostic_session_id != 0U)
      {
        usbDiagnosticsOnReportQueueDepth((uint16_t)qbufferAvailable(&report_q));
      }
    }
  }
  else
  {
    usbHidUpdateWakeUp(&USBD_Device);
  }

  return true;
}

bool usbHidSendReportEXK(uint8_t *p_data, uint16_t length)
{
  exk_report_info_t report_info;

  if (length > HID_EXK_EP_SIZE)
    return false;

  if (!USBD_is_suspended())
  {
    memcpy(hid_buf_exk, p_data, length);
    if (!USBD_HID_SendReportEXK((uint8_t *)hid_buf_exk, length))
    {
      report_info.len = length;
      memcpy(report_info.buf, p_data, length);
      if (qbufferWrite(&report_exk_q, (uint8_t *)&report_info, 1) != true)
      {
        usbDiagnosticsOnReportQueueDrop(usbDiagnosticsIsActive() ? micros() : 0U);  // V260823R2: EXK/마우스 드롭도 하드 이벤트
      }
    }    
  }
  else
  {
    usbHidUpdateWakeUp(&USBD_Device);
  }
  
  return true;
}


__weak void usbHidSetStatusLed(uint8_t led_bits)
{

}

static uint32_t usbHidBackupTimerOffsetUs(void)
{
  if (usbBootModeIsFullSpeed())
  {
    return 975U;                                                   // V251012R1 FS 프레임 종료 직전 백업 전송 예약
  }

  return 120U;                                                     // V251012R1 HS/uSOF 환경은 기존 120us 유지
}

void usbHidInitTimer(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_SlaveConfigTypeDef sSlaveConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 299;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 4294967295;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_OC_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sSlaveConfig.SlaveMode = TIM_SLAVEMODE_COMBINED_RESETTRIGGER;
  sSlaveConfig.InputTrigger = TIM_TS_ITR13;
  if (HAL_TIM_SlaveConfigSynchro(&htim2, &sSlaveConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_TIMING;
  sConfigOC.Pulse = usbHidBackupTimerOffsetUs();                    // V251012R1 속도별 백업 타이머 오프셋 적용
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_OC_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_TIM_OC_Start_IT(&htim2, TIM_CHANNEL_1);
}

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef* tim_baseHandle)
{

  if(tim_baseHandle->Instance==TIM2)
  {
    /* TIM2 clock enable */
    __HAL_RCC_TIM2_CLK_ENABLE();

    /* TIM2 interrupt Init */
    HAL_NVIC_SetPriority(TIM2_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
  }
}

void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef* tim_baseHandle)
{

  if(tim_baseHandle->Instance==TIM2)
  {
    /* Peripheral clock disable */
    __HAL_RCC_TIM2_CLK_DISABLE();

    /* TIM2 interrupt Deinit */
    HAL_NVIC_DisableIRQ(TIM2_IRQn);
  }
}

void TIM2_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim2);
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  if (qbufferAvailable(&report_q) > 0)
  {
    if (p_hhid != NULL && p_hhid->in_ep_busy[HID_EPIN_ADDR & 0x0FU] == 0U)  // V260824R1
    {
      report_info_t report_info;
      uint16_t queued_reports = (uint16_t)qbufferAvailable(&report_q);

      if (qbufferRead(&report_q, (uint8_t *)&report_info, 1) == true)
      {
        memcpy(hid_buf, report_info.buf, HID_KEYBOARD_REPORT_SIZE);
        (void)USBD_HID_SendReport((uint8_t *)hid_buf,
                                  HID_KEYBOARD_REPORT_SIZE,
                                  report_info.diagnostic_request_us,
                                  report_info.diagnostic_session_id,
                                  queued_reports);                    // V260823R2: 큐 대기 시간을 유지한 전송
      }
    }
  }

  if (qbufferAvailable(&report_exk_q) > 0)
  {
    if (p_hhid != NULL && p_hhid->in_ep_busy[HID_EXK_EP_IN & 0x0FU] == 0U)  // V260824R1
    {
      exk_report_info_t report_info;

      qbufferRead(&report_exk_q, (uint8_t *)&report_info, 1);

      memcpy(hid_buf_exk, report_info.buf, report_info.len);
      USBD_HID_SendReportEXK((uint8_t *)hid_buf_exk, report_info.len);
    }
  }

  return;
}
