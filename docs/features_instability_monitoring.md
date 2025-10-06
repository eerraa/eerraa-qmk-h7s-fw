# USB 불안정성 모니터 Codex 레퍼런스 (V251009R9)

## 1. 파일 개요
- **핵심 파일**
  - `src/hw/driver/usb/usb_hid/usbd_hid.c`: SOF 감시, 점수 기반 패널티 누적, 다운그레이드 큐 요청 트리거. `usbHidMonitorSof()`가 모든 감시 분기를 담당합니다.
  - `src/hw/driver/usb/usb.c`: 부트 모드 캐시와 `usbRequestBootModeDowngrade()` 큐, `usbProcess()` 서비스 루프를 통해 다운그레이드 요청을 확정합니다.
  - `src/hw/driver/usb/usb.h`: 부트 모드 열거형과 기대 간격 계산기(`usbCalcMissedFrames()`), 모니터 확인 지연 상수를 정의합니다.
- **호출 흐름**
  1. `usbHidMeasurePollRate()` → `usbHidMonitorSof(now_us)`에서 SOF 간격을 분석합니다.
  2. 이상이 감지되면 `usbRequestBootModeDowngrade()`로 큐를 세팅합니다.
  3. 메인 루프에서 `usbProcess()`가 큐 상태를 보고 로그/다운그레이드를 수행합니다.

## 2. 주요 상수 및 타이밍 규격
| 구분 | 심볼 | 값/정의 | 역할 | 위치 |
| --- | --- | --- | --- | --- |
| 구성 후 홀드오프 | `USB_SOF_MONITOR_CONFIG_HOLDOFF_US` | 750ms → 750,000us | 재구성 직후 워밍업 동안 점수 누적을 중지 | `usbd_hid.c` L489-L503 |
| 워밍업 최대 시간 | `USB_SOF_MONITOR_WARMUP_TIMEOUT_US` | 구성 홀드오프 + 2s | 안정 프레임 확보 실패 시 타임아웃 | `usbd_hid.c` L489-L503 |
| 다운그레이드 지연 | `USB_BOOT_MONITOR_CONFIRM_DELAY_MS/US` | 2000ms / 2,000,000us | 큐 ARM→COMMIT 사이 확인 대기 | `usb.h` L55-L61, `usbd_hid.c` L498-L502 |
| 안정 임계 배율 | `USB_SOF_MONITOR_STABLE_MARGIN_SHIFT` | 1 (×2) | 기대 간격 대비 허용 마진 | `usbd_hid.c` L499-L503 |
| 감쇠 주기 | `decay_interval_us` | HS: 4000us, FS: 20000us | 정상 프레임 지속 시 점수를 1씩 감소 | `usbd_hid.c` L530-L552 |
| 다운그레이드 임계 | `degrade_threshold` | HS: 12, FS: 6 | 점수가 임계 이상이면 큐 요청 | `usbd_hid.c` L530-L552 |

## 3. 핵심 데이터 구조 요약
| 구조체 | 주요 필드 | 설명 | 정의 |
| --- | --- | --- | --- |
| `usb_sof_monitor_t` | `prev_tick_us`, `score`, `holdoff_end_us`, `warmup_*`, `degrade_threshold`, `suspended_active` | SOF 간격, 점수, 홀드오프·워밍업 상태, 서스펜드 플래그를 한 번에 관리합니다. | `usbd_hid.c` L505-L529 |
| `usb_sof_monitor_params_t` | `speed_code`, `expected_us`, `decay_interval_us`, `degrade_threshold`, `warmup_target_frames` | 속도(HS/FS)별 파라미터 테이블. Prime 시점에 모니터 구조체로 복사됩니다. | `usbd_hid.c` L521-L538 |
| `usb_boot_mode_request_t` | `stage`, `log_pending`, `next_mode`, `delta_us`, `expected_us`, `missed_frames`, `ready_ms`, `timeout_ms` | 다운그레이드 큐 상태 머신. ARM 단계에서 로그, COMMIT 단계에서 저장/리셋을 수행합니다. | `usb.c` L42-L86 |

## 4. SOF 감시 파이프라인
### 4.1 상태 전이
- `usbHidMonitorSof()`는 USB 장치 상태와 속도를 매 호출마다 읽고, 상태 변화 시 `usbHidSofMonitorPrime()`으로 초기화를 수행합니다. 구성/속도 변경, 서스펜드 진입·복귀를 모두 Prime 루틴으로 통합했습니다. `usbd_hid.c` L1362-L1434.
- Prime은 타임스탬프, 점수, 워밍업/홀드오프 데드라인을 리셋하고 필요 시 속도 파라미터 캐시(`usbHidSofMonitorApplySpeedParams()`)를 재적용합니다. `usbd_hid.c` L530-L567, L576-L618.

### 4.2 워밍업 및 홀드오프 처리
- 구성 직후/속도 변경/다운그레이드 홀드오프 중에는 `holdoff_end_us` 비교로 조기 반환하며 점수 계산을 건너뜁니다. `usbd_hid.c` L1436-L1462.
- 워밍업 완료 전에는 안정 임계(`stable_threshold_us`) 이하 프레임을 누적해 `warmup_good_frames`를 올리고, 타임아웃 또는 목표 프레임 도달 시에만 본 감시 로직이 활성화됩니다. `usbd_hid.c` L1464-L1513.

### 4.3 점수 감쇠와 패널티 누적
- 워밍업 완료 후 정상 프레임이 지속되면 `decay_interval_us` 마다 점수를 1씩 감쇠합니다. `usbd_hid.c` L1515-L1538.
- 안정 임계 초과 시 `usbCalcMissedFrames()`로 누락 프레임 수를 산출하고, 패널티를 0~7 범위로 포화하여 `score`에 누적합니다. 임계 이상이면 즉시 다운그레이드 트리거가 참으로 유지됩니다. `usbd_hid.c` L1540-L1574, `usb.h` L66-L80.

### 4.4 다운그레이드 큐 요청
- 트리거가 발생하면 `usbHidResolveDowngradeTarget()`으로 다음 모드를 계산하고, `usbRequestBootModeDowngrade()`를 호출합니다. 큐 ARM/CONFIRM 결과에 따라 홀드오프를 2초로 연장하여 중복 요청을 차단합니다. `usbd_hid.c` L1574-L1604, `usb.c` L214-L268.

## 5. 다운그레이드 큐와 서비스 루프
- `usbRequestBootModeDowngrade()`는 요청 모드·측정치 유효성을 검사한 뒤, FSM(`IDLE → ARMED → COMMIT`) 단계별로 타임스탬프와 로그 플래그를 설정합니다. `usb.c` L214-L268.
- `usbProcess()`는 메인 루프에서 FSM을 소비합니다.
  - ARMED: 최초 로그 출력 후 타임아웃(`timeout_ms`) 도달 시 요청을 취소합니다.
  - COMMIT: 로그를 남기고 `usbBootModeSaveAndReset()`으로 모드를 저장한 뒤 리셋을 트리거합니다.
  `usb.c` L270-L323.

## 6. 부트 모드 캐시와 기대 간격
- 부트 모드별 기대 간격/HS `bInterval`은 테이블과 캐시(`usb_boot_mode_*_cache`)로 관리되어 ISR에서 분기 없이 접근할 수 있습니다. `usb.c` L18-L70.
- 공개 API:
  - `usbBootModeGetHsInterval()` → HS `bInterval` 캐시. `usb.c` L111-L121.
  - `usbBootModeGetExpectedIntervalUs()` → 기대 SOF 간격 캐시. `usb.c` L123-L129.
  - `usbCalcMissedFrames(expected, delta)` → 속도별 상수 나눗셈으로 누락 프레임을 계산. `usb.h` L66-L80.

## 7. 확장 체크리스트
1. **새 속도 정책 추가**: `usb_sof_monitor_params_t` 테이블과 `USB_BOOT_MODE_*` 열거형, 캐시 테이블(`usb_boot_mode_*_table`)을 동시에 업데이트합니다.
2. **임계값 조정 시**: `USB_SOF_MONITOR_STABLE_MARGIN_SHIFT`, `degrade_threshold`, `decay_interval_us`의 상호 영향(점수 감쇠 속도)을 검토합니다.
3. **다운그레이드 FSM 변경**: `usbRequestBootModeDowngrade()`와 `usbProcess()`가 동일한 단계 정의를 공유하므로 두 파일 모두에서 stage 전환과 타임아웃을 수정해야 합니다.
4. **로그 포맷 유지**: 큐 ARM/COMMIT 로그는 사용자 디버깅 기준이므로 메시지 변경 시 상위 호스트 스크립트 영향을 확인합니다. `usb.c` L284-L321.
5. **타이머 의존성**: SOF 감시는 `micros()`/`millis()` 기반으로 동작하므로 HAL 타이머 구성(`usbHidInitTimer()`) 변경 시 래핑 보정 유틸리티(`usbHidTimeIsBefore/AfterOrEqual`) 재검토가 필요합니다. `usbd_hid.c` L618-L640, L1520-L1604.
