#ifndef WS2812_H_
#define WS2812_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_WS2812

#define WS2812_MAX_CH  HW_WS2812_MAX_CH


#define WS2812_COLOR(r, g, b)   (((r)<<16) | ((g)<<8) | ((b)<<0))

#define WS2812_COLOR_RED        WS2812_COLOR(255,   0,   0)
#define WS2812_COLOR_GREEN      WS2812_COLOR(  0, 255,   0)
#define WS2812_COLOR_BLUE       WS2812_COLOR(  0,   0, 255)
#define WS2812_COLOR_OFF        WS2812_COLOR(  0,   0,   0)


bool ws2812Init(void);
void ws2812SetColor(uint32_t ch, uint32_t color);
bool ws2812Refresh(void);
void ws2812RequestRefresh(uint16_t leds);                  // V251010R1 WS2812 DMA 비동기 요청 API
void ws2812ServicePending(void);                           // V251010R6 메인 루프 단일 진입 WS2812 DMA 서비스 헬퍼
bool ws2812HandleDmaTransferCompleteFromISR(TIM_HandleTypeDef *htim);  // V251010R1 WS2812 DMA 완료 처리


#endif

#ifdef __cplusplus
}
#endif

#endif 
