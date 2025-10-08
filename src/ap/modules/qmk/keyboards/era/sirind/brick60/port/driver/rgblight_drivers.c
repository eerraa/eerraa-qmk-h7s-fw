#include "quantum.h"


static rgb_led_t last_frame[WS2812_MAX_CH];
static uint16_t  last_frame_len         = 0;
static bool      last_frame_initialized = false;

void ws2812_setleds(rgb_led_t *ledarray, uint16_t leds)
{
  uint16_t limit = leds;
  bool     frame_dirty = false;

  if (limit > WS2812_MAX_CH)
  {
    limit = WS2812_MAX_CH;  // V251010R5 WS2812 LED 범위 상한 보호
  }

  for (uint16_t i = 0; i < limit; i++)
  {
    rgb_led_t *current = &ledarray[i];
    rgb_led_t *cached  = &last_frame[i];
    if (!last_frame_initialized || cached->r != current->r || cached->g != current->g || cached->b != current->b)
    {
      ws2812SetColor(HW_WS2812_RGB + i, WS2812_COLOR(current->r, current->g, current->b));  // V251010R5 변경된 채널만 비트 버퍼 갱신
      *cached = *current;
      frame_dirty = true;
    }
  }

  for (uint16_t i = limit; i < last_frame_len; i++)
  {
    rgb_led_t *cached = &last_frame[i];
    if (cached->r != 0 || cached->g != 0 || cached->b != 0)
    {
      ws2812SetColor(HW_WS2812_RGB + i, WS2812_COLOR(0, 0, 0));  // V251010R5 감소한 채널 초기화
      cached->r = 0;
      cached->g = 0;
      cached->b = 0;
      frame_dirty = true;
    }
  }

  last_frame_len         = limit;
  last_frame_initialized = true;

  if (frame_dirty)
  {
    ws2812RequestRefresh(limit);  // V251011R1 부분 프레임 길이 큐잉
    ws2812Refresh();              // V251011R1 변경 발생 시 즉시 DMA 재기동
  }
}


const rgblight_driver_t rgblight_driver = {
  .setleds = ws2812_setleds,
};

