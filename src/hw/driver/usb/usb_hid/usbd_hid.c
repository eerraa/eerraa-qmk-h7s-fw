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

#include "cli.h"
#include "log.h"
#include "keys.h"
#include "qbuffer.h"
#include "report.h"
#include "usbd_hid_internal.h"           // V251009R9: 계측 전용 상수를 공유
#include "usbd_hid_instrumentation.h"    // V251009R9: HID 계측 로직을 전용 모듈로 이관


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


static void cliCmd(cli_args_t *args);
static bool usbHidUpdateWakeUp(USBD_HandleTypeDef *pdev);
static void usbHidInitTimer(void);
static uint32_t usbHidBackupTimerOffsetUs(void);                       // V251012R1 FS 백업 전송 지연 재조정
#ifdef USB_MONITOR_ENABLE
static void usbHidMonitorSof(uint32_t now_us);                          // V250924R2 SOF 안정성 추적
static void usbHidMonitorProcessDelta(uint32_t now_us, uint32_t delta_us);  // V251108R9 SOF 간격 평가
static void usbHidMonitorPrimeTimeout(uint32_t now_us);                 // V251108R9 SOF 누락 타임아웃 갱신
static bool usbHidMonitorCommitDowngrade(uint32_t now_us,
                                         uint32_t delta_us,
                                         uint32_t expected_us);         // V251109R1 공통 다운그레이드 처리
static void usbHidMonitorTrackEnumeration(uint32_t now_us,
                                          bool     monitor_enabled);    // V251109R1 열거 실패 감시
static UsbBootMode_t usbHidResolveDowngradeTarget(void);                // V250924R2 다운그레이드 대상 계산
void usbHidMonitorBackgroundTick(uint32_t now_us);                      // V251108R9 SOF 중단 감시
#endif





typedef struct
{
  uint8_t  buf[HID_KEYBOARD_REPORT_SIZE];
} report_info_t;

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
static uint32_t              via_report_pre_time;
static uint32_t              via_report_time = 20;
__ALIGN_BEGIN static uint8_t via_hid_usb_report[32] __ALIGN_END;
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
  4,                                                  /* bInterval: Polling Interval */

  /* 59 */
  0x07,                                               /* bLength: Endpoint Descriptor size */
  USB_DESC_TYPE_ENDPOINT,                             /* bDescriptorType:*/
  HID_VIA_EP_OUT,                                     /* bEndpointAddress: Endpoint Address (OUT) */
  USBD_EP_TYPE_INTR,                                  /* bmAttributes: Interrupt endpoint */
  HID_VIA_EP_SIZE,                                    /* wMaxPacketSize: */
  0x00,
  4,                                                  /* bInterval: Polling Interval */
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

#if 0
__ALIGN_BEGIN static uint8_t HID_MOUSE_ReportDesc[HID_MOUSE_REPORT_DESC_SIZE] __ALIGN_END =
{
  0x05, 0x01,        /* Usage Page (Generic Desktop Ctrls)     */
  0x09, 0x02,        /* Usage (Mouse)                          */
  0xA1, 0x01,        /* Collection (Application)               */
  0x09, 0x01,        /*   Usage (Pointer)                      */
  0xA1, 0x00,        /*   Collection (Physical)                */
  0x05, 0x09,        /*     Usage Page (Button)                */
  0x19, 0x01,        /*     Usage Minimum (0x01)               */
  0x29, 0x03,        /*     Usage Maximum (0x03)               */
  0x15, 0x00,        /*     Logical Minimum (0)                */
  0x25, 0x01,        /*     Logical Maximum (1)                */
  0x95, 0x03,        /*     Report Count (3)                   */
  0x75, 0x01,        /*     Report Size (1)                    */
  0x81, 0x02,        /*     Input (Data,Var,Abs)               */
  0x95, 0x01,        /*     Report Count (1)                   */
  0x75, 0x05,        /*     Report Size (5)                    */
  0x81, 0x01,        /*     Input (Const,Array,Abs)            */
  0x05, 0x01,        /*     Usage Page (Generic Desktop Ctrls) */
  0x09, 0x30,        /*     Usage (X)                          */
  0x09, 0x31,        /*     Usage (Y)                          */
  0x09, 0x38,        /*     Usage (Wheel)                      */
  0x15, 0x81,        /*     Logical Minimum (-127)             */
  0x25, 0x7F,        /*     Logical Maximum (127)              */
  0x75, 0x08,        /*     Report Size (8)                    */
  0x95, 0x03,        /*     Report Count (3)                   */
  0x81, 0x06,        /*     Input (Data,Var,Rel)               */
  0xC0,              /*   End Collection                       */
  0x09, 0x3C,        /*   Usage (Motion Wakeup)                */
  0x05, 0xFF,        /*   Usage Page (Reserved 0xFF)           */
  0x09, 0x01,        /*   Usage (0x01)                         */
  0x15, 0x00,        /*   Logical Minimum (0)                  */
  0x25, 0x01,        /*   Logical Maximum (1)                  */
  0x75, 0x01,        /*   Report Size (1)                      */
  0x95, 0x02,        /*   Report Count (2)                     */
  0xB1, 0x22,        /*   Feature (Data,Var,Abs,NoWrp)         */
  0x75, 0x06,        /*   Report Size (6)                      */
  0x95, 0x01,        /*   Report Count (1)                     */
  0xB1, 0x01,        /*   Feature (Const,Array,Abs,NoWrp)      */
  0xC0               /* End Collection                         */
};
#endif

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
  0xC0                      // End Collection
};

static USBD_HID_HandleTypeDef *p_hhid = NULL;
static uint8_t HIDInEpAdd = HID_EPIN_ADDR;
extern USBD_HandleTypeDef USBD_Device;
static TIM_HandleTypeDef htim2;

#ifdef USB_MONITOR_ENABLE  // V251009R6: USB 불안정성 감시 블록을 독립 매크로로 분리
enum
{ 
  USB_SOF_MONITOR_CONFIG_HOLDOFF_MS   = 50U,                                               // V251108R9 재협상/재개 지연 최소화
  USB_SOF_MONITOR_WARMUP_TIMEOUT_MS   = USB_SOF_MONITOR_CONFIG_HOLDOFF_MS + USB_BOOT_MONITOR_CONFIRM_DELAY_MS, // V250924R3 워밍업 최대 시간(ms)
  USB_SOF_MONITOR_WARMUP_FRAMES_HS    = 2048U,                                             // V250924R3 HS 안정성 확인 프레임 수
  USB_SOF_MONITOR_WARMUP_FRAMES_FS    = 128U,                                              // V250924R3 FS 안정성 확인 프레임 수
  USB_SOF_MONITOR_CONFIG_HOLDOFF_US   = USB_SOF_MONITOR_CONFIG_HOLDOFF_MS * 1000UL,        // 구성 직후 워밍업 지연(us)
  USB_SOF_MONITOR_WARMUP_TIMEOUT_US   = USB_SOF_MONITOR_WARMUP_TIMEOUT_MS * 1000UL,        // 워밍업 최대 시간(us)
  USB_SOF_MONITOR_RESUME_HOLDOFF_US   = 50U * 1000UL,                                      // V251108R9 일시중지 해제 후 감시 재개 지연(us)
  USB_SOF_MONITOR_RECOVERY_DELAY_US   = 50U * 1000UL,                                      // 다운그레이드 실패 후 지연(us)
  USB_SOF_MONITOR_NO_SOF_TIMEOUT_FACTOR = 64U,                                            // V251108R9 SOF 누락 감시용 시간 배수
  USB_ENUM_MONITOR_ATTEMPT_TIMEOUT_MS = 250U,                                             // V251109R1 열거 시도 타임아웃(ms)
  USB_ENUM_MONITOR_FAIL_THRESHOLD     = 3U,                                               // V251109R1 열거 실패 다운그레이드 임계
  USB_ENUM_MONITOR_SCORE_CAP          = 5U,                                               // V251109R1 열거 실패 점수 상한
  USB_ENUM_MONITOR_RECOVERY_MS        = 1000U,                                            // V251109R1 열거 안정 여부 감쇠(ms)
  USB_BOOT_MONITOR_CONFIRM_DELAY_US   = USB_BOOT_MONITOR_CONFIRM_DELAY_MS * 1000UL          // 다운그레이드 확인 대기(us)
};

#define USB_ENUM_MONITOR_ATTEMPT_TIMEOUT_US (USB_ENUM_MONITOR_ATTEMPT_TIMEOUT_MS * 1000UL)
#define USB_ENUM_MONITOR_RECOVERY_US       (USB_ENUM_MONITOR_RECOVERY_MS * 1000UL)

typedef struct
{
  uint32_t prev_tick_us;                                          // V250924R2 직전 SOF 타임스탬프(us)
  uint32_t last_decay_us;                                         // 점수 감소 시각(us)
  uint32_t slow_last_decay_us;                                    // V251108R9 느린 점수 감소/증가 기준 시각(us)
  uint32_t holdoff_end_us;                                        // 다운그레이드 홀드오프 종료 시각(us)
  uint32_t warmup_deadline_us;                                    // 워밍업 타임아웃 시각(us)
  uint32_t no_sof_deadline_us;                                    // V251108R9 SOF 미수신 타임아웃 시각(us)
  uint32_t expected_us;                                           // V250924R4 속도별 기대 SOF 주기(us)
  uint32_t stable_threshold_us;                                   // V250924R4 정상 범위 상한(us)
  uint32_t decay_interval_us;                                     // 점수 감쇠 주기(us)
  uint32_t slow_decay_interval_us;                                // V251108R9 느린 점수 감쇠 주기(us)
  uint16_t warmup_good_frames;                                    // V250924R3 누적 정상 프레임 수
  uint16_t warmup_target_frames;                                  // V250924R3 요구되는 정상 프레임 한계
  uint8_t  degrade_threshold;                                     // V250924R4 다운그레이드 임계 점수
  uint8_t  slow_degrade_threshold;                                // V251108R9 느린 점수 임계
  uint8_t  event_score_cap;                                       // V251108R9 속도별 단일 이벤트 점수 상한
  uint8_t  active_speed;                                          // V250924R4 캐시된 USB 속도 코드
  uint8_t  score;                                                 // V250924R2 누적 불안정 점수
  uint8_t  slow_score;                                            // V251108R9 느린 불안정 점수
  bool     warmup_complete;                                       // V250924R3 워밍업 완료 여부
} usb_sof_monitor_t;

static usb_sof_monitor_t sof_monitor = {0};                       // V250924R2 SOF 안정성 상태
static uint8_t           sof_prev_dev_state = USBD_STATE_DEFAULT; // V250924R2 마지막 USB 장치 상태

typedef struct                                             // V251109R1 USB 열거 실패 감시 상태
{
  uint32_t attempt_deadline_us;
  uint32_t stable_since_us;
  uint8_t  fail_score;
  uint8_t  pending_state;
  bool     waiting_config;
} usb_enumeration_monitor_t;

static usb_enumeration_monitor_t enum_monitor = {0};

static void usbHidSofMonitorApplySpeedParams(uint8_t speed_code)  // V250924R4 속도별 모니터링 파라미터 캐시
{
  sof_monitor.active_speed = speed_code;

  switch (speed_code)
  {
    case USBD_SPEED_HIGH:
      sof_monitor.expected_us        = 125U;
      sof_monitor.stable_threshold_us = 180U;                       // V251108R9 HS 환경 허용 오차 축소
      sof_monitor.decay_interval_us  = 4000U;
      sof_monitor.slow_decay_interval_us = 12000U;                  // V251108R9 느린 점수 감쇠 (약 12ms)
      sof_monitor.degrade_threshold  = 10U;
      sof_monitor.slow_degrade_threshold = 4U;
      sof_monitor.event_score_cap    = 6U;
      sof_monitor.warmup_target_frames = USB_SOF_MONITOR_WARMUP_FRAMES_HS;
      break;
    case USBD_SPEED_FULL:
      sof_monitor.expected_us        = 1000U;
      sof_monitor.stable_threshold_us = 1500U;                      // V251108R9 FS 허용 오차 축소
      sof_monitor.decay_interval_us  = 20000U;
      sof_monitor.slow_decay_interval_us = 60000U;                  // V251108R9 느린 점수 감쇠 (약 60ms)
      sof_monitor.degrade_threshold  = 5U;
      sof_monitor.slow_degrade_threshold = 3U;
      sof_monitor.event_score_cap    = 4U;
      sof_monitor.warmup_target_frames = USB_SOF_MONITOR_WARMUP_FRAMES_FS;
      break;
    default:
      sof_monitor.expected_us        = 0U;
      sof_monitor.stable_threshold_us = 0U;
      sof_monitor.decay_interval_us  = 0U;
      sof_monitor.degrade_threshold  = 0U;
      sof_monitor.slow_decay_interval_us = 0U;
      sof_monitor.slow_degrade_threshold = 0U;
      sof_monitor.event_score_cap    = 0U;
      sof_monitor.warmup_target_frames = 0U;
      break;
  }
}

#endif  // USB_MONITOR_ENABLE  // V251010R5: 모니터 전용 정의 영역을 조기 종료해 일반 HID 경로가 항상 컴파일되도록 조정


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

  pdev->ep_in[HID_VIA_EP_OUT & 0xFU].bInterval = pdev->dev_speed == USBD_SPEED_HIGH ? hs_interval:HID_FS_BINTERVAL;
  (void)USBD_LL_OpenEP(pdev, HID_VIA_EP_OUT, USBD_EP_TYPE_INTR, HID_VIA_EP_SIZE);
  pdev->ep_in[HID_VIA_EP_OUT & 0xFU].is_used = 1U;

  // EXK EP
  //
  pdev->ep_in[HID_EXK_EP_IN & 0xFU].bInterval = pdev->dev_speed == USBD_SPEED_HIGH ? hs_interval:HID_FS_BINTERVAL;
  (void)USBD_LL_OpenEP(pdev, HID_EXK_EP_IN, USBD_EP_TYPE_INTR, HID_EXK_EP_SIZE);
  pdev->ep_in[HID_EXK_EP_IN & 0xFU].is_used = 1U;


  hhid->state = USBD_HID_IDLE;

  /* Prepare Out endpoint to receive next packet */
  (void)USBD_LL_PrepareReceive(pdev, HID_VIA_EP_OUT, via_hid_usb_report, 32);


  static bool is_first = true;
  if (is_first)
  {
    is_first = false;

    qbufferCreateBySize(&report_q, (uint8_t *)report_buf, sizeof(report_info_t), 128); 
    qbufferCreateBySize(&via_report_q, (uint8_t *)via_report_q_buf, sizeof(via_report_info_t), 128); 
    qbufferCreateBySize(&report_exk_q, (uint8_t *)report_exk_buf, sizeof(report_info_t), 128); 

    logPrintf("[OK] USB Hid\n");
    logPrintf("     Keyboard\n");
    cliAdd("usbhid", cliCmd);

    usbHidInitTimer();
  }

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
bool USBD_HID_SendReport(uint8_t *report, uint16_t len)
{
  USBD_HandleTypeDef *pdev = &USBD_Device;
  bool ret = false;

  if (p_hhid == NULL)
  {
    return false;
  }

  if (pdev->dev_state == USBD_STATE_CONFIGURED)
  {
    if (p_hhid->state == USBD_HID_IDLE)
    {
      ret = true;
      p_hhid->state = USBD_HID_BUSY;
      (void)USBD_LL_Transmit(pdev, HID_EPIN_ADDR, report, len);
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
    if (p_hhid->state == USBD_HID_IDLE)
    {
      ret = true;
      p_hhid->state = USBD_HID_BUSY;
      (void)USBD_LL_Transmit(pdev, HID_EXK_EP_IN, report, len);
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
  UNUSED(epnum);
  /* Ensure that the FIFO is empty before a new transfer, this condition could
  be caused by  a new transfer before the end of the previous transfer */
  ((USBD_HID_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId])->state = USBD_HID_IDLE;

  if (epnum != (HID_EPIN_ADDR & 0x0F))
  {
    return (uint8_t)USBD_OK;
  }
  
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
  usbHidInstrumentationOnDataIn();                                    // V251009R7: HID 계측 활성 시에만 IN 완료 계수 갱신

  usbHidMeasureRateTime();                                            // V251009R7: 폴링 간격 측정은 계측 옵션에 따라 컴파일
#endif

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
    via_hid_receive_func(via_hid_usb_report, rx_size);
  }

  return (uint8_t)USBD_OK;
}

uint8_t USBD_HID_SOF(USBD_HandleTypeDef *pdev)
{
#if defined(USB_MONITOR_ENABLE) || _DEF_ENABLE_USB_HID_TIMING_PROBE
  bool need_sof_timestamp = false;
#if defined(USB_MONITOR_ENABLE)
  bool monitor_enabled = usbInstabilityIsEnabled();
  if (monitor_enabled)
  {
    need_sof_timestamp = true;
  }
#endif
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
  need_sof_timestamp = true;
#endif
  if (need_sof_timestamp)                                             // V251108R8: 필요 시에만 micros()를 호출해 ISR 부하 절감
  {
    uint32_t sof_now_us = usbHidInstrumentationNow();                 // V251009R7: SOF 타임스탬프는 모니터/계측 공용으로 취득
#if defined(USB_MONITOR_ENABLE)
    if (monitor_enabled)                                              // V251108R1: VIA 토글로 모니터 동작 제어
    {
      usbHidMonitorSof(sof_now_us);                                   // V251009R7: 모니터 활성 시 타임스탬프 전달
    }
#endif
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
    usbHidInstrumentationOnSof(sof_now_us);                           // V251009R7: 계측 활성 시 샘플 윈도우 갱신
#endif
  }
#endif

  if (qbufferAvailable(&via_report_q) && (millis()-via_report_pre_time) >= via_report_time)
  {
    qbufferRead(&via_report_q, (uint8_t *)via_hid_usb_report, 1);
    USBD_LL_Transmit(pdev, HID_VIA_EP_OUT, via_hid_usb_report, sizeof(via_hid_usb_report));
    USBD_LL_PrepareReceive(pdev, HID_VIA_EP_OUT, via_hid_usb_report, sizeof(via_hid_usb_report));
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

  via_report_pre_time = millis();                                     // V251108R8: 전송 지연 타이머 갱신
  return true;
}

bool usbHidSendReport(uint8_t *p_data, uint16_t length)
{
  report_info_t report_info;

  if (length > HID_KEYBOARD_REPORT_SIZE)
    return false;

  if (!USBD_is_suspended())
  {
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
    usbHidInstrumentationMarkReportStart();                            // V251009R7: 계측 시에만 리포트 시작 타임스탬프 기록
#endif

    memcpy(hid_buf, p_data, length);
    if (USBD_HID_SendReport((uint8_t *)hid_buf, HID_KEYBOARD_REPORT_SIZE))
    {
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
      uint32_t queued_reports = qbufferAvailable(&report_q);           // V251009R7: 큐 깊이 스냅샷도 계측 활성 시에만 계산
      usbHidInstrumentationOnImmediateSendSuccess(queued_reports);     // V251009R7: 즉시 전송 성공 계측 조건부 실행
#endif
    }
    else
    {
      memcpy(report_info.buf, p_data, length);
      qbufferWrite(&report_q, (uint8_t *)&report_info, 1);
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
      qbufferWrite(&report_exk_q, (uint8_t *)&report_info, 1);        
    }    
  }
  else
  {
    usbHidUpdateWakeUp(&USBD_Device);
  }
  
  return true;
}

#ifdef USB_MONITOR_ENABLE  // V251010R5: 모니터 비활성 빌드에서도 HID 본체가 유지되도록 함수 정의를 개별 가드로 분리

static UsbBootMode_t usbHidResolveDowngradeTarget(void)            // V250924R2 현재 모드 대비 하위 폴링 모드 계산
{
  UsbBootMode_t cur_mode = usbBootModeGet();

  switch (cur_mode)
  {
    case USB_BOOT_MODE_HS_8K:
      return USB_BOOT_MODE_HS_4K;
    case USB_BOOT_MODE_HS_4K:
      return USB_BOOT_MODE_HS_2K;
    case USB_BOOT_MODE_HS_2K:
      return USB_BOOT_MODE_FS_1K;
    default:
      return USB_BOOT_MODE_MAX;
  }
}

static void usbHidMonitorPrimeTimeout(uint32_t now_us)             // V251108R9 SOF 누락 감시 타임아웃 초기화
{
  uint32_t guard_window = USB_SOF_MONITOR_WARMUP_TIMEOUT_US;

  if (sof_monitor.expected_us > 0U)
  {
    guard_window = sof_monitor.expected_us * USB_SOF_MONITOR_NO_SOF_TIMEOUT_FACTOR;
    if (guard_window == 0U)
    {
      guard_window = sof_monitor.expected_us;
    }
  }

  sof_monitor.no_sof_deadline_us = now_us + guard_window;
}

static void usbHidMonitorSof(uint32_t now_us)
{
  USBD_HandleTypeDef *pdev = &USBD_Device;

  if (pdev->dev_state != sof_prev_dev_state)
  {
    sof_monitor.prev_tick_us       = now_us;
    sof_monitor.score              = 0U;
    sof_monitor.slow_score         = 0U;
    sof_monitor.last_decay_us      = now_us;
    sof_monitor.slow_last_decay_us = now_us;
    sof_monitor.holdoff_end_us =
        (pdev->dev_state == USBD_STATE_CONFIGURED) ? (now_us + USB_SOF_MONITOR_CONFIG_HOLDOFF_US) : now_us;
    sof_monitor.warmup_deadline_us =
        (pdev->dev_state == USBD_STATE_CONFIGURED) ? (now_us + USB_SOF_MONITOR_WARMUP_TIMEOUT_US) : now_us;
    sof_monitor.warmup_good_frames = 0U;
    sof_monitor.warmup_complete    = false;
    usbHidSofMonitorApplySpeedParams((pdev->dev_state == USBD_STATE_CONFIGURED) ? pdev->dev_speed : 0xFFU);
    usbHidMonitorPrimeTimeout(now_us);
    sof_prev_dev_state             = pdev->dev_state;
  }

  if (pdev->dev_state != USBD_STATE_CONFIGURED)
  {
    sof_monitor.prev_tick_us       = now_us;
    sof_monitor.score              = 0U;
    sof_monitor.slow_score         = 0U;
    sof_monitor.last_decay_us      = now_us;
    sof_monitor.slow_last_decay_us = now_us;
    sof_monitor.holdoff_end_us     = now_us;
    sof_monitor.warmup_deadline_us = now_us;
    sof_monitor.warmup_good_frames = 0U;
    sof_monitor.warmup_complete    = false;
    usbHidSofMonitorApplySpeedParams(0xFFU);
    usbHidMonitorPrimeTimeout(now_us);
    return;
  }

  if (USBD_is_suspended())
  {
    sof_monitor.prev_tick_us        = now_us;
    sof_monitor.score               = 0U;
    sof_monitor.slow_score          = 0U;
    sof_monitor.holdoff_end_us      = now_us + USB_SOF_MONITOR_RESUME_HOLDOFF_US;
    sof_monitor.warmup_deadline_us  = now_us + USB_SOF_MONITOR_WARMUP_TIMEOUT_US;
    sof_monitor.warmup_good_frames  = 0U;
    sof_monitor.warmup_complete     = false;
    sof_monitor.last_decay_us       = now_us;
    sof_monitor.slow_last_decay_us  = now_us;
    usbHidSofMonitorApplySpeedParams(pdev->dev_speed);
    usbHidMonitorPrimeTimeout(now_us);
    return;
  }

  if (pdev->dev_speed != USBD_SPEED_HIGH && pdev->dev_speed != USBD_SPEED_FULL)
  {
    sof_monitor.prev_tick_us       = now_us;
    sof_monitor.score              = 0U;
    sof_monitor.slow_score         = 0U;
    sof_monitor.last_decay_us      = now_us;
    sof_monitor.slow_last_decay_us = now_us;
    sof_monitor.warmup_deadline_us = now_us;
    sof_monitor.warmup_good_frames = 0U;
    sof_monitor.warmup_complete    = false;
    usbHidSofMonitorApplySpeedParams(0xFFU);
    usbHidMonitorPrimeTimeout(now_us);
    return;
  }

  if (pdev->dev_speed != sof_monitor.active_speed)
  {
    usbHidSofMonitorApplySpeedParams(pdev->dev_speed);
    sof_monitor.score              = 0U;
    sof_monitor.slow_score         = 0U;
    sof_monitor.last_decay_us      = now_us;
    sof_monitor.slow_last_decay_us = now_us;
    sof_monitor.holdoff_end_us     = now_us + USB_SOF_MONITOR_CONFIG_HOLDOFF_US;
    sof_monitor.warmup_deadline_us = now_us + USB_SOF_MONITOR_WARMUP_TIMEOUT_US;
    sof_monitor.warmup_good_frames = 0U;
    sof_monitor.warmup_complete    = false;
    usbHidMonitorPrimeTimeout(now_us);
  }

  if (sof_monitor.prev_tick_us == 0U)
  {
    sof_monitor.prev_tick_us = now_us;
    sof_monitor.last_decay_us = now_us;
    sof_monitor.slow_last_decay_us = now_us;
    usbHidMonitorPrimeTimeout(now_us);
    return;
  }

  uint32_t delta_us = now_us - sof_monitor.prev_tick_us;
  sof_monitor.prev_tick_us = now_us;

  usbHidMonitorProcessDelta(now_us, delta_us);
}

static void usbHidMonitorProcessDelta(uint32_t now_us, uint32_t delta_us)
{
  uint32_t expected_us            = sof_monitor.expected_us;
  uint32_t stable_threshold       = sof_monitor.stable_threshold_us;
  uint32_t decay_interval_us      = sof_monitor.decay_interval_us;
  uint32_t slow_decay_interval_us = sof_monitor.slow_decay_interval_us;
  uint8_t  degrade_threshold      = sof_monitor.degrade_threshold;
  uint8_t  slow_degrade_threshold = sof_monitor.slow_degrade_threshold;
  uint8_t  event_score_cap        = sof_monitor.event_score_cap;

  if (expected_us == 0U)
  {
    return;
  }

  usbHidMonitorPrimeTimeout(now_us);                               // V251108R9 SOF 누락 감시 타임아웃 갱신

  if (now_us < sof_monitor.holdoff_end_us)
  {
    sof_monitor.last_decay_us      = now_us;
    sof_monitor.slow_last_decay_us = now_us;
    return;
  }

  if (sof_monitor.warmup_complete == false)
  {
    if (delta_us < stable_threshold)
    {
      if (sof_monitor.warmup_good_frames < sof_monitor.warmup_target_frames)
      {
        sof_monitor.warmup_good_frames++;
      }
    }
    else
    {
      sof_monitor.warmup_good_frames = 0U;
    }

    if (sof_monitor.warmup_good_frames >= sof_monitor.warmup_target_frames || now_us >= sof_monitor.warmup_deadline_us)
    {
      sof_monitor.warmup_complete   = true;
      sof_monitor.last_decay_us     = now_us;
      sof_monitor.slow_last_decay_us = now_us;
    }
    else
    {
      return;
    }
  }

  if (delta_us < stable_threshold)
  {
    if (sof_monitor.score > 0U && decay_interval_us > 0U)
    {
      if ((now_us - sof_monitor.last_decay_us) >= decay_interval_us)
      {
        sof_monitor.score--;
        sof_monitor.last_decay_us = now_us;
      }
    }

    if (sof_monitor.slow_score > 0U && slow_decay_interval_us > 0U)
    {
      if ((now_us - sof_monitor.slow_last_decay_us) >= slow_decay_interval_us)
      {
        sof_monitor.slow_score--;
        sof_monitor.slow_last_decay_us = now_us;
      }
    }
    return;
  }

  uint32_t missed_frames = (delta_us + expected_us - 1U) / expected_us;
  uint8_t  delta_score   = 1U;
  uint32_t burst_points  = 0U;

  if (missed_frames > 1U)
  {
    burst_points = missed_frames - 1U;
  }

  if (burst_points > event_score_cap)
  {
    burst_points = event_score_cap;
  }

  if (burst_points > 0U)
  {
    if (burst_points > 0xFFU)
    {
      burst_points = 0xFFU;
    }
    delta_score = (uint8_t)burst_points;
  }

  if (delta_score < 1U)
  {
    delta_score = 1U;
  }

  if (sof_monitor.score <= (uint8_t)(0xFFU - delta_score))
  {
    sof_monitor.score += delta_score;
  }
  else
  {
    sof_monitor.score = 0xFFU;
  }

  sof_monitor.last_decay_us = now_us;

  if (sof_monitor.slow_score < 0xFFU)
  {
    sof_monitor.slow_score++;
  }
  sof_monitor.slow_last_decay_us = now_us;

  bool need_degrade = false;

  if (degrade_threshold > 0U && sof_monitor.score >= degrade_threshold)
  {
    need_degrade = true;
  }

  if (slow_degrade_threshold > 0U && sof_monitor.slow_score >= slow_degrade_threshold)
  {
    need_degrade = true;
  }

  if (need_degrade == false)
  {
    return;
  }

  (void)usbHidMonitorCommitDowngrade(now_us, delta_us, expected_us);
}

static bool usbHidMonitorCommitDowngrade(uint32_t now_us,
                                         uint32_t delta_us,
                                         uint32_t expected_us)
{
  bool downgrade_requested = false;
  UsbBootMode_t next_mode = usbHidResolveDowngradeTarget();

  if (next_mode < USB_BOOT_MODE_MAX)
  {
    uint32_t now_ms = millis();
    usb_boot_downgrade_result_t request_result = usbRequestBootModeDowngrade(next_mode,
                                                                             delta_us,
                                                                             expected_us,
                                                                             now_ms);

    if (request_result == USB_BOOT_DOWNGRADE_ARMED || request_result == USB_BOOT_DOWNGRADE_CONFIRMED)
    {
      sof_monitor.holdoff_end_us = now_us + USB_BOOT_MONITOR_CONFIRM_DELAY_US;
      downgrade_requested = true;
    }
    else
    {
      sof_monitor.holdoff_end_us = now_us + USB_SOF_MONITOR_RECOVERY_DELAY_US;
    }
  }
  else
  {
    sof_monitor.holdoff_end_us = now_us + USB_SOF_MONITOR_RECOVERY_DELAY_US;
  }

  sof_monitor.score      = 0U;
  sof_monitor.slow_score = 0U;

  return downgrade_requested;
}

static void usbHidMonitorTrackEnumeration(uint32_t now_us,
                                          bool     monitor_enabled)
{
  USBD_HandleTypeDef *pdev = &USBD_Device;
  uint8_t dev_state = pdev->dev_state;

  if (dev_state == USBD_STATE_CONFIGURED)
  {
    enum_monitor.waiting_config = false;
    enum_monitor.pending_state  = dev_state;

    if (enum_monitor.fail_score > 0U)
    {
      if (enum_monitor.stable_since_us == 0U)
      {
        enum_monitor.stable_since_us = now_us;
      }
      else if ((now_us - enum_monitor.stable_since_us) >= USB_ENUM_MONITOR_RECOVERY_US)
      {
        enum_monitor.fail_score--;
        enum_monitor.stable_since_us = (enum_monitor.fail_score > 0U) ? now_us : 0U;
      }
    }
    else
    {
      enum_monitor.stable_since_us = now_us;
    }
    return;
  }

  enum_monitor.stable_since_us = 0U;

  if (dev_state == USBD_STATE_DEFAULT || dev_state == USBD_STATE_ADDRESSED)
  {
    if (enum_monitor.waiting_config == false || enum_monitor.pending_state != dev_state)
    {
      enum_monitor.waiting_config    = true;
      enum_monitor.pending_state     = dev_state;
      enum_monitor.attempt_deadline_us = now_us + USB_ENUM_MONITOR_ATTEMPT_TIMEOUT_US;
      return;
    }

    if (enum_monitor.waiting_config && now_us >= enum_monitor.attempt_deadline_us)
    {
      if (enum_monitor.fail_score < USB_ENUM_MONITOR_SCORE_CAP)
      {
        enum_monitor.fail_score++;
      }
      enum_monitor.attempt_deadline_us = now_us + USB_ENUM_MONITOR_ATTEMPT_TIMEOUT_US;

      if (monitor_enabled && enum_monitor.fail_score >= USB_ENUM_MONITOR_FAIL_THRESHOLD)
      {
        uint32_t expected_us = (sof_monitor.expected_us > 0U) ? sof_monitor.expected_us : 125U;
        bool downgrade_ok = usbHidMonitorCommitDowngrade(now_us,
                                                         USB_ENUM_MONITOR_ATTEMPT_TIMEOUT_US,
                                                         expected_us);
#if HW_USB_LOG == 1
        logPrintf("[!] USB Enum Fail (%u/%u) -> downgrade %s\n",
                  enum_monitor.fail_score,
                  (uint32_t)USB_ENUM_MONITOR_FAIL_THRESHOLD,
                  downgrade_ok ? "request" : "rejected");
#endif
        enum_monitor.fail_score     = 0U;
        enum_monitor.waiting_config = false;
        enum_monitor.pending_state  = dev_state;
      }
    }
  }
  else
  {
    enum_monitor.waiting_config = false;
    enum_monitor.pending_state  = dev_state;
  }
}

void usbHidMonitorBackgroundTick(uint32_t now_us)                    // V251108R9 SOF 중단 감시
{
  bool monitor_enabled = usbInstabilityIsEnabled();
  usbHidMonitorTrackEnumeration(now_us, monitor_enabled);

  if (monitor_enabled == false)
  {
    return;
  }

  USBD_HandleTypeDef *pdev = &USBD_Device;

  if (pdev->dev_state != USBD_STATE_CONFIGURED)
  {
    return;
  }

  if (USBD_is_suspended())
  {
    return;
  }

  if (sof_monitor.prev_tick_us == 0U || sof_monitor.expected_us == 0U)
  {
    return;
  }

  if (sof_monitor.warmup_complete == false)
  {
    return;
  }

  if (now_us < sof_monitor.no_sof_deadline_us)
  {
    return;
  }

  uint32_t delta_us = now_us - sof_monitor.prev_tick_us;
  sof_monitor.prev_tick_us = now_us;

  usbHidMonitorProcessDelta(now_us, delta_us);
}

#endif  // USB_MONITOR_ENABLE  // V251010R5: 모니터 전용 함수 정의 범위 분리 완료


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
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
  usbHidInstrumentationOnTimerPulse();                                 // V251009R7: 계측 타이머 후크를 조건부 실행
#endif
  if (qbufferAvailable(&report_q) > 0)
  {
    if (p_hhid->state == USBD_HID_IDLE)
    {
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
      uint32_t queued_reports = qbufferAvailable(&report_q);          // V250928R3 큐에 남은 리포트 수 기록 (계측 활성 시)
#endif

      qbufferRead(&report_q, (uint8_t *)hid_buf, 1);
      USBD_HID_SendReport((uint8_t *)hid_buf, HID_KEYBOARD_REPORT_SIZE);
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
      usbHidInstrumentationOnReportDequeued(queued_reports);           // V251009R7: 큐 처리 계측을 조건부 실행
#endif
    }
  }

  if (qbufferAvailable(&report_exk_q) > 0)
  {
    if (p_hhid->state == USBD_HID_IDLE)
    {
      exk_report_info_t report_info;

      qbufferRead(&report_exk_q, (uint8_t *)&report_info, 1);

      memcpy(hid_buf_exk, report_info.buf, report_info.len);
      USBD_HID_SendReportEXK((uint8_t *)hid_buf_exk, report_info.len);
    }
  }

  return;
}

#ifdef _USE_HW_CLI
void cliCmd(cli_args_t *args)
{
  usbHidInstrumentationHandleCli(args);
}
#endif
