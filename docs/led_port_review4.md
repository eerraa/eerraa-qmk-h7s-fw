# LED/WS2812 경로 검토 (V251010R1~V251010R8 기준)

## 1. 현행 코드 흐름 요약

### 1.1 WS2812 DMA 요청 및 서비스
- `rgblight_set()`이 색상 버퍼를 재계산한 뒤 키보드 포트 드라이버의 `ws2812_setleds()`를 호출합니다. 이 함수는 마지막으로 전송한 프레임(`last_frame`)과 비교해 변경된 채널만 `ws2812SetColor()`로 갱신하고, 변경이 감지될 때에만 `ws2812RequestRefresh()`로 DMA를 요청합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/driver/rgblight_drivers.c†L4-L48】
- `ws2812RequestRefresh()`는 요청 길이를 계산해 `ws2812_pending_len`과 `ws2812_pending`을 원자적으로 갱신합니다. 실제 DMA 재시작은 메인 루프 말미의 `ws2812ServicePending()`에서만 수행되며, 이 함수가 크리티컬 섹션에서 `ws2812_pending`을 소비해 단일 DMA 전송을 재기동합니다.【F:src/hw/driver/ws2812.c†L195-L229】【F:src/ap/modules/qmk/quantum/keyboard.c†L905-L913】
- `ws2812HandleDmaTransferCompleteFromISR()`는 DMA 완료 인터럽트에서 `ws2812_busy`를 해제해 다음 요청이 루프에서 처리될 수 있게 합니다.【F:src/hw/driver/ws2812.c†L232-L240】

### 1.2 인디케이터 및 호스트 LED 경로
- 호스트 LED 변경은 `usbHidSetStatusLed()`에 큐잉되어 `host_led_pending_dirty`가 설정됩니다. 메인 루프에서 `host_keyboard_leds()`가 호출될 때 대기 중인 값을 반영해 `led_port_indicator_refresh()`를 트리거합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_host.c†L5-L60】
- `led_port_indicator_refresh()`는 EEPROM 설정과 호스트 상태를 조합해 RGB 인디케이터 버퍼를 갱신하고, 활성 마스크/색상 캐시가 변했을 때만 `rgblight_set()`을 재실행해 DMA 요청을 유발합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_indicator.c†L5-L204】
- VIA 경로는 각 인디케이터 타입에 대해 설정 변경을 처리한 뒤 필요 시 캐시를 더럽혀 `led_port_indicator_refresh()`로 이어지는 동일한 흐름을 사용합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_via.c†L5-L146】

### 1.3 RGB Matrix 연동
- RGB Matrix가 WS2812를 사용하는 경우 `rgb_matrix_driver.flush()`에서 `ws2812_setleds()`를 재사용하며, 더티 플래그를 통해 중복 DMA 요청을 억제합니다. 이때도 최종 전송은 `ws2812ServicePending()` 경유입니다.【F:src/ap/modules/qmk/quantum/rgb_matrix/rgb_matrix_drivers.c†L142-L190】

## 2. 개선 검토 결과

### 2.1 불필요한 코드 제거 관점
1. **글로벌 `is_init` 잔존 플래그**  
   - 현행 `ws2812.c`에는 `bool is_init`이 정의되어 초기화 시 `true`로 설정되지만, 다른 모듈에서 조회하거나 검증하지 않습니다.【F:src/hw/driver/ws2812.c†L11-L126】  
   - **제안**: `is_init` 선언 및 대입을 제거하고, 필요 시 향후에 `ws2812IsInit()` 등의 정식 API를 추가하도록 합니다.  
   - **부작용 추론**: 외부 코드가 비공식적으로 `extern bool is_init;`에 의존할 경우 링크 오류가 발생할 수 있으나, 저장소 전체 검색 결과 참조가 없어 영향은 없다고 판단됩니다.  
   - **적용 여부**: 적용 권장.

### 2.2 로직 경량화 관점
1. **프레임 길이 산출 캐시화**  
   - `ws2812RequestRefresh()`는 요청마다 `ws2812CalcFrameSize()`를 호출합니다. 실제 운용에서는 대부분 `WS2812_MAX_CH` 길이로 전송하므로, 매 호출마다 동일한 곱셈과 범위 조절을 반복합니다.【F:src/hw/driver/ws2812.c†L195-L225】  
   - **제안**: 초기화 시 계산한 `ws2812_pending_len`을 `ws2812_full_frame_len`으로 별도 보관하고, `leds >= ws2812.led_cnt`일 때는 즉시 재사용합니다. 부분 길이 요청이 필요한 경우에만 함수를 호출합니다.  
   - **부작용 추론**: 부분 전송을 자주 사용하는 외부 모듈이 있다면 조건 분기 때문에 코드가 복잡해질 수 있습니다. 그러나 현재 포트는 RGB 인디케이터 길이를 제한할 때만 부분 전송을 사용하므로 리스크가 낮습니다.  
   - **적용 여부**: 적용 권장.

2. **`ws2812SetColor()`의 메모리 복사 축소**  
   - 현행 구현은 색상마다 8바이트 스택 배열을 만들고 세 번의 `memcpy()`로 `bit_buf`를 채웁니다.【F:src/hw/driver/ws2812.c†L269-L340】  
   - **제안**: (a) `bit_buf` 내 대상 포인터를 직접 순회해 분기 없이 연속 대입하거나, (b) 256개 색상 값에 대한 8비트 패턴 룩업 테이블을 도입해 스택 배열과 `memcpy`를 제거합니다.  
   - **부작용 추론**: 방법 (a)은 구현 복잡도가 올라가며, 방법 (b)은 약 2KB의 플래시 상수 테이블이 추가되어 메모리 사용량이 늘어납니다. 또한 타이밍이 엄격한 WS2812 구동 특성상 새 구현 검증에 시간이 필요합니다.  
   - **적용 여부**: 추후 성능 병목이 확인될 때 적용 검토. 현재는 유지보수 비용 대비 이득이 제한적이라고 판단합니다.

### 2.3 오버헤드 및 CPU 점유율 개선 관점
1. **DMA 대기 상태 선검사 보강**  
   - `ws2812ServicePending()`은 진입 시 `ws2812_pending`과 `ws2812_busy`를 한 번 검사한 뒤 크리티컬 섹션을 구성합니다. 이 덕분에 불필요한 인터럽트 마스크 토글은 줄었지만, `HAL_TIM_PWM_Stop_DMA()`는 전송 길이가 0일 때도 호출될 수 없도록 현재 구조가 의존합니다.【F:src/hw/driver/ws2812.c†L206-L229】  
   - **제안**: `transfer_len > 0` 체크 이전에 `HAL_TIM_PWM_Stop_DMA()` 호출을 옮기거나, DMA가 이미 정지 상태일 때 재호출을 피하기 위해 `ws2812_busy`를 이용한 가드 절을 추가합니다. 이로써 불필요한 HAL 호출을 줄이고 CPU 사이클을 절약할 수 있습니다.  
   - **부작용 추론**: HAL 구현이 DMA를 재시작하기 전에 항상 `Stop_DMA()`를 요구한다면, 가드를 잘못 설정할 경우 마지막 프레임 잔상이 남을 위험이 있습니다. 따라서 HAL 동작을 재검증해야 하며, WS2812 파형 안정성에 영향을 줄 수 있습니다.  
   - **적용 여부**: 추가 검증 전에는 보류. WS2812 타이밍 리스크가 크므로, 측정 후 문제 없을 때 적용합니다.

2. **호스트 LED 큐 즉시 적용 조건**  
   - 현재 `usbHidSetStatusLed()`는 모든 변경을 지연 적용하도록 큐잉합니다. CapsLock 토글 등 즉각적인 피드백이 필요한 경우, 이미 메인 루프에서 동일 값으로 유지 중이라면 바로 `led_port_indicator_refresh()`를 호출해 한 프레임을 앞당길 수 있습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port_host.c†L27-L60】  
   - **제안**: 동일 프레임 내에 LED 토글이 반복되는 상황을 고려해, `host_led_pending_dirty`가 비어 있고 즉시 적용이 필요한 경우에는 큐 없이 바로 `led_port_indicator_refresh()`를 호출하는 패스트 패스를 추가합니다.  
   - **부작용 추론**: 메인 루프 외부(인터럽트 컨텍스트)에서 호출될 수 있으므로, 동기화 경로를 재검토해야 합니다. 또한 현재 구조의 “루프 말미에서 일괄 처리” 원칙이 흐트러질 수 있어 DMA 호출 타이밍이 늘어날 위험이 있습니다.  
   - **적용 여부**: 현행 비동기 큐 정책을 유지하고, 사용자 체감 문제 제기가 있을 때 재검토합니다.

## 3. 결론
- WS2812 DMA 요청/서비스 경로는 메인 루프 단일 진입과 캐시 기반 갱신 전략이 정착되어 있으며, 불필요한 인터럽트 토글이나 중복 전송은 제한된 상태입니다.
- 단기적으로는 사용되지 않는 전역 플래그 제거 및 프레임 길이 캐시화로 코드 간결성과 경량화를 달성할 수 있습니다.
- 보다 공격적인 최적화(비트 패턴 룩업, HAL 호출 생략, 즉시 적용 패스)는 WS2812 파형 안정성이나 이벤트 순서를 재검증해야 하므로 추후 과제로 남기는 것이 안전합니다.