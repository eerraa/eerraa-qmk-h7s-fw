#include "led_port_internal.h"

// V251011R1 호스트 LED 즉시 처리 경로 복원 및 캐시 연동 정리

static led_t host_led_state      = {0};
static led_t indicator_led_state = {0};

led_t led_port_host_cached_state(void)
{
  return host_led_state;  // V251011R1 인디케이터 합성용 호스트 LED 상태 제공
}

void led_port_host_store_cached_state(led_t led_state)
{
  host_led_state = led_state;  // V251011R1 led_set() 재호출 없이도 최신 상태 유지
}

void usbHidSetStatusLed(uint8_t led_bits)
{
  if (host_led_state.raw == led_bits)
  {
    return;  // V251011R1 동일 상태면 즉시 반환
  }

  host_led_state.raw = led_bits;  // V251011R1 최신 호스트 LED 상태 캐시 갱신
  led_set(led_bits);              // V251011R1 USB SET_REPORT 즉시 led_set()으로 위임
}

void led_update_ports(led_t led_state)
{
  led_port_host_store_cached_state(led_state);
  if (indicator_led_state.raw == led_state.raw)
  {
    return;  // V251011R1 호스트 LED 변화 없을 때 중복 갱신 방지
  }

  indicator_led_state = led_state;  // V251011R1 인디케이터 상태 캐시 갱신
  led_port_indicator_refresh();
}

uint8_t host_keyboard_leds(void)
{
  return host_led_state.raw;  // V251011R1 USB 요청 직후 캐시된 호스트 LED 상태 반환
}
