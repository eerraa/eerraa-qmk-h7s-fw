# 키 입력 경로 Codex 레퍼런스 (V251001R4)

## 1. 책임 범위 및 주요 구성요소
| 레이어 | 파일/모듈 | 역할 요약 |
|--------|-----------|-----------|
| 하드웨어 수집 | `src/hw/driver/keys.c` | TIM16과 GPDMA1을 이용해 행 드라이브/열 샘플링을 자동화하고, `.non_cache` 영역에 위치한 `col_rd_buf`에 16비트 행 데이터를 축적합니다. |
| 매트릭스 HAL | `src/ap/modules/qmk/port/matrix.c` | DMA 버퍼를 직접 참조해 스캔 값을 읽고, 디바운싱/시간 로그를 수행한 뒤 QMK 매트릭스에 전달합니다. |
| QMK 상위 로직 | `src/ap/modules/qmk/quantum/keyboard.c` | `matrix_task()`에서 행 변화와 고스트를 판정하고, 확정된 이벤트를 `action_exec()` 및 `switch_events()`로 분배합니다. |
| HID 전송 | `src/ap/modules/qmk/quantum/action_util.c`, `src/ap/modules/qmk/port/protocol/host.c` | 확정된 키코드 상태를 호스트 리포트로 패킹한 뒤 `usbHidSendReport()`를 통해 USB 엔드포인트로 전송합니다. |

## 2. 하드웨어 스캔 파이프라인
- `row_wr_buf`는 6행 스캔 패턴을 담고 있으며 TIM16 CH1 DMA로 GPIOA ODR에 순차 출력됩니다. `.non_cache` 섹션에 둔 `col_rd_buf`는 GPDMA1 CH2가 GPIOB 입력을 16비트 단위로 포착합니다. (행/열 초기화 및 버퍼 정의 참조)
- `keysInit()`은 GPIO, DMA, 타이머를 초기화한 후 TIM16을 스타트하여 하드웨어 스캔이 펌웨어 메인 루프와 독립적으로 순환하도록 구성합니다.
- `keysPeekColsBuf()`는 `const volatile` 포인터를 반환해 펌웨어가 DMA 버퍼를 복사 없이 참조하면서도 최신 값을 보장하도록 했습니다.

## 3. 매트릭스 HAL 흐름 (`matrix.c`)
- `matrix_init()`은 `matrix`/`raw_matrix`를 0으로 클리어하고 디바운서를 초기화하며 CLI에 `matrix` 명령을 등록합니다.
- `matrix_scan()`은 `keysPeekColsBuf()`로 DMA 버퍼를 읽고 `raw_matrix` 갱신 여부를 판단합니다. 변화가 있으면 디바운서를 호출해 확정된 행 상태를 `matrix[]`에 반영하고, HID 시간 로그(`usbHidSetTimeLog`)에 스캔 시작 시각을 전달합니다.
- 디버그 빌드에서는 `matrix_info()`가 1초 주기로 스캔 주파수와 USB 폴링 진단치를 로그합니다.

## 4. QMK `matrix_task()` 처리 흐름
1. `matrix_scan()` 결과 또는 이전 고스트 플래그(`ghost_pending`) 중 하나라도 true이면 행 비교 루프에 진입합니다.
2. 행별로 XOR 결과(`row_changes`)가 0이면 건너뛰고, 변화가 있으면 최초 한 번만 `sync_timer_read32()`로 32비트 타임스탬프를 취득해 이벤트 구조체에 재사용합니다.
3. `has_ghost_in_row()`가 true이면 행을 스킵하고 `ghost_pending`을 유지하여 다음 스캔에서 다시 확인합니다.
4. 실제 키 이벤트가 확정되면 `should_process_keypress()`를 호출한 뒤, 변화한 비트만 순회(`__builtin_ctz` + `pending &= pending - 1`)하면서 `action_exec()`와 `switch_events()`를 호출합니다.
5. 루프 종료 후 행 캐시(`matrix_previous`)를 최신 상태로 갱신하고, 이벤트가 있었다면 `pending_matrix_activity_time`을 업데이트하여 이후 전역 활동 타임스탬프에 반영합니다.

## 5. 고스트 필터링과 캐시 전략
- `mark_all_real_key_masks_dirty()`와 `refresh_real_key_mask()`는 키맵에 정의된 물리 키만을 비트 마스크로 캐시해, 고스트 판정 시 전체 열 순회를 반복하지 않도록 합니다.
- `get_cached_real_keys()`는 스캔 세대(epoch)를 추적해 동일 스캔 내 중복 필터링을 제거합니다.
- `has_ghost_in_row()`는 (1) 물리 행 데이터에 2키 이상이 있는지 빠르게 검사하고, (2) 동일 열을 공유하는 다른 행의 실제 키 마스크와 교차시켜 진짜 고스트만 걸러냅니다. 조건을 통과한 행만 `matrix_task()`에서 이벤트로 취급됩니다.

## 6. HID 보고서 전송 경로
1. `action_exec()`는 Quantum 처리 체인을 거쳐 `send_keyboard_report()`를 호출합니다.
2. `send_keyboard_report()`는 NKRO 설정을 확인해 6KRO 또는 NKRO 버퍼를 선택하고, 마지막으로 전송된 리포트와 비교 후 변경이 있을 때만 전송을 요청합니다.
3. `host_keyboard_send()`는 USB 출력 경로를 결정하고(블루투스 우선순위 포함), `usbHidSendReport()`로 실제 HID 엔드포인트에 보고서를 전송합니다.

## 7. 진단 및 지원 도구
- `matrix` CLI: `info`, `info on/off`, `row <value>` 명령으로 디버그 로그 제어와 행 데이터 강제 입력을 지원합니다.
- `keys` CLI: DMA 기반 행/열 상태를 직접 덤프하여 하드웨어 배선을 확인할 수 있습니다.
- 디버그 매크로 `DEBUG_MATRIX_SCAN_RATE`를 활성화하면 `matrix_scan_perf_task()`가 초당 스캔 횟수를 계산해 콘솔로 출력합니다.

## 8. 타임라인 및 변경 이력 참고
- V250924R5: DMA 버퍼 직접 참조 전환 (`keysPeekColsBuf`, `matrix_scan()`).
- V250924R6~R8: `matrix_task()` 행 루프 단축, 열 비트 스캔, 고스트 조기 종료 최적화.
- V250928R1~R3: 고스트 판정 시 키맵 순회 최소화 및 마스크 캐싱.
- V251001R3~R4: 키 이벤트 타임스탬프 공유 및 고스트 캐시 세대 도입으로 timer/sync 접근을 단일화.
