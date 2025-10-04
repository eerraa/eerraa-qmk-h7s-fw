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

#include "cli.h"
#include "log.h"
#include "keys.h"
#include "qbuffer.h"
#include "report.h"


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


#define HID_KEYBOARD_REPORT_SIZE (HW_KEYS_PRESS_MAX + 2U)
#define KEY_TIME_LOG_MAX         32


static uint8_t USBD_HID_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_HID_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_HID_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t USBD_HID_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_HID_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_HID_EP0_RxReady(USBD_HandleTypeDef *pdev);
static uint8_t USBD_HID_SOF(USBD_HandleTypeDef *pdev);
static uint32_t usbHidExpectedPollIntervalUs(void);                  // V250928R3 HID 폴링 간격 계산
static bool usbHidTimeIsBefore(uint32_t now_us, uint32_t target_us); // V251001R7 마이크로초 래핑 비교 유틸리티
static bool usbHidTimeIsAfterOrEqual(uint32_t now_us,
                                     uint32_t target_us);           // V251001R7 마이크로초 래핑 비교 유틸리티

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
static void usbHidMeasurePollRate(void);
static void usbHidMeasureRateTime(void);
static bool usbHidUpdateWakeUp(USBD_HandleTypeDef *pdev);
static void usbHidInitTimer(void);
static void usbHidMonitorSof(uint32_t now_us);                     // V250924R2 SOF 안정성 추적
static UsbBootMode_t usbHidResolveDowngradeTarget(void);           // V250924R2 다운그레이드 대상 계산
static void usbHidSofMonitorPrime(uint32_t now_us,
                                  uint32_t holdoff_delta_us,
                                  uint32_t warmup_delta_us,
                                  uint8_t speed_code);            // V251001R6 SOF 초기화 루틴 공용화
static inline void usbHidSofMonitorSyncTick(uint32_t now_us);      // V251003R2 SOF 타임스탬프 동기화 인라인화





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

static uint32_t sof_cnt = 0;

enum
{
  USB_SOF_MONITOR_CONFIG_HOLDOFF_MS = 750U,                                              // V250924R3 구성 직후 워밍업 지연(ms)
  USB_SOF_MONITOR_WARMUP_TIMEOUT_MS = USB_SOF_MONITOR_CONFIG_HOLDOFF_MS + USB_BOOT_MONITOR_CONFIRM_DELAY_MS, // V250924R3 워밍업 최대 시간(ms)
  USB_SOF_MONITOR_WARMUP_FRAMES_HS  = 2048U,                                             // V250924R3 HS 안정성 확인 프레임 수
  USB_SOF_MONITOR_WARMUP_FRAMES_FS  = 128U,                                              // V250924R3 FS 안정성 확인 프레임 수
  USB_SOF_MONITOR_SCORE_CAP         = 7U,                                                // V251005R4 대규모 SOF 누락 가중치 확장
  USB_SOF_MONITOR_CONFIG_HOLDOFF_US = USB_SOF_MONITOR_CONFIG_HOLDOFF_MS * 1000UL,        // 구성 직후 워밍업 지연(us)
  USB_SOF_MONITOR_WARMUP_TIMEOUT_US = USB_SOF_MONITOR_WARMUP_TIMEOUT_MS * 1000UL,        // 워밍업 최대 시간(us)
  USB_SOF_MONITOR_RESUME_HOLDOFF_US = 200U * 1000UL,                                      // 일시중지 해제 후 홀드오프(us)
  USB_SOF_MONITOR_RECOVERY_DELAY_US = 50U * 1000UL,                                      // 다운그레이드 실패 후 지연(us)
  USB_BOOT_MONITOR_CONFIRM_DELAY_US = USB_BOOT_MONITOR_CONFIRM_DELAY_MS * 1000UL          // 다운그레이드 확인 대기(us)
};

typedef struct usb_sof_monitor_params_s usb_sof_monitor_params_t;   // V251003R1 파라미터 전방 선언

typedef struct
{
  uint32_t                        prev_tick_us;               // V250924R2 직전 SOF 타임스탬프(us)
  uint32_t                        last_decay_us;              // 점수 감소 시각(us)
  uint32_t                        holdoff_end_us;             // 다운그레이드 홀드오프 종료 시각(us)
  uint32_t                        warmup_deadline_us;         // 워밍업 타임아웃 시각(us)
  uint16_t                        expected_us;                // V251005R7 16비트 캐시로 ISR 메모리 접근 축소
  uint16_t                        stable_threshold_us;        // V251005R7 16비트 안정 범위 캐시로 로드 폭 축소
  uint16_t                        decay_interval_us;          // V251005R7 16비트 감쇠 주기로 구조체 크기 경량화
  uint16_t                        warmup_target_frames;       // V251003R5 워밍업 목표 프레임 직접 캐시
  uint16_t                        warmup_good_frames;         // V250924R3 누적 정상 프레임 수
  uint8_t                         active_speed;               // V250924R4 캐시된 USB 속도 코드
  uint8_t                         degrade_threshold;          // V251003R5 다운그레이드 임계값 직접 캐시
  uint8_t                         score;                      // V250924R2 누적 불안정 점수
  bool                            warmup_complete;            // V250924R3 워밍업 완료 여부
  bool                            suspended_active;           // V251003R4 서스펜드 상태 캐시로 재초기화 최소화
} usb_sof_monitor_t;

struct usb_sof_monitor_params_s
{
  uint8_t  speed_code;                                            // V251002R1 속도별 SOF 파라미터 키
  uint16_t expected_us;                                           // V251005R7 16비트 기대 간격으로 테이블 플래시 절감
  uint16_t stable_threshold_us;                                   // V251005R7 16비트 정상 범위 상한으로 접근 폭 축소
  uint16_t decay_interval_us;                                     // V251005R7 16비트 감쇠 주기 저장으로 경량화
  uint8_t  degrade_threshold;                                     // V251002R1 다운그레이드 임계 점수
  uint16_t warmup_target_frames;                                  // V251002R1 워밍업에 필요한 정상 프레임 수
};

static const usb_sof_monitor_params_t sof_monitor_params[] =      // V251002R1 속도별 SOF 파라미터 테이블
{
  {USBD_SPEED_HIGH, 125U, 250U, 4000U, 12U, USB_SOF_MONITOR_WARMUP_FRAMES_HS},
  {USBD_SPEED_FULL, 1000U, 2000U, 20000U, 6U, USB_SOF_MONITOR_WARMUP_FRAMES_FS},
};

static usb_sof_monitor_t sof_monitor = {0};                       // V250924R2 SOF 안정성 상태
static uint8_t           sof_prev_dev_state = USBD_STATE_DEFAULT; // V250924R2 마지막 USB 장치 상태

static inline bool usbHidTimeIsBefore(uint32_t now_us,
                                      uint32_t target_us)           // V251005R2 마이크로초 비교 인라인화로 호출 오버헤드 축소
{
  return (int32_t)(now_us - target_us) < 0;
}

static inline bool usbHidTimeIsAfterOrEqual(uint32_t now_us,
                                            uint32_t target_us)     // V251005R2 마이크로초 비교 인라인화로 호출 오버헤드 축소
{
  return (int32_t)(now_us - target_us) >= 0;
}

static void usbHidSofMonitorApplySpeedParams(uint8_t speed_code)  // V250924R4 속도별 모니터링 파라미터 캐시
{
  sof_monitor.active_speed         = speed_code;
  sof_monitor.expected_us          = 0U;                            // V251006R3 기본값 0 초기화로 분기 최소화
  sof_monitor.stable_threshold_us  = 0U;
  sof_monitor.decay_interval_us    = 0U;
  sof_monitor.degrade_threshold    = 0U;
  sof_monitor.warmup_target_frames = 0U;

  if (speed_code <= USBD_SPEED_FULL)                                // V251006R3 열거형 인덱스를 직접 사용해 switch 제거
  {
    const usb_sof_monitor_params_t *params = &sof_monitor_params[speed_code];

    sof_monitor.expected_us          = params->expected_us;         // V251005R7 16비트 파라미터 직접 복사로 ISR 폭 축소 유지
    sof_monitor.stable_threshold_us  = params->stable_threshold_us; // V251005R7 16비트 파라미터 직접 복사로 메모리 접근 경량화
    sof_monitor.decay_interval_us    = params->decay_interval_us;   // V251005R7 16비트 감쇠 주기 복사로 구조체 축소
    sof_monitor.degrade_threshold    = params->degrade_threshold;   // V251003R5 속도 파라미터 직접 복사로 런타임 접근 최소화
    sof_monitor.warmup_target_frames = params->warmup_target_frames;// V251003R5 속도 파라미터 직접 복사로 런타임 접근 최소화
  }
}


static void usbHidSofMonitorPrime(uint32_t now_us,
                                  uint32_t holdoff_delta_us,
                                  uint32_t warmup_delta_us,
                                  uint8_t speed_code)
{
  bool reuse_speed_cache = (speed_code == sof_monitor.active_speed) &&
                           (sof_monitor.expected_us != 0U);        // V251005R8 동일 속도 Prime 시 파라미터 재적용 회피

  // V251002R3 속도 변경 홀드오프 경로까지 단일 초기화 루틴으로 통합
  sof_monitor.prev_tick_us       = now_us;                        // V251001R6 SOF 타임스탬프 초기화 일원화
  sof_monitor.score              = 0U;
  sof_monitor.last_decay_us      = now_us;
  sof_monitor.holdoff_end_us     = now_us + holdoff_delta_us;
  sof_monitor.warmup_deadline_us = now_us + warmup_delta_us;
  sof_monitor.warmup_good_frames = 0U;
  sof_monitor.warmup_complete    = false;
  sof_monitor.suspended_active   = false;                         // V251003R4 서스펜드 상태는 호출자 분기로 관리
  // V251005R6 속도 파라미터는 적용 함수에서 직접 갱신하도록 중복 초기화를 제거
  if (!reuse_speed_cache)
  {
    usbHidSofMonitorApplySpeedParams(speed_code);                // V251005R8 캐시 미사용 시에만 속도 파라미터 복사
  }
}

static inline void usbHidSofMonitorSyncTick(uint32_t now_us)        // V251003R2 SOF 타임스탬프 인라인 갱신으로 호출 오버헤드 제거
{
  sof_monitor.prev_tick_us = now_us;
  if (sof_monitor.score != 0U)                                     // V251006R1 점수 존재 시에만 감쇠 타임스탬프 동기화
  {
    sof_monitor.last_decay_us = now_us;
  }
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

static uint32_t data_in_cnt = 0;
static uint32_t data_in_rate = 0;

static bool     rate_time_req = false;
static uint32_t rate_time_pre = 0;
static uint32_t rate_time_us  = 0;
static uint32_t rate_time_min = 0;
static uint32_t rate_time_avg = 0;
static uint32_t rate_time_sum = 0;
static uint32_t rate_time_max = 0;
static uint32_t rate_time_min_check = 0xFFFF;
static uint32_t rate_time_max_check = 0;
static uint32_t rate_time_excess_max = 0;                    // V250928R3 폴링 지연 초과분 누적 최대값
static uint32_t rate_time_excess_max_check = 0;              // V250928R3 윈도우 내 초과분 최대값 추적
static uint32_t rate_queue_depth_snapshot = 0;               // V250928R3 폴링 시작 시점의 큐 길이 스냅샷
static uint32_t rate_queue_depth_max = 0;                    // V250928R3 큐 잔량 최대값
static uint32_t rate_queue_depth_max_check = 0;              // V250928R3 윈도우 내 큐 잔량 최대값 추적

static uint32_t rate_time_sof_pre = 0; 
static uint32_t rate_time_sof = 0; 

static uint16_t rate_his_buf[100];

static bool     key_time_req = false;
static uint32_t key_time_pre;
static uint32_t key_time_end;
static uint32_t key_time_idx = 0;
static uint32_t key_time_cnt = 0;
static uint32_t key_time_log[KEY_TIME_LOG_MAX];
static bool     key_time_raw_req = false;
static uint32_t key_time_raw_pre;
static uint32_t key_time_raw_log[KEY_TIME_LOG_MAX];
static uint32_t key_time_pre_log[KEY_TIME_LOG_MAX];

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
  
  data_in_cnt++;


  usbHidMeasureRateTime();

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

  #if 0
  USBD_LL_Transmit(pdev, HID_VIA_EP_OUT, via_hid_usb_report, sizeof(via_hid_usb_report));
  USBD_LL_PrepareReceive(pdev, HID_VIA_EP_OUT, via_hid_usb_report, sizeof(via_hid_usb_report));
  #else
  via_report_info_t info;
  memcpy(info.buf, via_hid_usb_report, sizeof(via_hid_usb_report));
  qbufferWrite(&via_report_q, (uint8_t *)&info, 1);
  via_report_pre_time = millis();
  #endif
  return (uint8_t)USBD_OK;
}

uint8_t USBD_HID_SOF(USBD_HandleTypeDef *pdev)
{
  usbHidMeasurePollRate();

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

static uint32_t usbHidExpectedPollIntervalUs(void)
{
  return (uint32_t)usbBootModeGetExpectedIntervalUs();                // V251006R5 중앙 테이블 조회 함수 재사용으로 데이터 중복 제거
}

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

bool usbHidSendReport(uint8_t *p_data, uint16_t length)
{
  report_info_t report_info;

  if (length > HID_KEYBOARD_REPORT_SIZE)
    return false;

  if (!USBD_is_suspended())
  {
    key_time_pre = micros();

    memcpy(hid_buf, p_data, length);
    if (USBD_HID_SendReport((uint8_t *)hid_buf, HID_KEYBOARD_REPORT_SIZE))
    {
      key_time_req = true;
      rate_time_req = true;
      rate_time_pre = micros();
      rate_queue_depth_snapshot = qbufferAvailable(&report_q);       // V250928R3 즉시 전송 시 큐 잔량 캡처
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

void usbHidMeasurePollRate(void)
{
  static uint32_t cnt = 0;
  uint32_t        sample_window = usbBootModeIsFullSpeed() ? 1000U : 8000U; // V250924R1 Align poll window with active USB speed

  uint32_t now_us = micros();

  usbHidMonitorSof(now_us);                                       // V250924R2 SOF 간격 모니터링
  rate_time_sof_pre = now_us;
  if (cnt >= sample_window)
  {
    cnt = 0;
    data_in_rate = data_in_cnt;
    rate_time_min = rate_time_min_check;
    rate_time_max = rate_time_max_check;
    rate_time_avg = rate_time_sum / (data_in_cnt + 1);
    rate_time_excess_max = rate_time_excess_max_check;               // V250928R3 초과 지연 최대값 라치
    rate_queue_depth_max = rate_queue_depth_max_check;               // V250928R3 큐 잔량 최대값 라치
    data_in_cnt = 0;

    rate_time_min_check = 0xFFFF;
    rate_time_max_check = 0;
    rate_time_sum = 0;
    rate_time_excess_max_check = 0;
    rate_queue_depth_max_check = 0;
  }
  cnt++;
}

static UsbBootMode_t usbHidResolveDowngradeTarget(void)            // V251005R7 Enum 순차 계산으로 분기 축소
{
  UsbBootMode_t cur_mode = usbBootModeGet();

  if (cur_mode < USB_BOOT_MODE_FS_1K)
  {
    return (UsbBootMode_t)(cur_mode + 1);                        // V251005R7 연속 Enum을 이용한 하위 모드 산출
  }

  return USB_BOOT_MODE_MAX;
}

static void usbHidMonitorSof(uint32_t now_us)
{
  USBD_HandleTypeDef *pdev = &USBD_Device;
  usb_sof_monitor_t  *mon  = &sof_monitor;                         // V251003R3 SOF 모니터 지역 캐시로 분기 경량화 유지
  uint8_t             dev_state;
  uint8_t             dev_speed;                                   // V251006R3 SOF ISR 초기에 속도를 단일 로드해 전 구간 공유

  dev_state = pdev->dev_state;
  dev_speed = 0U;                                                  // V251006R6 구성 전 단계에서는 속도 로드를 건너뛰어 MMIO 접근 축소

  if (dev_state == USBD_STATE_CONFIGURED || dev_state == USBD_STATE_SUSPENDED)
  {
    dev_speed = pdev->dev_speed;                                   // V251006R6 구성/서스펜드 상태에서만 속도 값을 읽어 필요 시에만 접근
  }

  if (dev_state != sof_prev_dev_state)
  {
    uint8_t  new_speed = (dev_state == USBD_STATE_CONFIGURED) ? dev_speed : 0xFFU; // V251006R3 Prime 경로에서도 캐시 사용
    uint32_t holdoff   = (dev_state == USBD_STATE_CONFIGURED) ? USB_SOF_MONITOR_CONFIG_HOLDOFF_US : 0U;
    uint32_t warmup    = (dev_state == USBD_STATE_CONFIGURED) ? USB_SOF_MONITOR_WARMUP_TIMEOUT_US : 0U;

    usbHidSofMonitorPrime(now_us, holdoff, warmup, new_speed);      // V251001R6 상태 전환 초기화 경로 일원화
    sof_prev_dev_state = dev_state;
    return;                                                         // V251006R8 상태 전환 시 추가 분기 실행을 건너뛰어 ISR 경량화
  }

  if (dev_state == USBD_STATE_SUSPENDED)                           // V251006R7 서스펜드 선행 처리로 불필요한 동기화 제거
  {
    if (mon->suspended_active == false)
    {
      if (dev_speed == USBD_SPEED_HIGH || dev_speed == USBD_SPEED_FULL)
      {
        if ((mon->active_speed != dev_speed) || (mon->expected_us == 0U))
        {
          usbHidSofMonitorApplySpeedParams(dev_speed);             // V251006R7 서스펜드 최초 진입 시 속도 파라미터만 갱신
        }
      }
      mon->suspended_active = true;                                // V251006R7 서스펜드 상태 플래그 세트로 반복 초기화 회피
    }
    return;
  }

  if (dev_state != USBD_STATE_CONFIGURED)
  {
    usbHidSofMonitorSyncTick(now_us);                              // V251006R7 비구성 상태에서만 타임스탬프 동기화 유지
    return;                                                        // V251006R7 구성 외 구간에서 감시 로직 미실행
  }

  if (mon->suspended_active)
  {
    usbHidSofMonitorPrime(now_us,
                          USB_SOF_MONITOR_RESUME_HOLDOFF_US,
                          USB_SOF_MONITOR_WARMUP_TIMEOUT_US,
                          dev_speed);                             // V251003R4 서스펜드 복귀 시점에만 홀드오프 적용
    return;
  }

  if (dev_speed != USBD_SPEED_HIGH && dev_speed != USBD_SPEED_FULL)
  {
    usbHidSofMonitorPrime(now_us, 0U, 0U, 0xFFU);                 // V251001R6 지원 속도 외 상황 초기화
    return;
  }

  if (dev_speed != mon->active_speed)
  {
    usbHidSofMonitorPrime(now_us,
                          USB_SOF_MONITOR_CONFIG_HOLDOFF_US,
                          USB_SOF_MONITOR_WARMUP_TIMEOUT_US,
                          dev_speed);                             // V251002R3 속도 변경 홀드오프도 공용 초기화 사용
  }

  if (mon->prev_tick_us == 0U)
  {
    usbHidSofMonitorSyncTick(now_us);                              // V251003R1 초기 타임스탬프 세팅 공통화
    return;
  }

  uint32_t holdoff_end_us = mon->holdoff_end_us;                   // V251003R9 구조체 필드 접근 캐시로 ISR 경량화

  if (usbHidTimeIsBefore(now_us, holdoff_end_us))                  // V251002R1 홀드오프 조기 반환으로 연산 절약
  {
    usbHidSofMonitorSyncTick(now_us);                              // V251003R1 홀드오프 구간 타임스탬프 처리 공통화
    return;
  }

  uint32_t prev_tick_us    = mon->prev_tick_us;
  uint32_t stable_threshold = mon->stable_threshold_us;
  uint32_t delta_us         = now_us - prev_tick_us;
  bool     delta_below_threshold = delta_us < stable_threshold;     // V251006R8 임계 비교 결과 캐시로 반복 비교 제거

  mon->prev_tick_us = now_us;

  uint8_t  score           = mon->score;                           // V251003R9 점수 로컬 캐시로 구조체 접근 최소화
  uint8_t  score_orig      = score;                                // V251003R9 구조체 반영 여부 판단용 원본
  uint32_t last_decay_us   = mon->last_decay_us;                   // V251003R9 감쇠 타임스탬프 로컬 캐시
  uint32_t last_decay_orig = last_decay_us;                        // V251003R9 구조체 반영 여부 판단용 원본
  bool     warmup_complete = mon->warmup_complete;                 // V251006R6 워밍업 상태를 로컬에 캐시해 구조체 접근 최소화

  if (warmup_complete == false)
  {
    uint16_t warmup_target        = mon->warmup_target_frames;          // V251003R5 구조체 직접 캐시 활용
    uint16_t warmup_good_frames   = mon->warmup_good_frames;            // V251003R8 워밍업 프레임 로컬 캐시로 접근 감소
    uint16_t warmup_good_original = warmup_good_frames;                 // V251005R2 구조체 갱신 최소화를 위한 원본 값 캐시

    if (delta_below_threshold)
    {
      if (warmup_good_frames < warmup_target)
      {
        warmup_good_frames++;
      }
    }
    else if (warmup_good_frames != 0U)                                  // V251005R2 동일 값 유지 시 불필요한 초기화 회피
    {
      warmup_good_frames = 0U;
    }

    if (warmup_good_frames != warmup_good_original)
    {
      mon->warmup_good_frames = warmup_good_frames;                    // V251005R2 값 변경 시에만 구조체 쓰기
    }

    if (warmup_good_frames >= warmup_target)
    {
      mon->warmup_complete = true;
      warmup_complete      = true;                                    // V251006R6 로컬 캐시와 구조체 상태를 동기화
      last_decay_us        = now_us;                                   // V251003R9 감쇠 시작점 로컬 캐시 갱신
    }
    else
    {
      uint32_t warmup_deadline = mon->warmup_deadline_us;              // V251005R2 타임아웃 접근을 필요한 경우로 지연

      if (usbHidTimeIsAfterOrEqual(now_us, warmup_deadline))           // V251001R7 래핑 대응 워밍업 마감 비교
      {
        mon->warmup_complete = true;
        warmup_complete      = true;                                  // V251006R6 로컬 캐시와 구조체 상태를 동기화
        last_decay_us        = now_us;                                 // V251003R9 감쇠 시작점 로컬 캐시 갱신
      }
      else
      {
        return;
      }
    }
  }

  if (warmup_complete == true && delta_below_threshold)             // V251006R8 임계 비교 캐시 재사용으로 분기 비용 축소
  {
    if (score > 0U)                                                    // V251003R8 감쇠 파라미터 조회를 점수 존재 시로 지연
    {
      uint32_t decay_interval_us = mon->decay_interval_us;             // V251003R7 워밍업 이후에만 감쇠 파라미터 로드

      if (decay_interval_us > 0U)
      {
        uint32_t elapsed = now_us - last_decay_us;                    // V251005R5 unsigned 차분으로 감쇠 조건 비교

        if (elapsed >= decay_interval_us)                             // V251005R5 추가 산술 없이 경과 시간 판정
        {
          score--;
          last_decay_us = now_us;                                     // V251003R9 구조체 쓰기 지연을 위한 로컬 갱신
        }
      }
    }
    if (score != score_orig)
    {
      mon->score = score;                                            // V251003R9 변경 발생 시에만 구조체 쓰기
    }
    if (last_decay_us != last_decay_orig)
    {
      mon->last_decay_us = last_decay_us;                            // V251003R9 감쇠 타임스탬프 변경 시에만 구조체 쓰기
    }
    return;
  }

  last_decay_us = now_us;                                         // V251003R6 감쇠 기준 타임스탬프 즉시 갱신

  uint16_t expected_us = mon->expected_us;                         // V251006R1 안정 감시 단계에서만 기대 간격 로드

  if ((delta_below_threshold == false) && (expected_us != 0U))     // V251006R9 임계 이하 간격에서는 누락 프레임 계산을 생략
  {
    uint32_t missed_frames = usbCalcMissedFrames((uint32_t)expected_us,
                                                 delta_us);         // V251005R6 속도별 상수 나눗셈으로 누락 프레임 산출
    uint32_t penalty_base  = (missed_frames > 0U) ? missed_frames - 1U : 0U; // V251005R5 누락 프레임 기반 패널티 초기값 산출

    if (penalty_base > USB_SOF_MONITOR_SCORE_CAP)
    {
      penalty_base = USB_SOF_MONITOR_SCORE_CAP;
    }

    uint8_t penalty = (uint8_t)penalty_base;                       // V251005R5 8비트 패널티로 산술 경량화

    uint8_t degrade_threshold = mon->degrade_threshold;            // V251003R7 임계 파라미터 접근 지연으로 ISR 경량화
    uint8_t next_score        = (uint8_t)(score + penalty);        // V251005R8 누락 패널티 누적을 단일 산술로 계산
    bool    downgrade_trigger = (score >= degrade_threshold) ||
                                (next_score >= degrade_threshold); // V251005R8 점수/패널티 단일 비교 기반 다운그레이드 판정

    if (!downgrade_trigger)
    {
      score = next_score;                                          // V251005R8 다운그레이드 미발생 시 누적 점수 갱신
    }

    if (downgrade_trigger)                                         // V251005R5 다운그레이드 경로 분리
    {
      uint16_t missed_frames_report = (missed_frames > UINT16_MAX) ? UINT16_MAX
                                                                  : (uint16_t)missed_frames; // V251006R2 다운그레이드 시에만 누락 프레임 포화 변환
      UsbBootMode_t next_mode = usbHidResolveDowngradeTarget();
      uint32_t      holdoff   = USB_SOF_MONITOR_RECOVERY_DELAY_US; // V251003R1 홀드오프 연장 경로 통합

      if (next_mode < USB_BOOT_MODE_MAX)
      {
        uint32_t now_ms = millis();
        usb_boot_downgrade_result_t request_result = usbRequestBootModeDowngrade(next_mode,
                                                                                 delta_us,
                                                                                 expected_us,
                                                                                 missed_frames_report,
                                                                                 now_ms); // V251005R9 ISR에서 16비트 포화 후 전달

        if (request_result == USB_BOOT_DOWNGRADE_ARMED || request_result == USB_BOOT_DOWNGRADE_CONFIRMED)
        {
          holdoff = USB_BOOT_MONITOR_CONFIRM_DELAY_US;
        }
      }
      mon->holdoff_end_us = now_us + holdoff;
      score               = 0U;                                    // V251003R9 로컬 점수 초기화 후 구조체 반영
    }
    else
    {
      // no-op: score가 8비트 누적으로 이미 갱신됨               // V251005R8 단일 비교 경로에서는 추가 처리 불필요
    }
  }

  if (score != score_orig)
  {
    mon->score = score;                                            // V251003R9 변경 발생 시에만 구조체 쓰기
  }
  if (last_decay_us != last_decay_orig)
  {
    mon->last_decay_us = last_decay_us;                            // V251003R9 감쇠 타임스탬프 변경 시에만 구조체 쓰기
  }
}

void usbHidMeasureRateTime(void)
{
  rate_time_sof = micros() - rate_time_sof_pre;

  if (rate_time_req)
  {
    uint32_t rate_time_cur;

    rate_time_cur = micros();
    rate_time_us  = rate_time_cur - rate_time_pre;
    rate_time_sum += rate_time_us;
    if (rate_time_min_check > rate_time_us)
    {
      rate_time_min_check = rate_time_us;
    }
    if (rate_time_max_check < rate_time_us)
    {
      rate_time_max_check = rate_time_us;
    }

    uint32_t expected_interval_us = usbHidExpectedPollIntervalUs();   // V250928R3 현재 모드 기준 기대 폴링 간격

    if (rate_time_us > expected_interval_us)
    {
      uint32_t excess_us = rate_time_us - expected_interval_us;       // V250928R3 초과 지연 계산

      if (rate_time_excess_max_check < excess_us)
      {
        rate_time_excess_max_check = excess_us;
      }

      if (rate_queue_depth_max_check < rate_queue_depth_snapshot)
      {
        rate_queue_depth_max_check = rate_queue_depth_snapshot;
      }
    }


    uint32_t rate_time_idx;

    rate_time_idx = constrain(rate_time_us/10, 0, 99);
    if (rate_his_buf[rate_time_idx] < 0xFFFF)
    {
      rate_his_buf[rate_time_idx]++;
    }  

    rate_time_req = false;
  }

  if (key_time_req)
  {
    key_time_end = micros()-key_time_pre;
    key_time_req = false;

    key_time_log[key_time_idx] = key_time_end;

    if (key_time_raw_req)
    {
      key_time_raw_req = false;
      key_time_raw_log[key_time_idx] = micros()-key_time_raw_pre;
      key_time_pre_log[key_time_idx] = key_time_pre-key_time_raw_pre;
    }
    else
    {
      key_time_raw_log[key_time_idx] = key_time_end;
    }

    key_time_idx = (key_time_idx + 1) % KEY_TIME_LOG_MAX;
    if (key_time_cnt < KEY_TIME_LOG_MAX)
    {
      key_time_cnt++;
    }
  }  
}

bool usbHidGetRateInfo(usb_hid_rate_info_t *p_info)
{
  p_info->freq_hz = data_in_rate;
  p_info->time_max = rate_time_max;
  p_info->time_min = rate_time_min;
  p_info->time_excess_max = rate_time_excess_max;                   // V250928R3 폴링 초과 지연 최대값 보고
  p_info->queue_depth_max = rate_queue_depth_max;                   // V250928R3 큐 잔량 최대값 보고
  return true;
}

bool usbHidSetTimeLog(uint16_t index, uint32_t time_us)
{
  key_time_raw_pre = time_us;
  key_time_raw_req = true;
  return true;
}

__weak void usbHidSetStatusLed(uint8_t led_bits)
{

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
  sConfigOC.Pulse = 120;
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

volatile int timer_cnt = 0;
volatile uint32_t timer_end = 0;

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  timer_cnt++;
  timer_end = micros()-rate_time_sof_pre;

  sof_cnt++;
  if (qbufferAvailable(&report_q) > 0)
  {
    if (p_hhid->state == USBD_HID_IDLE)
    {
      uint32_t queued_reports = qbufferAvailable(&report_q);          // V250928R3 큐에 남은 리포트 수 기록

      qbufferRead(&report_q, (uint8_t *)hid_buf, 1);
      key_time_req = true;

      USBD_HID_SendReport((uint8_t *)hid_buf, HID_KEYBOARD_REPORT_SIZE);
      rate_time_req = true;
      rate_time_pre = micros();
      rate_queue_depth_snapshot = (queued_reports > 0U) ? (queued_reports - 1U) : 0U; // V250928R3 송신 후 잔여 큐 길이 추적
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
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    ret = true;
  }

  if (args->argc >= 1 && args->isStr(0, "rate") == true)
  {
    uint32_t pre_time;
    uint32_t pre_time_key;
    uint32_t key_send_cnt = 0;

    memset(rate_his_buf, 0, sizeof(rate_his_buf));

    pre_time = millis();
    pre_time_key = millis();
    while(cliKeepLoop())
    {
      if (millis()-pre_time_key >= 2 && key_send_cnt < 50)
      {
        uint8_t buf[HID_KEYBOARD_REPORT_SIZE];

        memset(buf, 0, HID_KEYBOARD_REPORT_SIZE);

        pre_time_key = millis();    
        usbHidSendReport(buf, HID_KEYBOARD_REPORT_SIZE);      
        key_send_cnt++;
      }

      
      if (millis()-pre_time >= 1000)
      {
        pre_time = millis();
        cliPrintf("hid rate %d Hz, avg %4d us, max %4d us, min %d us, excess %4d us, queued %d, %d, %d\n", // V250928R3 진단 카운터 표시
          data_in_rate,
          rate_time_avg,
          rate_time_max,
          rate_time_min,
          rate_time_excess_max,
          rate_queue_depth_max,
          rate_time_sof,
          timer_end
          );
        
        for (int i=0; i<10; i++)
        {
          cliPrintf("%d us\n",key_time_log[i]);
        }

        cliPrintf("sof/tim cnt : %d/%d\n", sof_cnt, timer_cnt);
        timer_cnt = 0;
        key_send_cnt = 0;
        sof_cnt=0;
      }
    }

    if (args->argc == 2 && args->isStr(1, "his"))
    {
      for (int i=0; i<100; i++)
      {
        cliPrintf("%d %d\n", i, rate_his_buf[i]);
      }
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "log") && args->isStr(1, "clear"))
  {
    key_time_idx = 0;
    key_time_cnt = 0;
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "log") == true)
  {
    uint16_t index;
    uint16_t time_max[3] = {0, 0, 0};
    uint16_t time_min[3] = {0xFFFF, 0xFFFF, 0xFFFF};
    uint16_t time_sum[3] = {0, 0, 0};


    for (int i = 0; i < key_time_cnt; i++)
    {
      if (key_time_cnt == KEY_TIME_LOG_MAX)
        index = (key_time_idx + i) % KEY_TIME_LOG_MAX;
      else
        index = i;

      cliPrintf("%2d: %3d us, raw : %3d us, %d\n",
                i,
                key_time_log[index],
                key_time_raw_log[index],
                key_time_pre_log[index]);

      for (int j=0; j<3; j++)
      {
        uint16_t data;

        if (j == 0)
          data = key_time_log[index]; 
        else if (j == 1)
          data = key_time_raw_log[index]; 
        else
          data = key_time_pre_log[index];          

        time_sum[j] += data;
        if (data > time_max[j])
          time_max[j] = data;
        if (data < time_min[j])
          time_min[j] = data;
      }
    }

    cliPrintf("\n");
    if (key_time_cnt > 0)
    {
      cliPrintf("avg : %3d us %3d us %3d us\n",
                time_sum[0] / key_time_cnt,
                time_sum[1] / key_time_cnt,
                time_sum[2] / key_time_cnt);
      cliPrintf("max : %3d us %3d us %3d us\n", time_max[0], time_max[1], time_max[2]);
      cliPrintf("min : %3d us %3d us %3d us\n", time_min[0], time_min[1], time_min[2]);
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("usbhid info\n");
    cliPrintf("usbhid rate\n");
    cliPrintf("usbhid rate his\n");
    cliPrintf("usbhid log\n");
    cliPrintf("usbhid log clear\n");
  }
}
#endif
