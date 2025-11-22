#ifndef I2C_H_
#define I2C_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_I2C

#define I2C_MAX_CH       HW_I2C_MAX_CH

typedef struct
{
  uint32_t wait_count;
  uint32_t wait_last_ms;
  uint32_t wait_max_ms;
  uint8_t  wait_last_addr;
} i2c_ready_wait_stats_t;                                    // V251112R9: Ready wait 통계 구조체

bool i2cInit(void);
bool i2cIsInit(void);
bool i2cBegin(uint8_t ch, uint32_t freq_khz);
bool i2cIsBegin(uint8_t ch);
void i2cReset(uint8_t ch);
bool i2cIsDeviceReady(uint8_t ch, uint8_t dev_addr);
bool i2cRecovery(uint8_t ch);
bool i2cReadByte (uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t timeout);
bool i2cReadBytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout);
bool i2cReadA16Bytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout);

bool i2cWriteByte (uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t data, uint32_t timeout);
bool i2cWriteBytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout);
bool i2cWriteA16Bytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout);

bool i2cReadData(uint8_t ch, uint16_t dev_addr, uint8_t *p_data, uint32_t length, uint32_t timeout);
bool i2cWriteData(uint8_t ch, uint16_t dev_addr, uint8_t *p_data, uint32_t length, uint32_t timeout);


void     i2cSetTimeout(uint8_t ch, uint32_t timeout);
uint32_t i2cGetTimeout(uint8_t ch);

void     i2cClearErrCount(uint8_t ch);
uint32_t i2cGetErrCount(uint8_t ch);
void     i2cGetReadyWaitStats(uint8_t ch, i2c_ready_wait_stats_t *p_stats);   // V251112R9: Ready wait 통계 조회


#endif

#ifdef __cplusplus
}
#endif

#endif
