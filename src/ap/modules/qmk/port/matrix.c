#include "matrix.h"
#include "debounce.h"
#include "keyboard.h"
#include "util.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "cli.h"
#include "usb.h"
#include "keys.h"


/* matrix state(1:on, 0:off) */
static matrix_row_t raw_matrix[MATRIX_ROWS]; // raw values
static matrix_row_t matrix[MATRIX_ROWS];     // debounced values
static bool         is_info_enable = false;
#if _DEF_ENABLE_MATRIX_TIMING_PROBE
static uint32_t     key_scan_time  = 0;
#endif

static void cliCmd(cli_args_t *args);
static void matrix_info(void);





void matrix_init(void)
{
  memset(matrix, 0, sizeof(matrix));
  memset(raw_matrix, 0, sizeof(raw_matrix));

  debounce_init(MATRIX_ROWS);

  cliAdd("matrix", cliCmd);
}

void matrix_print(void)
{
}

bool matrix_can_read(void) 
{
  return true;
}

matrix_row_t matrix_get_row(uint8_t row)
{
  return matrix[row];
}

uint8_t matrix_scan(void)
{
  uint32_t     pre_time;
  bool         changed = false;


  pre_time = micros();

  _Static_assert(sizeof(matrix_row_t) == sizeof(uint16_t),
                 "matrix_row_t must match keysReadColsBuf element size");

  const volatile matrix_row_t *hw_matrix = (const volatile matrix_row_t *)keysPeekColsBuf();  // V250924R5: DMA 버퍼를 직접 참조하여 스캔 복사 비용 제거 (재검토: volatile 로 최신 스캔 보장)
  // V251009R3: DMA 폴백 블록을 제거해 단일 경로로 단순화

  for (uint32_t rows=0; rows<MATRIX_ROWS; rows++)
  {
    matrix_row_t new_state = (matrix_row_t)hw_matrix[rows];

    if (raw_matrix[rows] != new_state)
    {
      raw_matrix[rows] = new_state;
      changed          = true;
    }
  }

#if _DEF_ENABLE_MATRIX_TIMING_PROBE
  if (is_info_enable)
  {
    key_scan_time = micros() - pre_time;  // V251009R4: 런타임 플래그가 활성화된 경우에만 스캔 계측 실행
  }
#endif

  changed = debounce(raw_matrix, matrix, MATRIX_ROWS, changed);
  if (changed)
  {
    usbHidSetTimeLog(0, pre_time);
  }
  matrix_info();

  return (uint8_t)changed;
}

void matrix_info(void)
{
#ifdef DEBUG_MATRIX_SCAN_RATE
  static uint32_t pre_time = 0;

  if (is_info_enable)
  {
    if (millis()-pre_time >= 1000)
    {
      pre_time = millis();
      usb_hid_rate_info_t hid_info;

      usbHidGetRateInfo(&hid_info);

      logPrintf("Scan Rate : %d.%d KHz\n", get_matrix_scan_rate()/1000, get_matrix_scan_rate()%1000);
      logPrintf("Poll Rate : %d Hz, %d us(max), %d us(min), %d us(excess), %d queued(max)\n", // V250928R3 HID 진단 지표 노출
                hid_info.freq_hz,
                hid_info.time_max,
                hid_info.time_min,
                hid_info.time_excess_max,
                hid_info.queue_depth_max);
#if _DEF_ENABLE_MATRIX_TIMING_PROBE
      logPrintf("Scan Time : %d us\n", key_scan_time);
#else
      logPrintf("Scan Time : disabled\n");  // V251009R4: 계측 가드 비활성화 시 안내
#endif
    }
  }
#endif
}

void cliCmd(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("is_info_enable : %s\n", is_info_enable ? "on":"off");

    usb_hid_rate_info_t hid_info;

    usbHidGetRateInfo(&hid_info);

    logPrintf("Scan Rate : %d.%d KHz\n", get_matrix_scan_rate()/1000, get_matrix_scan_rate()%1000);
    logPrintf("Poll Rate : %d Hz, %d us(max), %d us(min), %d us(excess), %d queued(max)\n", // V250928R3 HID 진단 지표 노출
              hid_info.freq_hz,
              hid_info.time_max,
              hid_info.time_min,
              hid_info.time_excess_max,
              hid_info.queue_depth_max);
#if _DEF_ENABLE_MATRIX_TIMING_PROBE
    logPrintf("Scan Time : %d us\n", key_scan_time);
#else
    logPrintf("Scan Time : disabled\n");  // V251009R4: 빌드 타임으로 계측이 제외되었음을 안내
#endif

    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "row"))
  {
    uint16_t data;

    data = args->getData(1);

    cliPrintf("row 0:0x%X\n", data);
    matrix[0] = data;
    delay(50);

    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "info"))
  {
    if (args->isStr(1, "on"))
    {
      is_info_enable = true;
    }
    if (args->isStr(1, "off"))
    {
      is_info_enable = false;
#if _DEF_ENABLE_MATRIX_TIMING_PROBE
      key_scan_time  = 0;  // V251009R4: 플래그 비활성화 시 마지막 계측값 초기화
#endif
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("matrix info\n");
    cliPrintf("matrix row data\n");
    #ifdef DEBUG_MATRIX_SCAN_RATE
    cliPrintf("matrix info on:off\n");
    #endif    
  }
}