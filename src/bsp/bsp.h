#ifndef BSP_H_
#define BSP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "def.h"


#include "stm32h7rsxx_hal.h"



void logPrintf(const char *fmt, ...);



bool bspInit(void);

void delay(uint32_t time_ms);
uint32_t millis(void);
uint32_t micros(void);
void bspHeartbeatTouch(void);                                   // V251123R8: 메인 루프 헬스 체크
uint32_t bspHeartbeatMillis(void);
uint32_t bspHeartbeatSeq(void);

void Error_Handler(void);


#ifdef __cplusplus
}
#endif

#endif
