# 버전3
------------------
diff --git a/src/hw/driver/usb/usb_hid/usbd_hid.c b/src/hw/driver/usb/usb_hid/usbd_hid.c
index efdc620d1242855a99c63dad4fc0fda5f82b0daa..85f2a8a004892d58d06eb533c886d65a7b37e255 100644
--- a/src/hw/driver/usb/usb_hid/usbd_hid.c
+++ b/src/hw/driver/usb/usb_hid/usbd_hid.c
@@ -29,87 +29,163 @@
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
+#include "micros.h"                       // V251010R9: SOF/타이머 간 지연 측정을 직접 수행
+#include "cmsis_gcc.h"                    // V251010R9: 타이머 상태 스냅샷 시 인터럽트 마스크 보존
+#include "stm32h7rsxx_ll_tim.h"           // V251010R9: CCR 업데이트를 위한 LL 직접 접근
 
 
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
+#define USB_HID_TIMER_SYNC_TARGET_TICKS        120U  // V251010R9: SOF 기준 목표 지연(us)
+#define USB_HID_TIMER_SYNC_HS_INTERVAL_US      125U  // V251010R9: HS 모드 마이크로프레임 간격
+#define USB_HID_TIMER_SYNC_FS_INTERVAL_US      1000U // V251010R9: FS 모드 SOF 간격
+#define USB_HID_TIMER_SYNC_HS_MIN_TICKS        96U   // V251010R9: HS 모드 최소 허용 지연(us)
+#define USB_HID_TIMER_SYNC_HS_MAX_TICKS        144U  // V251010R9: HS 모드 최대 허용 지연(us)
+#define USB_HID_TIMER_SYNC_FS_MIN_TICKS        80U   // V251010R9: FS 모드 최소 허용 지연(us)
+#define USB_HID_TIMER_SYNC_FS_MAX_TICKS        220U  // V251010R9: FS 모드 최대 허용 지연(us)
+#define USB_HID_TIMER_SYNC_HS_GUARD_US         32U   // V251010R9: HS 모드 오차 가드 한계
+#define USB_HID_TIMER_SYNC_FS_GUARD_US         80U   // V251010R9: FS 모드 오차 가드 한계
+#define USB_HID_TIMER_SYNC_HS_KP_SHIFT         4U    // V251010R9: HS 비례 제어 시프트(1/16)
+#define USB_HID_TIMER_SYNC_FS_KP_SHIFT         5U    // V251010R9: FS 비례 제어 시프트(1/32)
+#define USB_HID_TIMER_SYNC_HS_INTEGRAL_SHIFT   7U    // V251010R9: HS 적분 항 시프트(1/128)
+#define USB_HID_TIMER_SYNC_FS_INTEGRAL_SHIFT   9U    // V251010R9: FS 적분 항 시프트(1/512)
+#define USB_HID_TIMER_SYNC_HS_INTEGRAL_LIMIT   (32 << USB_HID_TIMER_SYNC_HS_INTEGRAL_SHIFT)  // V251010R9: 적분 포화(HS)
+#define USB_HID_TIMER_SYNC_FS_INTEGRAL_LIMIT   (32 << USB_HID_TIMER_SYNC_FS_INTEGRAL_SHIFT)  // V251010R9: 적분 포화(FS)
+
+typedef enum
+{
+  USB_HID_TIMER_SYNC_SPEED_NONE = 0U,  // V251010R9: 타이머 동기화 미활성 상태
+  USB_HID_TIMER_SYNC_SPEED_HS,         // V251010R9: HS 8k/4k/2k 공용 파라미터
+  USB_HID_TIMER_SYNC_SPEED_FS,         // V251010R9: FS 1k 모드 파라미터
+} usb_hid_timer_sync_speed_t;
+
+typedef struct
+{
+  uint32_t                     last_sof_us;         // V251010R9: 직전 SOF 타임스탬프(us)
+  uint32_t                     last_delay_us;       // V251010R9: 직전 펄스 지연(us)
+  int32_t                      last_error_us;       // V251010R9: 직전 오차(us)
+  int32_t                      integral_accum;      // V251010R9: 적분 누산(오차 합)
+  int32_t                      integral_limit;      // V251010R9: 적분 포화 한계
+  uint16_t                     current_ticks;       // V251010R9: 현재 CCR1 값
+  uint16_t                     default_ticks;       // V251010R9: 목표 지연 틱(120us)
+  uint16_t                     min_ticks;           // V251010R9: 허용 최소 CCR1
+  uint16_t                     max_ticks;           // V251010R9: 허용 최대 CCR1
+  uint16_t                     guard_us;            // V251010R9: 오차 가드(us)
+  uint32_t                     expected_interval_us;// V251010R9: 현재 속도 SOF 간격(us)
+  uint8_t                      kp_shift;            // V251010R9: 비례 항 시프트
+  uint8_t                      integral_shift;      // V251010R9: 적분 항 시프트
+  usb_hid_timer_sync_speed_t   speed;               // V251010R9: 현재 적용 중인 속도
+  bool                         ready;               // V251010R9: SOF 타임스탬프 확보 여부
+  uint32_t                     update_count;        // V251010R9: 보정 적용 횟수
+  uint32_t                     guard_fault_count;   // V251010R9: 가드 초과로 리셋된 횟수
+  uint32_t                     reset_count;         // V251010R9: 초기화 횟수(모드 변경 포함)
+} usb_hid_timer_sync_t;
+
+static volatile usb_hid_timer_sync_t timer_sync =
+{
+  .last_sof_us = 0U,
+  .last_delay_us = 0U,
+  .last_error_us = 0,
+  .integral_accum = 0,
+  .integral_limit = USB_HID_TIMER_SYNC_HS_INTEGRAL_LIMIT,
+  .current_ticks = USB_HID_TIMER_SYNC_TARGET_TICKS,
+  .default_ticks = USB_HID_TIMER_SYNC_TARGET_TICKS,
+  .min_ticks = USB_HID_TIMER_SYNC_HS_MIN_TICKS,
+  .max_ticks = USB_HID_TIMER_SYNC_HS_MAX_TICKS,
+  .guard_us = USB_HID_TIMER_SYNC_HS_GUARD_US,
+  .expected_interval_us = 0U,
+  .kp_shift = USB_HID_TIMER_SYNC_HS_KP_SHIFT,
+  .integral_shift = USB_HID_TIMER_SYNC_HS_INTEGRAL_SHIFT,
+  .speed = USB_HID_TIMER_SYNC_SPEED_NONE,
+  .ready = false,
+  .update_count = 0U,
+  .guard_fault_count = 0U,
+  .reset_count = 0U,
+};
+
+static void usbHidTimerSyncInit(void);                                           // V251010R9: TIM2 비교기 PI 초기화
+static void usbHidTimerSyncOnSof(USBD_HandleTypeDef *pdev, uint32_t now_us);     // V251010R9: SOF 진입 시 보정 상태 갱신
+static void usbHidTimerSyncOnPulse(uint32_t pulse_us);                           // V251010R9: TIM2 펄스 시 오차 계산 및 CCR 조정
+static void usbHidTimerSyncApplySpeed(usb_hid_timer_sync_speed_t speed);         // V251010R9: 속도별 파라미터 적용
+static void usbHidTimerSyncForceDefault(bool count_reset);                      // V251010R9: CCR/적분 리셋
+static inline int32_t usbHidTimerSyncAbs(int32_t value);                         // V251010R9: 부호 없는 절댓값 헬퍼
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
 
@@ -1054,59 +1130,58 @@ static uint8_t USBD_HID_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
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
+  uint32_t sof_now_us = micros();                                     // V251010R9: 타이머 보정을 위해 SOF 타임스탬프 항상 확보
 #if _USE_USB_MONITOR
   usbHidMonitorSof(sof_now_us);                                       // V251009R7: 모니터 활성 시 타임스탬프 전달
 #endif
 #if _DEF_ENABLE_USB_HID_TIMING_PROBE
   usbHidInstrumentationOnSof(sof_now_us);                             // V251009R7: 계측 활성 시 샘플 윈도우 갱신
 #endif
-#endif
+  usbHidTimerSyncOnSof(pdev, sof_now_us);                             // V251010R9: SOF 동기화 상태 갱신
 
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
 
@@ -1395,137 +1470,353 @@ static void usbHidMonitorSof(uint32_t now_us)
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
 
+static inline int32_t usbHidTimerSyncAbs(int32_t value)
+{
+  return (value < 0) ? -value : value;                               // V251010R9: 부호 없는 절댓값 계산
+}
+
+static void usbHidTimerSyncForceDefault(bool count_reset)
+{
+  timer_sync.integral_accum = 0;
+  timer_sync.current_ticks = timer_sync.default_ticks;
+  timer_sync.last_sof_us = 0U;
+  timer_sync.last_delay_us = 0U;
+  timer_sync.last_error_us = 0;
+  timer_sync.ready = false;
+  LL_TIM_OC_SetCompareCH1(TIM2, timer_sync.current_ticks);           // V251010R9: 다음 프레임부터 기본 지연 적용
+  if (count_reset)
+  {
+    timer_sync.reset_count++;
+  }
+}
+
+static void usbHidTimerSyncInit(void)
+{
+  timer_sync.default_ticks = USB_HID_TIMER_SYNC_TARGET_TICKS;        // V251010R9: 기본 타겟 지연 120us 고정
+  timer_sync.current_ticks = USB_HID_TIMER_SYNC_TARGET_TICKS;
+  timer_sync.min_ticks = USB_HID_TIMER_SYNC_HS_MIN_TICKS;
+  timer_sync.max_ticks = USB_HID_TIMER_SYNC_HS_MAX_TICKS;
+  timer_sync.guard_us = USB_HID_TIMER_SYNC_HS_GUARD_US;
+  timer_sync.kp_shift = USB_HID_TIMER_SYNC_HS_KP_SHIFT;
+  timer_sync.integral_shift = USB_HID_TIMER_SYNC_HS_INTEGRAL_SHIFT;
+  timer_sync.integral_limit = USB_HID_TIMER_SYNC_HS_INTEGRAL_LIMIT;
+  timer_sync.expected_interval_us = 0U;
+  timer_sync.speed = USB_HID_TIMER_SYNC_SPEED_NONE;
+  timer_sync.last_sof_us = 0U;
+  timer_sync.last_delay_us = 0U;
+  timer_sync.last_error_us = 0;
+  timer_sync.integral_accum = 0;
+  timer_sync.update_count = 0U;
+  timer_sync.guard_fault_count = 0U;
+  timer_sync.reset_count = 0U;
+  timer_sync.ready = false;
+  LL_TIM_OC_SetCompareCH1(TIM2, timer_sync.current_ticks);           // V251010R9: 초기 CCR1 설정
+}
+
+static void usbHidTimerSyncApplySpeed(usb_hid_timer_sync_speed_t speed)
+{
+  timer_sync.speed = speed;
+
+  if (speed == USB_HID_TIMER_SYNC_SPEED_HS)
+  {
+    timer_sync.min_ticks = USB_HID_TIMER_SYNC_HS_MIN_TICKS;
+    timer_sync.max_ticks = USB_HID_TIMER_SYNC_HS_MAX_TICKS;
+    timer_sync.guard_us = USB_HID_TIMER_SYNC_HS_GUARD_US;
+    timer_sync.kp_shift = USB_HID_TIMER_SYNC_HS_KP_SHIFT;
+    timer_sync.integral_shift = USB_HID_TIMER_SYNC_HS_INTEGRAL_SHIFT;
+    timer_sync.integral_limit = USB_HID_TIMER_SYNC_HS_INTEGRAL_LIMIT;
+    timer_sync.expected_interval_us = USB_HID_TIMER_SYNC_HS_INTERVAL_US;
+  }
+  else if (speed == USB_HID_TIMER_SYNC_SPEED_FS)
+  {
+    timer_sync.min_ticks = USB_HID_TIMER_SYNC_FS_MIN_TICKS;
+    timer_sync.max_ticks = USB_HID_TIMER_SYNC_FS_MAX_TICKS;
+    timer_sync.guard_us = USB_HID_TIMER_SYNC_FS_GUARD_US;
+    timer_sync.kp_shift = USB_HID_TIMER_SYNC_FS_KP_SHIFT;
+    timer_sync.integral_shift = USB_HID_TIMER_SYNC_FS_INTEGRAL_SHIFT;
+    timer_sync.integral_limit = USB_HID_TIMER_SYNC_FS_INTEGRAL_LIMIT;
+    timer_sync.expected_interval_us = USB_HID_TIMER_SYNC_FS_INTERVAL_US;
+  }
+  else
+  {
+    timer_sync.min_ticks = USB_HID_TIMER_SYNC_HS_MIN_TICKS;
+    timer_sync.max_ticks = USB_HID_TIMER_SYNC_HS_MAX_TICKS;
+    timer_sync.guard_us = USB_HID_TIMER_SYNC_HS_GUARD_US;
+    timer_sync.kp_shift = USB_HID_TIMER_SYNC_HS_KP_SHIFT;
+    timer_sync.integral_shift = USB_HID_TIMER_SYNC_HS_INTEGRAL_SHIFT;
+    timer_sync.integral_limit = USB_HID_TIMER_SYNC_HS_INTEGRAL_LIMIT;
+    timer_sync.expected_interval_us = 0U;
+  }
+
+  usbHidTimerSyncForceDefault(true);                                 // V251010R9: 모드 변경 시 보정 상태 초기화
+}
+
+static void usbHidTimerSyncOnSof(USBD_HandleTypeDef *pdev, uint32_t now_us)
+{
+  usb_hid_timer_sync_speed_t next_speed = USB_HID_TIMER_SYNC_SPEED_NONE;
+
+  if (pdev->dev_state == USBD_STATE_CONFIGURED)
+  {
+    if (pdev->dev_speed == USBD_SPEED_HIGH)
+    {
+      next_speed = USB_HID_TIMER_SYNC_SPEED_HS;
+    }
+    else if (pdev->dev_speed == USBD_SPEED_FULL)
+    {
+      next_speed = USB_HID_TIMER_SYNC_SPEED_FS;
+    }
+  }
+
+  if (next_speed != timer_sync.speed)
+  {
+    usbHidTimerSyncApplySpeed(next_speed);                            // V251010R9: 속도 변경 시 파라미터 재설정
+  }
+
+  timer_sync.last_sof_us = now_us;
+  timer_sync.ready = (next_speed != USB_HID_TIMER_SYNC_SPEED_NONE);   // V251010R9: SOF 캡처 후 보정 활성화
+}
+
+static void usbHidTimerSyncOnPulse(uint32_t pulse_us)
+{
+  uint32_t delay_us = 0U;
+
+  if (timer_sync.ready && timer_sync.last_sof_us != 0U)
+  {
+    delay_us = pulse_us - timer_sync.last_sof_us;                     // V251010R9: SOF 대비 실제 지연 계산
+    timer_sync.last_delay_us = delay_us;
+
+    int32_t error_us = (int32_t)timer_sync.default_ticks - (int32_t)delay_us;
+    timer_sync.last_error_us = error_us;
+
+    if ((uint32_t)usbHidTimerSyncAbs(error_us) > (uint32_t)timer_sync.guard_us)
+    {
+      timer_sync.guard_fault_count++;
+      usbHidTimerSyncForceDefault(true);                              // V251010R9: 과도한 오차 시 기본값으로 복귀
+      timer_sync.last_delay_us = delay_us;                            // V251010R9: 직전 측정치는 유지
+      timer_sync.last_error_us = error_us;
+    }
+    else
+    {
+      timer_sync.integral_accum += error_us;
+      if (timer_sync.integral_accum > timer_sync.integral_limit)
+      {
+        timer_sync.integral_accum = timer_sync.integral_limit;
+      }
+      else if (timer_sync.integral_accum < -timer_sync.integral_limit)
+      {
+        timer_sync.integral_accum = -timer_sync.integral_limit;
+      }
+
+      int32_t proportional_term = error_us >> timer_sync.kp_shift;
+      int32_t integral_term = timer_sync.integral_accum >> timer_sync.integral_shift;
+      int32_t target_ticks = (int32_t)timer_sync.default_ticks + proportional_term + integral_term;
+
+      if (target_ticks > (int32_t)timer_sync.current_ticks + 1)
+      {
+        target_ticks = (int32_t)timer_sync.current_ticks + 1;         // V251010R9: 프레임당 ±1틱 제한으로 안정화
+      }
+      else if (target_ticks < (int32_t)timer_sync.current_ticks - 1)
+      {
+        target_ticks = (int32_t)timer_sync.current_ticks - 1;
+      }
+
+      if (target_ticks < (int32_t)timer_sync.min_ticks)
+      {
+        target_ticks = (int32_t)timer_sync.min_ticks;
+      }
+      else if (target_ticks > (int32_t)timer_sync.max_ticks)
+      {
+        target_ticks = (int32_t)timer_sync.max_ticks;
+      }
+
+      timer_sync.current_ticks = (uint16_t)target_ticks;
+      LL_TIM_OC_SetCompareCH1(TIM2, timer_sync.current_ticks);       // V251010R9: 다음 프레임 CCR 갱신
+      timer_sync.update_count++;
+    }
+  }
+  else
+  {
+    timer_sync.last_delay_us = 0U;
+    timer_sync.last_error_us = 0;
+  }
+
+#if _DEF_ENABLE_USB_HID_TIMING_PROBE
+  usbHidInstrumentationOnTimerPulse(delay_us, timer_sync.current_ticks);
+#else
+  (void)delay_us;
+#endif
+}
+
+bool usbHidTimerSyncGetState(usb_hid_timer_sync_state_t *p_state)
+{
+  if (p_state == NULL)
+  {
+    return false;
+  }
+
+  usb_hid_timer_sync_state_t state;
+
+  __disable_irq();                                                   // V251010R9: ISR 업데이트와 경합을 피하기 위해 보호
+  state.current_ticks = timer_sync.current_ticks;
+  state.default_ticks = timer_sync.default_ticks;
+  state.min_ticks = timer_sync.min_ticks;
+  state.max_ticks = timer_sync.max_ticks;
+  state.guard_us = timer_sync.guard_us;
+  state.last_delay_us = timer_sync.last_delay_us;
+  state.last_error_us = timer_sync.last_error_us;
+  state.integral_accum = timer_sync.integral_accum;
+  state.integral_limit = timer_sync.integral_limit;
+  state.expected_interval_us = timer_sync.expected_interval_us;
+  state.target_delay_us = timer_sync.default_ticks;
+  state.update_count = timer_sync.update_count;
+  state.guard_fault_count = timer_sync.guard_fault_count;
+  state.reset_count = timer_sync.reset_count;
+  state.kp_shift = timer_sync.kp_shift;
+  state.integral_shift = timer_sync.integral_shift;
+  state.speed = (uint8_t)timer_sync.speed;
+  state.ready = timer_sync.ready;
+  __enable_irq();
+
+  *p_state = state;
+  return (timer_sync.speed != USB_HID_TIMER_SYNC_SPEED_NONE);
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
 
   HAL_TIM_OC_Start_IT(&htim2, TIM_CHANNEL_1);
+  usbHidTimerSyncInit();                                             // V251010R9: TIM2 비교기 보정 상태 초기화
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
-#if _DEF_ENABLE_USB_HID_TIMING_PROBE
-  usbHidInstrumentationOnTimerPulse();                                 // V251009R7: 계측 타이머 후크를 조건부 실행
-#endif
+  if (htim->Instance != TIM2)
+  {
+    return;                                                            // V251010R9: HID 백업 타이머 외에는 처리 불필요
+  }
+
+  uint32_t pulse_now_us = micros();                                    // V251010R9: TIM2 펄스 시각을 캡처
+  usbHidTimerSyncOnPulse(pulse_now_us);                                // V251010R9: SOF 기반 PI 보정 수행
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
 
       memcpy(hid_buf_exk, report_info.buf, report_info.len);=
------------------
diff --git a/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c b/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c
index 8d9773d8476df44a6b7008ec54ad93d9c1af45ab..2a379c01e043c56a53148e608f433bdc31afd1eb 100644
--- a/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c
+++ b/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c
@@ -24,50 +24,51 @@ static uint32_t rate_time_min_check = 0xFFFF;
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
 static volatile uint32_t timer_sof_offset_us = 0;            // V251010R8: TIM2 펄스 시점의 SOF 기준 지연(us)
+static volatile uint16_t timer_compare_ticks = 120;          // V251010R9: 직전 펄스에 적용된 CCR1 값
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
@@ -79,57 +80,55 @@ void usbHidInstrumentationOnSof(uint32_t now_us)
     sample_cnt = 0;
     data_in_rate = data_in_cnt;
     rate_time_min = rate_time_min_check;
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
 
-void usbHidInstrumentationOnTimerPulse(void)
+void usbHidInstrumentationOnTimerPulse(uint32_t delay_us, uint16_t compare_ticks)
 {
   timer_pulse_total++;                                           // V251010R8: TIM2 펄스 누적
-  if (rate_time_sof_pre != 0U)
-  {
-    timer_sof_offset_us = micros()-rate_time_sof_pre;            // V251010R8: TIM2 펄스 지연을 SOF 기준으로 추적
-  }
+  timer_sof_offset_us = delay_us;                                // V251010R9: 본체에서 전달한 지연(us)을 그대로 기록
+  timer_compare_ticks = compare_ticks;                           // V251010R9: 적용된 CCR1 틱 값을 계측에서도 확인
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
@@ -289,50 +288,73 @@ void usbHidInstrumentationHandleCli(cli_args_t *args)
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
         cliPrintf("  SOF/타이머    : %lu / %lu (기대 %lu, Δ %+ld / %+ld, SOF %4lu us, TIM 오프셋 %4lu us)\n",
                   (unsigned long)sof_delta,
                   (unsigned long)timer_delta,
                   (unsigned long)expected_sof,
                   (long)sof_diff,
                   (long)timer_diff,
                   (unsigned long)rate_time_sof,
                   (unsigned long)timer_sof_offset_us);
 
+        usb_hid_timer_sync_state_t sync_state = {0};             // V251010R9: SOF 타이머 PI 상태를 CLI에 노출
+        bool sync_ready = usbHidTimerSyncGetState(&sync_state);
+        long integral_ticks = 0;
+        if (sync_state.integral_shift < 31U)
+        {
+          integral_ticks = (long)(sync_state.integral_accum >> sync_state.integral_shift);
+        }
+        cliPrintf("  타이머 CCR    : 현재 %3u (기본 %3u, 범위 %3u~%3u, 가드 %2u us, 기대 SOF %4lu us)\n",
+                  (unsigned int)timer_compare_ticks,
+                  (unsigned int)sync_state.default_ticks,
+                  (unsigned int)sync_state.min_ticks,
+                  (unsigned int)sync_state.max_ticks,
+                  (unsigned int)sync_state.guard_us,
+                  (unsigned long)sync_state.expected_interval_us);
+        cliPrintf("  보정 통계     : 오차 %+ld us / 적분 %+ld (틱 %+ld), 업데이트 %lu, 리셋 %lu, 가드 %lu, 준비 %s\n",
+                  (long)sync_state.last_error_us,
+                  (long)sync_state.integral_accum,
+                  integral_ticks,
+                  (unsigned long)sync_state.update_count,
+                  (unsigned long)sync_state.reset_count,
+                  (unsigned long)sync_state.guard_fault_count,
+                  sync_ready && sync_state.ready ? "ON" : "OFF");
+
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
         cliPrintf("%d %d\n", i, rate_his_buf[i]);
------------------
diff --git a/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h b/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h
index a12a41fe38d03ae21db62825f56210c28488019e..3bbcef72fe20e06860af69e6a41b6f4360103672 100644
--- a/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h
+++ b/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h
@@ -1,72 +1,74 @@
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
-void     usbHidInstrumentationOnTimerPulse(void);
+void     usbHidInstrumentationOnTimerPulse(uint32_t delay_us, uint16_t compare_ticks);
 void     usbHidInstrumentationOnDataIn(void);
 void     usbHidInstrumentationOnReportDequeued(uint32_t queued_reports);
 void     usbHidInstrumentationOnImmediateSendSuccess(uint32_t queued_reports);
 void     usbHidInstrumentationMarkReportStart(void);
 void     usbHidMeasureRateTime(void);
 
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
 
-static inline void usbHidInstrumentationOnTimerPulse(void)
+static inline void usbHidInstrumentationOnTimerPulse(uint32_t delay_us, uint16_t compare_ticks)
 {
-  // V251010R1: 릴리스 빌드에서 계측 타이머 콜백 제거
+  (void)delay_us;    // V251010R9: 릴리스 빌드에서 타이머 보정 정보 전달을 무효화
+  (void)compare_ticks;
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
 
 #endif
 
 void     usbHidInstrumentationHandleCli(cli_args_t *args);
+bool     usbHidTimerSyncGetState(usb_hid_timer_sync_state_t *p_state);  // V251010R9: 타이머 보정 상태를 CLI에서 조회
------------------
diff --git a/src/hw/driver/usb/usb_hid/usbd_hid_internal.h b/src/hw/driver/usb/usb_hid/usbd_hid_internal.h
index 68abcfed47dca94470079945dba31ba4ce8dffb7..8be3e3ccc8ff63f2a47312cd427366c06f921096 100644
--- a/src/hw/driver/usb/usb_hid/usbd_hid_internal.h
+++ b/src/hw/driver/usb/usb_hid/usbd_hid_internal.h
@@ -1,6 +1,31 @@
 #pragma once
 
 #include "hw_def.h"
 
+#include <stdbool.h>  // V251010R9: 타이머 보정 상태 구조체에서 bool을 직접 사용
+#include <stdint.h>   // V251010R9: 타이머 보정 상태 공유 구조체 정의용 기본 정수형 포함
+
 #define HID_KEYBOARD_REPORT_SIZE (HW_KEYS_PRESS_MAX + 2U)
 #define KEY_TIME_LOG_MAX         32  // V251009R9: 계측 모듈과 본체에서 공유
+
+typedef struct
+{
+  uint16_t current_ticks;       // V251010R9: 현재 CCR1 값(타겟 지연 틱)
+  uint16_t default_ticks;       // V251010R9: 기본 타겟 값(120us)
+  uint16_t min_ticks;           // V251010R9: 허용되는 최소 타겟 틱
+  uint16_t max_ticks;           // V251010R9: 허용되는 최대 타겟 틱
+  uint16_t guard_us;            // V251010R9: 보정 허용 오차(us)
+  uint32_t last_delay_us;       // V251010R9: 직전 펄스 지연 측정(us)
+  int32_t  last_error_us;       // V251010R9: 직전 오차(us)
+  int32_t  integral_accum;      // V251010R9: 적분 누산 값(오차 합)
+  int32_t  integral_limit;      // V251010R9: 적분 포화 한계
+  uint32_t expected_interval_us;// V251010R9: 현재 속도에서 기대되는 SOF 간격(us)
+  uint32_t target_delay_us;     // V251010R9: 목표 지연(us)
+  uint32_t update_count;        // V251010R9: 성공적인 보정 적용 횟수
+  uint32_t guard_fault_count;   // V251010R9: 가드 초과로 리셋된 횟수
+  uint32_t reset_count;         // V251010R9: 모드 변경 포함 전체 초기화 횟수
+  uint8_t  kp_shift;            // V251010R9: 비례항 시프트(1/2^kp_shift)
+  uint8_t  integral_shift;      // V251010R9: 적분항 시프트(1/2^integral_shift)
+  uint8_t  speed;               // V251010R9: 현재 추적 중인 속도(0=없음,1=HS,2=FS)
+  bool     ready;               // V251010R9: SOF 샘플 확보 후 보정 활성 여부
+} usb_hid_timer_sync_state_t;
------------------