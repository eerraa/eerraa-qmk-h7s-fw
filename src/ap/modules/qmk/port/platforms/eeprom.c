#include "quantum.h"
#include "usb.h"                                   // V251112R5: VIA EEPROM 클리어 시 BootMode 기본값 적용
#include "reset.h"                                 // V251112R4: VIA EEPROM CLEAR 즉시 리셋 UX
#include "eeprom_auto_factory_reset.h"             // V251112R4: AUTO_FACTORY_RESET/VIA 센티넬 공용화
#include "qmk/quantum/eeconfig.h"                  // V251112R3: AUTO_FACTORY_RESET/VIA 공용 초기화 루틴
#include "qmk/port/usb_monitor.h"                  // V251112R5: USB 모니터 기본값 적용
#include "qmk/port/port.h"


#define EEPROM_WRITE_Q_BUF_MAX         (TOTAL_EEPROM_BYTE_COUNT + 1)
#define EEPROM_WRITE_PAGE_SIZE         32          // V251112R5: 외부 EEPROM 페이지 크기
#define EEPROM_WRITE_SLICE_MAX_US      100         // V251112R5: 8 kHz 루프당 100us 안에서만 실 기록
#define EEPROM_WRITE_QUEUE_WAIT_MS     2           // V251112R2: 큐 가득 참 재시도 대기 시간
#define EEPROM_UPDATE_BLOCK_CHUNK      64          // V251112R2: 고정 버퍼로 블록 비교
#define EEPROM_WRITE_BURST_THRESHOLD   512         // V251112R5: 버스트 모드 진입 임계값(엔트리)
#define EEPROM_WRITE_BURST_EXTRA_CALLS 2           // V251112R5: 버스트 모드 시 추가 실행 횟수


typedef struct
{
  uint16_t addr;
  uint8_t  data;
} eeprom_write_t;

static uint8_t        eeprom_buf[TOTAL_EEPROM_BYTE_COUNT];
static qbuffer_t      write_q;
static eeprom_write_t write_buf[EEPROM_WRITE_Q_BUF_MAX];
static uint32_t       write_q_high_water = 0;
static uint32_t       write_q_overflow   = 0;
static uint8_t        page_batch_buf[EEPROM_WRITE_PAGE_SIZE];       // V251112R5: 페이지 단위 버퍼

static bool eeprom_peek_queue_entry(uint32_t offset, eeprom_write_t *out_entry)
{
  uint32_t pending = qbufferAvailable(&write_q);

  if (offset >= pending)
  {
    return false;
  }

  uint32_t slot = (write_q.out + offset) % write_q.len;

  *out_entry = write_buf[slot];                                      // V251112R8: 큐 엔트리를 안전하게 참조
  return true;
}

static void eeprom_update_queue_watermark(void)
{
  uint32_t pending = qbufferAvailable(&write_q);

  if (pending > write_q_high_water)
  {
    write_q_high_water = pending;
  }
}

static void eeprom_restore_auto_factory_reset_sentinel(void)
{
#if defined(AUTO_FACTORY_RESET_FLAG_MAGIC) && defined(AUTO_FACTORY_RESET_COOKIE)
  eeprom_write_dword((uint32_t *)EECONFIG_USER_EEPROM_CLEAR_FLAG, AUTO_FACTORY_RESET_FLAG_MAGIC);
  eeprom_write_dword((uint32_t *)EECONFIG_USER_EEPROM_CLEAR_COOKIE, AUTO_FACTORY_RESET_COOKIE);
#endif
}


void eeprom_init(void)
{
  eepromRead(0, eeprom_buf, TOTAL_EEPROM_BYTE_COUNT);
  qbufferCreateBySize(&write_q, (uint8_t *)write_buf, sizeof(eeprom_write_t), EEPROM_WRITE_Q_BUF_MAX); 
}

void eeprom_update(void)
{
  uint32_t slice_begin = micros();

  while (qbufferAvailable(&write_q) > 0)
  {
    if (eepromIsErasing() == true)
    {
      break;                                                          // V251112R8: 클린업 중이면 큐 제거 보류
    }

    if ((uint32_t)(micros() - slice_begin) >= EEPROM_WRITE_SLICE_MAX_US)
    {
      break;
    }

    eeprom_write_t first_entry;
    if (eeprom_peek_queue_entry(0, &first_entry) != true)
    {
      break;
    }

    uint32_t chunk_addr    = first_entry.addr;
    uint32_t page_end_addr = ((chunk_addr / EEPROM_WRITE_PAGE_SIZE) * EEPROM_WRITE_PAGE_SIZE) + EEPROM_WRITE_PAGE_SIZE;
    uint32_t pending       = qbufferAvailable(&write_q);
    uint8_t  chunk_len     = 0;

    while (chunk_len < pending && chunk_len < EEPROM_WRITE_PAGE_SIZE)
    {
      eeprom_write_t entry;
      if (eeprom_peek_queue_entry(chunk_len, &entry) != true)
      {
        break;
      }

      if (entry.addr != (chunk_addr + chunk_len))
      {
        break;
      }
      if ((chunk_addr + chunk_len) >= page_end_addr)
      {
        break;
      }

      page_batch_buf[chunk_len++] = entry.data;
    }

    if (chunk_len == 0)
    {
      break;
    }

    if (eepromWritePage(chunk_addr, page_batch_buf, chunk_len) != true)
    {
      if (eepromIsErasing() != true)
      {
        logPrintf("[!] eepromWritePage() fail addr=%lu len=%u\n", (unsigned long)chunk_addr, chunk_len);   // V251112R5: 페이지 쓰기 오류 감시
      }
      break;
    }

    qbufferRead(&write_q, NULL, chunk_len);
  }
}

bool eeprom_is_pending(void)
{
  return qbufferAvailable(&write_q) > 0;
}

void eeprom_flush_pending(void)
{
  while (eeprom_is_pending())
  {
    eeprom_update();
  }
}

bool eeprom_apply_factory_defaults(bool restore_factory_reset_sentinel)
{
  eeprom_flush_pending();                                      // V251112R3: 초기화 전 대기열 제거

  eeconfig_disable();
  eeconfig_init();
#if (EECONFIG_KB_DATA_SIZE) > 0
  eeconfig_init_kb_datablock();
#endif
#if (EECONFIG_USER_DATA_SIZE) > 0
  eeconfig_init_user_datablock();
#endif
  eeprom_flush_pending();

#ifdef BOOTMODE_ENABLE
  usbBootModeApplyDefaults();
#endif
#ifdef USB_MONITOR_ENABLE
  usb_monitor_storage_apply_defaults();
#endif
  eeprom_flush_pending();

  if (restore_factory_reset_sentinel)
  {
    eeprom_restore_auto_factory_reset_sentinel();              // V251112R3: 공용 초기화 루틴에서 센티넬 복구
    eeprom_flush_pending();
  }

  return true;
}

void eeprom_task(void)
{
  eeprom_update();                                              // V251112R4: VIA 초기화는 부팅 시 AUTO_FACTORY_RESET 경로로 처리
}

void eeprom_req_clean(void)
{
#if AUTO_FACTORY_RESET_ENABLE || defined(VIA_ENABLE)
  logPrintf("[  ] VIA EEPROM clear : scheduling deferred factory reset\n");    // V251112R4: VIA와 AUTO_FACTORY_RESET 경로 통일
  if (eepromScheduleDeferredFactoryReset() != true)
  {
    logPrintf("[!] VIA EEPROM clear : sentinel write fail\n");
    return;
  }

  logPrintf("[  ] VIA EEPROM clear : rebooting to apply defaults\n");
  resetToReset();
#else
  logPrintf("[!] VIA EEPROM clear : AUTO_FACTORY_RESET support disabled\n");
#endif
}

uint8_t  eeprom_read_byte(const uint8_t *addr)
{
  return eeprom_buf[(uint32_t)addr];
}

uint16_t eeprom_read_word(const uint16_t *addr)
{
  uint16_t ret = 0;

  ret  = eeprom_buf[((uint32_t)addr) + 0] << 0;
  ret |= eeprom_buf[((uint32_t)addr) + 1] << 8;

  return ret;
}

uint32_t eeprom_read_dword(const uint32_t *addr)
{
  uint32_t ret = 0;
  const uint8_t *p = (const uint8_t *)addr;

  ret  = eeprom_read_byte(p + 0) << 0;
  ret |= eeprom_read_byte(p + 1) << 8;
  ret |= eeprom_read_byte(p + 2) << 16;
  ret |= eeprom_read_byte(p + 3) << 24;

  return ret;
};

void eeprom_read_block(void *buf, const void *addr, uint32_t len)
{
  const uint8_t *p    = (const uint8_t *)addr;
  uint8_t       *dest = (uint8_t *)buf;
  while (len--)
  {
    *dest++ = eeprom_read_byte(p++);
  }
}

void eeprom_write_byte(uint8_t *addr, uint8_t value)
{
  eeprom_write_t write_byte;
  uint32_t       pre_time;
  bool           is_enqueued = false;

  eeprom_buf[(uint32_t)addr] = value;

  write_byte.addr = (uint32_t)addr;
  write_byte.data = value;

  pre_time = millis();
  while (is_enqueued != true)
  {
    if (qbufferWrite(&write_q, (uint8_t *)&write_byte, 1))
    {
      is_enqueued = true;
      eeprom_update_queue_watermark();                                 // V251112R2: 큐 하이워터 추적
      break;
    }

    eeprom_update();                                                   // V251112R2: 큐가 가득 찼다면 즉시 비우기
    if (millis()-pre_time >= EEPROM_WRITE_QUEUE_WAIT_MS)
    {
      break;
    }
  }

  if (is_enqueued != true)
  {
    write_q_overflow++;
    if (eepromWriteByte(write_byte.addr, write_byte.data) != true)
    {
      logPrintf("[!] EEPROM write queue overflow (addr=%lu) direct-write fail\n", write_byte.addr); // V251112R2 큐 오버플로 감시
    }
    else
    {
      logPrintf("[ ] EEPROM write queue overflow (addr=%lu) flushed inline\n", write_byte.addr);     // V251112R2 큐 오버플로 감시
    }
  }
}

void eeprom_write_word(uint16_t *addr, uint16_t value)
{
	uint8_t *p = (uint8_t *)addr;
	eeprom_write_byte(p++, value);
	eeprom_write_byte(p, value >> 8);
}

void eeprom_write_dword(uint32_t *addr, uint32_t value)
{
	uint8_t *p = (uint8_t *)addr;
	eeprom_write_byte(p++, value);
	eeprom_write_byte(p++, value >> 8);
	eeprom_write_byte(p++, value >> 16);
	eeprom_write_byte(p, value >> 24); 
}

void eeprom_write_block(const void *buf, void *addr, size_t len)
{
  uint8_t       *p   = (uint8_t *)addr;
  const uint8_t *src = (const uint8_t *)buf;
  while (len--)
  {
    eeprom_write_byte(p++, *src++);
  }
}

void eeprom_update_byte(uint8_t *addr, uint8_t value)
{
  uint8_t orig = eeprom_read_byte(addr);
  if (orig != value)
  {
    eeprom_write_byte(addr, value);
  }
}

void eeprom_update_word(uint16_t *addr, uint16_t value)
{
  uint16_t orig = eeprom_read_word(addr);
  if (orig != value)
  {
    eeprom_write_word(addr, value);
  }
}

void eeprom_update_dword(uint32_t *addr, uint32_t value)
{
  uint32_t orig = eeprom_read_dword(addr);
  if (orig != value)
  {
    eeprom_write_dword(addr, value);
  }
}

void eeprom_update_block(const void *buf, void *addr, size_t len)
{
  const uint8_t *src  = (const uint8_t *)buf;
  uint8_t       *dest = (uint8_t *)addr;
  uint8_t        read_buf[EEPROM_UPDATE_BLOCK_CHUNK];

  while (len > 0)
  {
    size_t chunk = len > EEPROM_UPDATE_BLOCK_CHUNK ? EEPROM_UPDATE_BLOCK_CHUNK : len;

    eeprom_read_block(read_buf, dest, chunk);
    for (size_t i = 0; i < chunk; i++)
    {
      if (src[i] != read_buf[i])
      {
        eeprom_write_byte(dest + i, src[i]);
      }
    }

    len  -= chunk;
    src  += chunk;
    dest += chunk;
  }
}

uint32_t eeprom_get_write_pending_count(void)
{
  return qbufferAvailable(&write_q);
}

uint32_t eeprom_get_write_pending_max(void)
{
  return write_q_high_water;
}

uint32_t eeprom_get_write_overflow_count(void)
{
  return write_q_overflow;
}

bool eeprom_is_burst_mode_active(void)
{
  return qbufferAvailable(&write_q) >= EEPROM_WRITE_BURST_THRESHOLD;     // V251112R5: 버스트 모드 임계값 비교
}

uint8_t eeprom_get_burst_extra_calls(void)
{
  return eeprom_is_burst_mode_active() ? EEPROM_WRITE_BURST_EXTRA_CALLS : 0;  // V251112R5: 추가 실행 횟수 산출
}
