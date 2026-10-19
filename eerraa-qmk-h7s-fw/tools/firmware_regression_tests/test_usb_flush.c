#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "usb_flush_defs.inc"

typedef struct {
  uint32_t unused;
} USB_OTG_GlobalTypeDef;

typedef struct {
  USB_OTG_GlobalTypeDef *Instance;
  HAL_LockTypeDef Lock;
} PCD_HandleTypeDef;

static USB_OTG_GlobalTypeDef controller;
static PCD_HandleTypeDef handle = {&controller, HAL_UNLOCKED};
static HAL_StatusTypeDef ll_status;
static unsigned tx_calls, rx_calls;
static uint32_t tx_fifo;
static USB_OTG_GlobalTypeDef *ll_instance;

static HAL_StatusTypeDef USB_FlushTxFifo(USB_OTG_GlobalTypeDef *instance, uint32_t fifo)
{
  assert(handle.Lock == HAL_LOCKED);
  tx_calls++;
  tx_fifo = fifo;
  ll_instance = instance;
  return ll_status;
}

static HAL_StatusTypeDef USB_FlushRxFifo(USB_OTG_GlobalTypeDef *instance)
{
  assert(handle.Lock == HAL_LOCKED);
  rx_calls++;
  ll_instance = instance;
  return ll_status;
}

#include "usb_flush_function.inc"

static void reset_calls(void)
{
  tx_calls = rx_calls = 0U;
  tx_fifo = UINT32_MAX;
  ll_instance = NULL;
}

static void check_status(uint8_t address, uint32_t endpoint, HAL_StatusTypeDef status)
{
  reset_calls();
  ll_status = status;
  assert(handle.Lock == HAL_UNLOCKED);
  assert(HAL_PCD_EP_Flush(&handle, address) == status);
  assert(handle.Lock == HAL_UNLOCKED);
  assert(handle.Instance == &controller && ll_instance == &controller);
  if (address & 0x80U) {
    assert(tx_calls == 1U && rx_calls == 0U);
    assert(tx_fifo == endpoint);
  } else {
    assert(tx_calls == 0U && rx_calls == 1U);
    assert(tx_fifo == UINT32_MAX);
  }
}

static void check_busy(uint8_t address)
{
  reset_calls();
  ll_status = HAL_ERROR;
  handle.Lock = HAL_LOCKED;
  assert(HAL_PCD_EP_Flush(&handle, address) == HAL_BUSY);
  assert(handle.Lock == HAL_LOCKED && handle.Instance == &controller);
  assert(tx_calls == 0U && rx_calls == 0U);
  assert(tx_fifo == UINT32_MAX && ll_instance == NULL);
  handle.Lock = HAL_UNLOCKED;
}

int main(void)
{
  const HAL_StatusTypeDef statuses[] = {HAL_OK, HAL_ERROR, HAL_TIMEOUT, HAL_BUSY};
  for (uint32_t endpoint = 0U; endpoint < 16U; endpoint++) {
    for (unsigned i = 0U; i < sizeof(statuses) / sizeof(statuses[0]); i++) {
      check_status((uint8_t)(0x80U | endpoint), endpoint, statuses[i]);
      check_status((uint8_t)endpoint, endpoint, statuses[i]);
    }
    check_busy((uint8_t)(0x80U | endpoint));
    check_busy((uint8_t)endpoint);
  }
  puts("PASS: production HAL endpoint flush propagates all LL statuses in both directions, selects the TX FIFO, unlocks, and preserves the busy lock without LL calls");
  return 0;
}
