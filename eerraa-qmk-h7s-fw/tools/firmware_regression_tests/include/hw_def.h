#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define UNUSED(x) ((void)(x))
#define HW_KEYS_PRESS_MAX 20U
#define HW_USB_LOG 0
#define CLI_USE(x) 0
#define TOTAL_EEPROM_BYTE_COUNT 4096U
#define EECONFIG_KB_DATA_SIZE 0
#define EECONFIG_USER_DATA_SIZE 0
#define HW_I2C_MAX_CH 1U
#define _DEF_I2C1 0U
extern uint32_t test_irqmask, test_ipsr;
static inline uint32_t __get_PRIMASK(void) { return test_irqmask; }
static inline void __disable_irq(void) { test_irqmask = 1U; }
static inline void __set_PRIMASK(uint32_t value) { assert(value <= 1U); test_irqmask = value; }
static inline uint32_t __get_IPSR(void) { return test_ipsr; }
uint32_t millis(void);
uint32_t micros(void);
void delay(uint32_t ms);
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { volatile uint32_t CR1, CR2, ISR, ICR; } I2C_TypeDef;
typedef struct {
  I2C_TypeDef *Instance;
  uint32_t State, Mode, Lock, ErrorCode;
  uint16_t XferCount, XferSize;
  void *XferISR;
} I2C_HandleTypeDef;
#define HAL_I2C_STATE_READY 1U
#define HAL_I2C_MODE_NONE 0U
#define HAL_UNLOCKED 0U
#define HAL_I2C_ERROR_NONE 0U
#define HAL_I2C_ERROR_AF 4U
#define HAL_I2C_ERROR_TIMEOUT 32U
#define HAL_I2C_ERROR_INVALID_PARAM 512U
#define I2C_MEMADD_SIZE_16BIT 2U
#define I2C_IT_TXI 1U
#define I2C_IT_RXI 2U
#define I2C_IT_ADDRI 4U
#define I2C_IT_NACKI 8U
#define I2C_IT_STOPI 16U
#define I2C_IT_TCI 32U
#define I2C_IT_ERRI 64U
#define I2C_FLAG_AF 8U
#define I2C_FLAG_STOPF 16U
#define I2C_FLAG_BERR 256U
#define I2C_FLAG_ARLO 512U
#define I2C_FLAG_OVR 1024U
#define I2C_FLAG_TIMEOUT 4096U
#define I2C_FLAG_PECERR 8192U
void host_i2c_disable(I2C_HandleTypeDef *h);
#define __HAL_I2C_DISABLE_IT(h,flags) ((h)->Instance->CR1 &= ~(flags))
#define __HAL_I2C_DISABLE(h) host_i2c_disable(h)
#define __HAL_I2C_ENABLE(h) ((h)->Instance->CR1 |= 128U)
#define __HAL_I2C_CLEAR_FLAG(h,flags) ((h)->Instance->ICR = (flags))
HAL_StatusTypeDef HAL_I2C_Mem_Write_IT(I2C_HandleTypeDef *, uint16_t, uint16_t, uint16_t, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_I2C_Master_Transmit_IT(I2C_HandleTypeDef *, uint16_t, uint8_t *, uint16_t);
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *);
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *);
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *);
HAL_StatusTypeDef HAL_I2C_Mem_Read_IT(I2C_HandleTypeDef *, uint16_t, uint16_t, uint16_t, uint8_t *, uint16_t);
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *);

#include "log.h"
