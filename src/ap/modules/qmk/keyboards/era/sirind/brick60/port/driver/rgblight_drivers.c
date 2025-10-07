#include "quantum.h"


void ws2812_setleds(rgb_led_t *ledarray, uint16_t leds)
{
  uint8_t r, g, b;


  for (int i=0; i<leds; i++)
  {
    r = ledarray[i].r;
    g = ledarray[i].g;
    b = ledarray[i].b;
    ws2812SetColor(HW_WS2812_RGB + i, WS2812_COLOR(r, g, b));
  }
  // V251010R1 WS2812 DMA 재기동을 메인 루프로 이관
  ws2812RequestRefresh(leds);
}


const rgblight_driver_t rgblight_driver = {
  .setleds = ws2812_setleds,
};

