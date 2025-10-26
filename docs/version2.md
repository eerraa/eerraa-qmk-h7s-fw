# 버전2
------------------
diff --git a/src/hw/driver/usb/usb_hid/usbd_hid.c b/src/hw/driver/usb/usb_hid/usbd_hid.c
index efdc620d1242855a99c63dad4fc0fda5f82b0daa..fe9e0386af12dd3bd5d6db064841c326aa82421a 100644
--- a/src/hw/driver/usb/usb_hid/usbd_hid.c
+++ b/src/hw/driver/usb/usb_hid/usbd_hid.c
@@ -21,120 +21,177 @@
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
+#include "micros.h"                                             // V251010R9: SOF/타이머 보정 루프에서 1us 타임스탬프 사용
+#include "stm32h7rsxx_ll_tim.h"                                 // V251010R9: TIM2 CCR 갱신을 LL 계층으로 직접 수행
 
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
+static void usbHidTimerSyncInit(void);                           // V251010R9: TIM2 비교 일정을 SOF 기준으로 동기화
+static void usbHidTimerSyncOnSof(USBD_HandleTypeDef *pdev, uint32_t now_us);  // V251010R9: SOF 시점 캡처 및 속도 전환 처리
+static void usbHidTimerSyncOnPulse(void);                        // V251010R9: TIM2 펄스 측정 및 PI 보정 적용
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
 
+enum
+{
+  USB_HID_TIMER_SYNC_FLAG_ACTIVE    = 0x01U,  // V251010R9: 보정 루프 활성화 상태 플래그
+  USB_HID_TIMER_SYNC_FLAG_WARMUP    = 0x02U,  // V251010R9: 워밍업 진행 중임을 표시
+  USB_HID_TIMER_SYNC_FLAG_SATURATED = 0x04U,  // V251010R9: PI 항이 한계에 도달했음을 표시
+  USB_HID_TIMER_SYNC_FLAG_RESET     = 0x08U   // V251010R9: 샘플 이상으로 리셋이 수행됐음을 표시
+};
+
+typedef struct
+{
+  uint32_t last_sof_us;       // V251010R9: 최신 SOF 타임스탬프(us)
+  uint32_t target_delay_us;   // V251010R9: TIM2 비교 목표 지연(us)
+  uint32_t frame_period_us;   // V251010R9: 현재 USB 속도의 기대 SOF 주기(us)
+  uint32_t guard_band_us;     // V251010R9: 오류 한계(us)
+  uint16_t nominal_ticks;     // V251010R9: CCR1 기본값
+  uint16_t min_ticks;         // V251010R9: CCR1 하한 보호
+  uint16_t max_ticks;         // V251010R9: CCR1 상한 보호
+  uint16_t current_ticks;     // V251010R9: 최근 적용된 CCR1 값
+  int32_t  integral;          // V251010R9: PI 적분 상태(us 누적)
+  int32_t  integral_min;      // V251010R9: 적분 하한
+  int32_t  integral_max;      // V251010R9: 적분 상한
+  uint8_t  kp_shift;          // V251010R9: 비례 이득(2^-kp_shift)
+  uint8_t  ki_shift;          // V251010R9: 적분 이득(2^-ki_shift)
+  uint8_t  warmup_frames;     // V251010R9: 워밍업에 필요한 유효 샘플 수
+  uint8_t  warmup_count;      // V251010R9: 누적된 유효 샘플 수
+  uint8_t  active_speed;      // V251010R9: 최근 적용된 USB 속도 코드
+  bool     active;            // V251010R9: 보정 루프 활성 여부
+  bool     reset_pending;     // V251010R9: 외부 조건으로 리셋되었음을 차기 펄스에 통지
+} usb_hid_timer_sync_t;
+
+static usb_hid_timer_sync_t timer_sync =
+{
+  .last_sof_us     = 0U,
+  .target_delay_us = 120U,
+  .frame_period_us = 125U,
+  .guard_band_us   = 24U,
+  .nominal_ticks   = 120U,
+  .min_ticks       = 104U,
+  .max_ticks       = 152U,
+  .current_ticks   = 120U,
+  .integral        = 0,
+  .integral_min    = -1024,
+  .integral_max    = 1024,
+  .kp_shift        = 3U,
+  .ki_shift        = 8U,
+  .warmup_frames   = 4U,
+  .warmup_count    = 0U,
+  .active_speed    = 0xFFU,
+  .active          = false,
+  .reset_pending   = false
+};  // V251010R9: USB 백업 타이머 PI 상태 기본값 초기화
+
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
@@ -1054,58 +1111,57 @@ static uint8_t USBD_HID_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
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
-#if _USE_USB_MONITOR || _DEF_ENABLE_USB_HID_TIMING_PROBE
-  uint32_t sof_now_us = usbHidInstrumentationNow();                   // V251009R7: SOF 타임스탬프는 모니터/계측 공용으로 취득
+  uint32_t sof_now_us = micros();                                     // V251010R9: SOF 동기화 및 모니터 공유 타임스탬프 취득
+  usbHidTimerSyncOnSof(pdev, sof_now_us);                             // V251010R9: 타이머 보정 루프에 SOF 시점 전달
 #if _USE_USB_MONITOR
-  usbHidMonitorSof(sof_now_us);                                       // V251009R7: 모니터 활성 시 타임스탬프 전달
+  usbHidMonitorSof(sof_now_us);                                       // V251010R9: 모니터 역시 동일 타임스탬프 사용
 #endif
 #if _DEF_ENABLE_USB_HID_TIMING_PROBE
-  usbHidInstrumentationOnSof(sof_now_us);                             // V251009R7: 계측 활성 시 샘플 윈도우 갱신
-#endif
+  usbHidInstrumentationOnSof(sof_now_us);                             // V251010R9: 계측 윈도우 갱신에 동일 샘플 제공
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
@@ -1395,137 +1451,363 @@ static void usbHidMonitorSof(uint32_t now_us)
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
 
+static void usbHidTimerSyncConfigure(uint8_t dev_speed)
+{
+  timer_sync.target_delay_us = 120U;                              // V251010R9: HS/FS 공용 타겟 지연(us)
+  timer_sync.nominal_ticks   = 120U;                              // V251010R9: CCR1 기본값 초기화
+  timer_sync.current_ticks   = timer_sync.nominal_ticks;          // V251010R9: 초기 CCR1을 타겟에 맞춤
+  timer_sync.min_ticks       = 104U;                              // V251010R9: HS 모드 기본 하한(±16us)
+  timer_sync.max_ticks       = 152U;                              // V251010R9: HS 모드 기본 상한(±32us)
+  timer_sync.frame_period_us = 125U;                              // V251010R9: HS SOF 주기(us)
+  timer_sync.guard_band_us   = 24U;                               // V251010R9: 오차 한계(us)
+  timer_sync.kp_shift        = 3U;                                // V251010R9: Kp=1/8
+  timer_sync.ki_shift        = 8U;                                // V251010R9: Ki=1/256
+  timer_sync.integral_min    = -1024;                             // V251010R9: 적분 항 하한(약 ±4틱)
+  timer_sync.integral_max    = 1024;                              // V251010R9: 적분 항 상한(약 ±4틱)
+  timer_sync.warmup_frames   = 4U;                                // V251010R9: 초기 4샘플 워밍업 유지
+
+  if (dev_speed == USBD_SPEED_FULL)
+  {
+    timer_sync.frame_period_us = 1000U;                           // V251010R9: FS SOF 주기(us)
+    timer_sync.guard_band_us   = 64U;                             // V251010R9: FS는 허용 오차 확대
+    timer_sync.kp_shift        = 4U;                              // V251010R9: Kp=1/16으로 완만하게 조정
+    timer_sync.ki_shift        = 9U;                              // V251010R9: Ki=1/512로 적분 완화
+    timer_sync.min_ticks       = 100U;                            // V251010R9: FS 구간 하한 확장
+    timer_sync.max_ticks       = 220U;                            // V251010R9: FS 구간 상한 확장
+    timer_sync.integral_min    = -2048;                           // V251010R9: FS 적분 범위 확장
+    timer_sync.integral_max    = 2048;                            // V251010R9: FS 적분 범위 확장
+    timer_sync.warmup_frames   = 6U;                              // V251010R9: FS 샘플 간 간격이 길어 워밍업 증가
+  }
+}
+
+static void usbHidTimerSyncInit(void)
+{
+  usbHidTimerSyncConfigure(USBD_SPEED_HIGH);                      // V251010R9: 초기 파라미터를 HS 기준으로 준비
+  timer_sync.integral      = 0;
+  timer_sync.warmup_count  = 0U;
+  timer_sync.active_speed  = 0xFFU;
+  timer_sync.active        = false;
+  timer_sync.reset_pending = false;
+  timer_sync.last_sof_us   = 0U;
+  LL_TIM_OC_SetCompareCH1(TIM2, timer_sync.current_ticks);        // V251010R9: CCR1 초기값을 미리 기록
+}
+
+static void usbHidTimerSyncOnSof(USBD_HandleTypeDef *pdev, uint32_t now_us)
+{
+  uint8_t dev_speed = (pdev->dev_state == USBD_STATE_CONFIGURED) ? pdev->dev_speed : 0xFFU;
+
+  if (dev_speed != USBD_SPEED_HIGH && dev_speed != USBD_SPEED_FULL)
+  {
+    if (timer_sync.active)
+    {
+      timer_sync.active        = false;                           // V251010R9: 연결 해제 시 보정 루프 비활성화
+      timer_sync.active_speed  = 0xFFU;
+      timer_sync.integral      = 0;
+      timer_sync.warmup_count  = 0U;
+      timer_sync.reset_pending = true;                            // V251010R9: 다음 펄스에 리셋 상태 보고
+    }
+    timer_sync.last_sof_us = 0U;
+    return;
+  }
+
+  if (!timer_sync.active || timer_sync.active_speed != dev_speed)
+  {
+    usbHidTimerSyncConfigure(dev_speed);                          // V251010R9: 속도 전환 시 파라미터 재적용
+    timer_sync.integral      = 0;
+    timer_sync.warmup_count  = 0U;
+    timer_sync.active        = true;
+    timer_sync.active_speed  = dev_speed;
+    timer_sync.current_ticks = timer_sync.nominal_ticks;
+    timer_sync.reset_pending = true;
+    LL_TIM_OC_SetCompareCH1(TIM2, timer_sync.current_ticks);      // V251010R9: 속도별 기본 지연으로 재설정
+  }
+  else if (timer_sync.last_sof_us != 0U)
+  {
+    uint32_t interval_us = now_us - timer_sync.last_sof_us;
+
+    if (interval_us > (timer_sync.frame_period_us + timer_sync.guard_band_us))
+    {
+      timer_sync.integral      = 0;                               // V251010R9: SOF 손실 시 적분 상태 초기화
+      timer_sync.warmup_count  = 0U;
+      timer_sync.current_ticks = timer_sync.nominal_ticks;
+      timer_sync.reset_pending = true;
+      LL_TIM_OC_SetCompareCH1(TIM2, timer_sync.current_ticks);    // V251010R9: 이상 간격에서는 CCR을 기본값으로 복구
+    }
+  }
+
+  timer_sync.last_sof_us = now_us;                                // V251010R9: 최신 SOF 타임스탬프 저장
+}
+
+static void usbHidTimerSyncOnPulse(void)
+{
+  uint32_t measured_delay_us = 0U;
+  int32_t  raw_error_us      = 0;
+  uint16_t status_flags      = 0U;
+  bool     saturated         = false;
+
+  if (timer_sync.active)
+  {
+    status_flags |= USB_HID_TIMER_SYNC_FLAG_ACTIVE;               // V251010R9: 활성 상태 보고
+    if (timer_sync.warmup_count < timer_sync.warmup_frames)
+    {
+      status_flags |= USB_HID_TIMER_SYNC_FLAG_WARMUP;             // V251010R9: 워밍업 중임을 표시
+    }
+    if (timer_sync.reset_pending)
+    {
+      status_flags |= USB_HID_TIMER_SYNC_FLAG_RESET;              // V251010R9: 직전 SOF 단계에서 리셋이 발생했음을 보고
+    }
+  }
+
+  if (!timer_sync.active || timer_sync.last_sof_us == 0U)
+  {
+    usbHidInstrumentationUpdateTimerSync(measured_delay_us,
+                                         raw_error_us,
+                                         timer_sync.integral,
+                                         timer_sync.current_ticks,
+                                         status_flags);            // V251010R9: 비활성 상태도 계측에 전달
+    timer_sync.reset_pending = false;
+    return;
+  }
+
+  measured_delay_us = micros() - timer_sync.last_sof_us;          // V251010R9: SOF 이후 TIM2 비교까지 경과 시간 측정
+  raw_error_us      = (int32_t)timer_sync.target_delay_us - (int32_t)measured_delay_us;
+
+  uint32_t max_valid_delay = timer_sync.frame_period_us + timer_sync.guard_band_us;
+  if (measured_delay_us == 0U || measured_delay_us > max_valid_delay)
+  {
+    timer_sync.integral      = 0;                                 // V251010R9: 이상 샘플에서는 상태를 초기화
+    timer_sync.warmup_count  = 0U;
+    timer_sync.current_ticks = timer_sync.nominal_ticks;
+    timer_sync.reset_pending = true;
+    status_flags |= USB_HID_TIMER_SYNC_FLAG_RESET;                // V251010R9: 이상 샘플로 인한 리셋 표시
+    LL_TIM_OC_SetCompareCH1(TIM2, timer_sync.current_ticks);      // V251010R9: CCR1을 즉시 기본값으로 복구
+    usbHidInstrumentationUpdateTimerSync(measured_delay_us,
+                                         raw_error_us,
+                                         timer_sync.integral,
+                                         timer_sync.current_ticks,
+                                         status_flags);
+    timer_sync.reset_pending = false;
+    return;
+  }
+
+  int32_t guard_limit    = (int32_t)timer_sync.guard_band_us;
+  int32_t limited_error  = raw_error_us;
+  if (limited_error > guard_limit)
+  {
+    limited_error = guard_limit;                                  // V251010R9: 오차는 보호 한계 내로 클램프
+  }
+  else if (limited_error < -guard_limit)
+  {
+    limited_error = -guard_limit;
+  }
+
+  if (timer_sync.warmup_count < timer_sync.warmup_frames)
+  {
+    timer_sync.warmup_count++;
+    timer_sync.integral      = 0;                                 // V251010R9: 워밍업 구간에서는 적분을 리셋
+    timer_sync.current_ticks = timer_sync.nominal_ticks;
+  }
+  else
+  {
+    timer_sync.integral += limited_error;                         // V251010R9: PI 적분 항 누적
+    if (timer_sync.integral > timer_sync.integral_max)
+    {
+      timer_sync.integral = timer_sync.integral_max;
+      saturated = true;
+    }
+    else if (timer_sync.integral < timer_sync.integral_min)
+    {
+      timer_sync.integral = timer_sync.integral_min;
+      saturated = true;
+    }
+
+    int32_t p_term        = limited_error >> timer_sync.kp_shift; // V251010R9: 비례 항 계산
+    int32_t i_term        = timer_sync.integral >> timer_sync.ki_shift; // V251010R9: 적분 항 계산
+    int32_t desired_ticks = (int32_t)timer_sync.nominal_ticks + p_term + i_term;
+
+    if (desired_ticks > (int32_t)timer_sync.current_ticks + 1)
+    {
+      desired_ticks = (int32_t)timer_sync.current_ticks + 1;      // V251010R9: 프레임당 ±1틱 제한으로 지터 억제
+    }
+    else if (desired_ticks < (int32_t)timer_sync.current_ticks - 1)
+    {
+      desired_ticks = (int32_t)timer_sync.current_ticks - 1;
+    }
+
+    if (desired_ticks > (int32_t)timer_sync.max_ticks)
+    {
+      desired_ticks = (int32_t)timer_sync.max_ticks;
+      saturated = true;
+    }
+    else if (desired_ticks < (int32_t)timer_sync.min_ticks)
+    {
+      desired_ticks = (int32_t)timer_sync.min_ticks;
+      saturated = true;
+    }
+
+    timer_sync.current_ticks = (uint16_t)desired_ticks;
+  }
+
+  LL_TIM_OC_SetCompareCH1(TIM2, timer_sync.current_ticks);        // V251010R9: 계산된 CCR1 값을 즉시 적용
+
+  if (saturated)
+  {
+    status_flags |= USB_HID_TIMER_SYNC_FLAG_SATURATED;            // V251010R9: 포화 상태 보고
+  }
+  if (timer_sync.warmup_count < timer_sync.warmup_frames)
+  {
+    status_flags |= USB_HID_TIMER_SYNC_FLAG_WARMUP;               // V251010R9: 워밍업 지속 여부 갱신
+  }
+  if (timer_sync.reset_pending)
+  {
+    status_flags |= USB_HID_TIMER_SYNC_FLAG_RESET;                // V251010R9: SOF 단계 리셋 보고
+    timer_sync.reset_pending = false;
+  }
+
+  usbHidInstrumentationUpdateTimerSync(measured_delay_us,
+                                       raw_error_us,
+                                       timer_sync.integral,
+                                       timer_sync.current_ticks,
+                                       status_flags);             // V251010R9: 계측/CLI용 상태 전달
+}
+
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
 
+  usbHidTimerSyncInit();                                         // V251010R9: TIM2 비교 보정 상태 초기화
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
+  if (htim->Instance != TIM2)
+  {
+    return;                                                       // V251010R9: USB 백업 타이머 이외의 콜백은 무시
+  }
 #if _DEF_ENABLE_USB_HID_TIMING_PROBE
   usbHidInstrumentationOnTimerPulse();                                 // V251009R7: 계측 타이머 후크를 조건부 실행
 #endif
+  usbHidTimerSyncOnPulse();                                         // V251010R9: TIM2 비교 시점에서 PI 보정 수행
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
------------------
diff --git a/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c b/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c
index 8d9773d8476df44a6b7008ec54ad93d9c1af45ab..280ccfd68fb81e03578be9d07d6f8f6fc70b294c 100644
--- a/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c
+++ b/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c
@@ -23,51 +23,55 @@ static uint32_t rate_time_max = 0;
 static uint32_t rate_time_min_check = 0xFFFF;
 static uint32_t rate_time_max_check = 0;
 static uint32_t rate_time_excess_max = 0;                    // V250928R3 폴링 지연 초과분 누적 최대값
 static uint32_t rate_time_excess_max_check = 0;              // V250928R3 윈도우 내 초과분 최대값 추적
 static uint32_t rate_queue_depth_snapshot = 0;               // V250928R3 폴링 시작 시점의 큐 길이 스냅샷
 static uint32_t rate_queue_depth_max = 0;                    // V250928R3 큐 잔량 최대값
 static uint32_t rate_queue_depth_max_check = 0;              // V250928R3 윈도우 내 큐 잔량 최대값 추적
 
 static uint32_t rate_time_sof_pre = 0;
 static uint32_t rate_time_sof = 0;                         // V251010R8: 직전 SOF 간격(us)
 
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
 
 static volatile uint32_t timer_pulse_total = 0;              // V251010R8: TIM2 펄스 누적 카운트
-static volatile uint32_t timer_sof_offset_us = 0;            // V251010R8: TIM2 펄스 시점의 SOF 기준 지연(us)
+static volatile uint32_t timer_sof_offset_us = 0;            // V251010R9: 보정 루프가 측정한 SOF 대비 지연(us)
+static volatile int32_t  timer_sync_error_us = 0;            // V251010R9: PI 루프의 생(raw) 오차(us)
+static volatile int32_t  timer_sync_integral = 0;            // V251010R9: 적분 상태(us 누적)
+static volatile uint16_t timer_sync_ccr_ticks = 120;         // V251010R9: 최신 CCR1 값
+static volatile uint16_t timer_sync_status = 0;              // V251010R9: 상태 플래그 비트필드
 static volatile uint32_t sof_total = 0;                      // V251010R8: SOF 누적 카운트
 
 static uint32_t usbHidExpectedPollIntervalUs(void);
 
 #endif
 
 #if _DEF_ENABLE_USB_HID_TIMING_PROBE
 
 uint32_t usbHidInstrumentationNow(void)
 {
 #if _USE_USB_MONITOR || _DEF_ENABLE_USB_HID_TIMING_PROBE
   return micros();  // V251009R9: 모니터 또는 계측 활성 시에만 타이머 접근
 #else
   return 0U;
 #endif
 }
 
 void usbHidInstrumentationOnSof(uint32_t now_us)
 {
   static uint32_t sample_cnt = 0;
   uint32_t        sample_window = usbBootModeIsFullSpeed() ? 1000U : 8000U; // V251009R9: USB 속도에 맞춰 윈도우 계산 유지
 
   if (rate_time_sof_pre != 0U)
   {
     rate_time_sof = now_us - rate_time_sof_pre;                 // V251010R8: 연속 SOF 간격을 직접 측정해 보고
@@ -82,82 +86,91 @@ void usbHidInstrumentationOnSof(uint32_t now_us)
     rate_time_max = rate_time_max_check;
     if (data_in_cnt > 0U)
     {
       rate_time_avg = rate_time_sum / data_in_cnt;              // V251010R8: 샘플 수만큼 나누어 평균 증가 편향 제거
     }
     else
     {
       rate_time_avg = 0U;
     }
     rate_time_excess_max = rate_time_excess_max_check;               // V251009R9: 폴링 초과 지연을 윈도우 경계에서 라치
     rate_queue_depth_max = rate_queue_depth_max_check;
     data_in_cnt = 0;
 
     rate_time_min_check = 0xFFFF;
     rate_time_max_check = 0;
     rate_time_sum = 0;
     rate_time_excess_max_check = 0;
     rate_queue_depth_max_check = 0;
   }
   sample_cnt++;
 }
 
 void usbHidInstrumentationOnTimerPulse(void)
 {
   timer_pulse_total++;                                           // V251010R8: TIM2 펄스 누적
-  if (rate_time_sof_pre != 0U)
-  {
-    timer_sof_offset_us = micros()-rate_time_sof_pre;            // V251010R8: TIM2 펄스 지연을 SOF 기준으로 추적
-  }
 }
 
 void usbHidInstrumentationOnDataIn(void)
 {
   data_in_cnt++;
 }
 
 void usbHidInstrumentationOnReportDequeued(uint32_t queued_reports)
 {
   key_time_req = true;
   rate_time_req = true;
   rate_time_pre = micros();
   rate_queue_depth_snapshot = (queued_reports > 0U) ? (queued_reports - 1U) : 0U;
 }
 
 void usbHidInstrumentationOnImmediateSendSuccess(uint32_t queued_reports)
 {
   key_time_req = true;
   rate_time_req = true;
   rate_time_pre = micros();
   rate_queue_depth_snapshot = queued_reports;
 }
 
 void usbHidInstrumentationMarkReportStart(void)
 {
   key_time_pre = micros();
 }
 
+void usbHidInstrumentationUpdateTimerSync(uint32_t delay_us,
+                                          int32_t  error_us,
+                                          int32_t  integral_state,
+                                          uint16_t current_ticks,
+                                          uint16_t status_flags)
+{
+  timer_sof_offset_us = delay_us;                               // V251010R9: 보정 루프 측정 지연을 CLI에 전달
+  timer_sync_error_us = error_us;
+  timer_sync_integral = integral_state;
+  timer_sync_ccr_ticks = current_ticks;
+  timer_sync_status    = status_flags;
+}
+
 void usbHidMeasureRateTime(void)
 {
   if (rate_time_req)
   {
     uint32_t rate_time_cur = micros();
 
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
@@ -280,58 +293,63 @@ void usbHidInstrumentationHandleCli(cli_args_t *args)
         uint32_t cur_sof_total = sof_total;                          // V251010R8: 윈도우 내 SOF 누적 증가분 계산
         uint32_t cur_timer_total = timer_pulse_total;                 // V251010R8: TIM2 펄스 누적 증가분 계산
         uint32_t sof_delta = cur_sof_total - prev_sof_total;
         uint32_t timer_delta = cur_timer_total - prev_timer_total;
         prev_sof_total = cur_sof_total;
         prev_timer_total = cur_timer_total;
 
         uint32_t expected_sof = usbBootModeIsFullSpeed() ? 1000U : 8000U;
         int32_t  sof_diff = (int32_t)sof_delta - (int32_t)expected_sof;
         int32_t  timer_diff = (int32_t)timer_delta - (int32_t)expected_sof;
         uint32_t expected_interval_us = usbHidExpectedPollIntervalUs();
 
         cliPrintf("hid rate %lu Hz (샘플 %lu)\n",
                   (unsigned long)data_in_rate,
                   (unsigned long)data_in_rate);
         cliPrintf("  지연(us)      : 평균 %4lu / 최소 %4lu / 최대 %4lu\n",
                   (unsigned long)rate_time_avg,
                   (unsigned long)rate_time_min,
                   (unsigned long)rate_time_max);
         cliPrintf("  초과 지연(us) : 최대 %4lu (기대 %4lu)\n",
                   (unsigned long)rate_time_excess_max,
                   (unsigned long)expected_interval_us);
         cliPrintf("  큐 잔량       : 최대 %lu / 최근 %lu\n",
                   (unsigned long)rate_queue_depth_max,
                   (unsigned long)rate_queue_depth_snapshot);
-        cliPrintf("  SOF/타이머    : %lu / %lu (기대 %lu, Δ %+ld / %+ld, SOF %4lu us, TIM 오프셋 %4lu us)\n",
+        cliPrintf("  SOF/타이머    : %lu / %lu (기대 %lu, Δ %+ld / %+ld, SOF %4lu us)\n",  // V251010R9: SOF-타이머 동기화 출력 정리
                   (unsigned long)sof_delta,
                   (unsigned long)timer_delta,
                   (unsigned long)expected_sof,
                   (long)sof_diff,
                   (long)timer_diff,
-                  (unsigned long)rate_time_sof,
-                  (unsigned long)timer_sof_offset_us);
+                  (unsigned long)rate_time_sof);
+        cliPrintf("  백업 타이머  : 지연 %4lu us / 오차 %+4ld us / 적분 %+4ld / CCR %3u / 플래그 0x%02X\n",  // V251010R9: PI 보정 상태를 CLI에 노출
+                  (unsigned long)timer_sof_offset_us,
+                  (long)timer_sync_error_us,
+                  (long)timer_sync_integral,
+                  (unsigned int)timer_sync_ccr_ticks,
+                  (unsigned int)timer_sync_status);
 
         uint32_t recent_count = (key_time_cnt < 10U) ? key_time_cnt : 10U;
         if (recent_count > 0U)
         {
           cliPrintf("  최근 지연(us) :");
           for (uint32_t i = 0; i < recent_count; i++)
           {
             uint32_t idx = (key_time_idx + KEY_TIME_LOG_MAX - recent_count + i) % KEY_TIME_LOG_MAX;
             cliPrintf(" %3lu", (unsigned long)key_time_log[idx]);
           }
           cliPrintf("\n");
         }
         else
         {
           cliPrintf("  최근 지연(us) : 기록 없음\n");
         }
 
         key_send_cnt = 0;
       }
     }
 
     if (args->argc == 2 && args->isStr(1, "his"))
     {
       for (int i=0; i<100; i++)
       {
------------------
diff --git a/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h b/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h
index a12a41fe38d03ae21db62825f56210c28488019e..1408659ffc4372292a9fe5f258ef7ce560bccd58 100644
--- a/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h
+++ b/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h
@@ -1,72 +1,90 @@
 #pragma once
 
 #include <stdbool.h>
 #include <stdint.h>
 
 #include "def.h"  // V251010R1: 인라인 스텁에서 _USE_USB_MONITOR 플래그를 참조
 #include "cli.h"
 #include "usbd_hid.h"
 #include "usbd_hid_internal.h"
 
 #if _DEF_ENABLE_USB_HID_TIMING_PROBE
 
 uint32_t usbHidInstrumentationNow(void);
 void     usbHidInstrumentationOnSof(uint32_t now_us);
 void     usbHidInstrumentationOnTimerPulse(void);
 void     usbHidInstrumentationOnDataIn(void);
 void     usbHidInstrumentationOnReportDequeued(uint32_t queued_reports);
 void     usbHidInstrumentationOnImmediateSendSuccess(uint32_t queued_reports);
 void     usbHidInstrumentationMarkReportStart(void);
 void     usbHidMeasureRateTime(void);
+void     usbHidInstrumentationUpdateTimerSync(uint32_t delay_us,
+                                              int32_t  error_us,
+                                              int32_t  integral_state,
+                                              uint16_t current_ticks,
+                                              uint16_t status_flags);  // V251010R9: TIM2 보정 루프 계측 스냅샷 전달
 
 #else
 
 #include "micros.h"  // V251010R1: 계측 비활성 시 릴리스 경로에서 호출 오버헤드 제거용 인라인 스텁 제공
 
 static inline uint32_t usbHidInstrumentationNow(void)
 {
 #if _USE_USB_MONITOR || _DEF_ENABLE_USB_HID_TIMING_PROBE
   return micros();  // V251010R1: 모니터 활성 시 타임스탬프 유지
 #else
   return 0U;
 #endif
 }
 
 static inline void usbHidInstrumentationOnSof(uint32_t now_us)
 {
   (void)now_us;  // V251010R1: 릴리스 빌드에서 호출 제거
 }
 
 static inline void usbHidInstrumentationOnTimerPulse(void)
 {
   // V251010R1: 릴리스 빌드에서 계측 타이머 콜백 제거
 }
 
 static inline void usbHidInstrumentationOnDataIn(void)
 {
   // V251010R1: 릴리스 빌드에서 데이터 계측 무효화
 }
 
 static inline void usbHidInstrumentationOnReportDequeued(uint32_t queued_reports)
 {
   (void)queued_reports;  // V251010R1: 릴리스 빌드에서 큐 스냅샷 무효화
 }
 
 static inline void usbHidInstrumentationOnImmediateSendSuccess(uint32_t queued_reports)
 {
   (void)queued_reports;  // V251010R1: 릴리스 빌드에서 즉시 전송 계측 무효화
 }
 
 static inline void usbHidInstrumentationMarkReportStart(void)
 {
   // V251010R1: 릴리스 빌드에서 시작 타임스탬프 제거
 }
 
 static inline void usbHidMeasureRateTime(void)
 {
   // V251010R1: 릴리스 빌드에서 폴링 간격 측정 비활성화
 }
 
+static inline void usbHidInstrumentationUpdateTimerSync(uint32_t delay_us,
+                                                        int32_t  error_us,
+                                                        int32_t  integral_state,
+                                                        uint16_t current_ticks,
+                                                        uint16_t status_flags)
+{
+  (void)delay_us;
+  (void)error_us;
+  (void)integral_state;
+  (void)current_ticks;
+  (void)status_flags;  // V251010R9: 릴리스 빌드에서는 타이머 보정 스냅샷을 무시
+}
+
 #endif
 
 void     usbHidInstrumentationHandleCli(cli_args_t *args);
------------------