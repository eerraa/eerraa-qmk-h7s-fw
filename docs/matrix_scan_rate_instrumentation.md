# `matrix.c` Scan Rate 계측 경로 추적

## 1. 빌드 플래그 구성
- `DEBUG_MATRIX_SCAN_RATE`가 키보드 구성(`config.h`)에 정의되면 스캔 계측 코드가 컴파일되며, 미정의 시에는 관련 헬퍼가 빈 매크로로 대체됩니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/config.h†L34-L37】【F:src/ap/modules/qmk/quantum/keyboard.c†L202-L227】

## 2. 스캔 루프 계측 수집
- `matrix_scan_perf_task()`는 매트릭스 스캔마다 호출되어 카운터를 증가시키고, 1초 주기로 `timer_read32()`와 `TIMER_DIFF_32()`를 사용해 초당 스캔 횟수를 `last_matrix_scan_count`에 저장합니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L208-L223】
- 콘솔이 활성화된 빌드에서는 동일 주기로 `dprintf()`를 통해 현재 스캔 빈도를 즉시 출력할 수 있습니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L212-L216】

## 3. 매트릭스 스케줄과의 연동
- `matrix_task()`는 매 스캔 주기에 `matrix_scan_perf_task()`를 호출하여 계측이 항상 최신 스캔 횟수를 반영하도록 보장합니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L615-L633】
- `DEBUG_MATRIX_SCAN_RATE`가 비활성화된 빌드에서는 동일 위치에 비어 있는 매크로가 삽입되어 릴리스 구성에서 추가 오버헤드가 발생하지 않습니다.【F:src/ap/modules/qmk/quantum/keyboard.c†L225-L227】

## 4. CLI 및 로그 출력 경로
- `matrix_info()`는 `is_info_enable`이 활성화된 동안 1초마다 `get_matrix_scan_rate()` 값을 읽어 `Scan Rate`를 KHz 단위로 출력합니다.【F:src/ap/modules/qmk/port/matrix.c†L81-L111】
- `matrix info` CLI는 수동 실행 시 동일한 스캔 빈도와 함께 HID Poll Rate, 스캔 시간 계측 결과를 묶어서 노출합니다.【F:src/ap/modules/qmk/port/matrix.c†L115-L144】

## 5. 동작 토글 및 초기화
- `matrix info on/off` 하위 명령으로 로그를 활성화/비활성화할 수 있으며, 비활성화 시에는 계측 누산값을 `matrixInstrumentationReset()`으로 초기화합니다.【F:src/ap/modules/qmk/port/matrix.c†L159-L181】
- 따라서 `DEBUG_MATRIX_SCAN_RATE`를 유지한 현재 보드 구성에서는 릴리스 빌드에서도 CLI 토글만으로 Scan Rate 계측을 즉시 활용할 수 있습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/config.h†L34-L37】【F:src/ap/modules/qmk/port/matrix.c†L81-L181】
