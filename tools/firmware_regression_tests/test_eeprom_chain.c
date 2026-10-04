#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "hw_def.h"
#include "i2c.h"
#include "eeprom.h"
#include "qmk/port/platforms/eeprom.h"

uint32_t test_irqmask, test_ipsr;
static uint32_t clock_ms, blocking_calls, starts, probes, forced_stops;
static bool auto_clock, complete_irqs = true, fail_start, fail_read, corrupt_program;
static uint32_t readbacks, macro_revision, keymap_revision;
void era_state_sync_bump_keymap(void) { keymap_revision++; }
void era_state_sync_bump_macro(void) { macro_revision++; }
bool dynamic_keymap_macro_set_buffer_checked(uint16_t offset, uint16_t size, uint8_t *data);
bool dynamic_keymap_macro_reset_checked(void);
void dynamic_keymap_macro_get_buffer(uint16_t offset, uint16_t size, uint8_t *data);
static I2C_TypeDef regs;
static I2C_HandleTypeDef handle = {.Instance = &regs, .State = HAL_I2C_STATE_READY};
static uint8_t device[16384];
static struct { bool active, probe, read; uint16_t offset, length; uint8_t *data; uint32_t start; } bus;
static struct { bool pending; uint16_t offset, length; uint8_t data[32]; uint32_t ready; } program;

static const char *cut_file;
static unsigned cut_boundary, cut_steps;
static void power_cut_point(void)
{
  if (!cut_file || ++cut_steps != cut_boundary) return;
  FILE *f = fopen(cut_file, "wb"); assert(f);
  assert(fwrite(device, 1, sizeof(device), f) == sizeof(device)); fclose(f);
  exit(0);  // cold recovery runs in a new process, with no preserved RAM state
}

static void complete_bus(void)
{
  if (!bus.active || test_irqmask || !complete_irqs) return;
  bus.active = false;
  handle.State = HAL_I2C_STATE_READY;
  handle.XferISR = NULL;
  uint32_t saved = test_ipsr; test_ipsr = 1U;
  if (bus.read) {
    memcpy(bus.data, &device[bus.offset], bus.length);
    HAL_I2C_MemRxCpltCallback(&handle);
  } else if (!bus.probe) {
    assert(!program.pending);
    program.pending = true;
    program.offset = bus.offset; program.length = bus.length;
    memcpy(program.data, bus.data, bus.length);
    program.ready = clock_ms + 3U;
    HAL_I2C_MemTxCpltCallback(&handle);
  } else if (program.pending && (int32_t)(clock_ms - program.ready) < 0) {
    handle.ErrorCode = HAL_I2C_ERROR_AF;
    assert(i2cAsyncOnError(&handle));
  } else {
    if (program.pending) {
      if (!corrupt_program) for (unsigned i=0; i<program.length; i++) {
        device[program.offset+i] = program.data[i]; power_cut_point();
      }
      program.pending = false;
    }
    HAL_I2C_MasterTxCpltCallback(&handle);
  }
  test_ipsr = saved;
  power_cut_point();
}
uint32_t millis(void)
{
  if (auto_clock) { clock_ms++; if (bus.active && (uint32_t)(clock_ms - bus.start) >= 1U) complete_bus(); }
  return clock_ms;
}
uint32_t micros(void) { return clock_ms * 1000U; }
void delay(uint32_t ms) { blocking_calls++; clock_ms += ms; }
void eeconfig_disable(void) {}
bool eeconfig_init_quantum_checked(void) { return true; }
void eeconfig_publish_quantum_defaults(void) {}
void usbBootModeApplyDefaults(void) {}
void host_i2c_disable(I2C_HandleTypeDef *h) { (void)h; forced_stops++; bus.active = false; }
I2C_HandleTypeDef *i2cGetHandle(uint8_t ch) { return ch == 0U ? &handle : NULL; }
bool i2cIsBegin(uint8_t ch) { return ch == 0U; }
bool i2cBegin(uint8_t ch, uint32_t frequency) { (void)frequency; return ch == 0U; }
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *h) { return h->ErrorCode; }
HAL_StatusTypeDef HAL_I2C_Mem_Write_IT(I2C_HandleTypeDef *h, uint16_t address, uint16_t offset,
                                     uint16_t mode, uint8_t *data, uint16_t length)
{
  assert(test_irqmask == 1U && address == 0xA0U && mode == I2C_MEMADD_SIZE_16BIT);
  assert(!bus.active && length > 0U && length <= 32U && (offset % 32U) + length <= 32U);
  if (fail_start) return HAL_BUSY;
  starts++;
  h->ErrorCode = 0U;
  h->XferISR = (void *)(uintptr_t)1U;
  bus.active = true; bus.probe = false; bus.read = false; bus.offset = offset; bus.length = length; bus.data = data; bus.start = clock_ms;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Master_Transmit_IT(I2C_HandleTypeDef *h, uint16_t address, uint8_t *data, uint16_t length)
{
  assert(test_irqmask == 1U && address == 0xA0U && data == NULL && length == 0U && !bus.active);
  h->ErrorCode = 0U;
  probes++;
  h->XferISR = (void *)(uintptr_t)2U;
  bus.active = true; bus.probe = true; bus.read = false; bus.start = clock_ms;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Mem_Read_IT(I2C_HandleTypeDef *h, uint16_t address, uint16_t offset,
                                   uint16_t mode, uint8_t *data, uint16_t length)
{
  assert(test_irqmask == 1U && address == 0xA0U && mode == I2C_MEMADD_SIZE_16BIT && !bus.active);
  if (fail_read) return HAL_ERROR;
  readbacks++;
  h->ErrorCode = 0U;
  h->XferISR = (void *)(uintptr_t)3U;
  bus.active = true; bus.probe = false; bus.read = true;
  bus.offset = offset; bus.length = length; bus.data = data; bus.start = clock_ms;
  return HAL_OK;
}
bool i2cReadA16Bytes(uint8_t ch, uint16_t dev, uint16_t offset, uint8_t *data, uint32_t length, uint32_t timeout)
{
  (void)ch; (void)dev; (void)timeout; blocking_calls++;
  if (fail_read || bus.active || offset + length > sizeof(device)) return false;
  memcpy(data, &device[offset], length);
  return true;
}
bool i2cWriteA16Bytes(uint8_t ch, uint16_t dev, uint16_t offset, uint8_t *data, uint32_t length, uint32_t timeout)
{
  (void)ch; (void)dev; (void)timeout; blocking_calls++;
  assert(!bus.active && offset + length <= sizeof(device));
  memcpy(&device[offset], data, length); return true;
}
bool i2cIsDeviceReady(uint8_t ch, uint8_t dev) { (void)ch; (void)dev; blocking_calls++; return !program.pending; }
static void step(void) { clock_ms++; complete_bus(); eeprom_update(); }
static void settle(void)
{
  for (unsigned i = 0; i < 10000U && eeprom_is_pending(); i++) step();
  assert(!eeprom_is_pending() && !bus.active && !program.pending);
}
static uint8_t *address(uintptr_t value) { return (uint8_t *)value; }
int main(int argc, char **argv)
{
  if (argc == 3 && strcmp(argv[1], "recover") == 0) {
    FILE *f = fopen(argv[2], "rb"); assert(f);
    assert(fread(device, 1, sizeof(device), f) == sizeof(device)); fclose(f);
    assert(eepromInit() && eeprom_init());
    if (device[3071U] == 0U) {
      bool old = true, completed = true;
      for (unsigned i=0; i<28; i++) { old &= device[1024U+i] == 0U; completed &= device[1024U+i] == 0xA7U; }
      assert(old || completed);
    } else assert(device[3071U] == 0xFFU);
    auto_clock = true;
    uint8_t opened=0xFFU, closed=0U, payload[28]; memset(payload,0xA7,sizeof(payload));
    assert(dynamic_keymap_macro_set_buffer_checked(2047U,1U,&opened));
    assert(dynamic_keymap_macro_set_buffer_checked(0U,sizeof(payload),payload));
    assert(dynamic_keymap_macro_set_buffer_checked(2047U,1U,&closed));
    assert(eeprom_flush_pending() && device[3071U] == 0U);
    assert(memcmp(&device[1024U],payload,sizeof(payload)) == 0);
    return 0;
  }
  if (argc == 4 && strcmp(argv[1], "cut") == 0) {
    memset(device,0xFF,sizeof(device)); assert(eepromInit() && eeprom_init()); auto_clock = true;
    assert(dynamic_keymap_macro_reset_checked() && eeprom_flush_pending());
    cut_file = argv[3]; cut_boundary = (unsigned)strtoul(argv[2],NULL,10);
    uint8_t opened=0xFFU, closed=0U, payload[28]; memset(payload,0xA7,sizeof(payload));
    assert(dynamic_keymap_macro_set_buffer_checked(2047U,1U,&opened));
    assert(dynamic_keymap_macro_set_buffer_checked(0U,sizeof(payload),payload));
    assert(dynamic_keymap_macro_set_buffer_checked(2047U,1U,&closed));
    assert(eeprom_flush_pending());
    return 2;  // no cut: matrix has visited all physical byte and bus boundaries
  }
  memset(device, 0xFF, sizeof(device));
  assert(eepromInit()); eeprom_init(); assert(eeprom_is_ready());
  blocking_calls = 0U;
  uint8_t timeout_data = 0x62U;
  assert(i2cWriteA16BytesAsync(0U, 0x50U, 0U, &timeout_data, 1U));
  assert(handle.XferISR != NULL && bus.active);
  clock_ms += 10U;
  uint32_t timeout_error;
  assert(i2cAsyncPoll(0U, &timeout_error) == I2C_ASYNC_ERROR);
  assert(timeout_error == HAL_I2C_ERROR_TIMEOUT && handle.XferISR == NULL && !bus.active && !i2cAsyncOwned(0U));
  for (unsigned i = 0; i < 100000U; i++) eeprom_write_byte(address(17U), (uint8_t)i);
  assert(eeprom_get_write_pending_count() == 1U && !bus.active);
  uint8_t desired = eeprom_read_byte(address(17U));
  eeprom_update();
  assert(bus.active && !bus.probe && eeprom_get_write_pending_count() == 1U);
  assert(!i2cWriteA16BytesAsync(0U, 0x50U, 17U, &desired, 1U));
  uint8_t first_snapshot = bus.data[17U];
  eeprom_write_byte(address(17U), 0x5AU);
  assert(bus.data[17U] == first_snapshot);  // active hardware buffer is immutable
  clock_ms++; complete_bus(); eeprom_update();
  assert(eeprom_is_pending() && device[17] == 0xFFU && program.pending);
  clock_ms++; complete_bus(); eeprom_update();  // first ACK probe gets NACK
  assert(eeprom_is_pending() && device[17] == 0xFFU);
  settle();
  assert(device[17] == 0x5AU && probes >= 2U);
  uint32_t before = starts;
  eeprom_write_byte(address(17U), 0x5AU);
  eeprom_update(); assert(!eeprom_is_pending() && starts == before);

  for (unsigned i = 0; i < TOTAL_EEPROM_BYTE_COUNT; i++) eeprom_write_byte(address(i), (uint8_t)(i * 17U));
  assert(eeprom_get_write_pending_count() > 4000U);
  settle();
  for (unsigned i = 0; i < TOTAL_EEPROM_BYTE_COUNT; i++) assert(device[i] == (uint8_t)(i * 17U));
  assert(eeprom_get_write_pending_count() == 0U && eeprom_get_write_overflow_count() == 0U);

  eeprom_write_byte(address(123U), 0x72U);
  fail_start = true; auto_clock = true;
  assert(!eeprom_flush_pending());
  assert(eeprom_is_pending() && eeprom_read_byte(address(123U)) == 0x72U && device[123] != 0x72U);
  fail_start = false;
  assert(eeprom_flush_pending());
  assert(device[123] == 0x72U);
  auto_clock = false;

  eeprom_write_byte(address(511U), 0x19U);
  uint32_t prior_timeouts = forced_stops;
  complete_irqs = false; auto_clock = true;
  assert(!eeprom_flush_pending());
  // A later retry may own the bus at the flush deadline; the failed transaction was quiesced above.
  assert(eeprom_is_pending() && forced_stops > prior_timeouts);
  complete_irqs = true;
  assert(eeprom_flush_pending());
  auto_clock = false;
  assert(device[511] == 0x19U);

  clock_ms = UINT32_MAX - 3U;
  eeprom_write_byte(address(95U), 0x9EU); settle();
  assert(device[95] == 0x9EU);
  eeprom_write_byte(address(1023U), 0x33U);
  test_irqmask = 1U; assert(!eeprom_flush_pending() && test_irqmask == 1U); test_irqmask = 0U;
  test_ipsr = 1U; assert(!eeprom_flush_pending()); test_ipsr = 0U;
  settle();
  assert(blocking_calls == 0U);  // no synchronous HAL/ready/delay in all runtime persistence paths

  uint32_t prior_failures = eeprom_get_write_failure_count();
  corrupt_program = true; auto_clock = true;
  eeprom_write_byte(address(123U), 0xB4U);
  assert(!eeprom_flush_pending());
  assert(eeprom_is_pending() && device[123U] != 0xB4U && readbacks > 0U);
  assert(eeprom_get_write_failure_count() > prior_failures);
  /* Same-value retry must retain dirty intent after ACK-without-program. */
  eeprom_write_byte(address(123U), 0xB4U);
  corrupt_program = false;
  assert(eeprom_flush_pending() && device[123U] == 0xB4U);
  auto_clock = false;

  /* Actual macro caller: unopened/payload/close, retained failure, equal retry. */
  const uint32_t marker = 3071U;
  uint8_t closed = 0U, opened = 0xFFU, payload[28];
  memset(payload, 0xA7, sizeof(payload));
  auto_clock = true;
  assert(dynamic_keymap_macro_reset_checked());
  assert(eeprom_flush_pending());
  uint32_t revision = macro_revision;
  assert(dynamic_keymap_macro_set_buffer_checked(2047U, 1U, &opened));
  assert(device[marker] == 0xFFU);
  assert(dynamic_keymap_macro_set_buffer_checked(0U, sizeof(payload), payload));
  assert(dynamic_keymap_macro_set_buffer_checked(2047U, 1U, &closed));
  uint8_t visible;
  dynamic_keymap_macro_get_buffer(2047U, 1U, &visible);
  assert(visible != 0U && macro_revision == revision + 1U + sizeof(payload));
  revision = macro_revision;
  corrupt_program = true;
  assert(!eeprom_flush_pending());
  assert(eeprom_read_byte(address(marker)) != 0U && macro_revision == revision);
  corrupt_program = false;
  assert(eeprom_flush_pending());
  assert(device[marker] == 0U && macro_revision == revision + 1U);
  assert(memcmp(&device[1024U], payload, sizeof(payload)) == 0);
  assert(dynamic_keymap_macro_set_buffer_checked(2047U, 1U, &closed));
  assert(eeprom_flush_pending() && macro_revision == revision + 1U);

  /* An opener must not synchronously drain unrelated keymap SAVE backlog. */
  for (unsigned i = 256U; i < 768U; i++)
    eeprom_write_byte(address(i), eeprom_read_byte(address(i)) ^ 0x5AU);
  uint32_t opener_writes = starts, opener_reads = readbacks;
  assert(dynamic_keymap_macro_set_buffer_checked(2047U, 1U, &opened));
  assert(device[marker] == 0xFFU && starts == opener_writes + 1U && readbacks == opener_reads + 1U);
  assert(eeprom_get_write_pending_count() == 512U);
  assert(dynamic_keymap_macro_set_buffer_checked(2047U, 1U, &closed));
  assert(eeprom_flush_pending());

  /* Preserve an unrelated in-flight snapshot, then prioritize marker readback. */
  for (unsigned i = 256U; i < 768U; i++)
    eeprom_write_byte(address(i), eeprom_read_byte(address(i)) ^ 0x39U);
  auto_clock = false;
  for (unsigned i = 0; !bus.active && i < 128U; i++) eeprom_update();
  assert(bus.active && !bus.probe && !bus.read && bus.offset >= 256U && bus.offset < 768U);
  uint16_t in_flight_address = bus.offset;
  uint8_t in_flight_snapshot = bus.data[0];
  eeprom_write_byte(address(in_flight_address), in_flight_snapshot ^ 0xA5U);
  opener_writes = starts; opener_reads = readbacks; auto_clock = true;
  assert(dynamic_keymap_macro_set_buffer_checked(2047U, 1U, &opened));
  assert(starts == opener_writes + 1U && readbacks == opener_reads + 2U);
  assert(device[in_flight_address] == in_flight_snapshot && device[marker] == 0xFFU);
  assert(eeprom_byte_is_pending(in_flight_address) && eeprom_get_write_pending_count() > 0U);
  assert(dynamic_keymap_macro_set_buffer_checked(2047U, 1U, &closed));
  assert(eeprom_flush_pending());
  assert(device[in_flight_address] == (uint8_t)(in_flight_snapshot ^ 0xA5U));

  /* A snapshot of the marker page containing the previous valid marker cannot retire invalidation. */
  eeprom_write_byte(address(marker - 1U), eeprom_read_byte(address(marker - 1U)) ^ 0x47U);
  auto_clock = false;
  for (unsigned i = 0; !bus.active && i < 128U; i++) eeprom_update();
  assert(bus.active && bus.offset == (marker & ~31U) && bus.data[31U] == 0U);
  opener_writes = starts; opener_reads = readbacks; auto_clock = true;
  assert(dynamic_keymap_macro_set_buffer_checked(2047U, 1U, &opened));
  assert(device[marker] == 0xFFU && starts == opener_writes + 1U && readbacks == opener_reads + 2U);
  assert(dynamic_keymap_macro_set_buffer_checked(2047U, 1U, &closed));
  assert(eeprom_flush_pending());

  /* Failed invalidation must neither accept/drop a chunk nor allow a later CLOSE. */
  fail_start = true;
  payload[0] = 0x91U;
  uint32_t invalidation_begin = clock_ms;
  assert(!dynamic_keymap_macro_set_buffer_checked(0U, sizeof(payload), payload));
  assert((uint32_t)(clock_ms - invalidation_begin) <= 210U);
  fail_start = false;
  assert(eeprom_flush_pending());
  assert(!dynamic_keymap_macro_set_buffer_checked(2047U, 1U, &closed));
  assert(eeprom_read_byte(address(marker)) != 0U && device[1024U] != 0x91U);
  assert(dynamic_keymap_macro_set_buffer_checked(2047U, 1U, &opened));
  assert(dynamic_keymap_macro_set_buffer_checked(0U, sizeof(payload), payload));
  assert(dynamic_keymap_macro_set_buffer_checked(2047U, 1U, &closed));
  assert(eeprom_flush_pending());
  assert(device[1024U] == 0x91U && device[marker] == 0U);
  test_ipsr = 1U;
  assert(!dynamic_keymap_macro_set_buffer_checked(2047U, 1U, &opened));
  test_ipsr = 0U;
  assert(eeprom_flush_pending() && device[marker] == 0xFFU);
  assert(!dynamic_keymap_macro_set_buffer_checked(2047U, 1U, &closed));
  assert(dynamic_keymap_macro_set_buffer_checked(2047U, 1U, &opened));
  assert(dynamic_keymap_macro_set_buffer_checked(2047U, 1U, &closed));
  assert(eeprom_flush_pending());
  auto_clock = false;

  /* Invalid raw spans must fail before any valid prefix reaches the bus. */
  uint8_t invalid_raw[2] = {0x61U, 0x62U};
  uint32_t raw_calls = blocking_calls;
  uint8_t last_physical_byte = device[sizeof(device) - 1U];
  assert(!eepromWrite(sizeof(device) - 1U, invalid_raw, sizeof(invalid_raw)));
  assert(!eepromWrite(UINT32_MAX, invalid_raw, sizeof(invalid_raw)));
  assert(!eepromWrite(0U, NULL, 1U));
  assert(blocking_calls == raw_calls && device[sizeof(device) - 1U] == last_physical_byte);

  uint32_t invalid_before = eeprom_get_invalid_access_count();
  eeprom_write_byte(address(TOTAL_EEPROM_BYTE_COUNT), 0U);
  assert(eeprom_read_byte(address(TOTAL_EEPROM_BYTE_COUNT)) == 0xFFU);
  assert(eeprom_get_invalid_access_count() >= invalid_before + 2U);
  assert(eeprom_read_word((const uint16_t *)address(TOTAL_EEPROM_BYTE_COUNT - 1U)) == UINT16_MAX);
  assert(eeprom_read_dword((const uint32_t *)address(UINTPTR_MAX)) == UINT32_MAX);
  fail_read = true; eeprom_init(); assert(!eeprom_is_ready());
  eeprom_write_byte(address(0U), 0xACU); assert(!eeprom_is_pending());
  fail_read = false; eeprom_init(); assert(eeprom_is_ready());
  assert(!i2cWriteA16BytesAsync(1U, 0x50U, 0U, &desired, 1U));
  assert(!eepromWritePageStart(31U, &desired, 2U));
  puts("PASS: actual EEPROM image + chip state machine + I2C IT chain: 100000 updates, full image, ACK/NACK, failed start, IRQ timeout, dirty retention, wrap, bounds, PRIMASK");
  return 0;
}
