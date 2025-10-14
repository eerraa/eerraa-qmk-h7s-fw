#include "usb.h"                        // V251010R2: 계측 매크로 정의를 선행해 헤더 확장 시 중복 심벌 생성을 방지
#include "usbd_hid_instrumentation.h"   // V251009R9: HID 계측 경로 분리 구현

#include <string.h>

#include "bsp.h"
#include "def.h"
#include "log.h"
#include "micros.h"

#if _DEF_ENABLE_USB_HID_TIMING_PROBE

static uint32_t data_in_cnt = 0;                                   // V251009R5: 개발용 계측이 활성화될 때만 누적 카운트 유지
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

static volatile int      timer_cnt = 0;                      // V251009R5: 계측 활성 시에만 타이머 인터럽트 카운터 유지
static volatile uint32_t timer_end = 0;
static uint32_t          sof_cnt = 0;

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

  rate_time_sof_pre = now_us;
  if (sample_cnt >= sample_window)
  {
    sample_cnt = 0;
    data_in_rate = data_in_cnt;
    rate_time_min = rate_time_min_check;
    rate_time_max = rate_time_max_check;
    rate_time_avg = rate_time_sum / (data_in_cnt + 1U);
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
  timer_cnt++;
  timer_end = micros()-rate_time_sof_pre;
  sof_cnt++;
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

void usbHidMeasureRateTime(void)
{
  rate_time_sof = micros() - rate_time_sof_pre;

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
        rate_time_excess_max_check = excess_us;
      }

      if (rate_queue_depth_max_check < rate_queue_depth_snapshot)
      {
        rate_queue_depth_max_check = rate_queue_depth_snapshot;
      }
    }

    uint32_t rate_time_idx = constrain(rate_time_us/10, 0, 99);
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

#endif  // V251010R1: 계측 비활성 시 인라인 스텁 사용을 위해 함수 정의를 조건부로 제한

bool usbHidGetRateInfo(usb_hid_rate_info_t *p_info)
{
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
  p_info->freq_hz = data_in_rate;
  p_info->time_max = rate_time_max;
  p_info->time_min = rate_time_min;
  p_info->time_excess_max = rate_time_excess_max;                   // V250928R3 폴링 초과 지연 최대값 보고
  p_info->queue_depth_max = rate_queue_depth_max;                   // V250928R3 큐 잔량 최대값 보고
  return true;
#else
  p_info->freq_hz = 0;
  p_info->time_max = 0;
  p_info->time_min = 0;
  p_info->time_excess_max = 0;
  p_info->queue_depth_max = 0;
  return false;                                                     // V251009R5: 계측 비활성 시 통계 제공 불가 안내
#endif
}

bool usbHidSetTimeLog(uint16_t index, uint32_t time_us)
{
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
  (void)index;
  key_time_raw_pre = time_us;
  key_time_raw_req = true;
  return true;
#else
  (void)index;
  (void)time_us;
  return false;                                                     // V251009R5: 릴리스 빌드에서는 키 타이밍 로그를 축적하지 않음
#endif
}

void usbHidInstrumentationHandleCli(cli_args_t *args)
{
#ifdef _USE_HW_CLI
#if _DEF_ENABLE_USB_HID_TIMING_PROBE
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    ret = true;
  }

  if (args->argc >= 1 && args->isStr(0, "rate") == true)
  {
    uint32_t pre_time = millis();
    uint32_t pre_time_key = millis();
    uint32_t key_send_cnt = 0;

    memset(rate_his_buf, 0, sizeof(rate_his_buf));

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
    uint16_t time_max_val[3] = {0, 0, 0};
    uint16_t time_min_val[3] = {0xFFFF, 0xFFFF, 0xFFFF};
    uint16_t time_sum_val[3] = {0, 0, 0};

    for (int i = 0; i < (int)key_time_cnt; i++)
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

        time_sum_val[j] += data;
        if (data > time_max_val[j])
          time_max_val[j] = data;
        if (data < time_min_val[j])
          time_min_val[j] = data;
      }
    }

    cliPrintf("\n");
    if (key_time_cnt > 0)
    {
      cliPrintf("avg : %3d us %3d us %3d us\n",
                time_sum_val[0] / key_time_cnt,
                time_sum_val[1] / key_time_cnt,
                time_sum_val[2] / key_time_cnt);
      cliPrintf("max : %3d us %3d us %3d us\n", time_max_val[0], time_max_val[1], time_max_val[2]);
      cliPrintf("min : %3d us %3d us %3d us\n", time_min_val[0], time_min_val[1], time_min_val[2]);
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
#else
  (void)args;
  cliPrintf("usbhid 계측이 비활성화되었습니다 (_DEF_ENABLE_USB_HID_TIMING_PROBE=0). USB 불안정성 감지는 계속 동작합니다.\n"); // V251009R5: 릴리스 빌드 안내 메시지
#endif
#else
  (void)args;
#endif
}

#if _DEF_ENABLE_USB_HID_TIMING_PROBE

static uint32_t usbHidExpectedPollIntervalUs(void)
{
  if (usbBootModeIsFullSpeed())
  {
    return 1000U;                                                   // V250928R3 FS 1kHz = 1000us 간격
  }

  uint8_t hs_interval = usbBootModeGetHsInterval();                  // V250928R3 HS 모드 bInterval 읽기

  if (hs_interval < 1U)
  {
    hs_interval = 1U;
  }

  uint32_t microframes = 1UL << (hs_interval - 1U);                  // V250928R3 2^(bInterval-1) 마이크로프레임 수

  return microframes * 125U;                                         // V250928R3 1 마이크로프레임 = 125us
}

#endif
