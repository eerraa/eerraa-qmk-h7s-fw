#ifndef LOG_H_
#define LOG_H_


#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_LOG

#define LOG_CH            HW_LOG_CH
#define LOG_BOOT_BUF_MAX  HW_LOG_BOOT_BUF_MAX
#define LOG_LIST_BUF_MAX  HW_LOG_LIST_BUF_MAX


bool logInit(void);
void logEnable(void);
void logDisable(void);
bool logOpen(uint8_t ch, uint32_t baud);
void logBoot(uint8_t enable);
void logProcess(void);                                         // V251017R3 CDC 로그 플러시 최적화 루틴
void logPrintf(const char *fmt, ...);

#endif

#ifdef __cplusplus
}
#endif



#endif
