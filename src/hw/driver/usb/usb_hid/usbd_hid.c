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
#include "usbd_hid_internal.h"           // V251009R9: 계측 전용 상수를 공유
#include "usbd_hid_instrumentation.h"    // V251009R9: HID 계측 로직을 전용 모듈로 이관
#include "micros.h"                       // V251010R9: SOF/타이머 간 지연 측정을 직접 수행
#include "cmsis_gcc.h"                    // V251010R9: 타이머 상태 스냅샷 시 인터럽트 마스크 보존
#include "stm32h7rsxx_ll_tim.h"           // V251010R9: CCR 업데이트를 위한 LL 직접 접근


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
#define USB_HID_TIMER_SYNC_TARGET_TICKS        120U  // V251010R9: SOF 기준 목표 지연(us)
#define USB_HID_TIMER_SYNC_HS_INTERVAL_US      125U  // V251010R9: HS 모드 마이크로프레임 간격
#define USB_HID_TIMER_SYNC_FS_INTERVAL_US      1000U // V251010R9: FS 모드 SOF 간격
#define USB_HID_TIMER_SYNC_HS_MIN_TICKS        96U   // V251010R9: HS 모드 최소 허용 지연(us)
#define USB_HID_TIMER_SYNC_HS_MAX_TICKS        144U  // V251010R9: HS 모드 최대 허용 지연(us)
#define USB_HID_TIMER_SYNC_FS_MIN_TICKS        80U   // V251010R9: FS 모드 최소 허용 지연(us)
#define USB_HID_TIMER_SYNC_FS_MAX_TICKS        220U  // V251010R9: FS 모드 최대 허용 지연(us)
#define USB_HID_TIMER_SYNC_HS_GUARD_US         32U   // V251010R9: HS 모드 오차 가드 한계
#define USB_HID_TIMER_SYNC_FS_GUARD_US         80U   // V251010R9: FS 모드 오차 가드 한계
#define USB_HID_TIMER_SYNC_HS_KP_SHIFT         4U    // V251010R9: HS 비례 제어 시프트(1/16)
#define USB_HID_TIMER_SYNC_FS_KP_SHIFT         5U    // V251010R9: FS 비례 제어 시프트(1/32)
#define USB_HID_TIMER_SYNC_HS_INTEGRAL_SHIFT   7U    // V251010R9: HS 적분 항 시프트(1/128)
#define USB_HID_TIMER_SYNC_FS_INTEGRAL_SHIFT   9U    // V251010R9: FS 적분 항 시프트(1/512)
#define USB_HID_TIMER_SYNC_HS_INTEGRAL_LIMIT   (32 << USB_HID_TIMER_SYNC_HS_INTEGRAL_SHIFT)  // V251010R9: 적분 포화(HS)
#define USB_HID_TIMER_SYNC_FS_INTEGRAL_LIMIT   (32 << USB_HID_TIMER_SYNC_FS_INTEGRAL_SHIFT)  // V251010R9: 적분 포화(FS)

typedef enum
{
  USB_HID_TIMER_SYNC_SPEED_NONE = 0U,  // V251010R9: 타이머 동기화 미활성 상태
  USB_HID_TIMER_SYNC_SPEED_HS,         // V251010R9: HS 8k/4k/2k 공용 파라미터
  USB_HID_TIMER_SYNC_SPEED_FS,         // V251010R9: FS 1k 모드 파라미터
} usb_hid_timer_sync_speed_t;

typedef struct
{
  uint32_t                     last_sof_us;          // V251010R9: 직전 SOF 타임스탬프(us)
  uint32_t                     last_delay_us;        // V251011R1: 호스트 기준 직전 지연(us)
  uint32_t                     last_local_delay_us;  // V251011R1: SOF 대비 로컬 타이머 지연(us)
  uint32_t                     last_residual_us;     // V251011R1: 전송 후 호스트 폴링까지 잔차(us)
  uint32_t                     last_send_us;         // V251011R1: 타이머 경로로 마지막 전송 시각(us)
  uint32_t                     last_complete_us;     // V251011R1: 마지막 DataIn 완료 시각(us)
  int32_t                      last_error_us;        // V251010R9: 직전 오차(us)
  int32_t                      integral_accum;       // V251010R9: 적분 누산(오차 합)
  int32_t                      integral_limit;       // V251010R9: 적분 포화 한계
  uint16_t                     current_ticks;        // V251010R9: 현재 CCR1 값
  uint16_t                     default_ticks;        // V251010R9: 목표 지연 틱(120us)
  uint16_t                     min_ticks;            // V251010R9: 허용 최소 CCR1
  uint16_t                     max_ticks;            // V251010R9: 허용 최대 CCR1
  uint16_t                     guard_us;             // V251010R9: 오차 가드(us)
  uint32_t                     expected_interval_us; // V251010R9: 현재 속도 SOF 간격(us)
  uint32_t                     target_residual_us;   // V251011R1: 기대 호스트 잔차(us)
  uint8_t                      kp_shift;             // V251010R9: 비례 항 시프트
  uint8_t                      integral_shift;       // V251010R9: 적분 항 시프트
  usb_hid_timer_sync_speed_t   speed;                // V251010R9: 현재 적용 중인 속도
  bool                         ready;                // V251010R9: SOF 타임스탬프 확보 여부
  bool                         pending_timer_report; // V251011R1: 타이머 경로 전송 후 응답 대기 여부
  uint32_t                     update_count;         // V251010R9: 보정 적용 횟수
  uint32_t                     guard_fault_count;    // V251010R9: 가드 초과로 리셋된 횟수
  uint32_t                     reset_count;          // V251010R9: 초기화 횟수(모드 변경 포함)
} usb_hid_timer_sync_t;

static volatile usb_hid_timer_sync_t timer_sync =
{
  .last_sof_us = 0U,
  .last_delay_us = 0U,
  .last_local_delay_us = 0U,
  .last_residual_us = 0U,
  .last_send_us = 0U,
  .last_complete_us = 0U,
  .last_error_us = 0,
  .integral_accum = 0,
  .integral_limit = USB_HID_TIMER_SYNC_HS_INTEGRAL_LIMIT,
  .current_ticks = USB_HID_TIMER_SYNC_TARGET_TICKS,
  .default_ticks = USB_HID_TIMER_SYNC_TARGET_TICKS,
  .min_ticks = USB_HID_TIMER_SYNC_HS_MIN_TICKS,
  .max_ticks = USB_HID_TIMER_SYNC_HS_MAX_TICKS,
  .guard_us = USB_HID_TIMER_SYNC_HS_GUARD_US,
  .expected_interval_us = 0U,
  .target_residual_us = USB_HID_TIMER_SYNC_HS_INTERVAL_US - USB_HID_TIMER_SYNC_TARGET_TICKS,
  .kp_shift = USB_HID_TIMER_SYNC_HS_KP_SHIFT,
  .integral_shift = USB_HID_TIMER_SYNC_HS_INTEGRAL_SHIFT,
  .speed = USB_HID_TIMER_SYNC_SPEED_NONE,
  .ready = false,
  .pending_timer_report = false,
  .update_count = 0U,
  .guard_fault_count = 0U,
  .reset_count = 0U,
};

static void usbHidTimerSyncInit(void);                                           // V251010R9: TIM2 비교기 PI 초기화
static void usbHidTimerSyncOnSof(USBD_HandleTypeDef *pdev, uint32_t now_us);     // V251010R9: SOF 진입 시 보정 상태 갱신
static void usbHidTimerSyncOnPulse(uint32_t pulse_us);                           // V251010R9: TIM2 펄스 시 오차 계산 및 CCR 조정
static void usbHidTimerSyncOnTimerReport(uint32_t send_us);                      // V251011R1: 타이머 경로 전송 시각 추적
static void usbHidTimerSyncOnDataIn(uint32_t complete_us);                       // V251011R1: 호스트 잔차 기반 보정 업데이트
static void usbHidTimerSyncApplySpeed(usb_hid_timer_sync_speed_t speed);         // V251010R9: 속도별 파라미터 적용
static void usbHidTimerSyncForceDefault(bool count_reset);                      // V251010R9: CCR/적분 리셋
static inline int32_t usbHidTimerSyncAbs(int32_t value);                         // V251010R9: 부호 없는 절댓값 헬퍼
#if _USE_USB_MONITOR
static void usbHidMonitorSof(uint32_t now_us);                     // V250924R2 SOF 안정성 추적
static UsbBootMode_t usbHidResolveDowngradeTarget(void);           // V250924R2 다운그레이드 대상 계산
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

#if _USE_USB_MONITOR  // V251009R6: USB 불안정성 감시 블록을 독립 매크로로 분리
enum
{ 
  USB_SOF_MONITOR_CONFIG_HOLDOFF_MS = 750U,                                              // V250924R3 구성 직후 워밍업 지연(ms)
  USB_SOF_MONITOR_WARMUP_TIMEOUT_MS = USB_SOF_MONITOR_CONFIG_HOLDOFF_MS + USB_BOOT_MONITOR_CONFIRM_DELAY_MS, // V250924R3 워밍업 최대 시간(ms)
  USB_SOF_MONITOR_WARMUP_FRAMES_HS  = 2048U,                                             // V250924R3 HS 안정성 확인 프레임 수
  USB_SOF_MONITOR_WARMUP_FRAMES_FS  = 128U,                                              // V250924R3 FS 안정성 확인 프레임 수
  USB_SOF_MONITOR_SCORE_CAP         = 3U,                                                // V250924R2 단일 이벤트 점수 상한
  USB_SOF_MONITOR_CONFIG_HOLDOFF_US = USB_SOF_MONITOR_CONFIG_HOLDOFF_MS * 1000UL,        // 구성 직후 워밍업 지연(us)
  USB_SOF_MONITOR_WARMUP_TIMEOUT_US = USB_SOF_MONITOR_WARMUP_TIMEOUT_MS * 1000UL,        // 워밍업 최대 시간(us)
  USB_SOF_MONITOR_RESUME_HOLDOFF_US = 200U * 1000UL,                                      // 일시중지 해제 후 홀드오프(us)
  USB_SOF_MONITOR_RECOVERY_DELAY_US = 50U * 1000UL,                                      // 다운그레이드 실패 후 지연(us)
  USB_BOOT_MONITOR_CONFIRM_DELAY_US = USB_BOOT_MONITOR_CONFIRM_DELAY_MS * 1000UL          // 다운그레이드 확인 대기(us)
};

typedef struct
{
  uint32_t prev_tick_us;                                          // V250924R2 직전 SOF 타임스탬프(us)
  uint32_t last_decay_us;                                         // 점수 감소 시각(us)
  uint32_t holdoff_end_us;                                        // 다운그레이드 홀드오프 종료 시각(us)
  uint32_t warmup_deadline_us;                                    // 워밍업 타임아웃 시각(us)
  uint32_t expected_us;                                           // V250924R4 속도별 기대 SOF 주기(us)
  uint32_t stable_threshold_us;                                   // V250924R4 정상 범위 상한(us)
  uint32_t decay_interval_us;                                     // 점수 감쇠 주기(us)
  uint16_t warmup_good_frames;                                    // V250924R3 누적 정상 프레임 수
  uint16_t warmup_target_frames;                                  // V250924R3 요구되는 정상 프레임 한계
  uint8_t  degrade_threshold;                                     // V250924R4 다운그레이드 임계 점수
  uint8_t  active_speed;                                          // V250924R4 캐시된 USB 속도 코드
  uint8_t  score;                                                 // V250924R2 누적 불안정 점수
  bool     warmup_complete;                                       // V250924R3 워밍업 완료 여부
} usb_sof_monitor_t;

static usb_sof_monitor_t sof_monitor = {0};                       // V250924R2 SOF 안정성 상태
static uint8_t           sof_prev_dev_state = USBD_STATE_DEFAULT; // V250924R2 마지막 USB 장치 상태

static void usbHidSofMonitorApplySpeedParams(uint8_t speed_code)  // V250924R4 속도별 모니터링 파라미터 캐시
{
  sof_monitor.active_speed = speed_code;

  switch (speed_code)
  {
    case USBD_SPEED_HIGH:
      sof_monitor.expected_us        = 125U;
      sof_monitor.stable_threshold_us = 250U;
      sof_monitor.decay_interval_us  = 4000U;
      sof_monitor.degrade_threshold  = 12U;
      sof_monitor.warmup_target_frames = USB_SOF_MONITOR_WARMUP_FRAMES_HS;
      break;
    case USBD_SPEED_FULL:
      sof_monitor.expected_us        = 1000U;
      sof_monitor.stable_threshold_us = 2000U;
      sof_monitor.decay_interval_us  = 20000U;
      sof_monitor.degrade_threshold  = 6U;
      sof_monitor.warmup_target_frames = USB_SOF_MONITOR_WARMUP_FRAMES_FS;
      break;
    default:
      sof_monitor.expected_us        = 0U;
      sof_monitor.stable_threshold_us = 0U;
      sof_monitor.decay_interval_us  = 0U;
      sof_monitor.degrade_threshold  = 0U;
      sof_monitor.warmup_target_frames = 0U;
      break;
  }
}

#endif  // _USE_USB_MONITOR  // V251010R5: 모니터 전용 정의 영역을 조기 종료해 일반 HID 경로가 항상 컴파일되도록 조정


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

  usbHidTimerSyncOnDataIn(micros());                                  // V251011R1: 호스트 잔차 기반 타이머 보정 업데이트

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
  uint32_t sof_now_us = micros();                                     // V251010R9: 타이머 보정을 위해 SOF 타임스탬프 항상 확보
#if _USE_USB_MONITOR
  usbHidMonitorSof(sof_now_us);                                       // V251009R7: 모니터 활성 시 타임스탬프 전달
#endif
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
  usbHidInstrumentationOnSof(sof_now_us);                             // V251009R7: 계측 활성 시 샘플 윈도우 갱신
#endif
  usbHidTimerSyncOnSof(pdev, sof_now_us);                             // V251010R9: SOF 동기화 상태 갱신

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

bool usbHidSendReport(uint8_t *p_data, uint16_t length)
{
  report_info_t report_info;

  if (length > HID_KEYBOARD_REPORT_SIZE)
    return false;

  if (!USBD_is_suspended())
  {
    uint32_t send_us = micros();                                       // V251011R2: 즉시 전송 타임스탬프 확보
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
    usbHidInstrumentationMarkReportStart(send_us);                     // V251011R2: 계측과 보정이 동일 기준을 사용하도록 전달
#endif
    memcpy(hid_buf, p_data, length);
    if (USBD_HID_SendReport((uint8_t *)hid_buf, HID_KEYBOARD_REPORT_SIZE))
    {
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
      uint32_t queued_reports = qbufferAvailable(&report_q);           // V251009R7: 큐 깊이 스냅샷도 계측 활성 시에만 계산
      usbHidInstrumentationOnImmediateSendSuccess(queued_reports);     // V251009R7: 즉시 전송 성공 계측 조건부 실행
#endif
      usbHidTimerSyncOnTimerReport(send_us);                           // V251011R2: 즉시 전송도 잔차 기반 보정에 포함
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

#if _USE_USB_MONITOR  // V251010R5: 모니터 비활성 빌드에서도 HID 본체가 유지되도록 함수 정의를 개별 가드로 분리

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

static void usbHidMonitorSof(uint32_t now_us)
{
  USBD_HandleTypeDef *pdev = &USBD_Device;

  if (pdev->dev_state != sof_prev_dev_state)
  {
    sof_monitor.prev_tick_us       = now_us;
    sof_monitor.score              = 0U;
    sof_monitor.last_decay_us      = now_us;
    sof_monitor.holdoff_end_us =
        (pdev->dev_state == USBD_STATE_CONFIGURED) ? (now_us + USB_SOF_MONITOR_CONFIG_HOLDOFF_US) : now_us;
    sof_monitor.warmup_deadline_us =
        (pdev->dev_state == USBD_STATE_CONFIGURED) ? (now_us + USB_SOF_MONITOR_WARMUP_TIMEOUT_US) : now_us;
    sof_monitor.warmup_good_frames = 0U;
    sof_monitor.warmup_complete    = false;
    usbHidSofMonitorApplySpeedParams((pdev->dev_state == USBD_STATE_CONFIGURED) ? pdev->dev_speed : 0xFFU);
    sof_prev_dev_state             = pdev->dev_state;
  }

  if (pdev->dev_state != USBD_STATE_CONFIGURED)
  {
    sof_monitor.prev_tick_us       = now_us;
    sof_monitor.score              = 0U;
    sof_monitor.last_decay_us      = now_us;
    sof_monitor.holdoff_end_us     = now_us;
    sof_monitor.warmup_deadline_us = now_us;
    sof_monitor.warmup_good_frames = 0U;
    sof_monitor.warmup_complete    = false;
    usbHidSofMonitorApplySpeedParams(0xFFU);
    return;
  }

  if (USBD_is_suspended())
  {
    sof_monitor.prev_tick_us        = now_us;
    sof_monitor.score               = 0U;
    sof_monitor.holdoff_end_us      = now_us + USB_SOF_MONITOR_RESUME_HOLDOFF_US;
    sof_monitor.warmup_deadline_us  = now_us + USB_SOF_MONITOR_WARMUP_TIMEOUT_US;
    sof_monitor.warmup_good_frames  = 0U;
    sof_monitor.warmup_complete     = false;
    sof_monitor.last_decay_us       = now_us;
    usbHidSofMonitorApplySpeedParams(pdev->dev_speed);
    return;
  }

  if (pdev->dev_speed != USBD_SPEED_HIGH && pdev->dev_speed != USBD_SPEED_FULL)
  {
    sof_monitor.prev_tick_us       = now_us;
    sof_monitor.score              = 0U;
    sof_monitor.last_decay_us      = now_us;
    sof_monitor.warmup_deadline_us = now_us;
    sof_monitor.warmup_good_frames = 0U;
    sof_monitor.warmup_complete    = false;
    usbHidSofMonitorApplySpeedParams(0xFFU);
    return;
  }

  if (pdev->dev_speed != sof_monitor.active_speed)
  {
    usbHidSofMonitorApplySpeedParams(pdev->dev_speed);
    sof_monitor.score              = 0U;
    sof_monitor.last_decay_us      = now_us;
    sof_monitor.holdoff_end_us     = now_us + USB_SOF_MONITOR_CONFIG_HOLDOFF_US;
    sof_monitor.warmup_deadline_us = now_us + USB_SOF_MONITOR_WARMUP_TIMEOUT_US;
    sof_monitor.warmup_good_frames = 0U;
    sof_monitor.warmup_complete    = false;
  }

  if (sof_monitor.prev_tick_us == 0U)
  {
    sof_monitor.prev_tick_us = now_us;
    sof_monitor.last_decay_us = now_us;
    return;
  }

  uint32_t expected_us       = sof_monitor.expected_us;
  uint32_t stable_threshold  = sof_monitor.stable_threshold_us;
  uint32_t decay_interval_us = sof_monitor.decay_interval_us;
  uint8_t  degrade_threshold = sof_monitor.degrade_threshold;

  if (expected_us == 0U)
  {
    return;
  }

  uint32_t delta_us = now_us - sof_monitor.prev_tick_us;
  sof_monitor.prev_tick_us = now_us;

  if (now_us < sof_monitor.holdoff_end_us)
  {
    sof_monitor.last_decay_us = now_us;
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
    else if (sof_monitor.warmup_good_frames > 0U)
    {
      sof_monitor.warmup_good_frames = 0U;
    }

    if (sof_monitor.warmup_good_frames >= sof_monitor.warmup_target_frames || now_us >= sof_monitor.warmup_deadline_us)
    {
      sof_monitor.warmup_complete = true;
      sof_monitor.last_decay_us   = now_us;
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
    return;
  }

  uint32_t missed_frames = (delta_us + expected_us - 1U) / expected_us;
  uint8_t  delta_score   = 1U;

  if (missed_frames > 4U)
  {
    delta_score = 4U;
  }
  else if (missed_frames > 1U)
  {
    delta_score = (uint8_t)(missed_frames - 1U);
  }

  if (delta_score > USB_SOF_MONITOR_SCORE_CAP)
  {
    delta_score = USB_SOF_MONITOR_SCORE_CAP;
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

  if (sof_monitor.score >= degrade_threshold)
  {
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

    sof_monitor.score = 0U;
  }
}

#endif  // _USE_USB_MONITOR  // V251010R5: 모니터 전용 함수 정의 범위 분리 완료


__weak void usbHidSetStatusLed(uint8_t led_bits)
{

}

static inline int32_t usbHidTimerSyncAbs(int32_t value)
{
  return (value < 0) ? -value : value;                               // V251010R9: 부호 없는 절댓값 계산
}

static void usbHidTimerSyncForceDefault(bool count_reset)
{
  timer_sync.integral_accum = 0;
  timer_sync.current_ticks = timer_sync.default_ticks;
  timer_sync.last_sof_us = 0U;
  timer_sync.last_delay_us = 0U;
  timer_sync.last_local_delay_us = 0U;                                      // V251011R1: 로컬 지연도 초기화
  timer_sync.last_residual_us = 0U;                                         // V251011R1: 잔차 초기화
  timer_sync.last_send_us = 0U;                                             // V251011R1: 대기 중인 전송 타임스탬프 제거
  timer_sync.last_complete_us = 0U;                                         // V251011R1: 직전 완료 시각도 리셋
  timer_sync.last_error_us = 0;
  timer_sync.ready = false;
  timer_sync.pending_timer_report = false;                                  // V251011R1: 응답 대기 상태 해제
  LL_TIM_OC_SetCompareCH1(TIM2, timer_sync.current_ticks);           // V251010R9: 다음 프레임부터 기본 지연 적용
  if (count_reset)
  {
    timer_sync.reset_count++;
  }
}

static void usbHidTimerSyncInit(void)
{
  timer_sync.default_ticks = USB_HID_TIMER_SYNC_TARGET_TICKS;        // V251010R9: 기본 타겟 지연 120us 고정
  timer_sync.current_ticks = USB_HID_TIMER_SYNC_TARGET_TICKS;
  timer_sync.min_ticks = USB_HID_TIMER_SYNC_HS_MIN_TICKS;
  timer_sync.max_ticks = USB_HID_TIMER_SYNC_HS_MAX_TICKS;
  timer_sync.guard_us = USB_HID_TIMER_SYNC_HS_GUARD_US;
  timer_sync.kp_shift = USB_HID_TIMER_SYNC_HS_KP_SHIFT;
  timer_sync.integral_shift = USB_HID_TIMER_SYNC_HS_INTEGRAL_SHIFT;
  timer_sync.integral_limit = USB_HID_TIMER_SYNC_HS_INTEGRAL_LIMIT;
  timer_sync.expected_interval_us = 0U;
  timer_sync.target_residual_us = USB_HID_TIMER_SYNC_HS_INTERVAL_US - USB_HID_TIMER_SYNC_TARGET_TICKS; // V251011R1: 기본 잔차 5us
  timer_sync.speed = USB_HID_TIMER_SYNC_SPEED_NONE;
  timer_sync.last_sof_us = 0U;
  timer_sync.last_delay_us = 0U;
  timer_sync.last_local_delay_us = 0U;                                // V251011R1: 로컬 지연 초기화
  timer_sync.last_residual_us = 0U;                                   // V251011R1: 잔차 초기화
  timer_sync.last_send_us = 0U;                                       // V251011R1: 전송 타임스탬프 초기화
  timer_sync.last_complete_us = 0U;                                   // V251011R1: 완료 타임스탬프 초기화
  timer_sync.last_error_us = 0;
  timer_sync.integral_accum = 0;
  timer_sync.update_count = 0U;
  timer_sync.guard_fault_count = 0U;
  timer_sync.reset_count = 0U;
  timer_sync.ready = false;
  timer_sync.pending_timer_report = false;                            // V251011R1: 응답 대기 상태 초기화
  LL_TIM_OC_SetCompareCH1(TIM2, timer_sync.current_ticks);           // V251010R9: 초기 CCR1 설정
}

static void usbHidTimerSyncApplySpeed(usb_hid_timer_sync_speed_t speed)
{
  timer_sync.speed = speed;

  if (speed == USB_HID_TIMER_SYNC_SPEED_HS)
  {
    timer_sync.min_ticks = USB_HID_TIMER_SYNC_HS_MIN_TICKS;
    timer_sync.max_ticks = USB_HID_TIMER_SYNC_HS_MAX_TICKS;
    timer_sync.guard_us = USB_HID_TIMER_SYNC_HS_GUARD_US;
    timer_sync.kp_shift = USB_HID_TIMER_SYNC_HS_KP_SHIFT;
    timer_sync.integral_shift = USB_HID_TIMER_SYNC_HS_INTEGRAL_SHIFT;
    timer_sync.integral_limit = USB_HID_TIMER_SYNC_HS_INTEGRAL_LIMIT;
    timer_sync.expected_interval_us = USB_HID_TIMER_SYNC_HS_INTERVAL_US;
    timer_sync.target_residual_us = USB_HID_TIMER_SYNC_HS_INTERVAL_US - timer_sync.default_ticks; // V251011R1: HS 잔차 목표 5us
  }
  else if (speed == USB_HID_TIMER_SYNC_SPEED_FS)
  {
    timer_sync.min_ticks = USB_HID_TIMER_SYNC_FS_MIN_TICKS;
    timer_sync.max_ticks = USB_HID_TIMER_SYNC_FS_MAX_TICKS;
    timer_sync.guard_us = USB_HID_TIMER_SYNC_FS_GUARD_US;
    timer_sync.kp_shift = USB_HID_TIMER_SYNC_FS_KP_SHIFT;
    timer_sync.integral_shift = USB_HID_TIMER_SYNC_FS_INTEGRAL_SHIFT;
    timer_sync.integral_limit = USB_HID_TIMER_SYNC_FS_INTEGRAL_LIMIT;
    timer_sync.expected_interval_us = USB_HID_TIMER_SYNC_FS_INTERVAL_US;
    timer_sync.target_residual_us = USB_HID_TIMER_SYNC_FS_INTERVAL_US - timer_sync.default_ticks; // V251011R1: FS 잔차 목표 880us
  }
  else
  {
    timer_sync.min_ticks = USB_HID_TIMER_SYNC_HS_MIN_TICKS;
    timer_sync.max_ticks = USB_HID_TIMER_SYNC_HS_MAX_TICKS;
    timer_sync.guard_us = USB_HID_TIMER_SYNC_HS_GUARD_US;
    timer_sync.kp_shift = USB_HID_TIMER_SYNC_HS_KP_SHIFT;
    timer_sync.integral_shift = USB_HID_TIMER_SYNC_HS_INTEGRAL_SHIFT;
    timer_sync.integral_limit = USB_HID_TIMER_SYNC_HS_INTEGRAL_LIMIT;
    timer_sync.expected_interval_us = 0U;
    timer_sync.target_residual_us = USB_HID_TIMER_SYNC_HS_INTERVAL_US - timer_sync.default_ticks; // V251011R1: 비활성 시 기본 잔차 유지
  }

  usbHidTimerSyncForceDefault(true);                                 // V251010R9: 모드 변경 시 보정 상태 초기화
}

static void usbHidTimerSyncOnSof(USBD_HandleTypeDef *pdev, uint32_t now_us)
{
  usb_hid_timer_sync_speed_t next_speed = USB_HID_TIMER_SYNC_SPEED_NONE;

  if (pdev->dev_state == USBD_STATE_CONFIGURED)
  {
    if (pdev->dev_speed == USBD_SPEED_HIGH)
    {
      next_speed = USB_HID_TIMER_SYNC_SPEED_HS;
    }
    else if (pdev->dev_speed == USBD_SPEED_FULL)
    {
      next_speed = USB_HID_TIMER_SYNC_SPEED_FS;
    }
  }

  if (next_speed != timer_sync.speed)
  {
    usbHidTimerSyncApplySpeed(next_speed);                            // V251010R9: 속도 변경 시 파라미터 재설정
  }

  timer_sync.last_sof_us = now_us;
  timer_sync.ready = (next_speed != USB_HID_TIMER_SYNC_SPEED_NONE);   // V251010R9: SOF 캡처 후 보정 활성화
}

static void usbHidTimerSyncOnPulse(uint32_t pulse_us)
{
  uint32_t delay_us = 0U;

  if (timer_sync.ready && timer_sync.last_sof_us != 0U)
  {
    delay_us = pulse_us - timer_sync.last_sof_us;                     // V251010R9: SOF 대비 로컬 타이머 지연 측정
    timer_sync.last_local_delay_us = delay_us;                        // V251011R1: 로컬 지연 기록
  }
  else
  {
    timer_sync.last_local_delay_us = 0U;                              // V251011R1: 준비되지 않은 상태에서는 0으로 유지
  }

#if _DEF_ENABLE_USB_HID_TIMING_PROBE
  usbHidInstrumentationOnTimerPulse(delay_us, timer_sync.current_ticks);
#else
  (void)delay_us;
#endif
}

static void usbHidTimerSyncOnTimerReport(uint32_t send_us)
{
  if (!timer_sync.ready)
  {
    timer_sync.pending_timer_report = false;                          // V251011R1: SOF 미동기 상태에서는 대기 상태 제거
    return;
  }

  timer_sync.last_send_us = send_us;                                  // V251011R1: 전송 타임스탬프 보관
  timer_sync.pending_timer_report = true;                             // V251011R1: 다음 DataIn에서 잔차 계산 예정
}

static void usbHidTimerSyncOnDataIn(uint32_t complete_us)
{
  timer_sync.last_complete_us = complete_us;                          // V251011R1: DataIn 완료 시각 기록

  if (!timer_sync.ready || timer_sync.expected_interval_us == 0U)
  {
    timer_sync.pending_timer_report = false;                          // V251011R1: 동기화 미준비 상태에서는 잔차 업데이트 없음
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
    usbHidInstrumentationOnTimerResidual(0U, timer_sync.last_delay_us); // V251011R1: 계측에 기본값 전달
#endif
    return;
  }

  if (!timer_sync.pending_timer_report)
  {
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
    usbHidInstrumentationOnTimerResidual(0U, timer_sync.last_delay_us); // V251011R1: 타이머 경로가 아닌 완료는 보고만 유지
#endif
    return;
  }

  timer_sync.pending_timer_report = false;

  uint32_t interval_us = timer_sync.expected_interval_us;
  uint32_t residual_us = complete_us - timer_sync.last_send_us;       // V251011R1: 전송→호스트 폴링 지연 측정

  if (interval_us == 0U)
  {
    timer_sync.last_residual_us = 0U;
    timer_sync.last_delay_us = timer_sync.default_ticks;
    timer_sync.last_error_us = 0;
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
    usbHidInstrumentationOnTimerResidual(0U, timer_sync.last_delay_us);
#endif
    return;
  }

  if (residual_us >= (interval_us * 4U))                                // V251011R1: 비정상적으로 큰 잔차는 무시
  {
    timer_sync.last_residual_us = 0U;
    timer_sync.last_delay_us = timer_sync.default_ticks;
    timer_sync.last_error_us = 0;
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
    usbHidInstrumentationOnTimerResidual(0U, timer_sync.last_delay_us);
#endif
    return;
  }

  residual_us %= interval_us;                                          // V251011R1: 잔차 범위를 한 프레임 이내로 정규화
  timer_sync.last_residual_us = residual_us;

  uint32_t actual_delay_us = interval_us - residual_us;                // V251011R1: 호스트 기준 타이머 지연
  if (actual_delay_us >= interval_us)
  {
    actual_delay_us = 0U;                                             // V251011R1: wrap 보정
  }
  timer_sync.last_delay_us = actual_delay_us;

  int32_t target_residual = (int32_t)timer_sync.target_residual_us;
  int32_t error_us = (int32_t)residual_us - target_residual;           // V251011R1: 잔차 편차 계산
  int32_t half_interval = (int32_t)interval_us / 2;

  if (error_us > half_interval)
  {
    error_us -= (int32_t)interval_us;                                  // V251011R1: 0/interval 경계에서 최소 오차로 환산
  }
  else if (error_us < -half_interval)
  {
    error_us += (int32_t)interval_us;
  }

  timer_sync.last_error_us = error_us;

  if ((uint32_t)usbHidTimerSyncAbs(error_us) > (uint32_t)timer_sync.guard_us)
  {
    timer_sync.guard_fault_count++;                                   // V251011R1: 잔차가 허용 범위를 벗어나면 리셋
    usbHidTimerSyncForceDefault(true);
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
    usbHidInstrumentationOnTimerResidual(residual_us, actual_delay_us);
#endif
    return;
  }

  timer_sync.integral_accum += error_us;                               // V251011R1: 호스트 잔차 기반 적분 업데이트
  if (timer_sync.integral_accum > timer_sync.integral_limit)
  {
    timer_sync.integral_accum = timer_sync.integral_limit;
  }
  else if (timer_sync.integral_accum < -timer_sync.integral_limit)
  {
    timer_sync.integral_accum = -timer_sync.integral_limit;
  }

  int32_t proportional_term = error_us >> timer_sync.kp_shift;
  int32_t integral_term = timer_sync.integral_accum >> timer_sync.integral_shift;
  int32_t target_ticks = (int32_t)timer_sync.default_ticks + proportional_term + integral_term;

  if (target_ticks > (int32_t)timer_sync.current_ticks + 1)
  {
    target_ticks = (int32_t)timer_sync.current_ticks + 1;              // V251011R1: ±1틱 제한으로 노이즈 억제
  }
  else if (target_ticks < (int32_t)timer_sync.current_ticks - 1)
  {
    target_ticks = (int32_t)timer_sync.current_ticks - 1;
  }

  if (target_ticks < (int32_t)timer_sync.min_ticks)
  {
    target_ticks = (int32_t)timer_sync.min_ticks;
  }
  else if (target_ticks > (int32_t)timer_sync.max_ticks)
  {
    target_ticks = (int32_t)timer_sync.max_ticks;
  }

  timer_sync.current_ticks = (uint16_t)target_ticks;
  LL_TIM_OC_SetCompareCH1(TIM2, timer_sync.current_ticks);             // V251011R1: 다음 펄스 지연 갱신
  timer_sync.update_count++;

#if _DEF_ENABLE_USB_HID_TIMING_PROBE
  usbHidInstrumentationOnTimerResidual(residual_us, actual_delay_us);  // V251011R1: 계측에 잔차와 실제 지연 보고
#endif
}

bool usbHidTimerSyncGetState(usb_hid_timer_sync_state_t *p_state)
{
  if (p_state == NULL)
  {
    return false;
  }

  usb_hid_timer_sync_state_t state;

  __disable_irq();                                                   // V251010R9: ISR 업데이트와 경합을 피하기 위해 보호
  state.current_ticks = timer_sync.current_ticks;
  state.default_ticks = timer_sync.default_ticks;
  state.min_ticks = timer_sync.min_ticks;
  state.max_ticks = timer_sync.max_ticks;
  state.guard_us = timer_sync.guard_us;
  state.last_delay_us = timer_sync.last_delay_us;
  state.last_local_delay_us = timer_sync.last_local_delay_us;         // V251011R1: 로컬 지연 측정 전달
  state.last_residual_us = timer_sync.last_residual_us;               // V251011R1: 호스트 잔차 전달
  state.last_error_us = timer_sync.last_error_us;
  state.integral_accum = timer_sync.integral_accum;
  state.integral_limit = timer_sync.integral_limit;
  state.expected_interval_us = timer_sync.expected_interval_us;
  state.target_delay_us = timer_sync.default_ticks;
  state.target_residual_us = timer_sync.target_residual_us;           // V251011R1: 잔차 목표 보고
  state.update_count = timer_sync.update_count;
  state.guard_fault_count = timer_sync.guard_fault_count;
  state.reset_count = timer_sync.reset_count;
  state.kp_shift = timer_sync.kp_shift;
  state.integral_shift = timer_sync.integral_shift;
  state.speed = (uint8_t)timer_sync.speed;
  state.ready = timer_sync.ready;
  __enable_irq();

  *p_state = state;
  return (timer_sync.speed != USB_HID_TIMER_SYNC_SPEED_NONE);
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
  usbHidTimerSyncInit();                                             // V251010R9: TIM2 비교기 보정 상태 초기화
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
  if (htim->Instance != TIM2)
  {
    return;                                                            // V251010R9: HID 백업 타이머 외에는 처리 불필요
  }

  uint32_t pulse_now_us = micros();                                    // V251010R9: TIM2 펄스 시각을 캡처
  usbHidTimerSyncOnPulse(pulse_now_us);                                // V251010R9: SOF 기반 PI 보정 수행
  if (qbufferAvailable(&report_q) > 0)
  {
    if (p_hhid->state == USBD_HID_IDLE)
    {
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
      uint32_t queued_reports = qbufferAvailable(&report_q);          // V250928R3 큐에 남은 리포트 수 기록 (계측 활성 시)
#endif

      qbufferRead(&report_q, (uint8_t *)hid_buf, 1);
      USBD_HID_SendReport((uint8_t *)hid_buf, HID_KEYBOARD_REPORT_SIZE);
      usbHidTimerSyncOnTimerReport(pulse_now_us);                     // V251011R1: 타이머 경로 전송 시각을 보정기로 전달
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
