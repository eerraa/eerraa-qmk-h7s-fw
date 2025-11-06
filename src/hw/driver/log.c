/*
 * log.c
 *
 *  Created on: Nov 12, 2021
 *      Author: baram
 */




#include "log.h"
#include "uart.h"
#include <string.h>                                             // V251017R3 부트 로그 공유 영역 초기화
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


#define LOG_BOOT_MAGIC  0x424F4F54UL                            // V251017R3 부트로더 로그 공유 영역 식별

extern uint32_t _fw_flash_begin;


static uint32_t log_boot_magic __attribute__((section(".non_cache"))); // V251017R3 부트로더 로그 공유 영역 유지

log_buf_t log_buf_boot __attribute__((section(".non_cache")));        // V251017R3 부트로더 로그 공유 영역 유지
log_buf_t log_buf_list;

static uint8_t buf_boot[LOG_BOOT_BUF_MAX] __attribute__((section(".non_cache"))); // V251017R3 부트로더 로그 버퍼 공유
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

static uint32_t logBufferAdvance(const log_buf_t *p_log, uint32_t index, uint32_t step);   // V251017R3 CDC 링버퍼 순환 인덱스 계산
static uint32_t logBufferAlignToLineHead(const log_buf_t *p_log, uint32_t start, uint32_t *p_available); // V251017R3 CDC 라인 정렬 헬퍼
static void logCdcTryFlush(void);
static void logCdcFlushBuffered(void);
static void logCdcDrainBuffer(log_buf_t *p_log, uint64_t *p_sent_total);
#endif



#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif





bool logInit(void)
{
#ifdef _USE_HW_RTOS
  mutex_lock = xSemaphoreCreateMutex();
#endif

  bool is_app_image = ((uint32_t)&_fw_flash_begin >= 0x24000000U);      // V251017R3 펌웨어/부트 구분
  bool reuse_boot_log = false;

  if (is_app_image == true)
  {
    if (log_boot_magic == LOG_BOOT_MAGIC &&
        log_buf_boot.buf == buf_boot &&
        log_buf_boot.buf_length_max == LOG_BOOT_BUF_MAX)
    {
      reuse_boot_log = true;                                            // V251017R3 부트로더 로그 인계
    }
  }

  if (reuse_boot_log != true)
  {
    memset(&log_buf_boot, 0, sizeof(log_buf_boot));                      // V251017R3 부트로더 로그 영역 초기화
    memset(buf_boot, 0, sizeof(buf_boot));                               // V251017R3 부트로더 로그 버퍼 클리어
  }
  else
  {
    if (log_buf_boot.buf_length > LOG_BOOT_BUF_MAX)                      // V251017R3 인계 데이터 경계 보정
    {
      log_buf_boot.buf_length = LOG_BOOT_BUF_MAX;
    }
    if (log_buf_boot.buf_index >= LOG_BOOT_BUF_MAX)
    {
      log_buf_boot.buf_index %= LOG_BOOT_BUF_MAX;
    }
  }

  log_buf_boot.buf_length_max = LOG_BOOT_BUF_MAX;
  log_buf_boot.buf            = buf_boot;

  if (reuse_boot_log != true)
  {
    log_buf_boot.line_index   = 0;
    log_buf_boot.total_length = 0;
  }

  log_boot_magic = LOG_BOOT_MAGIC;                                      // V251017R3 부트 로그 공유 영역 활성화


  log_buf_list.line_index     = 0;
  log_buf_list.buf_length     = 0;
  log_buf_list.buf_length_max = LOG_LIST_BUF_MAX;
  log_buf_list.buf_index      = 0;
  log_buf_list.buf            = buf_list;
  log_buf_list.total_length   = 0;

#ifdef _USE_HW_CDC
  log_cdc_ready        = false;                                // V251017R1 CDC 관련 상태 초기화
  log_cdc_boot_sent    = 0;
  log_cdc_list_sent    = 0;
  log_list_boot_mirror_end = 0;                                // V251017R2 CDC 리스트 로그 동기화 초기화
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
    log_list_boot_mirror_end = log_buf_list.total_length;      // V251017R2 부트 로그와 리스트 로그 경계 기록
    if (log_cdc_ready == true && log_cdc_list_sent < log_list_boot_mirror_end)
    {
      log_cdc_list_sent = log_list_boot_mirror_end;            // V251017R2 CDC 전송 시 부트 로그 중복 차단
    }
  }
  else if (is_boot_log == false && enable == true)
  {
    log_list_boot_mirror_end = 0;                              // V251017R2 부트 로그 재시작 시 경계 초기화
    if (log_cdc_ready == true)
    {
      log_cdc_list_sent = log_buf_list.total_length;           // V251017R2 기존 리스트 로그는 새 부트 로그 이후부터 전송
    }
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

bool logBufPrintf(log_buf_t *p_log, char *p_data, uint32_t length)
{
  uint32_t buf_last;
  uint8_t *p_buf;
  int buf_len;


  buf_last = p_log->buf_index + length + 8;
  if (buf_last > p_log->buf_length_max)
  {
    p_log->buf_index = 0;
    buf_last = p_log->buf_index + length + 8;

    if (buf_last > p_log->buf_length_max)
    {
      return false;
    }
  }

  p_buf = &p_log->buf[p_log->buf_index];

  buf_len = snprintf((char *)p_buf, length + 8, "%04X\t%s", p_log->line_index, p_data);
  p_log->line_index++;
  p_log->buf_index += buf_len;


  if (buf_len + p_log->buf_length <= p_log->buf_length_max)
  {
    p_log->buf_length += buf_len;
  }

  p_log->total_length += (uint32_t)buf_len;

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

  if (is_open == true && is_enable == true)
  {
    uartWrite(log_ch, (uint8_t *)print_buf, len);
  }

  if (is_boot_log)
  {
    logBufPrintf(&log_buf_boot, print_buf, len);
  }
  logBufPrintf(&log_buf_list, print_buf, len);
#ifdef _USE_HW_CDC
  logCdcTryFlush();                                            // V251017R1 CDC 연결 시 버퍼 전송 시도
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

  lock();
  logCdcTryFlush();                                            // V251017R1 주기적 CDC 플러시
  unLock();
}

static void logCdcTryFlush(void)
{
  if (cdcIsConnect() != true)
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
    uint64_t trimmed_sent = total - capacity;                  // V251017R3 링버퍼 초과 영역 컷오프
    uint32_t start        = (uint32_t)(trimmed_sent % capacity);
    uint32_t trimmed_available = capacity;

    start = logBufferAlignToLineHead(p_log, start, &trimmed_available);
    *p_sent_total = total - trimmed_available;
    available     = trimmed_available;

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

    start     = logBufferAdvance(p_log, start, sent);
    remaining -= sent;
    *p_sent_total += sent;

    if (sent < chunk)
    {
      return;
    }
  }
}
#endif

#ifdef _USE_HW_CDC
static uint32_t logBufferAdvance(const log_buf_t *p_log, uint32_t index, uint32_t step)
{
  uint32_t capacity = p_log->buf_length_max;

  if (capacity == 0)
  {
    return 0;                                                   // V251017R3 빈 버퍼 예외 처리
  }

  index += step;
  if (index >= capacity)
  {
    index -= capacity;
  }

  return index;
}

static uint32_t logBufferAlignToLineHead(const log_buf_t *p_log, uint32_t start, uint32_t *p_available)
{
  uint32_t capacity  = p_log->buf_length_max;
  uint32_t available = *p_available;

  while (available > 0)
  {
    uint32_t chunk = capacity - start;
    if (chunk > available)
    {
      chunk = available;
    }

    uint8_t *newline = (uint8_t *)memchr(&p_log->buf[start], '\n', chunk);
    if (newline != NULL)
    {
      uint32_t step = (uint32_t)(newline - &p_log->buf[start]) + 1;
      available    -= step;
      *p_available  = available;
      return logBufferAdvance(p_log, start, step);              // V251017R3 라인 헤더 단위 정렬
    }

    available -= chunk;
    start      = logBufferAdvance(p_log, start, chunk);
  }

  *p_available = 0;
  return start;                                                  // V251017R3 개행이 없으면 전송 없음
}
#else
void logProcess(void)
{
  // V251017R1 CDC 미사용 빌드 호환
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
