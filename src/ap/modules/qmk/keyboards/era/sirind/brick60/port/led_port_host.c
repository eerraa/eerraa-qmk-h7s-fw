#include "led_port_internal.h"

// V251010R5 호스트 LED 큐 처리 모듈 분리 및 캐시 연동

static led_t   host_led_state      = {0};
static led_t   indicator_led_state = {0};
static uint8_t host_led_pending_raw   = 0;
static bool    host_led_pending_dirty = false;

static void service_pending_host_led(void)
{
  if (!host_led_pending_dirty)
  {
    return;  // V251010R5 지연 적용할 호스트 LED 없음
  }

  host_led_state.raw      = host_led_pending_raw;  // V251010R5 지연 적용된 호스트 LED 반영
  host_led_pending_dirty  = false;                 // V251010R5 적용 완료 플래그 초기화
}

led_t led_port_host_cached_state(void)
{
  return host_led_state;  // V251010R5 인디케이터 합성용 호스트 LED 상태 제공
}

void led_port_host_store_cached_state(led_t led_state)
{
  host_led_state = led_state;  // V251010R5 호스트 LED 상태 캐시 갱신
}

void usbHidSetStatusLed(uint8_t led_bits)
{
  if (host_led_pending_dirty && host_led_pending_raw == led_bits)
  {
    return;  // V251010R5 동일한 지연 적용 요청 중복 방지
  }

  if (!host_led_pending_dirty && host_led_state.raw == led_bits)
  {
    return;  // V251010R5 변화가 없으면 추가 처리 불필요
  }

  host_led_pending_raw   = led_bits;  // V251010R5 호스트 LED 변경분 저장
  host_led_pending_dirty = true;      // V251010R5 메인 루프 적용 예약
}

void led_update_ports(led_t led_state)
{
  led_port_host_store_cached_state(led_state);
  if (indicator_led_state.raw == led_state.raw)
  {
    return;  // V251010R5 호스트 LED 변화 없을 때 중복 갱신 방지
  }

  indicator_led_state = led_state;  // V251010R5 인디케이터 상태 캐시 갱신
  led_port_indicator_refresh();
}

uint8_t host_keyboard_leds(void)
{
  service_pending_host_led();
  return host_led_state.raw;  // V251010R5 메인 루프에서 유지되는 호스트 LED 상태 반환
}
