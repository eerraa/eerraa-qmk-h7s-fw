#include "quantum.h"
#include "usb.h"                                   // V251112R5: VIA EEPROM 클리어 시 BootMode 기본값 적용
#include "bootloader.h"                            // V250310R6: VIA CLEAN 이후 응답 송신 보장 리셋 래퍼
#include "eeprom_auto_factory_reset.h"             // V251112R4: AUTO_FACTORY_RESET/VIA 센티넬 공용화
#include "qmk/quantum/eeconfig.h"                  // V251112R3: AUTO_FACTORY_RESET/VIA 공용 초기화 루틴
#include "qmk/port/port.h"


// V260909R1: RAM 최신 이미지 + byte dirty bitmap. pending 데이터는 ACK 이전/실패 시 절대 제거하지 않는다.
#define EEPROM_WRITE_PAGE_SIZE 32U
#define EEPROM_PAGE_COUNT ((TOTAL_EEPROM_BYTE_COUNT + EEPROM_WRITE_PAGE_SIZE - 1U) / EEPROM_WRITE_PAGE_SIZE)
#define EEPROM_SCAN_PAGES_PER_CALL 8U
#define EEPROM_FLUSH_STALL_TIMEOUT_MS 200U
#define EEPROM_FAILURE_BACKOFF_MS 10U
static uint8_t eeprom_buf[TOTAL_EEPROM_BYTE_COUNT];
static uint32_t dirty_pages[EEPROM_PAGE_COUNT];
static uint8_t page_snapshot[EEPROM_WRITE_PAGE_SIZE];
static uint32_t pending_bytes, pending_max, failure_count, invalid_accesses, completed_pages;
static uint32_t page_cursor, active_page, active_length, retry_after_ms;
static bool image_ready, write_active, retry_pending;
_Static_assert(TOTAL_EEPROM_BYTE_COUNT <= 16384U, "EEPROM image exceeds chip");

static bool eeprom_address_valid(uintptr_t addr, size_t length)
{
  return addr <= TOTAL_EEPROM_BYTE_COUNT && length <= TOTAL_EEPROM_BYTE_COUNT - addr;
}

bool eeprom_is_ready(void) { return image_ready; }

static void eeprom_restore_auto_factory_reset_sentinel(void)
{
#if defined(AUTO_FACTORY_RESET_FLAG_MAGIC) && defined(AUTO_FACTORY_RESET_COOKIE)
  eeprom_write_dword((uint32_t *)EECONFIG_USER_EEPROM_CLEAR_FLAG, AUTO_FACTORY_RESET_FLAG_MAGIC);
  eeprom_write_dword((uint32_t *)EECONFIG_USER_EEPROM_CLEAR_COOKIE, AUTO_FACTORY_RESET_COOKIE);
#endif
}


void eeprom_init(void)
{
  // V260909R1: 재초기화도 미저장 의도를 버리지 않는다. 초기 읽기 실패는 hwInit이 전파한다.
  if (pending_bytes != 0U && !eeprom_flush_pending()) return;
  image_ready = eepromRead(0U, eeprom_buf, sizeof(eeprom_buf));
  if (!image_ready) { failure_count++; return; }
  memset(dirty_pages, 0, sizeof(dirty_pages));
  pending_bytes = 0U;
  page_cursor = 0U;
  write_active = retry_pending = false;
}

static void eeprom_complete_page(void)
{
  uint32_t addr = active_page * EEPROM_WRITE_PAGE_SIZE;
  // 전송 중 바뀐 byte는 dirty로 남긴다. 예전 완료가 새로운 저장 의도를 지울 수 없다.
  for (uint32_t i = 0; i < active_length; i++) {
    uint32_t bit = 1UL << i;
    if ((dirty_pages[active_page] & bit) && eeprom_buf[addr + i] == page_snapshot[i]) {
      dirty_pages[active_page] &= ~bit;
      pending_bytes--;
    }
  }
  completed_pages++;
}

void eeprom_update(void)
{
  if (!image_ready || pending_bytes == 0U || eepromIsErasing()) return;
  if (write_active) {
#if defined(EEPROM_CHIP_ZD24C128)
    eeprom_async_result_t result = eepromWritePagePoll();
    if (result == EEPROM_ASYNC_BUSY) return;
    write_active = false;
    if (result == EEPROM_ASYNC_DONE) {
      eeprom_complete_page();
    } else {
      failure_count++;
      retry_after_ms = millis() + EEPROM_FAILURE_BACKOFF_MS;
      retry_pending = true;
    }
#endif
    return;  // 한 호출에 완료 처리 또는 시작 중 한 단계만 수행한다.
  }
  if (retry_pending) {
    if ((int32_t)(millis() - retry_after_ms) < 0) return;
    retry_pending = false;
  }
  // 전체 이미지를 매 loop 순회하지 않는다. 최대8 page 확인 후 다음 입력 처리로 돌아간다.
  for (uint32_t scanned = 0; scanned < EEPROM_SCAN_PAGES_PER_CALL; scanned++) {
    uint32_t page = page_cursor;
    page_cursor = (page_cursor + 1U) % EEPROM_PAGE_COUNT;
    if (dirty_pages[page] == 0U) continue;
    uint32_t addr = page * EEPROM_WRITE_PAGE_SIZE;
    active_page = page;
    active_length = TOTAL_EEPROM_BYTE_COUNT - addr;
    if (active_length > EEPROM_WRITE_PAGE_SIZE) active_length = EEPROM_WRITE_PAGE_SIZE;
    memcpy(page_snapshot, &eeprom_buf[addr], active_length);
#if defined(EEPROM_CHIP_ZD24C128)
    write_active = eepromWritePageStart(addr, page_snapshot, active_length);
    if (!write_active) {
      failure_count++;
      retry_after_ms = millis() + EEPROM_FAILURE_BACKOFF_MS;
      retry_pending = true;
    }
#else
    // 미검증 flash-emulation 빌드는 기존 동기 backend를 유지한다. 외부 EEPROM과 같은 시간 보장은 없다.
    if (eepromWritePage(addr, page_snapshot, active_length)) eeprom_complete_page();
    else { failure_count++; retry_after_ms = millis() + EEPROM_FAILURE_BACKOFF_MS; retry_pending = true; }
#endif
    return;
  }
}

bool eeprom_is_pending(void) { return pending_bytes != 0U; }

bool eeprom_flush_pending(void)
{
  // V260909R1: 부팅/명시적 reset의 durability barrier만 대기한다. 일반 SAVE는 비동기다.
  if (!image_ready || __get_IPSR() != 0U || __get_PRIMASK() != 0U) return false;
  uint32_t last_progress_ms = millis();
  uint32_t last_completed = completed_pages;
  while (eeprom_is_pending()) {
    eeprom_update();
    if (completed_pages != last_completed) {
      last_completed = completed_pages;
      last_progress_ms = millis();
    } else if ((uint32_t)(millis() - last_progress_ms) >= EEPROM_FLUSH_STALL_TIMEOUT_MS) {
      failure_count++;
      return false;  // dirty 및 in-flight 소유권을 유지해 다음 service에서 계속 처리한다.
    }
  }
  return true;
}

bool eeprom_apply_factory_defaults(bool restore_factory_reset_sentinel)
{
  if (eeprom_flush_pending() != true)                                      // V251112R3: 초기화 전 대기열 제거
  {
    return false;
  }

  eeconfig_disable();
  eeconfig_init();
#if (EECONFIG_KB_DATA_SIZE) > 0
  eeconfig_init_kb_datablock();
#endif
#if (EECONFIG_USER_DATA_SIZE) > 0
  eeconfig_init_user_datablock();
#endif
  if (eeprom_flush_pending() != true)
  {
    return false;
  }

#if (EECONFIG_USER_DATA_SIZE) == 0
#ifdef BOOTMODE_ENABLE
  usbBootModeApplyDefaults();                               // V251114R4: USER 블록이 없을 때만 기본값 백업 적용
#endif
  // V260823R2: USB 진단에는 EEPROM 기본값/flush 경로가 없다.
#endif
  if (eeprom_flush_pending() != true)
  {
    return false;
  }

  if (restore_factory_reset_sentinel)
  {
    eeprom_restore_auto_factory_reset_sentinel();              // V251112R3: 공용 초기화 루틴에서 센티넬 복구
    if (eeprom_flush_pending() != true)
    {
      return false;
    }
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
  if (mcu_reset_deferred() != true)
  {
    mcu_reset();                                                   // V250310R6: deferred 예약 실패 시 기존 리셋 경로로 폴백
  }
#else
  logPrintf("[!] VIA EEPROM clear : AUTO_FACTORY_RESET support disabled\n");
#endif
}

uint8_t eeprom_read_byte(const uint8_t *addr)
{
  uintptr_t offset = (uintptr_t)addr;
  if (!image_ready || !eeprom_address_valid(offset, 1U)) { invalid_accesses++; return 0xFFU; }
  return eeprom_buf[offset];
}

uint16_t eeprom_read_word(const uint16_t *addr)
{
  uintptr_t offset = (uintptr_t)addr;
  if (!eeprom_address_valid(offset, 2U)) { invalid_accesses++; return UINT16_MAX; }
  return (uint16_t)eeprom_read_byte((const uint8_t *)offset) |
         ((uint16_t)eeprom_read_byte((const uint8_t *)(offset + 1U)) << 8);
}

uint32_t eeprom_read_dword(const uint32_t *addr)
{
  uintptr_t offset = (uintptr_t)addr;
  if (!eeprom_address_valid(offset, 4U)) { invalid_accesses++; return UINT32_MAX; }
  uint32_t result = 0U;
  for (uint32_t i = 0; i < 4U; i++) result |= (uint32_t)eeprom_read_byte((const uint8_t *)(offset + i)) << (8U * i);
  return result;
}

void eeprom_read_block(void *buf, const void *addr, uint32_t len)
{
  if (buf == NULL) { invalid_accesses++; return; }
  if (!image_ready || !eeprom_address_valid((uintptr_t)addr, len)) {
    invalid_accesses++;
    memset(buf, 0xFF, len);
    return;
  }
  memcpy(buf, &eeprom_buf[(uintptr_t)addr], len);
}

void eeprom_write_byte(uint8_t *addr, uint8_t value)
{
  uintptr_t offset = (uintptr_t)addr;
  if (!image_ready || !eeprom_address_valid(offset, 1U)) { invalid_accesses++; return; }
  if (eeprom_buf[offset] == value) return;
  eeprom_buf[offset] = value;
  uint32_t page = offset / EEPROM_WRITE_PAGE_SIZE;
  uint32_t bit = 1UL << (offset % EEPROM_WRITE_PAGE_SIZE);
  if (!(dirty_pages[page] & bit)) {
    dirty_pages[page] |= bit;
    pending_bytes++;
    if (pending_bytes > pending_max) pending_max = pending_bytes;
  }
}

void eeprom_write_word(uint16_t *addr, uint16_t value)
{
  if (!eeprom_address_valid((uintptr_t)addr, 2U)) { invalid_accesses++; return; }
  for (uint32_t i = 0; i < 2U; i++) eeprom_write_byte((uint8_t *)((uintptr_t)addr + i), value >> (8U * i));
}

void eeprom_write_dword(uint32_t *addr, uint32_t value)
{
  if (!eeprom_address_valid((uintptr_t)addr, 4U)) { invalid_accesses++; return; }
  for (uint32_t i = 0; i < 4U; i++) eeprom_write_byte((uint8_t *)((uintptr_t)addr + i), value >> (8U * i));
}

void eeprom_write_block(const void *buf, void *addr, size_t len)
{
  if (buf == NULL || !eeprom_address_valid((uintptr_t)addr, len)) { invalid_accesses++; return; }
  const uint8_t *src = buf;
  for (size_t i = 0; i < len; i++) eeprom_write_byte((uint8_t *)((uintptr_t)addr + i), src[i]);
}

void eeprom_update_byte(uint8_t *addr, uint8_t value) { eeprom_write_byte(addr, value); }
void eeprom_update_word(uint16_t *addr, uint16_t value) { eeprom_write_word(addr, value); }
void eeprom_update_dword(uint32_t *addr, uint32_t value) { eeprom_write_dword(addr, value); }
void eeprom_update_block(const void *buf, void *addr, size_t len) { eeprom_write_block(buf, addr, len); }
uint32_t eeprom_get_write_pending_count(void) { return pending_bytes; }
uint32_t eeprom_get_write_pending_max(void) { return pending_max; }
uint32_t eeprom_get_write_overflow_count(void) { return 0U; }  // 호환 조회: dirty bitmap에는 queue-full이 없다.
uint32_t eeprom_get_write_failure_count(void) { return failure_count; }
uint32_t eeprom_get_invalid_access_count(void) { return invalid_accesses; }
