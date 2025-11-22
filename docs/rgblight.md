# rgblight 경로 가이드

본 문서는 V251122R6 시점의 rgblight 구성/동작/패치 이력을 정리한 가이드입니다. 캐시가 제거된 최신 구조를 기준으로 하며, 향후 Codex가 빠르게 파악할 수 있도록 흐름과 API를 요약합니다.

## 1. 계층/전역 상태 개요
- **구성/상태**  
  - `rgblight_config`(EEPROM 연동): enable, velocikey, mode, HSV, speed.  
  - `rgblight_status`: `timer_enabled`, `base_mode`, split 동기화 플래그.  
  - `animation_status`(RGBLIGHT_USE_TIMER): `pos`, `current_hue`, `delta`, `last_timer`, `next_timer_due`, `restart`.  
  - `rgblight_ranges`: 효과 범위/클리핑/인디케이터 범위.  
  - 인디케이터 상태(`rgblight_indicator_state`): host LED, 색상 캐시, 범위, `overrides_all`, `needs_render`.  
  - LED 버퍼: `led[RGBLIGHT_LED_COUNT]` (RGBW 옵션 시 w 포함).
- **메모리/타이머**  
  - 16비트 `sync_timer_read()` 기반. `rgblight_next_run`으로 1kHz 게이트.  
  - WS2812 전송은 메인 루프에서만 수행하여 인터럽트와 분리.

## 2. 동작 흐름
1. **초기화**: `rgblight_init()` → EEPROM 로드/보정 → `rgblight_timer_init()` → enable이면 `rgblight_mode_noeeprom()` → 인디케이터 초기 평가·렌더 예약.
2. **이벤트 처리**: 모드/HSV/속도/Velocikey 토글 시 타이머 on/off 갱신, 렌더 예약.
3. **주기 태스크**: `rgblight_task()`(약 1ms 게이트)  
   - 호스트 LED 큐 소비 → `rgblight_timer_task()`로 애니메이션 만료 확인·이펙트 실행 → 렌더 큐 플러시 → Velocikey 감속.
4. **렌더**: `rgblight_render_frame()`이 베이스 이펙트 결과 위에 인디케이터 오버레이 적용 후 LED 맵/RGBW 변환을 거쳐 드라이버 `setleds`.

## 3. 주요 함수 빠른 참조
| 범주 | 함수 | 설명 |
| --- | --- | --- |
| 전원/모드 | `rgblight_enable/disable[_noeeprom]`, `rgblight_toggle`, `rgblight_mode[_noeeprom]`, `rgblight_step/step_reverse` | enable/disable 및 모드 전환, 정적 모드에서는 타이머 자동 off |
| 색/밝기 | `rgblight_sethsv/rgb`, `*_at`, `*_range`, `rgblight_set_speed` | HSV/RGB 설정 및 인덱스/범위 지정 |
| 상태/EEPROM | `rgblight_get_mode`, `rgblight_is_enabled`, `rgblight_read_qword`, `rgblight_update_qword`, `eeconfig_update_rgblight_current/default` | 상태 조회·저장 |
| 타이머/애니메이션 | `rgblight_timer_enable/disable/toggle/init`, `rgblight_timer_task`, `rgblight_reload_from_eeprom` | 동적 모드에서 애니메이션 스케줄 관리 |
| 부가 기능 | Velocikey(`rgblight_velocikey_*`), 인디케이터(`rgblight_indicator_*`, port 계층), 레이어(`rgblight_layers_write`), split 동기화 매크로(`RGBLIGHT_SPLIT_*`) | 특수 기능 제어 |

## 4. Velocikey/인디케이터 특이 사항
- **Velocikey**: `preprocess_rgblight()`에서 키 입력 시 가속, `rgblight_task()` 말미에 감속. `get_interval_time()`이 런타임 속도에 따라 주기를 결정하며, 캐시가 없으므로 즉시 반영.
- **인디케이터**: `overrides_all`이면 전면 덮어쓰기 후 베이스 이펙트를 위에서 덮음. 호스트 LED 큐는 인터럽트에서 적재되고 메인 루프에서 소비.

## 5. 프리징 패치 타임라인
- **V251121R1/R5 (83dc2679…)**: 캐시 없음, 프리징 미재현.
- **V251122R2/R3 (261efa94…)**: 이펙트 함수/주기 캐시 + 만료 선행 분기 도입. 16비트 타이머 창(≈32s) 밖으로 `next_timer_due`가 밀리거나 Velocikey/비활성 상태에서 캐시가 stale 될 때 애니메이션·렌더 정지 → 10분 내외 프리징 재현.
- **V251122R5 (980a2c8…)**: 캐시 무효화·게이트 정렬 보완 시도, 여전히 스톨 재현.
- **V251122R6 (현재)**: 캐시/만료 선행 분기 완전 제거, V251121R5 수준으로 복원. 타이머 재시작 시 `next_timer_due`만 초기화. 프리징 해소 확인.

## 6. 테스트 가이드 요약
1. RGB OFF/Static/Snake + Velocikey on/off 조합으로 15분 이상 방치 후 키 입력·LED 토글 반응 확인.
2. Split/인디케이터 활성 보드에서 caps/num/scroll 토글 → 오버레이 덮어쓰기/베이스 유지 여부 확인.
3. 필요 시 `cmake -S . -B build -DKEYBOARD_PATH=...` → `cmake --build build -j10` 빌드 확인.

## 7. 참고 경로
- 코드: `src/ap/modules/qmk/quantum/rgblight/rgblight.c`
- 인디케이터/포팅: `src/ap/modules/qmk/port/` (indicator/port 계층)
- 드라이버: `src/hw/driver/ws2812.c`, `src/hw/hw_def.h` (버전 문자열)
