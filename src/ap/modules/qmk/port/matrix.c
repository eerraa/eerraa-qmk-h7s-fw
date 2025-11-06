#include "matrix.h"
#include "debounce.h"
#include "keyboard.h"
#include "util.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "cli.h"
#include "usb.h"
#include "keys.h"
#include "matrix_debug.h"           // V251017R3: 매트릭스 디버그 로그 인터페이스 추가
#include "matrix_instrumentation.h"  // V251009R9: 매트릭스 계측 경로를 독립 모듈로 이관


/* matrix state(1:on, 0:off) */
static matrix_row_t raw_matrix[MATRIX_ROWS]; // raw values
static matrix_row_t matrix[MATRIX_ROWS];     // debounced values
static bool         is_info_enable      = false;
static bool         matrix_debug_enable = false;  // V251017R3: USB_CDC 기반 매트릭스 디버그 토글
static uint32_t     matrix_debug_last_scan_us = 0; // V251017R3: 스캔 간격 측정을 위한 타임스탬프 유지

static void cliCmd(cli_args_t *args);
static void matrix_info(void);


bool matrixDebugIsEnabled(void)
{
  return matrix_debug_enable;  // V251017R3: 외부에서 매트릭스 디버그 상태 조회
}

void matrixDebugLog(const char *fmt, ...)
{
  if (matrix_debug_enable == false)
  {
    return;
  }

  va_list args;
  char    log_buf[192];

  va_start(args, fmt);
  int written = vsnprintf(log_buf, sizeof(log_buf), fmt, args);
  va_end(args);

  if (written > 0)
  {
    logPrintf("[V251017R3][matrix] %s", log_buf);  // V251017R3: 매트릭스 디버그 로그 통일 프리픽스
  }
}





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
  uint32_t     now_us   = micros();            // V251017R3: 스캔 주기 관찰을 위한 타임스탬프 확보
  uint32_t     interval = 0;

  if (matrix_debug_last_scan_us != 0)
  {
    interval = now_us - matrix_debug_last_scan_us;
  }
  matrix_debug_last_scan_us = now_us;

  if (matrix_debug_enable)
  {
    matrixDebugLog("scan begin t=%lu us interval=%lu us\n", (unsigned long)now_us, (unsigned long)interval);
  }

  _Static_assert(sizeof(matrix_row_t) == sizeof(uint16_t),
                 "matrix_row_t must match keysReadColsBuf element size");

  const volatile matrix_row_t *hw_matrix = (const volatile matrix_row_t *)keysPeekColsBuf();  // V250924R5: DMA 버퍼를 직접 참조하여 스캔 복사 비용 제거 (재검토: volatile 로 최신 스캔 보장)
  // V251009R3: DMA 폴백 블록을 제거해 단일 경로로 단순화
  // V251017R2: DMA 프레임 tear 감지를 위해 이중 스냅샷 검증을 수행해 일관된 행 상태 확보
  matrix_row_t matrix_snapshot[MATRIX_ROWS];
  matrix_row_t matrix_verify[MATRIX_ROWS];
  matrix_row_t matrix_before[MATRIX_ROWS];
  bool         frame_consistent = false;
  uint32_t     mismatch_attempt = 0;
  uint32_t     mismatch_row     = 0;
  matrix_row_t mismatch_snapshot = 0;
  matrix_row_t mismatch_verify   = 0;

  for (uint32_t attempt=0; attempt<3 && frame_consistent == false; attempt++)
  {
    for (uint32_t rows=0; rows<MATRIX_ROWS; rows++)
    {
      matrix_snapshot[rows] = hw_matrix[rows];
    }

    for (uint32_t rows=0; rows<MATRIX_ROWS; rows++)
    {
      matrix_verify[rows] = hw_matrix[rows];
    }

    frame_consistent = true;
    for (uint32_t rows=0; rows<MATRIX_ROWS; rows++)
    {
      if (matrix_snapshot[rows] != matrix_verify[rows])
      {
        frame_consistent = false;
        mismatch_attempt  = attempt + 1U;
        mismatch_row      = rows;
        mismatch_snapshot = matrix_snapshot[rows];
        mismatch_verify   = matrix_verify[rows];
        break;
      }
    }

    if (matrix_debug_enable)
    {
      if (frame_consistent)
      {
        matrixDebugLog("scan frame stable after attempt %lu\n", (unsigned long)(attempt + 1U));
      }
      else
      {
        matrixDebugLog("scan tear attempt %lu row %lu snap=0x%04X verify=0x%04X\n",
                       (unsigned long)mismatch_attempt,
                       (unsigned long)mismatch_row,
                       mismatch_snapshot,
                       mismatch_verify);
      }
    }
  }

  if (frame_consistent == false)
  {
    memcpy(matrix_snapshot, matrix_verify, sizeof(matrix_snapshot));  // V251017R2: tear 감지 실패 시 최신 검증 결과로 강제 동기화
    if (matrix_debug_enable)
    {
      matrixDebugLog("scan tear unresolved after retries, fallback row %lu value=0x%04X\n",
                     (unsigned long)mismatch_row,
                     mismatch_verify);
    }
  }

  uint32_t raw_changed_mask = 0;
  for (uint32_t rows=0; rows<MATRIX_ROWS; rows++)
  {
    matrix_row_t prev_state = raw_matrix[rows];
    matrix_row_t new_state  = matrix_snapshot[rows];

    if (prev_state != new_state)
    {
      raw_matrix[rows] = new_state;
      changed          = true;
      raw_changed_mask |= (1U << rows);
      if (matrix_debug_enable)
      {
        matrixDebugLog("raw[%lu] 0x%04X -> 0x%04X\n",
                       (unsigned long)rows,
                       prev_state,
                       new_state);
      }
    }
  }

  if (matrix_debug_enable && raw_changed_mask == 0)
  {
    matrixDebugLog("raw matrix unchanged\n");
  }

  matrixInstrumentationLogScan(pre_time, is_info_enable);

  memcpy(matrix_before, matrix, sizeof(matrix_before));
  changed = debounce(raw_matrix, matrix, MATRIX_ROWS, changed);

  if (matrix_debug_enable)
  {
    uint32_t debounced_mask = 0;
    for (uint32_t rows=0; rows<MATRIX_ROWS; rows++)
    {
      if (matrix_before[rows] != matrix[rows])
      {
        debounced_mask |= (1U << rows);
        matrixDebugLog("debounce[%lu] 0x%04X -> 0x%04X\n",
                       (unsigned long)rows,
                       matrix_before[rows],
                       matrix[rows]);
      }
    }
    matrixDebugLog("scan complete changed=%u frame_consistent=%s raw_mask=0x%04X debounced_mask=0x%04X\n",
                   changed ? 1U : 0U,
                   frame_consistent ? "yes":"no",
                   (unsigned int)raw_changed_mask,
                   (unsigned int)debounced_mask);
  }

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

  if (args->argc == 2 && args->isStr(0, "debug"))
  {
    if (args->isStr(1, "on"))
    {
      matrix_debug_enable      = true;   // V251017R3: 매트릭스 디버그 로그 활성화
      matrix_debug_last_scan_us = 0;
      matrixDebugLog("debug logging enabled\n");
      cliPrintf("matrix debug : on\n");
      ret = true;
    }
    else if (args->isStr(1, "off"))
    {
      if (matrix_debug_enable)
      {
        logPrintf("[V251017R3][matrix] debug logging disabled\n");
      }
      matrix_debug_enable       = false;
      matrix_debug_last_scan_us = 0;
      cliPrintf("matrix debug : off\n");
      ret = true;
    }
  }

  if (args->argc == 1 && args->isStr(0, "debug"))
  {
    cliPrintf("matrix debug : %s\n", matrix_debug_enable ? "on":"off");
    ret = true;
  }

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
