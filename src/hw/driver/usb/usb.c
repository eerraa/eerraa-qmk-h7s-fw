/*
 * usb.c
 *
 *  Created on: 2018. 3. 16.
 *      Author: HanCheol Cho
 */


#include "usb.h"
#include "cdc.h"
#include "cli.h"
#include "reset.h"
#include "eeprom.h"
#include "qmk/port/port.h"
#include "qmk/port/platforms/eeprom.h"


#ifdef _USE_HW_USB
#include "usbd_cmp.h"
#include "usbd_hid.h"


static bool          is_init = false;
static UsbMode_t     is_usb_mode = USB_NON_MODE;
static UsbBootMode_t usb_boot_mode = USB_BOOT_MODE_HS_8K;                    // V250923R1 Current USB boot mode target

static const char *const usb_boot_mode_name[USB_BOOT_MODE_MAX] = {          // V250923R1 Mode labels for logging/CLI
  "HS 8K",
  "HS 4K",
  "HS 2K",
  "FS 1K",
};

static const char *usbBootModeLabel(UsbBootMode_t mode);                     // V250923R1 helpers
static bool        usbBootModeStore(UsbBootMode_t mode);

typedef enum
{
  USB_BOOT_MODE_REQ_STAGE_IDLE = 0,
  USB_BOOT_MODE_REQ_STAGE_ARMED,
  USB_BOOT_MODE_REQ_STAGE_COMMIT,
} usb_boot_mode_request_stage_t;

USBD_HandleTypeDef USBD_Device;
extern PCD_HandleTypeDef hpcd_USB_OTG_HS;

extern USBD_DescriptorsTypeDef VCP_Desc;
extern USBD_DescriptorsTypeDef MSC_Desc;
extern USBD_DescriptorsTypeDef HID_Desc;
extern USBD_DescriptorsTypeDef CMP_Desc;

static USBD_DescriptorsTypeDef *p_desc = NULL;

typedef struct
{
  usb_boot_mode_request_stage_t stage;                           // V250924R2 다운그레이드 요청 단계
  bool                          log_pending;                    // V250924R2 로그 출력 요청 플래그
  UsbBootMode_t                 next_mode;                      // V250924R2 요청된 다음 부트 모드
  uint32_t                      delta_us;                       // V250924R2 측정된 SOF 간격(us)
  uint32_t                      expected_us;                    // V250924R2 기대 SOF 간격(us)
  uint32_t                      ready_ms;                       // V250924R2 2차 확인 가능 시각(ms)
  uint32_t                      timeout_ms;                     // V250924R2 요청 만료 시각(ms)
  uint32_t                      missed_frames;                  // V251004R2 로그용 누락 프레임 캐시
} usb_boot_mode_request_t;

static usb_boot_mode_request_t boot_mode_request = {0};          // V250924R2 USB 안정성 이벤트 큐

static uint32_t usbBootModeRequestMissedFrames(void);            // V251004R2 다운그레이드 트리거 프레임 산출

static void usbBootModeRequestReset(void)
{
  boot_mode_request.stage      = USB_BOOT_MODE_REQ_STAGE_IDLE;
  boot_mode_request.log_pending = false;
  boot_mode_request.next_mode  = USB_BOOT_MODE_HS_8K;
  boot_mode_request.delta_us   = 0U;
  boot_mode_request.expected_us = 0U;
  boot_mode_request.ready_ms   = 0U;
  boot_mode_request.timeout_ms = 0U;
  boot_mode_request.missed_frames = 0U;                          // V251004R2 누락 프레임 캐시 초기화
}

static uint32_t usbBootModeRequestMissedFrames(void)             // V251005R3 로그용 누락 프레임 캐시 보강 계산기
{
  uint32_t cached = boot_mode_request.missed_frames;              // V251005R3 ISR에서 전달된 누락 프레임 우선 사용

  if (cached > 0U)
  {
    return cached;
  }

  uint32_t expected_us = boot_mode_request.expected_us;

  if (expected_us == 0U)
  {
    return 0U;
  }

  uint32_t frames = boot_mode_request.delta_us / expected_us;     // V251005R3 임계 구간 몫 계산으로 누락 프레임 복원

  if (frames == 0U)
  {
    frames = 1U;                                                  // V251005R3 예외 경로 최소 1프레임 보장
  }

  boot_mode_request.missed_frames = frames;                       // V251005R3 재계산 값을 캐시에 저장

  return frames;
}

#if HW_USB_CMP == 1
static uint8_t hid_ep_tbl[] = {
  HID_EPIN_ADDR, 
  HID_VIA_EP_IN, 
  HID_VIA_EP_OUT,
  HID_EXK_EP_IN,
  };

static uint8_t cdc_ep_tbl[] = {
  CDC_IN_EP,
  CDC_OUT_EP,
  CDC_CMD_EP};
#endif

static const char *usbBootModeLabel(UsbBootMode_t mode)
{
  if (mode < USB_BOOT_MODE_MAX)
  {
    return usb_boot_mode_name[mode];
  }
  return "UNKNOWN";
}

bool usbBootModeLoad(void)
{
  uint32_t raw_mode = eeprom_read_dword((const uint32_t *)EECONFIG_USER_BOOTMODE);

  if (raw_mode >= USB_BOOT_MODE_MAX)
  {
    raw_mode = USB_BOOT_MODE_HS_8K;
  }

  usb_boot_mode = (UsbBootMode_t)raw_mode;
  logPrintf("[  ] USB BootMode : %s\n", usbBootModeLabel(usb_boot_mode));  // V250923R1 Log persisted boot mode

  return true;
}

UsbBootMode_t usbBootModeGet(void)
{
  return usb_boot_mode;
}

bool usbBootModeIsFullSpeed(void)
{
  return usb_boot_mode == USB_BOOT_MODE_FS_1K;
}

uint8_t usbBootModeGetHsInterval(void)
{
  switch (usb_boot_mode)
  {
    case USB_BOOT_MODE_HS_4K:
      return 0x02;
    case USB_BOOT_MODE_HS_2K:
      return 0x03;
    case USB_BOOT_MODE_FS_1K:
      return 0x01;
    case USB_BOOT_MODE_HS_8K:
    default:
      return 0x01;
  }
}

static bool usbBootModeStore(UsbBootMode_t mode)
{
  uint32_t raw_mode = (uint32_t)mode;
  uint32_t addr     = (uint32_t)EECONFIG_USER_BOOTMODE;

  if (mode >= USB_BOOT_MODE_MAX)
  {
    return false;
  }

  if (usb_boot_mode == mode)
  {
    return true;
  }

  if (eepromWrite(addr, (uint8_t *)&raw_mode, sizeof(raw_mode)) == true)
  {
    usb_boot_mode = mode;
    return true;
  }

  return false;
}

bool usbBootModeSaveAndReset(UsbBootMode_t mode)
{
  if (usbBootModeStore(mode) != true)
  {
    return false;
  }

  resetToReset();
  return true;
}

usb_boot_downgrade_result_t usbRequestBootModeDowngrade(UsbBootMode_t mode,
                                                        uint32_t      measured_delta_us,
                                                        uint32_t      expected_us,
                                                        uint32_t      missed_frames,
                                                        uint32_t      now_ms)  // V251004R2 누락 프레임 전달을 포함한 다운그레이드 요청
{
  if (mode >= USB_BOOT_MODE_MAX)
  {
    return USB_BOOT_DOWNGRADE_REJECTED;
  }

  if (boot_mode_request.stage == USB_BOOT_MODE_REQ_STAGE_IDLE)
  {
    boot_mode_request.stage      = USB_BOOT_MODE_REQ_STAGE_ARMED;
    boot_mode_request.log_pending = true;
    boot_mode_request.next_mode  = mode;
    boot_mode_request.delta_us   = measured_delta_us;
    boot_mode_request.expected_us = expected_us;
    boot_mode_request.missed_frames = missed_frames;              // V251004R2 ISR에서 전달한 누락 프레임 기록
    boot_mode_request.ready_ms   = now_ms + USB_BOOT_MONITOR_CONFIRM_DELAY_MS;
    boot_mode_request.timeout_ms = boot_mode_request.ready_ms + USB_BOOT_MONITOR_CONFIRM_DELAY_MS;
    return USB_BOOT_DOWNGRADE_ARMED;
  }

  if (boot_mode_request.stage == USB_BOOT_MODE_REQ_STAGE_ARMED)
  {
    boot_mode_request.next_mode   = mode;
    boot_mode_request.delta_us    = measured_delta_us;
    boot_mode_request.expected_us = expected_us;
    boot_mode_request.missed_frames = missed_frames;              // V251004R2 누락 프레임 갱신

    if ((int32_t)(now_ms - (int32_t)boot_mode_request.ready_ms) >= 0)
    {
      boot_mode_request.stage       = USB_BOOT_MODE_REQ_STAGE_COMMIT;
      boot_mode_request.log_pending = true;
      return USB_BOOT_DOWNGRADE_CONFIRMED;
    }

    return USB_BOOT_DOWNGRADE_ARMED;
  }

  return USB_BOOT_DOWNGRADE_REJECTED;
}

void usbProcess(void)                                                                  // V250924R3 USB 안정성 이벤트 처리 루프
{
  usb_boot_mode_request_stage_t stage = boot_mode_request.stage;                       // V251005R1 Stage 로컬 캐시로 분기 비용 축소

  if (stage == USB_BOOT_MODE_REQ_STAGE_IDLE)                                           // V250924R3 비활성 시 오버헤드 방지
  {
    return;
  }

  switch (stage)
  {
    case USB_BOOT_MODE_REQ_STAGE_ARMED:
      if (boot_mode_request.log_pending == true)
      {
        uint32_t missed_frames = usbBootModeRequestMissedFrames();             // V251001R5 누락 프레임 수 로깅
        logPrintf("[NG] USB poll instability detected: expected %lu us, measured %lu us (~%lu frames, awaiting confirmation)\n",
                  boot_mode_request.expected_us,
                  boot_mode_request.delta_us,
                  missed_frames);
        logPrintf("[NG] USB poll downgrade pending -> %s\n", usbBootModeLabel(boot_mode_request.next_mode)); // V251001R5 영문화
        boot_mode_request.log_pending = false;
      }

      {
        uint32_t now_ms = millis();                                           // V251005R1 타임아웃 계산 경로에서만 millis() 호출

        if ((int32_t)(now_ms - (int32_t)boot_mode_request.timeout_ms) >= 0)
        {
          usbBootModeRequestReset();
        }
      }
      break;

    case USB_BOOT_MODE_REQ_STAGE_COMMIT:
      if (boot_mode_request.log_pending == true)
      {
        uint32_t missed_frames = usbBootModeRequestMissedFrames();             // V251001R5 누락 프레임 수 로깅
        logPrintf("[NG] USB poll instability confirmed: expected %lu us, measured %lu us (~%lu frames)\n",
                  boot_mode_request.expected_us,
                  boot_mode_request.delta_us,
                  missed_frames);
        logPrintf("[NG] USB poll downgrade -> %s\n", usbBootModeLabel(boot_mode_request.next_mode)); // V251001R5 영문화
        boot_mode_request.log_pending = false;
      }

      if (usbBootModeSaveAndReset(boot_mode_request.next_mode) != true)
      {
        logPrintf("[NG] USB poll downgrade persist failed\n");                                 // V251001R5 영문화
      }

      usbBootModeRequestReset();
      break;

    default:
      break;
  }
}

#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
static void cliBoot(cli_args_t *args);                                       // V250923R1 Boot mode CLI handler
#endif




bool usbInit(void)
{
#ifdef _USE_HW_USB
  usbBootModeRequestReset();
#endif
#ifdef _USE_HW_CLI
  cliAdd("usb", cliCmd);
  cliAdd("boot", cliBoot);                                    // V250923R1 Expose boot mode control
#endif
  return true;
}

bool usbBegin(UsbMode_t usb_mode)
{
  is_init = true;

  if (usb_mode == USB_CDC_MODE)
  {
    #if HW_USB_CDC == 1    
    /* Init Device Library */
    USBD_Init(&USBD_Device, &VCP_Desc, DEVICE_HS);

    /* Add Supported Class */
    USBD_RegisterClass(&USBD_Device, USBD_CDC_CLASS);

    /* Add CDC Interface Class */
    USBD_CDC_RegisterInterface(&USBD_Device, &USBD_CDC_fops);

    /* Start Device Process */
    USBD_Start(&USBD_Device);

    // HAL_PWREx_EnableUSBVoltageDetector();

    is_usb_mode = USB_CDC_MODE;

    p_desc = &VCP_Desc;
    logPrintf("[OK] usbBegin()\n");
    logPrintf("     USB_CDC\r\n");
    logPrintf("     BootMode  : %s\r\n", usbBootModeLabel(usbBootModeGet())); // V250923R1 Report selected polling mode
    #endif
  }
  else if (usb_mode == USB_MSC_MODE)
  {
    #if HW_USB_MSC == 1
    /* Init Device Library */
    USBD_Init(&USBD_Device, &MSC_Desc, DEVICE_HS);

    /* Add Supported Class */
    USBD_RegisterClass(&USBD_Device, USBD_MSC_CLASS);

    /* Add Storage callbacks for MSC Class */
    USBD_MSC_RegisterStorage(&USBD_Device, &USBD_DISK_fops);

    /* Start Device Process */
    USBD_Start(&USBD_Device);

    HAL_PWREx_EnableUSBVoltageDetector();

    is_usb_mode = USB_MSC_MODE;

    p_desc = &MSC_Desc;
    logPrintf("[OK] usbBegin()\n");
    logPrintf("     USB_MSC\r\n");
    logPrintf("     BootMode  : %s\r\n", usbBootModeLabel(usbBootModeGet())); // V250923R1 Report selected polling mode
    #endif
  }
  else if (usb_mode == USB_HID_MODE)
  {
    #if HW_USB_HID == 1
    /* Init Device Library */
    USBD_Init(&USBD_Device, &HID_Desc, DEVICE_HS);

    /* Add Supported Class */
    USBD_RegisterClass(&USBD_Device, USBD_HID_CLASS);

    /* Start Device Process */
    USBD_Start(&USBD_Device);

    is_usb_mode = USB_HID_MODE;

    p_desc = &HID_Desc;
    logPrintf("[OK] usbBegin()\n");
    logPrintf("     USB_HID\r\n");
    logPrintf("     BootMode  : %s\r\n", usbBootModeLabel(usbBootModeGet())); // V250923R1 Report selected polling mode
    #endif
  }
  else if (usb_mode == USB_CMP_MODE)
  {
    #if HW_USB_CMP == 1
    USBD_Init(&USBD_Device, &CMP_Desc, DEVICE_HS);


    /* Add Supported Class */
    USBD_RegisterClassComposite(&USBD_Device, USBD_HID_CLASS, CLASS_TYPE_HID, hid_ep_tbl);
    USBD_RegisterClassComposite(&USBD_Device, USBD_CDC_CLASS, CLASS_TYPE_CDC, cdc_ep_tbl);

    USBD_CDC_RegisterInterface(&USBD_Device, &USBD_CDC_fops);

    /* Start Device Process */
    USBD_Start(&USBD_Device);

    is_usb_mode = USB_CDC_MODE;

    p_desc = &CMP_Desc;
    logPrintf("[OK] usbBegin()\n");
    logPrintf("     USB_CMP\r\n");
    logPrintf("     BootMode  : %s\r\n", usbBootModeLabel(usbBootModeGet())); // V250923R1 Report selected polling mode
    #endif
  }
  else
  {
    is_init = false;

    logPrintf("[NG] usbBegin()\n");
  }

  return is_init;
}

void usbDeInit(void)
{
  if (is_init == true)
  {
    USBD_DeInit(&USBD_Device);
  }
}

bool usbIsOpen(void)
{
  #if HW_USB_CDC == 1
  return cdcIsConnect();
  #else
  return false;
  #endif
}

bool usbIsConnect(void)
{
  if (USBD_Device.pClassData == NULL)
  {
    return false;
  }
  if (USBD_Device.dev_state != USBD_STATE_CONFIGURED)
  {
    return false;
  }
  if (USBD_Device.dev_config == 0)
  {
    return false;
  }
  if (USBD_is_connected() == false)
  {
    return false;
  }
  
  return true;
}

bool usbIsSuspended(void)
{
  return USBD_is_suspended();
}

UsbMode_t usbGetMode(void)
{
  return is_usb_mode;
}

UsbType_t usbGetType(void)
{
#if HW_USB_CDC == 1  
  return (UsbType_t)cdcGetType();
#elif HW_USE_KBD == 1
  return USB_CON_KBD;
#else
  return USB_CON_CDC;
#endif
}

void OTG_HS_IRQHandler(void)
{
  HAL_PCD_IRQHandler(&hpcd_USB_OTG_HS);
}


#ifdef _USE_HW_CLI
void cliCmd(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    uint16_t vid = 0;
    uint16_t pid = 0;
    uint8_t *p_data;
    uint16_t len = 0;


    if (p_desc != NULL)
    {
      p_data = p_desc->GetDeviceDescriptor(USBD_SPEED_HIGH, &len);
      vid = (p_data[ 9]<<8)|(p_data[ 8]<<0);
      pid = (p_data[11]<<8)|(p_data[10]<<0);
    }

    cliPrintf("USB PID     : 0x%04X\n", vid);
    cliPrintf("USB VID     : 0x%04X\n", pid);

    while(cliKeepLoop())
    {
      cliPrintf("USB Mode    : %d\n", usbGetMode());
      cliPrintf("USB Type    : %d\n", usbGetType());
      cliPrintf("USB Connect : %d\n", usbIsConnect());
      cliPrintf("USB Open    : %d\n", usbIsOpen());
      cliPrintf("\x1B[%dA", 4);
      delay(100);
    }
    cliPrintf("\x1B[%dB", 4);

    ret = true;
  }
#if HW_USB_CDC == 1
  if (args->argc == 1 && args->isStr(0, "tx") == true)
  {
    uint32_t pre_time;
    uint32_t tx_cnt = 0;
    uint32_t sent_len = 0;

    pre_time = millis();
    while(cliKeepLoop())
    {
      if (millis()-pre_time >= 1000)
      {
        pre_time = millis();
        logPrintf("tx : %d KB/s\n", tx_cnt/1024);
        tx_cnt = 0;
      }
      sent_len = cdcWrite((uint8_t *)"123456789012345678901234567890\n", 31);
      tx_cnt += sent_len;
    }
    cliPrintf("\x1B[%dB", 2);

    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "rx") == true)
  {
    uint32_t pre_time;
    uint32_t rx_cnt = 0;
    uint32_t rx_len;

    pre_time = millis();
    while(cliKeepLoop())
    {
      if (millis()-pre_time >= 1000)
      {
        pre_time = millis();
        logPrintf("rx : %d KB/s\n", rx_cnt/1024);
        rx_cnt = 0;
      }

      rx_len = cdcAvailable();

      for (int i=0; i<rx_len; i++)
      {
        cdcRead();
      }

      rx_cnt += rx_len;
    }
    cliPrintf("\x1B[%dB", 2);

    ret = true;
  }
#endif

  if (ret == false)
  {
    cliPrintf("usb info\n");
    #if HW_USB_CDC == 1
    cliPrintf("usb tx\n");
    cliPrintf("usb rx\n");
    #endif
  }
}

void cliBoot(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    cliPrintf("Boot Mode   : %s\n", usbBootModeLabel(usbBootModeGet()));       // V250923R1 Display stored mode
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "set") == true)
  {
    UsbBootMode_t req_mode = USB_BOOT_MODE_MAX;

    if (args->isStr(1, "8k") == true)
    {
      req_mode = USB_BOOT_MODE_HS_8K;
    }
    else if (args->isStr(1, "4k") == true)
    {
      req_mode = USB_BOOT_MODE_HS_4K;
    }
    else if (args->isStr(1, "2k") == true)
    {
      req_mode = USB_BOOT_MODE_HS_2K;
    }
    else if (args->isStr(1, "1k") == true)
    {
      req_mode = USB_BOOT_MODE_FS_1K;
    }

    if (req_mode < USB_BOOT_MODE_MAX)
    {
      cliPrintf("Boot Mode   : %s -> %s\n", usbBootModeLabel(usbBootModeGet()), usbBootModeLabel(req_mode));
      if (usbBootModeSaveAndReset(req_mode) != true)
      {
        cliPrintf("Boot mode save failed\n");
      }
      ret = true;
    }
  }

  if (ret == false)
  {
    cliPrintf("boot info\n");
    cliPrintf("boot set 8k\n");
    cliPrintf("boot set 4k\n");
    cliPrintf("boot set 2k\n");
    cliPrintf("boot set 1k\n");
  }
}
#endif

#endif