# 버전4
------------------
diff --git a/src/hw/driver/usb/usb_hid/usbd_hid.c b/src/hw/driver/usb/usb_hid/usbd_hid.c
index efdc620d1242855a99c63dad4fc0fda5f82b0daa..3aed821fe07b2b1443cabf8ebe45695a963e1d9b 100644
--- a/src/hw/driver/usb/usb_hid/usbd_hid.c
+++ b/src/hw/driver/usb/usb_hid/usbd_hid.c
@@ -29,50 +29,51 @@
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
+#include "micros.h"                       // V251010R9: SOF-타이머 지연 측정을 위한 타임스탬프 접근
 
 
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
@@ -448,50 +449,98 @@ __ALIGN_BEGIN static uint8_t HID_EXK_ReportDesc[HID_EXK_REPORT_DESC_SIZE] __ALIG
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
 
+enum
+{
+  USB_HID_TIMER_SYNC_TARGET_DELAY_US = 120U,     // V251010R9: SOF 이후 백업 경로 목표 지연(us)
+  USB_HID_TIMER_SYNC_MIN_TICKS       = 32U,      // V251010R9: 안전을 위한 CCR 하한(약 32us)
+  USB_HID_TIMER_SYNC_MAX_TICKS_HS    = 512U,     // V251010R9: HS 모드에서 허용할 CCR 상한
+  USB_HID_TIMER_SYNC_MAX_TICKS_FS    = 2000U,    // V251010R9: FS 모드에서 허용할 CCR 상한
+  USB_HID_TIMER_SYNC_MAX_FAIL_COUNT  = 8U        // V251010R9: 연속 이상 샘플 수 허용치
+};
+
+#define USB_HID_TIMER_SYNC_INVALID_MARGIN_US   200U    // V251010R9: SOF 간격 오차 허용 한계(us)
+#define USB_HID_TIMER_SYNC_MAX_STEP_TICKS      1       // V251010R9: 루프당 CCR 보정 최대 1틱 제한
+#define USB_HID_TIMER_SYNC_HS_KP_SHIFT         3       // V251010R9: HS 모드 비례 이득(1/8)
+#define USB_HID_TIMER_SYNC_HS_KI_SHIFT         8       // V251010R9: HS 모드 적분 이득(1/256)
+#define USB_HID_TIMER_SYNC_FS_KP_SHIFT         2       // V251010R9: FS 모드 비례 이득(1/4)
+#define USB_HID_TIMER_SYNC_FS_KI_SHIFT         9       // V251010R9: FS 모드 적분 이득(1/512)
+
+typedef struct
+{
+  uint32_t last_sof_us;          // V251010R9: 직전 SOF 타임스탬프(us)
+  uint32_t target_delay_us;      // V251010R9: 목표 지연(us)
+  uint32_t expected_interval_us; // V251010R9: USB 속도별 기대 SOF 주기(us)
+  uint32_t last_delay_us;        // V251010R9: 최근 측정된 SOF→TIM2 지연(us)
+  int32_t  last_error_us;        // V251010R9: 최근 계산된 지연 오차(us)
+  int32_t  integral_acc;         // V251010R9: 적분항 누적 상태
+  int16_t  last_adjust_ticks;    // V251010R9: 직전 루프에서 적용된 CCR 보정틱
+  uint16_t compare_ticks;        // V251010R9: 현재 TIM2 CCR1 값
+  uint16_t nominal_ticks;        // V251010R9: 모드별 기본 CCR 기준값
+  uint16_t min_ticks;            // V251010R9: 허용 최소 CCR 값
+  uint16_t max_ticks;            // V251010R9: 허용 최대 CCR 값
+  uint8_t  speed;                // V251010R9: USBD_SPEED_* 코드(HS/FS)
+  uint8_t  valid_sample;         // V251010R9: 최근 측정값 유효 여부
+  uint8_t  fail_count;           // V251010R9: 연속 이상 샘플 카운터
+} usb_hid_timer_sync_state_t;
+
+static usb_hid_timer_sync_state_t timer_sync =
+{
+  .target_delay_us = USB_HID_TIMER_SYNC_TARGET_DELAY_US,  // V251010R9: 초기 목표값
+  .nominal_ticks   = USB_HID_TIMER_SYNC_TARGET_DELAY_US,  // V251010R9: 초기 CCR 기준값
+  .compare_ticks   = USB_HID_TIMER_SYNC_TARGET_DELAY_US,  // V251010R9: 초기 비교값
+};
+
+static void    usbHidTimerSyncReset(uint8_t speed_code, uint32_t sof_timestamp_us);  // V251010R9: 속도 전환 시 보정기 초기화
+static void    usbHidTimerSyncOnSof(USBD_HandleTypeDef *pdev, uint32_t now_us);      // V251010R9: SOF 시점 처리
+static void    usbHidTimerSyncOnCompare(uint32_t now_us);                            // V251010R9: TIM2 비교 시점 처리
+static uint32_t usbHidTimerSyncExpectedInterval(uint8_t speed_code);                 // V251010R9: 속도별 기대 SOF 주기 조회
+static uint16_t usbHidTimerSyncMaxTicks(uint8_t speed_code);                         // V251010R9: 속도별 CCR 상한 계산
+static int32_t usbHidTimerSyncApplyGain(int32_t value, uint8_t shift);               // V251010R9: 시프트 기반 이득 적용
+
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
@@ -1054,58 +1103,59 @@ static uint8_t USBD_HID_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
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
+  uint32_t sof_now_us = micros();                                     // V251010R9: 보정 루프와 모니터 공용 타임스탬프 취득
+
+  usbHidTimerSyncOnSof(pdev, sof_now_us);                             // V251010R9: SOF 시점의 보정 상태 갱신
+
 #if _USE_USB_MONITOR
-  usbHidMonitorSof(sof_now_us);                                       // V251009R7: 모니터 활성 시 타임스탬프 전달
+  usbHidMonitorSof(sof_now_us);                                       // V251010R9: 모니터에도 동일 타임스탬프 전달
 #endif
 #if _DEF_ENABLE_USB_HID_TIMING_PROBE
-  usbHidInstrumentationOnSof(sof_now_us);                             // V251009R7: 계측 활성 시 샘플 윈도우 갱신
-#endif
+  usbHidInstrumentationOnSof(sof_now_us);                             // V251010R9: 계측 활성 시 샘플 윈도우 갱신
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
@@ -1162,61 +1212,242 @@ bool usbHidSendReport(uint8_t *p_data, uint16_t length)
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
-      qbufferWrite(&report_exk_q, (uint8_t *)&report_info, 1);        
-    }    
+      qbufferWrite(&report_exk_q, (uint8_t *)&report_info, 1);
+    }
   }
   else
   {
     usbHidUpdateWakeUp(&USBD_Device);
   }
-  
+
   return true;
 }
 
+static uint32_t usbHidTimerSyncExpectedInterval(uint8_t speed_code)
+{
+  if (speed_code == USBD_SPEED_FULL)
+  {
+    return 1000U;  // V251010R9: FS 모드 기대 SOF 주기(1ms)
+  }
+  return 125U;     // V251010R9: HS 모드 기대 SOF 주기(125us)
+}
+
+static uint16_t usbHidTimerSyncMaxTicks(uint8_t speed_code)
+{
+  return (speed_code == USBD_SPEED_FULL) ? USB_HID_TIMER_SYNC_MAX_TICKS_FS
+                                         : USB_HID_TIMER_SYNC_MAX_TICKS_HS;  // V251010R9: 속도별 CCR 상한 선택
+}
+
+static int32_t usbHidTimerSyncApplyGain(int32_t value, uint8_t shift)
+{
+  if (shift == 0U)
+  {
+    return value;  // V251010R9: 시프트 0이면 원값 유지
+  }
+
+  uint32_t abs_value = (value < 0) ? (uint32_t)(-value) : (uint32_t)value;
+  uint32_t rounding  = 1U << (shift - 1U);
+  int32_t  scaled    = (int32_t)((abs_value + rounding) >> shift);
+
+  return (value < 0) ? -scaled : scaled;  // V251010R9: 부호 복원
+}
+
+static void usbHidTimerSyncReset(uint8_t speed_code, uint32_t sof_timestamp_us)
+{
+  if (speed_code != USBD_SPEED_FULL)
+  {
+    speed_code = USBD_SPEED_HIGH;  // V251010R9: 기타 값은 HS로 취급
+  }
+
+  timer_sync.speed                = speed_code;
+  timer_sync.expected_interval_us = usbHidTimerSyncExpectedInterval(speed_code);
+  timer_sync.target_delay_us      = USB_HID_TIMER_SYNC_TARGET_DELAY_US;
+  timer_sync.nominal_ticks        = USB_HID_TIMER_SYNC_TARGET_DELAY_US;
+  timer_sync.min_ticks            = USB_HID_TIMER_SYNC_MIN_TICKS;
+  timer_sync.max_ticks            = usbHidTimerSyncMaxTicks(speed_code);
+  timer_sync.compare_ticks        = timer_sync.nominal_ticks;
+  timer_sync.integral_acc         = 0;
+  timer_sync.last_error_us        = 0;
+  timer_sync.last_adjust_ticks    = 0;
+  timer_sync.last_delay_us        = timer_sync.target_delay_us;
+  timer_sync.valid_sample         = 0U;
+  timer_sync.fail_count           = 0U;
+  timer_sync.last_sof_us          = sof_timestamp_us;
+
+  TIM2->CCR1 = timer_sync.compare_ticks;  // V251010R9: CCR 초기화 시 직접 레지스터 기록으로 오버헤드 절감
+}
+
+static void usbHidTimerSyncOnSof(USBD_HandleTypeDef *pdev, uint32_t now_us)
+{
+  uint8_t speed_code = pdev->dev_speed;
+
+  if (speed_code != USBD_SPEED_FULL)
+  {
+    speed_code = USBD_SPEED_HIGH;  // V251010R9: 인식 불가 속도는 HS로 보정
+  }
+
+  if (timer_sync.speed != speed_code)
+  {
+    usbHidTimerSyncReset(speed_code, now_us);  // V251010R9: 속도 전환 시 상태 초기화
+    return;
+  }
+
+  timer_sync.last_sof_us  = now_us;
+  timer_sync.valid_sample = 0U;  // V251010R9: 다음 비교 이벤트에서 새 샘플 필요 표시
+}
+
+static void usbHidTimerSyncOnCompare(uint32_t now_us)
+{
+  if (timer_sync.last_sof_us == 0U)
+  {
+    return;  // V251010R9: SOF 타임스탬프가 없으면 측정 불가
+  }
+
+  uint32_t delta_us = now_us - timer_sync.last_sof_us;
+
+  timer_sync.last_delay_us = delta_us;
+
+  uint32_t expected_guard = timer_sync.expected_interval_us + USB_HID_TIMER_SYNC_INVALID_MARGIN_US;
+
+  if (delta_us < (uint32_t)timer_sync.min_ticks || delta_us > expected_guard)
+  {
+    timer_sync.last_error_us = 0;
+    if (timer_sync.fail_count < USB_HID_TIMER_SYNC_MAX_FAIL_COUNT)
+    {
+      timer_sync.fail_count++;  // V251010R9: 연속 이상 샘플 카운트
+    }
+    else
+    {
+      usbHidTimerSyncReset(timer_sync.speed, timer_sync.last_sof_us);  // V251010R9: 과도 오차 시 기본값 복귀
+    }
+    return;
+  }
+
+  timer_sync.fail_count   = 0U;
+  timer_sync.valid_sample = 1U;
+
+  int32_t error_us = (int32_t)timer_sync.target_delay_us - (int32_t)delta_us;
+  timer_sync.last_error_us = error_us;
+
+  timer_sync.integral_acc += error_us;
+
+  int32_t integral_limit = (int32_t)(timer_sync.expected_interval_us * 8U);
+  if (timer_sync.integral_acc > integral_limit)
+  {
+    timer_sync.integral_acc = integral_limit;
+  }
+  else if (timer_sync.integral_acc < -integral_limit)
+  {
+    timer_sync.integral_acc = -integral_limit;
+  }
+
+  uint8_t kp_shift = (timer_sync.speed == USBD_SPEED_FULL) ? USB_HID_TIMER_SYNC_FS_KP_SHIFT
+                                                          : USB_HID_TIMER_SYNC_HS_KP_SHIFT;
+  uint8_t ki_shift = (timer_sync.speed == USBD_SPEED_FULL) ? USB_HID_TIMER_SYNC_FS_KI_SHIFT
+                                                          : USB_HID_TIMER_SYNC_HS_KI_SHIFT;
+
+  int32_t proportional  = usbHidTimerSyncApplyGain(error_us, kp_shift);
+  int32_t integral_term = usbHidTimerSyncApplyGain(timer_sync.integral_acc, ki_shift);
+  int32_t adjust_ticks  = proportional + integral_term;
+
+  if (adjust_ticks > USB_HID_TIMER_SYNC_MAX_STEP_TICKS)
+  {
+    adjust_ticks = USB_HID_TIMER_SYNC_MAX_STEP_TICKS;
+  }
+  else if (adjust_ticks < -USB_HID_TIMER_SYNC_MAX_STEP_TICKS)
+  {
+    adjust_ticks = -USB_HID_TIMER_SYNC_MAX_STEP_TICKS;
+  }
+
+  timer_sync.last_adjust_ticks = (int16_t)adjust_ticks;
+
+  int32_t next_ticks = (int32_t)timer_sync.compare_ticks + adjust_ticks;
+
+  if (next_ticks < (int32_t)timer_sync.min_ticks)
+  {
+    next_ticks = timer_sync.min_ticks;
+  }
+  else if (next_ticks > (int32_t)timer_sync.max_ticks)
+  {
+    next_ticks = timer_sync.max_ticks;
+  }
+
+  if ((uint16_t)next_ticks != timer_sync.compare_ticks)
+  {
+    timer_sync.compare_ticks = (uint16_t)next_ticks;
+    TIM2->CCR1 = timer_sync.compare_ticks;  // V251010R9: HAL 호출 없이 직접 CCR 업데이트
+  }
+  else
+  {
+    timer_sync.last_adjust_ticks = 0;  // V251010R9: 변화가 없으면 보정량 0으로 리포트
+  }
+}
+
+bool usbHidTimerSyncGetInfo(usb_hid_timer_sync_info_t *p_info)
+{
+  if (p_info == NULL)
+  {
+    return false;  // V251010R9: 널 포인터 방어
+  }
+
+  p_info->target_delay_us      = timer_sync.target_delay_us;
+  p_info->measured_delay_us    = timer_sync.last_delay_us;
+  p_info->expected_interval_us = timer_sync.expected_interval_us;
+  p_info->last_error_us        = timer_sync.last_error_us;
+  p_info->integral_acc         = timer_sync.integral_acc;
+  p_info->last_adjust_ticks    = timer_sync.last_adjust_ticks;
+  p_info->compare_ticks        = timer_sync.compare_ticks;
+  p_info->nominal_ticks        = timer_sync.nominal_ticks;
+  p_info->speed                = timer_sync.speed;
+  p_info->valid_sample         = timer_sync.valid_sample;
+
+  return timer_sync.last_sof_us != 0U;  // V251010R9: SOF 동기화 여부 반환
+}
+
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
@@ -1443,88 +1674,99 @@ void usbHidInitTimer(void)
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
+
+  usbHidTimerSyncReset(USBD_SPEED_HIGH, 0U);  // V251010R9: 초기 SOF 이전에 타이머 보정 상태 정렬
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
+    return;  // V251010R9: TIM2 이외의 타이머에서 호출된 경우 무시
+  }
+
+  uint32_t compare_now_us = micros();                                  // V251010R9: 타이머 비교 시점 타임스탬프 확보
+
+  usbHidTimerSyncOnCompare(compare_now_us);                            // V251010R9: PI 보정 루프 실행
+
 #if _DEF_ENABLE_USB_HID_TIMING_PROBE
-  usbHidInstrumentationOnTimerPulse();                                 // V251009R7: 계측 타이머 후크를 조건부 실행
+  usbHidInstrumentationOnTimerPulse();                                 // V251010R9: 계측 타이머 후크를 조건부 실행
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
------------------
diff --git a/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c b/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c
index 8d9773d8476df44a6b7008ec54ad93d9c1af45ab..7c1ec47d80393b9bcdb97670a7cfd26cccc0b4a2 100644
--- a/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c
+++ b/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c
@@ -289,50 +289,73 @@ void usbHidInstrumentationHandleCli(cli_args_t *args)
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
 
+        usb_hid_timer_sync_info_t sync_info;
+        bool                     sync_ready = usbHidTimerSyncGetInfo(&sync_info);    // V251010R9: 타이머 보정 상태 조회
+
+        if (sync_ready)
+        {
+          const char *speed_label = (sync_info.speed == USBD_SPEED_FULL) ? "FS" : "HS";
+          const char *state_label = (sync_info.valid_sample != 0U) ? "안정" : "대기";
+
+          cliPrintf("  타이머 보정  : 목표 %3lu us / 측정 %3lu us (CCR %3u, Δ %+ld us, I %+ld, %+d틱, %s/%s)\n",
+                    (unsigned long)sync_info.target_delay_us,
+                    (unsigned long)sync_info.measured_delay_us,
+                    (unsigned int)sync_info.compare_ticks,
+                    (long)sync_info.last_error_us,
+                    (long)sync_info.integral_acc,
+                    (int)sync_info.last_adjust_ticks,
+                    speed_label,
+                    state_label);
+        }
+        else
+        {
+          cliPrintf("  타이머 보정  : 초기 샘플 대기중\n");
+        }
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
diff --git a/src/hw/driver/usb/usb_hid/usbd_hid_internal.h b/src/hw/driver/usb/usb_hid/usbd_hid_internal.h
index 68abcfed47dca94470079945dba31ba4ce8dffb7..46b0341a659d3adb538cfb306330c3a16a2525a4 100644
--- a/src/hw/driver/usb/usb_hid/usbd_hid_internal.h
+++ b/src/hw/driver/usb/usb_hid/usbd_hid_internal.h
@@ -1,6 +1,22 @@
 #pragma once
 
 #include "hw_def.h"
 
 #define HID_KEYBOARD_REPORT_SIZE (HW_KEYS_PRESS_MAX + 2U)
 #define KEY_TIME_LOG_MAX         32  // V251009R9: 계측 모듈과 본체에서 공유
+
+typedef struct
+{
+  uint32_t target_delay_us;         // V251010R9: TIM2 비교 목표 지연(us)
+  uint32_t measured_delay_us;       // V251010R9: 최근 측정된 SOF→TIM2 지연(us)
+  uint32_t expected_interval_us;    // V251010R9: 현재 USB 모드 기준 기대 SOF 간격(us)
+  int32_t  last_error_us;           // V251010R9: PI 루프에 사용된 최근 오차(us)
+  int32_t  integral_acc;            // V251010R9: 적분항 누적 상태
+  int16_t  last_adjust_ticks;       // V251010R9: 직전 루프에서 적용된 CCR 보정틱
+  uint16_t compare_ticks;           // V251010R9: 현재 TIM2 CCR1 설정값
+  uint16_t nominal_ticks;           // V251010R9: USB 모드별 기본 CCR 기준값
+  uint8_t  speed;                   // V251010R9: USBD_SPEED_* 상수로 표현한 현재 속도
+  uint8_t  valid_sample;            // V251010R9: 최신 샘플 유효 여부(0: 초기화/보류)
+} usb_hid_timer_sync_info_t;
+
+bool usbHidTimerSyncGetInfo(usb_hid_timer_sync_info_t *p_info);
------------------