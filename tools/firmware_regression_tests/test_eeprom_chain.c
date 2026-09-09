#include <assert.h>
#include <stdio.h>
#include "hw_def.h"
#include "i2c.h"
#include "eeprom.h"
#include "qmk/port/platforms/eeprom.h"

uint32_t test_irqmask, test_ipsr;
static uint32_t clock_ms, blocking_calls, starts, probes, forced_stops;
static bool auto_clock, complete_irqs = true, fail_start, fail_read;
static I2C_TypeDef regs;
static I2C_HandleTypeDef handle = {.Instance = &regs, .State = HAL_I2C_STATE_READY};
static uint8_t device[16384];
static struct { bool active, probe; uint16_t offset, length; uint8_t *data; uint32_t start; } bus;
static struct { bool pending; uint16_t offset, length; uint8_t data[32]; uint32_t ready; } program;

static void complete_bus(void)
{
  if (!bus.active || test_irqmask || !complete_irqs) return;
  bus.active = false;
  handle.State = HAL_I2C_STATE_READY;
  handle.XferISR = NULL;
  uint32_t saved = test_ipsr; test_ipsr = 1U;
  if (!bus.probe) {
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
      memcpy(&device[program.offset], program.data, program.length);
      program.pending = false;
    }
    HAL_I2C_MasterTxCpltCallback(&handle);
  }
  test_ipsr = saved;
}
uint32_t millis(void)
{
  if (auto_clock) { clock_ms++; if (bus.active && (uint32_t)(clock_ms - bus.start) >= 1U) complete_bus(); }
  return clock_ms;
}
uint32_t micros(void) { return clock_ms * 1000U; }
void delay(uint32_t ms) { blocking_calls++; clock_ms += ms; }
void eeconfig_disable(void) {}
void eeconfig_init(void) {}
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
  bus.active = true; bus.probe = false; bus.offset = offset; bus.length = length; bus.data = data; bus.start = clock_ms;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Master_Transmit_IT(I2C_HandleTypeDef *h, uint16_t address, uint8_t *data, uint16_t length)
{
  assert(test_irqmask == 1U && address == 0xA0U && data == NULL && length == 0U && !bus.active);
  h->ErrorCode = 0U;
  probes++;
  h->XferISR = (void *)(uintptr_t)2U;
  bus.active = true; bus.probe = true; bus.start = clock_ms;
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
int main(void)
{
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
