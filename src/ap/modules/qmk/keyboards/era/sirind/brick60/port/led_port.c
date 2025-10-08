#include "led_port_internal.h"

// V251010R5 LED 호스트 큐 전용 모듈

static uint8_t host_led_pending_raw = 0;                      // V251010R4 호스트 LED 지연 적용 버퍼
static bool    host_led_pending_dirty = false;                // V251010R4 호스트 LED 지연 적용 플래그
static led_t   host_led_state = {0};                          // V251010R5 호스트 LED 상태 캐시 분리

static void service_pending_host_led(void);

void led_port_set_host_state(led_t state)
{
  host_led_state = state;                                     // V251010R5 QMK 경로 호스트 LED 상태 동기화
}

led_t led_port_get_host_state(void)
{
  return host_led_state;                                      // V251010R5 호스트 LED 상태 조회 핸들러
}

static void service_pending_host_led(void)
{
  if (!host_led_pending_dirty)
  {
    return;  // V251010R4 지연 적용할 호스트 LED 없음
  }

  host_led_state.raw     = host_led_pending_raw;              // V251010R4 지연 적용된 호스트 LED 반영
  host_led_pending_dirty = false;                             // V251010R4 적용 완료 플래그 클리어
}

void usbHidSetStatusLed(uint8_t led_bits)
{
  if (host_led_pending_dirty && host_led_pending_raw == led_bits)
  {
    return;  // V251010R4 동일한 지연 적용 요청 중복 방지
  }

  if (!host_led_pending_dirty && host_led_state.raw == led_bits)
  {
    return;  // V251010R4 변화가 없으면 추가 처리 불필요
  }

  host_led_pending_raw   = led_bits;   // V251010R4 호스트 LED 변경분 저장
  host_led_pending_dirty = true;       // V251010R4 메인 루프 적용 예약
}

uint8_t host_keyboard_leds(void)
{
  service_pending_host_led();  // V251010R4 지연 적용된 호스트 LED 상태 소화
  return host_led_state.raw;   // V251010R5 메인 루프에서 유지되는 호스트 LED 상태 반환
}

