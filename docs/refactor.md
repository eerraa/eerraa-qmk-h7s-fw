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

## 적용 현황 (V251122R1 시점)
- 적용됨: `rgblight_task()` 1kHz 희박화 + 긴급 플래그 우회, `animation_status.next_timer_due` 애니메이션 만료 캐시 및 0 wrap 정상 처리.
- 미적용: 렌더 플러시 우선순위 조정(Idle 등으로 이동) — 플러시 지연·경로 누락 리스크와 upstream 충돌 가능성이 있어 실제 지터 문제가 확인되면 측정 기반으로 진행 예정.
- 미적용: `rgblight_render_frame()` 전후 계측 추가 — 빌드 플래그/로그 설계와 측정 시나리오 정리 후 반영 예정.

## 추가 개선 검토 (V251122R1 기준)
- 유효 제안
  - 타이머 분기 순서 최적화: `rgblight_timer_task()`에서 만료 체크를 모드 분기 이전으로 이동하고, 모드별 `effect_func`/`interval_time`을 전환 시점에 캐시해 1kHz 게이트 구간의 PROGMEM 접근을 제거.
  - 호출 시각 공유: `rgblight_task()`가 읽은 `now`를 `rgblight_timer_task()`에 전달하고, `rgblight_next_run`을 `animation_status.next_timer_due`(최소 1ms)와 연동해 타이머 읽기/호출을 애니메이션 만료 시각에 맞춰 자동 희박화.
  - 전면 인디케이터 시 베이스 렌더 스킵: `overrides_all`이 true이면 베이스 이펙트가 LED 버퍼를 구축·전송하지 않도록 분기해 캡스락 상시 점등 등에서 불필요한 WS2812 전송과 큐잉을 제거(복귀 시 1프레임 재렌더).
- 추가 탐색
  - 비활성 상태 조기 반환: `rgblight_config.enable`이 0이고 렌더/호스트 LED/Velocikey 플래그도 없을 때는 `rgblight_task()`를 즉시 종료해 All Off 상태의 1kHz 호출을 완전히 제거.
  - 지터/응답성 검증을 위한 경량 계측: `rgblight_render_frame()` 시작/종료 시 타임스탬프/대기열 길이를 빌드 플래그 기반으로 로깅해 위 최적화 적용 전후의 HID 지터와 인디케이터 반응성을 확인.

## 제안 선별 및 적용 계획 (V251122R1 기준)
- 유의미/필수
  - 타이머 분기 순서 최적화: 긴급 플래그가 세트되면 `rgblight_task()`가 ≈250 kHz 호출되므로, 만료되지 않은 대부분의 호출에서 모드 분기/PROGMEM 접근을 건너뛰게 해 CPU 낭비를 큰 폭으로 줄여야 함.
  - 비활성 상태 조기 반환: `rgblight_config.enable == 0`이며 긴급 플래그·Velocikey가 없을 때 1 kHz 호출을 즉시 리턴해 All Off 장기 대기 시의 불필요한 경로 실행을 제거.
  - 전면 인디케이터 시 베이스 렌더 스킵: `overrides_all` 활성 시에도 베이스 렌더·WS2812 큐잉이 계속 발생하는 논리적 낭비를 제거. 긴급 플래그가 켜진 상태에서 250 kHz 호출이 반복될 수 있으므로 연산/버스 절감 효과가 있음.
- 효과 제한/우선도 낮음
  - 호출 시각 공유: 1 kHz 게이트 구간에서 타이머 읽기 1회 감소 효과에 그쳐 우선도 낮음.
  - 경량 계측: 기능상 필수는 아니며, 최적화 전후 지터 검증용으로 필요 시 한시적으로 추가.
- 적용 단계 권장
  1) 필수 항목(타이머 분기 순서 최적화, 비활성 조기 반환, 전면 인디케이터 시 베이스 렌더 스킵)을 한 번에 적용해 구조 변화를 통합 관리하고, 250 kHz 경로 부하를 우선 제거.
  2) 이후 여유 시 호출 시각 공유를 소규모 패치로 추가.
  3) 계측은 최적화 전후 비교가 필요할 때 빌드 플래그로 단기간 활성화.
