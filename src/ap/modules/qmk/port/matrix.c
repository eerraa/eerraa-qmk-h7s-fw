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
#include "matrix_instrumentation.h"  // V251009R9: 매트릭스 계측 경로를 독립 모듈로 이관


/* matrix state(1:on, 0:off) */
static matrix_row_t raw_matrix[MATRIX_ROWS]; // raw values
static matrix_row_t matrix[MATRIX_ROWS];     // debounced values
static bool         is_info_enable = false;

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
  bool         changed = false;
  uint32_t     pre_time = matrixInstrumentationCaptureStart();

  _Static_assert(sizeof(matrix_row_t) == sizeof(uint16_t),
                 "matrix_row_t must match keysReadColsBuf element size");

  const volatile matrix_row_t *hw_matrix = (const volatile matrix_row_t *)keysPeekColsBuf();  // V250924R5: DMA 버퍼를 직접 참조하여 스캔 복사 비용 제거 (재검토: volatile 로 최신 스캔 보장)
  // V251009R3: DMA 폴백 블록을 제거해 단일 경로로 단순화
  // V251017R1: DMA 버퍼를 직접 참조하되 한 번에 스냅샷으로 복사해 프레임 일관성 확보
  matrix_row_t matrix_snapshot[MATRIX_ROWS];

  for (uint32_t rows=0; rows<MATRIX_ROWS; rows++)
  {
    matrix_snapshot[rows] = hw_matrix[rows];
  }

  // V251017R1: DMA 버퍼를 스냅샷으로 복사해 한 스캔 호출 내 행 일관성을 확보
  for (uint32_t rows=0; rows<MATRIX_ROWS; rows++)
  {
    matrix_row_t new_state = matrix_snapshot[rows];

    if (raw_matrix[rows] != new_state)
    {
      raw_matrix[rows] = new_state;
      changed          = true;
    }
  }

  matrixInstrumentationLogScan(pre_time, is_info_enable);

  changed = debounce(raw_matrix, matrix, MATRIX_ROWS, changed);
  matrixInstrumentationPropagate(changed, pre_time);
  matrix_info();

  return (uint8_t)changed;
}

void matrix_info(void)
{
#if _DEF_ENABLE_MATRIX_TIMING_PROBE
  // V251010R4: DEBUG_MATRIX_SCAN_RATE 의존성을 제거하고 단일 빌드 가드로 통합
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
      if (matrixInstrumentationIsCompileEnabled())
      {
        logPrintf("Scan Time : %d us\n", matrixInstrumentationGetScanTime());
      }
      else
      {
        logPrintf("Scan Time : disabled\n");  // V251009R4: 계측 가드 비활성화 시 안내
      }
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

    #if _DEF_ENABLE_MATRIX_TIMING_PROBE
    logPrintf("Scan Rate : %d.%d KHz\n", get_matrix_scan_rate()/1000, get_matrix_scan_rate()%1000);
    #else
    logPrintf("Scan Rate : disabled\n");  // V251010R3: 빌드 타임으로 스캔 계측이 제거된 경우 안내
    #endif
    logPrintf("Poll Rate : %d Hz, %d us(max), %d us(min), %d us(excess), %d queued(max)\n", // V250928R3 HID 진단 지표 노출
              hid_info.freq_hz,
              hid_info.time_max,
              hid_info.time_min,
              hid_info.time_excess_max,
              hid_info.queue_depth_max);
    if (matrixInstrumentationIsCompileEnabled())
    {
      logPrintf("Scan Time : %d us\n", matrixInstrumentationGetScanTime());
    }
    else
    {
      logPrintf("Scan Time : disabled\n");  // V251009R4: 빌드 타임으로 계측이 제외되었음을 안내
    }

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
    #if _DEF_ENABLE_MATRIX_TIMING_PROBE
    if (args->isStr(1, "on"))
    {
      is_info_enable = true;
    }
    if (args->isStr(1, "off"))
    {
      is_info_enable = false;
      matrixInstrumentationReset();  // V251009R9: 계측 모듈로 상태 초기화 이관
    }
    #else
    cliPrintf("matrix scan 계측이 비활성화되었습니다 (_DEF_ENABLE_MATRIX_TIMING_PROBE=0).\n"); // V251010R3: 릴리스 빌드 안내
    #endif
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("matrix info\n");
    cliPrintf("matrix row data\n");
    #if _DEF_ENABLE_MATRIX_TIMING_PROBE
    cliPrintf("matrix info on:off\n");
    #else
    cliPrintf("matrix info on:off (disabled)\n");  // V251010R3: 계측 비활성화 상태 안내
    #endif
  }
}
