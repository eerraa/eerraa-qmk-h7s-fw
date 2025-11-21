# rgblight 스케줄링/애니메이션 경로 점검

## 흐름 요약
- `apMain()` → `qmkUpdate()` → `keyboard_task()` 순서로 메인 루프가 돌아가며, `keyboard_task()` 안에서 `rgblight_task()`가 매 반복마다 호출된다(`src/ap/ap.c:25`, `src/ap/modules/qmk/qmk.c:40`, `src/ap/modules/qmk/quantum/keyboard.c:815`, `:829`).
- `rgblight_task()`는 (1) 인터럽트에서 큐잉된 호스트 LED 이벤트 소비 → (2) `rgblight_timer_task()`로 애니메이션 스케줄링 → (3) `rgblight_render_pending` 플러시 → (4) Velocikey 감속 순으로 수행한다(`src/ap/modules/qmk/quantum/rgblight/rgblight.c:2153`, `:2179`).
- 렌더 경로는 `rgblight_set()`이 `rgblight_render_pending`만 세팅하고, 실제 WS2812 전송은 `rgblight_task()` 안의 `rgblight_render_frame()`에서 처리된다(`src/ap/modules/qmk/quantum/rgblight/rgblight.c:1537`, `:2169`).
- 애니메이션은 `rgblight_timer_task()`가 현재 모드에 맞춰 `interval_time`을 결정하고, 만료 시점에만 개별 이펙트 함수를 실행해 `rgblight_set()`을 호출한다(`src/ap/modules/qmk/quantum/rgblight/rgblight.c:1651`, `:1675`~`:1757`).
- 키 입력 기반 Pulse 계열은 `preprocess_rgblight()`가 키 이벤트를 전달해 상태 머신만 갱신하고, 실 출력은 역시 타이머 경로에서만 전송된다(`src/ap/modules/qmk/quantum/rgblight/rgblight.c:2140`).

## 호출 빈도/부하 관찰
- 메인 루프는 “as fast as possible” 주석 그대로 동작하며, `matrix_scan_perf_task()`가 1초마다 누적 호출 수를 `get_matrix_scan_rate()`로 공개한다. 현재 설정(8 kHz 매트릭스 스캔 목표)에서는 `rgblight_task()`도 초당 약 8,000회 호출된다(`src/ap/modules/qmk/quantum/keyboard.c:211`, `:815`).
- 반면 애니메이션 스텝 주기는 최소 5ms(200Hz, Pulse/Twinkle), 그 외 효과는 10~500ms 수준이다(`src/ap/modules/qmk/quantum/rgblight/rgblight.c:1675`~`:1733`, `:1736`~`:1756`, `:2063`). 실질적인 `rgblight_set()`/`rgblight_render_frame()` 실행 빈도는 이 주기에 의해 제한된다.
- 캡스/넘락 인디케이터도 IRQ에서 큐잉 → 메인 루프 처리 구조이므로, 소수의 플래그 체크만 8kHz로 반복된다. 렌더 전송 자체는 큐가 설정될 때만 발생한다(`src/ap/modules/qmk/quantum/rgblight/rgblight.c:2153`~`:2166`, `:2169`~`:2176`).
- 요약하면 CPU는 초당 8k회 `rgblight_task()`를 스케줄하지만, 실 WS2812 전송은 최대 200Hz 수준이라 호출 빈도가 체감 기능 요구 사항보다 상당히 높다.

## 개선 아이디어
- 스케줄링 희박화: `rgblight_task()`를 1kHz 내외로 샘플링하거나, `rgblight_status.timer_enabled`/`rgblight_render_pending`/`rgblight_host_led_pending`/Velocikey 활성 상태가 모두 false일 때는 즉시 리턴하도록 가볍게 게이트하면 메인 루프 부하를 줄일 수 있다.
- 다음 만료 캐시: `rgblight_timer_task()`가 매 호출마다 `sync_timer_read()`와 모드 분기, `get_interval_time()`을 반복한다. `animation_status`에 `next_due`를 저장해 만료 시점 전에 빠르게 대기(return)하면 8kHz 호출 시 불필요한 타이머 연산을 피할 수 있다.
- 렌더 플러시 우선순위 조정: `rgblight_render_pending` 플러시를 Idle 계층(예: `idle_task()` 전)으로 옮기거나, WS2812 DMA 시작을 매트릭스/HID 전송 직후가 아닌 틱 경계에 묶으면 매트릭스 스캔 지터를 더 줄일 여지가 있다.
- 계측 추가: `_DEF_ENABLE_MATRIX_TIMING_PROBE` 수준의 간단한 프로브를 `rgblight_render_frame()` 전후에 넣어 전송 시간/대기열 길이를 기록하면, 호출 주기를 낮출 때 HID 지터나 인디케이터 반응 지연이 없는지 확인하기 쉽다.
