#include "hw_def.h"
#include "../../src/hw/driver/ws2812.c"

static void settle(void) { mock_advance_us(2000); ws2812Task(); }
static void boot(void) { mock_reset(); assert(ws2812Init()); settle(); assert(ws2812IsFrameComplete(ws2812GetRequestedGeneration())); }
static void color(uint32_t c) { for (unsigned i=0; i<WS2812_MAX_CH; ++i) ws2812SetColor(i,c); assert(ws2812Refresh()); }
static void expect_color(uint8_t r,uint8_t g,uint8_t b) {
  assert(mock_wire_count);
  for (unsigned i=0;i<WS2812_MAX_CH;++i) {
    const uint8_t *p=&mock_wire[mock_wire_count-1][i*3]; assert(p[0]==g && p[1]==r && p[2]==b);
  }
}
static void test_frames(void) {
  boot(); unsigned before=mock_starts;
  color(WS2812_COLOR(0x81,0xA5,0x3C)); uint32_t first=ws2812GetRequestedGeneration();
  assert(!ws2812IsFrameComplete(first));
  const uint8_t *active=mock_dma_data;
  color(WS2812_COLOR_GREEN); color(WS2812_COLOR_BLUE);
  uint32_t last=ws2812GetRequestedGeneration();
  assert(mock_starts==before+1 && mock_dma_data==active);
  mock_advance_us(WS2812_FRAME_US - 100U); assert(!ws2812IsFrameComplete(first));
  settle(); expect_color(0x81,0xA5,0x3C);
  assert(ws2812IsFrameComplete(first) && !ws2812IsFrameComplete(last));
  settle(); expect_color(0,0,255); assert(ws2812IsFrameComplete(last));
  assert(mock_gpio.ODR==0 && (mock_gpio.MODER&(3U<<24))==(1U<<24));
  /* Tail alone is enough even when main observes TC at its earliest possible instant. */
  assert((BIT_ZERO-2)*BIT_PERIOD>28000);
  assert(mock_low_ns>280000);
}
static void test_failures(void) {
  boot(); mock_fail_starts=2; color(WS2812_COLOR_RED); uint32_t gen=ws2812GetRequestedGeneration();
  assert(!ws2812IsFrameComplete(gen)); ws2812Task(); ws2812Task(); settle(); expect_color(255,0,0);
  assert(ws2812IsFrameComplete(gen));
  color(WS2812_COLOR_GREEN); gen=ws2812GetRequestedGeneration(); mock_advance_us(600);
  mock_dma->State=HAL_DMA_STATE_READY; mock_dma->ErrorCode=1;
  ws2812Task(); assert(!ws2812IsFrameComplete(gen)); settle(); expect_color(0,255,0);
  assert(ws2812IsFrameComplete(gen));
  color(WS2812_COLOR_BLUE); gen=ws2812GetRequestedGeneration(); mock_suppress_irq=true;
  mock_advance_us(WS2812_SERVICE_TIMEOUT_US+1); assert(mock_dma->State==HAL_DMA_STATE_BUSY);
  ws2812Task(); assert(ws2812IsFrameComplete(gen)); expect_color(0,0,255);
}
static void test_abort_and_wrap(void) {
  boot(); color(WS2812_COLOR_RED); uint32_t failed=ws2812GetRequestedGeneration();
  mock_frozen=mock_hold_abort=true; mock_advance_us(WS2812_SERVICE_TIMEOUT_US+1); ws2812Task();
  assert(ws2812_phase==WS2812_QUIESCING && !ws2812IsFrameComplete(failed));
  unsigned starts=mock_starts; color(WS2812_COLOR_GREEN);
  for(unsigned i=0;i<10;++i)ws2812Task(); assert(mock_starts==starts);
  mock_frozen=mock_hold_abort=false; ws2812Task(); settle(); expect_color(0,255,0);
  ws2812_requested_generation=UINT32_MAX-1; ws2812_completed_generation=UINT32_MAX-1;
  mock_now_ns=(uint64_t)(UINT32_MAX-600)*1000U;
  color(WS2812_COLOR_RED); assert(ws2812GetRequestedGeneration()==UINT32_MAX); settle();
  assert(ws2812IsFrameComplete(UINT32_MAX));
  color(WS2812_COLOR_BLUE); assert(ws2812GetRequestedGeneration()==1); settle();
  assert(ws2812IsFrameComplete(1)); expect_color(0,0,255);
}
static void test_preempted_start(void) {
  boot(); mock_start_latency_us=2500; // DMA finishes before Start_DMA returns to its caller.
  color(WS2812_COLOR_RED);
  uint32_t generation=ws2812GetRequestedGeneration();
  ws2812Task(); assert(ws2812IsFrameComplete(generation)); expect_color(255,0,0);
}
int main(void) {
  test_frames(); test_failures(); test_abort_and_wrap(); test_preempted_start();
  puts("PASS: production WS2812 init/encoder/scheduler: RESET, CCR preload, immutable DMA, coalescing, completion generations, start/error/missing-IRQ/async-abort recovery and wrap");
}
