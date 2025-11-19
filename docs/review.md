# RGB 인디케이터 정지 현상 리뷰

## 1. 증상 재정리
- 펌웨어 버전(V251109R3~V251115R6)과 무관하게 CAPS ON 인디케이터가 전체 30개의 LED에 균일하게 적용되지 않고 일부 구간만 녹색으로 출력되거나 이전 RGB 이펙트 색상이 남습니다.
- CAPS OFF 이후에도 약 5~29초 동안 녹색 인디케이터가 유지된 뒤 원래의 Snake 6 이펙트가 재개됩니다.
- CAPS 상태 신호 자체는 누락되지 않았으므로, 인디케이터 색상/범위가 준비된 뒤 WS2812 전송이 완료되기 전에 깨지는 것으로 추정됩니다.

## 2. 코드 흐름 요약
- `rgblight_indicator_update_config()`와 `rgblight_indicator_apply_host_led()`가 `rgblight_indicator_commit_state()`를 호출하여 `needs_render` 플래그를 세팅하고, `rgblight_request_render()`를 큐합니다(`src/ap/modules/qmk/quantum/rgblight/rgblight.c:534-555`).
- `rgblight_render_frame()`은 `rgblight_indicator_state.overrides_all`이 true인 경우(Brick60 Caps 범위 전체) 기본 이펙트를 건너뛰고 `rgblight_indicator_apply_overlay()`로 RGB 버퍼 전체를 녹색으로 덮습니다(`src/ap/modules/qmk/quantum/rgblight/rgblight.c:1434-1501`, `src/ap/modules/qmk/quantum/rgblight/rgblight.c:502-530`).
- `rgblight_driver.setleds()`는 Brick60 전용 드라이버 `ws2812_setleds()`를 호출하여 CPU 버퍼(`bit_buf`)에 비트를 채우고, 즉시 `ws2812Refresh()`로 DMA 전송을 시작합니다(`src/ap/modules/qmk/keyboards/era/sirind/brick60/port/driver/rgblight_drivers.c:4-21`, `src/hw/driver/ws2812.c:195-251`).
- `ws2812Refresh()`는 DMA를 중단하지 않은 상태에서도 `bit_buf`를 덮어쓴 뒤, 마지막에 `HAL_TIM_PWM_Stop_DMA()` → `HAL_TIM_PWM_Start_DMA()` 순으로 재시작하는 구조입니다(`src/hw/driver/ws2812.c:178-193`).

## 3. 원인 추정(우선순위 순)
1. **DMA 전송 중 버퍼 경합**  
   `ws2812_setleds()`는 DMA가 `bit_buf`를 읽는 동안에도 동일 버퍼를 즉시 덮어씁니다. CAPS ON 트리거가 WS2812 프레임 전송(약 1ms) 동안 발생하면 DMA가 읽은 앞쪽 LED 데이터는 이전 이펙트 색상으로 남고, 나머지 뒷부분만 인디케이터 녹색으로 갱신되어 “부분만 그려진” 현상이 재현됩니다. 동일한 문제로 CAPS OFF 이후에도 첫 몇 프레임이 깨지면 재시도 시점까지 녹색이 남게 됩니다.
2. **DMA 재시작 실패 시 재렌더 누락**  
   `ws2812Refresh()`는 실패 시 `false`를 반환하지만 호출부(`rgblight_render_frame()`)는 결과를 확인하지 않습니다. DMA BUSY가 3회 연속 발생하면 실제로는 아무 데이터도 전송되지 않는데도 `rgblight_indicator_state.needs_render`가 `false`로 바뀌어 재시도가 이뤄지지 않습니다. 이 경우 다음 CAPS 토글이나 QMK 이펙트가 새로운 프레임을 전송하기 전까지 인디케이터가 반쯤 남은 상태로 유지됩니다.
3. **인디케이터 해제 직후 즉시 프레임이 존재하지 않음**  
   `rgblight_indicator_commit_state()`는 CAPS OFF 시 동적 이펙트에 대해 `rgblight_request_render()`만 큐잉하고, 실제로 RGB 버퍼를 새로 채우는 작업은 다음 이펙트 타이머 틱까지 지연됩니다. 주 루프가 USB 작업 등으로 일시 정지되면(예: instability monitor, CLI) 재생성된 프레임이 늦게 도착해 “녹색이 수 초 유지됨”으로 보일 수 있습니다. 현재 구조에서는 인디케이터 해제 시 즉시 `rgblight_render_frame()`을 강제 실행하거나, 최소한 `rgblight_mode_noeeprom()`을 한번 호출해 버퍼를 강제로 갱신해야 딜레이가 없습니다.

## 4. 대응 제안
1. **DMA Busy 보호**  
   - `ws2812_setleds()` 진입 시 DMA 완료 여부를 폴링/플래그로 확인하고, 전송 중이면 완료 또는 타임아웃까지 대기한 뒤 `bit_buf`를 덮어쓰도록 수정합니다.  
   - 대안: CPU 작업용 버퍼와 DMA 버퍼를 분리하여 더블 버퍼링 후, `ws2812Refresh()`에서 포인터를 스왑합니다.
2. **DMA 오류 로깅 및 재시도**  
   - `ws2812Refresh()`가 `false`를 반환한 경우 CLI/UART 경고(예: `logPrintf("[WS2812] DMA busy %lu\n", attempt);`)를 남기고, `rgblight_indicator_state.needs_render`를 유지하여 다음 루프에서 다시 전송하도록 처리합니다(`rgblight_render_frame()`에서 반환값 검사).  
   - 필요 시 `hw_log` 경로(`src/hw/driver/ws2812.c`)에 `LOG_CH_WS2812` 수준의 플래그를 추가해 DMA 실패 빈도를 계측합니다.
3. **인디케이터 해제 즉시 복구 루틴 추가**  
   - CAPS OFF 등으로 `rgblight_indicator_state.active`가 false가 되면 `rgblight_mode_noeeprom()`을 강제 호출하거나, `rgblight_render_frame()`을 즉시 호출하여 RGB 버퍼가 지연 없이 Snake 이펙트로 돌아가도록 합니다.  
   - 필요 시 `rgblight_indicator_commit_state()`에서 `rgblight_request_render()`를 큐잉하는 대신 `rgblight_render_pending` 상태를 확인하고 즉시 플러시합니다.

## 5. 추가 확인/로그 지점
- DMA 충돌 여부를 확인하기 위해 `ws2812Refresh()` 진입 시 DMA 상태 레지스터와 타임스탬프를 UART에 1회 출력하는 디버그 토글을 추가하면(예: `LOG_CH_WS2812`), CAPS 토글 직후 BUSY 발생 여부를 손쉽게 확인할 수 있습니다.
- 인디케이터 상태 머신을 추적하려면 `rgblight_indicator_commit_state()`에 CAPS 타깃, `needs_render` 플래그, `rgblight_render_pending` 값을 `LOG_LEVEL_VERBOSE` 조건부로 출력하는 것이 좋습니다(`src/ap/modules/qmk/quantum/rgblight/rgblight.c`).
