# 버전1
------------------
diff --git a/src/hw/driver/usb/usb_hid/usbd_hid.c b/src/hw/driver/usb/usb_hid/usbd_hid.c
index efdc620d1242855a99c63dad4fc0fda5f82b0daa..1f3855d41852aabf212ab1a950656d2633839435 100644
--- a/src/hw/driver/usb/usb_hid/usbd_hid.c
+++ b/src/hw/driver/usb/usb_hid/usbd_hid.c
@@ -29,87 +29,95 @@
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
+#include "micros.h"                       // V251010R9: SOF-타이머 동기화 타임스탬프 취득
+
+#include "stm32h7rsxx_ll_tim.h"          // V251010R9: CCR 업데이트에 LL 레지스터 접근 사용
 
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
+static void usbHidTimerSyncReset(uint8_t speed_code, uint32_t now_us);      // V251010R9: SOF-타이머 PI 보정기 초기화
+static void usbHidTimerSyncOnSof(USBD_HandleTypeDef *pdev, uint32_t now_us); // V251010R9: SOF 시점 타임스탬프 갱신
+static void usbHidTimerSyncOnPulse(void);                                   // V251010R9: TIM2 펄스에서 PI 보정 수행
+static uint32_t usbHidTimerSyncResolveInterval(uint8_t speed_code);         // V251010R9: 속도별 기대 SOF 간격 계산
+static int32_t usbHidTimerSyncRoundedShift(int32_t value, uint8_t shift);   // V251010R9: 부호 유지 라운드 시프트
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
 
@@ -448,50 +456,104 @@ __ALIGN_BEGIN static uint8_t HID_EXK_ReportDesc[HID_EXK_REPORT_DESC_SIZE] __ALIG
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
+  USB_HID_TIMER_SYNC_TARGET_TICKS       = 120U,  // V251010R9: 목표 지연 120us에 대응하는 CCR 기본값
+  USB_HID_TIMER_SYNC_MIN_COMPARE_TICKS  = 80U,   // V251010R9: 보정 허용 하한(안정성 보호)
+  USB_HID_TIMER_SYNC_MAX_COMPARE_TICKS  = 200U,  // V251010R9: 보정 허용 상한(안정성 보호)
+  USB_HID_TIMER_SYNC_RESET_ERROR_US     = 60U,   // V251010R9: 오차가 ±60us를 넘으면 재초기화
+  USB_HID_TIMER_SYNC_INTEGRAL_LIMIT     = 4096,  // V251010R9: 적분 항 클램프 범위
+  USB_HID_TIMER_SYNC_MAX_STEP           = 2,     // V251010R9: 단일 프레임당 최대 보정 틱 수
+  USB_HID_TIMER_SYNC_KP_SHIFT           = 2U,    // V251010R9: 비례 항 = error / 4
+  USB_HID_TIMER_SYNC_KI_SHIFT           = 8U     // V251010R9: 적분 항 = integral / 256
+};
+
+typedef struct
+{
+  uint32_t target_delay_ticks;       // V251010R9: 목표 CCR1 틱(120us)
+  uint32_t expected_interval_us;     // V251010R9: 현재 USB 속도에서 기대되는 SOF 간격
+  uint32_t last_sof_us;              // V251010R9: 마지막 SOF 타임스탬프(us)
+  uint32_t last_interval_us;         // V251010R9: 직전 SOF 간격(us)
+  uint32_t last_delay_us;            // V251010R9: 직전 TIM2 펄스 지연(us)
+  uint32_t update_count;             // V251010R9: 보정 루프 누적 실행 횟수
+  uint32_t reset_count;              // V251010R9: 재초기화 누적 횟수
+  uint32_t saturation_count;         // V251010R9: 비교값이 한계에 닿은 횟수
+  int32_t  integral;                 // V251010R9: PI 적분 항 누적값
+  int32_t  last_error_us;            // V251010R9: 직전 오차(us)
+  uint16_t compare_ticks;            // V251010R9: 현재 CCR1 설정값
+  uint16_t default_compare_ticks;    // V251010R9: 기본 CCR1 값(120us)
+  uint16_t compare_min_ticks;        // V251010R9: 비교값 하한
+  uint16_t compare_max_ticks;        // V251010R9: 비교값 상한
+  uint8_t  active_speed;             // V251010R9: 현재 USB 속도 코드(USBD_SPEED_*)
+  bool     has_reference;            // V251010R9: SOF 기준 확보 여부
+  bool     locked;                   // V251010R9: 최소 1회 이상 보정 루프가 유효하게 실행되었는지
+} usb_hid_timer_sync_t;
+
+static usb_hid_timer_sync_t timer_sync =
+{
+  .target_delay_ticks    = USB_HID_TIMER_SYNC_TARGET_TICKS,
+  .expected_interval_us  = 125U,
+  .last_sof_us           = 0U,
+  .last_interval_us      = 125U,
+  .last_delay_us         = USB_HID_TIMER_SYNC_TARGET_TICKS,
+  .update_count          = 0U,
+  .reset_count           = 0U,
+  .saturation_count      = 0U,
+  .integral              = 0,
+  .last_error_us         = 0,
+  .compare_ticks         = USB_HID_TIMER_SYNC_TARGET_TICKS,
+  .default_compare_ticks = USB_HID_TIMER_SYNC_TARGET_TICKS,
+  .compare_min_ticks     = USB_HID_TIMER_SYNC_MIN_COMPARE_TICKS,
+  .compare_max_ticks     = USB_HID_TIMER_SYNC_MAX_COMPARE_TICKS,
+  .active_speed          = USBD_SPEED_HIGH,
+  .has_reference         = false,
+  .locked                = false,
+};
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
@@ -516,50 +578,209 @@ static void usbHidSofMonitorApplySpeedParams(uint8_t speed_code)  // V250924R4 
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
 
 
+static uint32_t usbHidTimerSyncResolveInterval(uint8_t speed_code)
+{
+  if (speed_code == USBD_SPEED_FULL)
+  {
+    return 1000U;                                                        // V251010R9: FS 모드는 1kHz (1000us)
+  }
+
+  if (usbBootModeIsFullSpeed())
+  {
+    return 1000U;                                                        // V251010R9: HS PHY라도 FS 강제 시 1kHz 간격 유지
+  }
+
+  return 125U;                                                           // V251010R9: HS 기본 마이크로프레임 125us
+}
+
+
+static int32_t usbHidTimerSyncRoundedShift(int32_t value, uint8_t shift)
+{
+  if (shift == 0U)
+  {
+    return value;
+  }
+
+  int32_t offset = 1 << (shift - 1U);
+
+  if (value >= 0)
+  {
+    return (value + offset) >> shift;                                   // V251010R9: 양수는 양의 방향 라운딩
+  }
+
+  return -(((-value) + offset) >> shift);                                // V251010R9: 음수는 절댓값 라운딩 후 부호 복원
+}
+
+
+static void usbHidTimerSyncReset(uint8_t speed_code, uint32_t now_us)
+{
+  timer_sync.active_speed         = speed_code;
+  timer_sync.expected_interval_us = usbHidTimerSyncResolveInterval(speed_code);
+  timer_sync.compare_ticks        = timer_sync.default_compare_ticks;
+  timer_sync.integral             = 0;
+  timer_sync.last_error_us        = 0;
+  timer_sync.last_delay_us        = timer_sync.target_delay_ticks;
+  timer_sync.last_interval_us     = timer_sync.expected_interval_us;
+  timer_sync.update_count         = 0U;
+  timer_sync.saturation_count     = 0U;
+  timer_sync.locked               = false;
+  timer_sync.has_reference        = false;
+  timer_sync.reset_count++;
+
+  if (htim2.Instance != NULL)
+  {
+    LL_TIM_OC_SetCompareCH1(htim2.Instance, timer_sync.compare_ticks);   // V251010R9: CCR1을 즉시 초기값으로 복원
+  }
+
+  timer_sync.last_sof_us = now_us;
+}
+
+
+static void usbHidTimerSyncOnSof(USBD_HandleTypeDef *pdev, uint32_t now_us)
+{
+  uint8_t  cur_speed = pdev->dev_speed;
+  uint32_t expected_interval = usbHidTimerSyncResolveInterval(cur_speed);
+
+  if ((timer_sync.active_speed != cur_speed) ||
+      (timer_sync.expected_interval_us != expected_interval))
+  {
+    usbHidTimerSyncReset(cur_speed, now_us);                             // V251010R9: 속도/간격 변경 시 적분 상태 초기화
+  }
+  else if (timer_sync.has_reference)
+  {
+    timer_sync.last_interval_us = now_us - timer_sync.last_sof_us;       // V251010R9: 정상 구간에서는 SOF 간격 추적
+  }
+
+  timer_sync.last_sof_us = now_us;
+  timer_sync.has_reference = true;
+  timer_sync.expected_interval_us = expected_interval;
+}
+
+
+static void usbHidTimerSyncOnPulse(void)
+{
+  if (!timer_sync.has_reference)
+  {
+    return;                                                              // V251010R9: SOF 기준이 없다면 보정 생략
+  }
+
+  uint32_t now_us = micros();
+  uint32_t delay_us = now_us - timer_sync.last_sof_us;
+
+  timer_sync.last_delay_us = delay_us;
+
+  int32_t error = (int32_t)timer_sync.target_delay_ticks - (int32_t)delay_us;
+  timer_sync.last_error_us = error;
+
+  uint32_t abs_error = (error >= 0) ? (uint32_t)error : (uint32_t)(-error);
+
+  if (abs_error > USB_HID_TIMER_SYNC_RESET_ERROR_US)
+  {
+    usbHidTimerSyncReset(timer_sync.active_speed, now_us);               // V251010R9: 오차 한계 초과 시 재동기화
+    return;
+  }
+
+  timer_sync.integral += error;
+
+  if (timer_sync.integral > USB_HID_TIMER_SYNC_INTEGRAL_LIMIT)
+  {
+    timer_sync.integral = USB_HID_TIMER_SYNC_INTEGRAL_LIMIT;
+  }
+  else if (timer_sync.integral < -USB_HID_TIMER_SYNC_INTEGRAL_LIMIT)
+  {
+    timer_sync.integral = -USB_HID_TIMER_SYNC_INTEGRAL_LIMIT;
+  }
+
+  int32_t proportional = usbHidTimerSyncRoundedShift(error, USB_HID_TIMER_SYNC_KP_SHIFT);
+  int32_t integral_term = usbHidTimerSyncRoundedShift(timer_sync.integral, USB_HID_TIMER_SYNC_KI_SHIFT);
+  int32_t delta_ticks = proportional + integral_term;
+
+  if (delta_ticks > USB_HID_TIMER_SYNC_MAX_STEP)
+  {
+    delta_ticks = USB_HID_TIMER_SYNC_MAX_STEP;
+  }
+  else if (delta_ticks < -USB_HID_TIMER_SYNC_MAX_STEP)
+  {
+    delta_ticks = -USB_HID_TIMER_SYNC_MAX_STEP;
+  }
+
+  if (delta_ticks != 0)
+  {
+    int32_t new_compare = (int32_t)timer_sync.compare_ticks + delta_ticks;
+
+    if (new_compare < (int32_t)timer_sync.compare_min_ticks)
+    {
+      new_compare = (int32_t)timer_sync.compare_min_ticks;
+      timer_sync.integral = 0;                                            // V251010R9: 포화 시 적분 항을 초기화해 오버슈트 방지
+      timer_sync.saturation_count++;
+    }
+    else if (new_compare > (int32_t)timer_sync.compare_max_ticks)
+    {
+      new_compare = (int32_t)timer_sync.compare_max_ticks;
+      timer_sync.integral = 0;
+      timer_sync.saturation_count++;
+    }
+
+    uint16_t next_ticks = (uint16_t)new_compare;
+
+    if (next_ticks != timer_sync.compare_ticks)
+    {
+      timer_sync.compare_ticks = next_ticks;
+
+      if (htim2.Instance != NULL)
+      {
+        LL_TIM_OC_SetCompareCH1(htim2.Instance, timer_sync.compare_ticks); // V251010R9: LL 레벨로 CCR1 갱신
+      }
+    }
+  }
+
+  timer_sync.update_count++;
+  timer_sync.locked = true;
+}
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
@@ -584,92 +805,98 @@ static uint8_t USBD_HID_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
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
 
-    qbufferCreateBySize(&report_q, (uint8_t *)report_buf, sizeof(report_info_t), 128); 
-    qbufferCreateBySize(&via_report_q, (uint8_t *)via_report_q_buf, sizeof(via_report_info_t), 128); 
-    qbufferCreateBySize(&report_exk_q, (uint8_t *)report_exk_buf, sizeof(report_info_t), 128); 
+    qbufferCreateBySize(&report_q, (uint8_t *)report_buf, sizeof(report_info_t), 128);
+    qbufferCreateBySize(&via_report_q, (uint8_t *)via_report_q_buf, sizeof(via_report_info_t), 128);
+    qbufferCreateBySize(&report_exk_q, (uint8_t *)report_exk_buf, sizeof(report_info_t), 128);
 
     logPrintf("[OK] USB Hid\n");
     logPrintf("     Keyboard\n");
     cliAdd("usbhid", cliCmd);
 
     usbHidInitTimer();
   }
 
+  usbHidTimerSyncReset(pdev->dev_speed, micros());                      // V251010R9: 열거 시마다 타이머 보정 상태를 재초기화
+
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
 
+  timer_sync.has_reference = false;                                      // V251010R9: 디바이스 분리 시 보정 루프 정지
+  timer_sync.locked = false;
+  timer_sync.integral = 0;
+
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
@@ -1054,58 +1281,59 @@ static uint8_t USBD_HID_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
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
+  uint32_t sof_now_us = micros();                                     // V251010R9: SOF 타임스탬프는 동기화 용도로 항상 취득
+
+  usbHidTimerSyncOnSof(pdev, sof_now_us);                             // V251010R9: SOF 기준 보정 상태 갱신
+
 #if _USE_USB_MONITOR
-  usbHidMonitorSof(sof_now_us);                                       // V251009R7: 모니터 활성 시 타임스탬프 전달
+  usbHidMonitorSof(sof_now_us);                                       // V251010R9: 모니터에도 동일 타임스탬프 전달
 #endif
 #if _DEF_ENABLE_USB_HID_TIMING_PROBE
-  usbHidInstrumentationOnSof(sof_now_us);                             // V251009R7: 계측 활성 시 샘플 윈도우 갱신
-#endif
+  usbHidInstrumentationOnSof(sof_now_us);                             // V251010R9: 계측 윈도우 갱신
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
@@ -1173,50 +1401,73 @@ bool usbHidSendReport(uint8_t *p_data, uint16_t length)
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
 
+bool usbHidGetTimerSyncInfo(usb_hid_timer_sync_info_t *p_info)
+{
+  if (p_info == NULL)
+  {
+    return false;
+  }
+
+  p_info->last_delay_us        = timer_sync.last_delay_us;
+  p_info->last_error_us        = timer_sync.last_error_us;
+  p_info->integral_accum       = timer_sync.integral;
+  p_info->compare_ticks        = timer_sync.compare_ticks;
+  p_info->target_ticks         = timer_sync.target_delay_ticks;
+  p_info->last_interval_us     = timer_sync.last_interval_us;
+  p_info->expected_interval_us = timer_sync.expected_interval_us;
+  p_info->update_count         = timer_sync.update_count;
+  p_info->reset_count          = timer_sync.reset_count;
+  p_info->saturation_count     = timer_sync.saturation_count;
+  p_info->speed_code           = timer_sync.active_speed;
+  p_info->is_locked            = timer_sync.locked;
+
+  return timer_sync.has_reference;                                        // V251010R9: SOF 기준 확보 여부 반환
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
@@ -1479,50 +1730,56 @@ void HAL_TIM_Base_MspInit(TIM_HandleTypeDef* tim_baseHandle)
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
+    return;                                                              // V251010R9: HID 경로는 TIM2 펄스에만 반응
+  }
+
+  usbHidTimerSyncOnPulse();                                              // V251010R9: SOF 대비 TIM2 지연 보정 수행
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
------------------
diff --git a/src/hw/driver/usb/usb_hid/usbd_hid.h b/src/hw/driver/usb/usb_hid/usbd_hid.h
index c85490b8b5f8aa7b1cabbdf96dcf5a0b77c0a708..0579bf74bcc294cb8232ba160685031d3adc7543 100644
--- a/src/hw/driver/usb/usb_hid/usbd_hid.h
+++ b/src/hw/driver/usb/usb_hid/usbd_hid.h
@@ -4,50 +4,51 @@
   * @author  MCD Application Team
   * @brief   Header file for the usbd_hid_core.c file.
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
   */
 
 /* Define to prevent recursive inclusion -------------------------------------*/
 #ifndef __USB_HID_H
 #define __USB_HID_H
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 
 /* Includes ------------------------------------------------------------------*/
 #include  "usbd_ioreq.h"
+#include  "usbd_hid_internal.h"    // V251010R9: 타이머 동기화 상태 구조체 공유
 
 /** @addtogroup STM32_USB_DEVICE_LIBRARY
   * @{
   */
 
 /** @defgroup USBD_HID
   * @brief This file is the Header file for usbd_hid.c
   * @{
   */
 
 
 /** @defgroup USBD_HID_Exported_Defines
   * @{
   */
 #define HID_EP_SIZE                                     64U
 
 #define HID_EPIN_ADDR                                   0x81U
 #define HID_EPIN_SIZE                                   64U
 
 #define HID_VIA_EP_IN                                   0x84U
 #define HID_VIA_EP_OUT                                  0x04U
 #define HID_VIA_EP_SIZE                                 32U
 
 #define HID_EXK_EP_IN                                   0x85U
 #define HID_EXK_EP_SIZE                                 8U
@@ -146,45 +147,46 @@ extern USBD_ClassTypeDef USBD_HID;
 uint32_t USBD_HID_GetPollingInterval(USBD_HandleTypeDef *pdev);
 
 
 enum 
 {
   USB_HID_LED_NUM_LOCK    = (1 << 0),
   USB_HID_LED_CAPS_LOCK   = (1 << 1),
   USB_HID_LED_SCROLL_LOCK = (1 << 2),
   USB_HID_LED_COMPOSE     = (1 << 3),
   USB_HID_LED_KANA        = (1 << 4)
 };
 
 typedef struct
 {
   uint32_t freq_hz;
   uint32_t time_max;
   uint32_t time_min;
   uint32_t time_excess_max;  // V250928R3 예상 간격 초과분 최대값(us)
   uint32_t queue_depth_max;  // V250928R3 폴링 지연 당시 대기 중이던 큐 길이 최대값
 } usb_hid_rate_info_t;
 
 bool usbHidSetViaReceiveFunc(void (*func)(uint8_t *, uint8_t));
 bool usbHidSendReport(uint8_t *p_data, uint16_t length);
 bool usbHidSendReportEXK(uint8_t *p_data, uint16_t length);
 bool usbHidGetRateInfo(usb_hid_rate_info_t *p_info);
+bool usbHidGetTimerSyncInfo(usb_hid_timer_sync_info_t *p_info);  // V251010R9: TIM2 SOF 보정 상태 제공
 bool usbHidSetTimeLog(uint16_t index, uint32_t time_us);
 void usbHidSetStatusLed(uint8_t led_bits);
 
 /**
   * @}
   */
 
 #ifdef __cplusplus
 }
 #endif
 
 #endif  /* __USB_HID_H */
 /**
   * @}
   */
 
 /**
   * @}
   */
------------------
diff --git a/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c b/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c
index 8d9773d8476df44a6b7008ec54ad93d9c1af45ab..29fc4b68adb7d7d5730922abfc41e2aff7396a54 100644
--- a/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c
+++ b/src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c
@@ -289,50 +289,78 @@ void usbHidInstrumentationHandleCli(cli_args_t *args)
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
+        bool sync_ready = usbHidGetTimerSyncInfo(&sync_info);             // V251010R9: SOF 기반 TIM2 보정 상태 조회
+
+        if (sync_ready)
+        {
+          cliPrintf("  보정 상태    : 잠금 %s, 지연 %3lu us / 목표 %3u us, 오차 %+4ld us\n",
+                    (sync_info.is_locked ? "ON" : "OFF"),
+                    (unsigned long)sync_info.last_delay_us,
+                    (unsigned int)sync_info.target_ticks,
+                    (long)sync_info.last_error_us);
+          cliPrintf("                 CCR %3u, I %+6ld, 업데이트 %lu, 리셋 %lu, 포화 %lu\n",
+                    (unsigned int)sync_info.compare_ticks,
+                    (long)sync_info.integral_accum,
+                    (unsigned long)sync_info.update_count,
+                    (unsigned long)sync_info.reset_count,
+                    (unsigned long)sync_info.saturation_count);
+          cliPrintf("                 SOF 간격 %4lu us / 기대 %4lu us, 속도 코드 %u\n",
+                    (unsigned long)sync_info.last_interval_us,
+                    (unsigned long)sync_info.expected_interval_us,
+                    (unsigned int)sync_info.speed_code);
+        }
+        else
+        {
+          cliPrintf("  보정 상태    : 동기화 대기 (리셋 %lu, 기대 SOF %4lu us)\n",
+                    (unsigned long)sync_info.reset_count,
+                    (unsigned long)sync_info.expected_interval_us);
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
index 68abcfed47dca94470079945dba31ba4ce8dffb7..60b2fbc419a8d03a76f84b68b0634f9b48c006be 100644
--- a/src/hw/driver/usb/usb_hid/usbd_hid_internal.h
+++ b/src/hw/driver/usb/usb_hid/usbd_hid_internal.h
@@ -1,6 +1,25 @@
 #pragma once
 
+#include <stdbool.h>  // V251010R9: 타이머 동기화 정보 구조체에 bool 타입 사용
+#include <stdint.h>   // V251010R9: 공용 상태 구조체에 고정 폭 정수 타입 명시
+
 #include "hw_def.h"
 
 #define HID_KEYBOARD_REPORT_SIZE (HW_KEYS_PRESS_MAX + 2U)
 #define KEY_TIME_LOG_MAX         32  // V251009R9: 계측 모듈과 본체에서 공유
+
+typedef struct
+{
+  uint32_t last_delay_us;           // V251010R9: 직전 TIM2 펄스의 SOF 기준 지연(us)
+  int32_t  last_error_us;           // V251010R9: 목표 지연과의 차이(us)
+  int32_t  integral_accum;          // V251010R9: PI 적분항 누적 상태
+  uint16_t compare_ticks;           // V251010R9: 현재 CCR1 설정값
+  uint16_t target_ticks;            // V251010R9: 목표 CCR1 값(120us)
+  uint32_t last_interval_us;        // V251010R9: 최근 SOF 간격(us)
+  uint32_t expected_interval_us;    // V251010R9: 속도별 기대 SOF 간격(us)
+  uint32_t update_count;            // V251010R9: 보정 루프 누적 실행 횟수
+  uint32_t reset_count;             // V251010R9: 속도 전환 및 보호 초기화 누적 횟수
+  uint32_t saturation_count;        // V251010R9: 비교값이 한계에 도달한 누적 횟수
+  uint8_t  speed_code;              // V251010R9: USB 장치 속도 코드(USBD_SPEED_*)
+  bool     is_locked;               // V251010R9: SOF 기준이 확보되어 보정이 활성화되었는지 여부
+} usb_hid_timer_sync_info_t;
------------------