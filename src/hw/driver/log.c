/*
 * log.c
 *
 *  Created on: Nov 12, 2021
 *      Author: baram
 */




#include "log.h"
#include "uart.h"
#ifdef _USE_HW_CLI
#include "cli.h"
#endif
#ifdef _USE_HW_CDC
#include "cdc.h"
#endif


#ifdef _USE_HW_LOG

#ifdef _USE_HW_RTOS
#define lock()      xSemaphoreTake(mutex_lock, portMAX_DELAY);
#define unLock()    xSemaphoreGive(mutex_lock);
#else
#define lock()
#define unLock()
#endif


typedef struct
{
  uint16_t line_index;
  uint16_t buf_length;
  uint16_t buf_length_max;
  uint16_t buf_index;
  uint8_t *buf;
  uint64_t total_length;                                       // V251017R1 CDC 동기화를 위한 누적 바이트 수
} log_buf_t;


enum
{
  LOG_PREFIX_RESERVE = 8U                                      // V251017R3 CDC/CLI 공통 라인 헤더 예약 길이
};


log_buf_t log_buf_boot;
log_buf_t log_buf_list;

static uint8_t buf_boot[LOG_BOOT_BUF_MAX];
static uint8_t buf_list[LOG_LIST_BUF_MAX];

static bool is_init = false;
static bool is_boot_log = true;
static bool is_enable = true;
static bool is_open = false;

static uint8_t  log_ch = LOG_CH;
static uint32_t log_baud = 115200;

static char print_buf[256];

#ifdef _USE_HW_RTOS
static SemaphoreHandle_t mutex_lock;
#endif

#ifdef _USE_HW_CDC
static bool     log_cdc_ready        = false;                  // V251017R1 CDC 연결 상태 캐시
static uint64_t log_cdc_boot_sent    = 0;                      // V251017R1 부트 로그 CDC 전송 위치
static uint64_t log_cdc_list_sent    = 0;                      // V251017R1 일반 로그 CDC 전송 위치
static uint64_t log_list_boot_mirror_end = 0;                  // V251017R2 부트 로그 전송 경계값 유지

static void logCdcTryFlush(bool is_connected);                 // V251017R3 CDC 연결 상태 분리로 재호출 최소화
static void logCdcFlushBuffered(void);
static void logCdcDrainBuffer(log_buf_t *p_log, uint64_t *p_sent_total);
static void logCdcResetState(void);                            // V251017R3 CDC 상태 초기화 헬퍼
static void logCdcApplyBootDisable(void);                      // V251017R3 부트 로그 종료 시 CDC 동기화 처리
static void logCdcApplyBootEnable(void);                       // V251017R3 부트 로그 시작 시 CDC 동기화 처리
#endif



#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif





static void logBufSetup(log_buf_t *p_log, uint8_t *p_buf, uint16_t capacity); // V251017R3 버퍼 초기화 공통화


bool logInit(void)
{
#ifdef _USE_HW_RTOS
  mutex_lock = xSemaphoreCreateMutex();
#endif

  logBufSetup(&log_buf_boot, buf_boot, LOG_BOOT_BUF_MAX);      // V251017R3 반복 초기화 제거
  logBufSetup(&log_buf_list, buf_list, LOG_LIST_BUF_MAX);      // V251017R3 반복 초기화 제거

#ifdef _USE_HW_CDC
  logCdcResetState();                                          // V251017R3 CDC 상태 초기화 공통 처리
#endif

  is_init = true;

#ifdef _USE_HW_CLI
  cliAdd("log", cliCmd);
#endif

  return true;
}

void logEnable(void)
{
  is_enable = true;
}

void logDisable(void)
{
  is_enable = false;
}

void logBoot(uint8_t enable)
{
#ifdef _USE_HW_CDC
  if (is_boot_log == true && enable == false)
  {
    logCdcApplyBootDisable();                                  // V251017R3 CDC 부트 로그 종료 처리
  }
  else if (is_boot_log == false && enable == true)
  {
    logCdcApplyBootEnable();                                   // V251017R3 CDC 부트 로그 시작 처리
  }
#endif
  is_boot_log = enable;
}

bool logOpen(uint8_t ch, uint32_t baud)
{
  log_ch   = ch;
  log_baud = baud;
  is_open  = true;

  is_open = uartOpen(ch, baud);

  return is_open;
}

static void logBufSetup(log_buf_t *p_log, uint8_t *p_buf, uint16_t capacity)
{
  p_log->line_index     = 0;
  p_log->buf_length     = 0;
  p_log->buf_length_max = capacity;
  p_log->buf_index      = 0;
  p_log->buf            = p_buf;
  p_log->total_length   = 0;
}


static bool logBufPrintf(log_buf_t *p_log, const char *p_data, uint32_t length)
{
  if (p_log->buf_length_max == 0 || length == 0)
  {
    return false;
  }

  uint32_t needed = length + LOG_PREFIX_RESERVE;
  if (needed > p_log->buf_length_max)
  {
    return false;
  }

  uint32_t buf_last = p_log->buf_index + needed;
  if (buf_last > p_log->buf_length_max)
  {
    p_log->buf_index = 0;
    buf_last = needed;

    if (buf_last > p_log->buf_length_max)
    {
      return false;
    }
  }

  uint8_t *p_buf = &p_log->buf[p_log->buf_index];
  int32_t written = snprintf((char *)p_buf, needed, "%04X\t%s", p_log->line_index, p_data);
  if (written <= 0)
  {
    return false;
  }

  uint32_t write_len = (uint32_t)written;
  p_log->line_index++;
  p_log->buf_index += write_len;


  if (write_len + p_log->buf_length <= p_log->buf_length_max)
  {
    p_log->buf_length += write_len;
  }

  p_log->total_length += (uint64_t)write_len;                  // V251017R3 CDC 누적 카운터 정밀도 유지

  return true;
}

void logPrintf(const char *fmt, ...)
{

  va_list args;
  int len;


  if (is_init != true) return;

  lock();
  va_start(args, fmt);
  len = vsnprintf(print_buf, 256, fmt, args);

  if (len <= 0)
  {
    va_end(args);
    unLock();
    return;                                                    // V251017R3 포맷 실패 시 즉시 종료
  }

  uint32_t data_len = (uint32_t)len;

  if (is_open == true && is_enable == true)
  {
    uartWrite(log_ch, (uint8_t *)print_buf, data_len);
  }

  if (is_boot_log)
  {
    logBufPrintf(&log_buf_boot, print_buf, data_len);
  }
  logBufPrintf(&log_buf_list, print_buf, data_len);
#ifdef _USE_HW_CDC
  logCdcTryFlush(cdcIsConnect());                              // V251017R3 CDC 연결 상태를 재사용
#endif

  va_end(args);

  unLock();
}

#ifdef _USE_HW_CDC
void logProcess(void)
{
  if (is_init != true)
  {
    return;
  }

  bool is_connected = cdcIsConnect();
  if (is_connected != true && log_cdc_ready == false)
  {
    return;                                                    // V251017R3 비연결 상태에서 불필요한 락 제거
  }

  lock();
  logCdcTryFlush(is_connected);                                // V251017R3 주기적 CDC 플러시
  unLock();
}

static void logCdcTryFlush(bool is_connected)
{
  if (is_connected != true)
  {
    log_cdc_ready = false;
    return;
  }

  if (log_cdc_ready == false)
  {
    log_cdc_ready = true;
    log_cdc_boot_sent = 0;                                     // V251017R2 CDC 재연결 시 부트 로그 전체 재전송
    if (log_list_boot_mirror_end > 0)
    {
      if (log_cdc_list_sent < log_list_boot_mirror_end)
      {
        log_cdc_list_sent = log_list_boot_mirror_end;          // V251017R2 이미 전송한 부트 로그 범위 스킵
      }
    }
    else
    {
      log_cdc_list_sent = log_buf_list.total_length;           // V251017R2 부트 진행 중이면 리스트 로그 생략
    }
  }

  logCdcFlushBuffered();
}

static void logCdcFlushBuffered(void)
{
  logCdcDrainBuffer(&log_buf_boot, &log_cdc_boot_sent);
  logCdcDrainBuffer(&log_buf_list, &log_cdc_list_sent);
}

static void logCdcDrainBuffer(log_buf_t *p_log, uint64_t *p_sent_total)
{
  uint32_t capacity = p_log->buf_length_max;
  uint64_t total    = p_log->total_length;

  if (capacity == 0)
  {
    return;
  }

  if (total < *p_sent_total)
  {
    *p_sent_total = total;
  }

  uint64_t available = total - *p_sent_total;
  if (available == 0)
  {
    return;
  }

  if (available > capacity)
  {
    *p_sent_total = total - capacity;
    available = capacity;
    uint32_t idx     = (uint32_t)(*p_sent_total % capacity);   // V251017R2 라인 경계 보존을 위해 헤더 조정
    uint32_t eaten   = 0;
    while (eaten < available)
    {
      if (p_log->buf[idx] == '\n')
      {
        idx = (idx + 1) % capacity;
        eaten++;
        break;
      }
      idx = (idx + 1) % capacity;
      eaten++;
    }
    *p_sent_total += eaten;
    available     -= eaten;
    if (available == 0)
    {
      return;
    }
  }

  uint32_t start     = (uint32_t)(*p_sent_total % capacity);
  uint32_t remaining = (uint32_t)available;

  while (remaining > 0)
  {
    uint32_t limit = start + remaining;
    if (limit > capacity)
    {
      limit = capacity;
    }

    uint32_t chunk = limit - start;
    uint32_t sent  = cdcWrite(&p_log->buf[start], chunk);

    if (sent == 0)
    {
      return;
    }

    start     += sent;
    remaining -= sent;
    *p_sent_total += sent;

    if (start >= capacity)
    {
      start = 0;
    }

    if (sent < chunk)
    {
      return;
    }
  }
}


static void logCdcResetState(void)
{
  log_cdc_ready            = false;
  log_cdc_boot_sent        = 0;
  log_cdc_list_sent        = 0;
  log_list_boot_mirror_end = 0;
}


static void logCdcApplyBootDisable(void)
{
  log_list_boot_mirror_end = log_buf_list.total_length;        // V251017R2 부트 로그와 리스트 로그 경계 기록
  if (log_cdc_ready == true && log_cdc_list_sent < log_list_boot_mirror_end)
  {
    log_cdc_list_sent = log_list_boot_mirror_end;              // V251017R2 CDC 전송 시 부트 로그 중복 차단
  }
}


static void logCdcApplyBootEnable(void)
{
  log_list_boot_mirror_end = 0;                                // V251017R2 부트 로그 재시작 시 경계 초기화
  if (log_cdc_ready == true)
  {
    log_cdc_list_sent = log_buf_list.total_length;             // V251017R2 기존 리스트 로그는 새 부트 로그 이후부터 전송
  }
}
#else
void logProcess(void)
{
  // V251017R3 CDC 미사용 빌드 호환
}
#endif

#ifdef _USE_HW_CLI
void cliCmd(cli_args_t *args)
{
  bool ret = false;



  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("boot.line_index %d\n", log_buf_boot.line_index);
    cliPrintf("boot.buf_length %d\n", log_buf_boot.buf_length);
    cliPrintf("\n");
    cliPrintf("list.line_index %d\n", log_buf_list.line_index);
    cliPrintf("list.buf_length %d\n", log_buf_list.buf_length);

    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "boot"))
  {
    uint32_t index = 0;

    while(cliKeepLoop())
    {
      uint32_t buf_len;

      buf_len = log_buf_boot.buf_length - index;
      if (buf_len == 0)
      {
        break;
      }
      if (buf_len > 64)
      {
        buf_len = 64;
      }

      lock();
      cliWrite((uint8_t *)&log_buf_boot.buf[index], buf_len);
      index += buf_len;
      unLock();
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "list"))
  {
    uint32_t index = 0;

    while(cliKeepLoop())
    {
      uint32_t buf_len;

      buf_len = log_buf_list.buf_length - index;
      if (buf_len == 0)
      {
        break;
      }
      if (buf_len > 64)
      {
        buf_len = 64;
      }

      lock();
      cliWrite((uint8_t *)&log_buf_list.buf[index], buf_len);
      index += buf_len;
      unLock();
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("log info\n");
    cliPrintf("log boot\n");
    cliPrintf("log list\n");
  }
}
#endif


#endif
