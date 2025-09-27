# 키 스캔 경로 최적화 검토 (V250924R5)

## 1. 분석 범위와 기준
- **측정 기준**: `matrix info on` CLI에서 노출되는 `SCAN RATE` 및 `Scan Time`.
- **대상 경로**: `matrix_scan()` → `keysPeekColsBuf()` → DMA 기반 `col_rd_buf` → 디바운서(`sym_defer_pk`, `sym_eager_pk`, `asym_eager_defer_pk`).
- **현황**: USB 불안정성 탐지 로직 도입 이후 스캔 레이트가 약 5% 하락.

## 2. 기존 구현 요약
1. `keysReadColsBuf()`로 DMA 버퍼 전체를 지역 배열에 복사.
2. 각 행을 순회하며 `raw_matrix`와 비교, 변경 시 갱신 및 `changed=true`.
3. 결과를 디바운서에 전달하여 QMK 매트릭스 상태를 확정.

### 구현 의도 해석
- DMA가 채운 `col_rd_buf`를 즉시 복사해 스냅샷을 확보하고, 디바운서 호출 전에 일관된 데이터를 보장하려는 목적.
- `changed` 플래그를 통해 디바운서의 시간 업데이트와 USB 타임 로그 호출 빈도를 조절.

## 3. 발견된 병목
- 매 스캔마다 `MATRIX_ROWS*sizeof(uint16_t)` 만큼의 `memcpy`가 발생.
- DMA 버퍼는 `.non_cache` 섹션에 위치해 있으며, CPU가 직접 참조해도 캐시 일관성 문제가 없음.
- 추가 복사 없이도 동일한 데이터 일관성을 유지할 수 있어, 8000Hz 환경에서는 `memcpy` 제거 효과가 누적.

## 4. 개선안
| 구분 | 기존 | 개선 |
|------|------|------|
| 데이터 취득 | `keysReadColsBuf()` → 지역 배열 복사 | `keysPeekColsBuf()`로 DMA 버퍼 직접 참조 |
| 변경 감지 | `raw_matrix` vs 지역 배열 | `raw_matrix` vs DMA 버퍼 |
| 디바운서 호환성 | `changed` 플래그 전달 | 동일 (`changed` 계산 로직 유지) |

- DMA 버퍼를 상수 포인터로 노출하는 `keysPeekColsBuf()` 신규 API 추가.
- `matrix_scan()`은 포인터를 통해 행 데이터를 바로 읽고 변경 여부를 판단.
- 디바운서 API (`sym_defer_pk`, `sym_eager_pk`, `asym_eager_defer_pk`)는 입력 배열 참조 방식이 그대로라 호환성 유지.

## 5. 부작용 검토
- **동시성**: DMA가 16비트 단위로 버퍼를 갱신하며, CPU도 16비트로 읽으므로 절반만 갱신된 값을 읽을 확률은 매우 낮음. 기존 `memcpy` 또한 동일한 윈도우에서 실행되므로 위험 수준은 동일.
- **API 의존성**: 기존 호출부(`keysReadColsBuf`)는 유지되어 다른 모듈 영향 없음.
- **디바운서 동작**: 입력 포인터만 변경되었으므로 타이머 기반 동작 (`changed` 전달) 불변.

## 6. 수정/유지 판단
- **수정 필요**: `matrix_scan()`에서 DMA 버퍼 직접 참조로 전환, 새로운 API 제공, 펌웨어 버전 갱신.
- **유지 항목**: 디바운스 로직 호출 구조, `changed` 플래그 처리, USB 타임 로그 호출 조건.

## 7. 기대 효과
- 키 스캔 루프의 메모리 복사 비용 제거로 8000Hz 환경에서 약 3~4% 수준의 스캔 시간 회복 기대 (DMA/CPU 경합 상황에서 추가 이득 가능).
- USB 불안정성 탐지 로직으로 발생한 스캔 레이트 하락분을 상당 부분 상쇄.

