# DEV_MONITOR CODEX 레퍼런스

Codex가 USB 불안정성 탐지 로직을 빠르게 파악하도록 **핵심 심볼 · 호출 흐름 · 파라미터 테이블**만 남기고 정리했습니다. 필요 시 각 섹션을 독립적으로 참조할 수 있도록 구성했습니다.

---

## 1. 빠른 레이더(파일 ↔ 책임)
| 파일 | 주요 심볼 | 역할 요약 |
| --- | --- | --- |
| `src/hw/driver/usb/usb_hid/usbd_hid.c` | `usbHidMonitorSof`, `usbHidSofMonitorPrime`, `usbHidSofMonitorApplySpeedParams` | SOF ISR, 초기화, 속도 캐시 및 점수 계산.
| `src/hw/driver/usb/usb.c` | `usbRequestBootModeDowngrade`, `usbProcess`, `usbCalcMissedFrames` | 다운그레이드 큐, 누락 프레임 계산 도우미, BootMode 저장.
| `src/hw/driver/usb/usbd_conf.c` | `usbBootModeIsFullSpeed` | PHY 속도 강제, `pdev->dev_speed` 상태 전달.
| `src/ap/ap.c` | `usbProcess` 호출 | 메인 루프에서 큐 서비스.
| `src/hw/hw.c`, `src/hw/hw_def.h` | `usbBootModeLoad`, `_DEF_FIRMWATRE_VERSION` | 부팅 시 BootMode 복원, 펌웨어 버전 태깅.

---

## 2. 데이터 구조 스냅샷
### 2.1 `usb_sof_monitor_t` (SOF 모니터)
- **주요 필드**: `score`, `holdoff_end_us`, `warmup_deadline_us`, `warmup_good_frames`, `expected_us`, `stable_threshold_us`, `decay_interval_us`, `degrade_threshold`, `active_speed`, `warmup_complete`, `suspended_active`.
- **핵심 전이**
  1. 속도/상태 변화 → `usbHidSofMonitorPrime()` 호출로 캐시 리셋.
  2. 워밍업 조건 달성(HS 2048·FS 128 프레임) → 감시 활성화.
  - 워밍업 마감 비교는 `warmup_deadline` 로컬 캐시를 사용해 ISR 메모리 접근을 줄인다. *(V251005R1)*
- 워밍업 카운터는 값이 변할 때만 구조체에 기록해 동일 값 반복 쓰기를 피한다. *(V251005R2)*
- Prime 경량화 이후 속도 파라미터는 `usbHidSofMonitorApplySpeedParams()`에서 직접 채워져, 상태 전환 시 불필요한 0 초기화가 사라졌다. *(V251005R6)*
- 기대 간격·안정 임계·감쇠 주기는 16비트로 저장되어 ISR에서의 로드/스토어 폭이 줄었다. *(V251005R7)*
- 동일 속도로 Prime이 반복될 때는 캐시된 속도 파라미터를 재사용해 추가 메모리 쓰기를 방지한다. *(V251005R8)*
- 비구성 상태에서는 `score`가 0일 때 구조체 쓰기를 생략해 반복 초기화를 줄인다. *(V251005R9)*
- 홀드오프·비구성 구간에서 점수가 0이면 `usbHidSofMonitorSyncTick()`이 `last_decay_us`를 건드리지 않아 불필요한 쓰기를 제거한다. *(V251006R1)*
  3. 간격 초과 → 누락 프레임을 8비트 패널티로 환산하고 `score + penalty` 비교로 점수를 누적하거나 즉시 다운그레이드. *(V251005R8)*
  4. `score >= degrade_threshold` → 다운그레이드 큐에 요청하며 누락 프레임 수를 함께 캐시.

### 2.2 `usb_boot_mode_request_t` (다운그레이드 큐)
- **필드**: `stage`(IDLE→ARMED→COMMIT), `next_mode`, `delta_us`, `expected_us`, `missed_frames`, `ready_ms`, `timeout_ms`, `log_pending`.
- **동작 요약**
  - `ARMED`: 첫 로그 출력 후 `USB_BOOT_MONITOR_CONFIRM_DELAY_MS` 대기.
  - `COMMIT`: BootMode 저장 성공 시 리부트 → `usbHidSofMonitorPrime()` 재호출.
  - 실패 경로는 경고 로그 출력 후 큐 리셋.
  - `usbProcess()`는 Stage 값을 로컬에 캐시하고, `ARMED` 단계에서만 `millis()`를 호출해 메인 루프 오버헤드를 줄인다. *(V251005R1)*
  - `usbProcess()`는 `stage == IDLE`일 때 즉시 반환.
  - `missed_frames` 캐시는 ISR에서 전달된 값을 유지하며, 로그 경로도 동일한 값을 재사용한다. *(V251005R3)*
  - 누락 프레임 재계산 시 `usbCalcMissedFrames()`를 사용해 ISR과 동일한 상수 분기 경로를 공유한다. *(V251005R6)*
  - 기대 간격·누락 프레임 캐시는 16비트로 저장되며, ISR이 포화한 값을 그대로 받아 추가 연산 없이 유지한다. *(V251005R9)*

---

## 3. 파라미터 레퍼런스 (`sof_monitor_params[]`)
| USB 속도 | `expected_us` | `stable_threshold_us` | `decay_interval_us` | `degrade_threshold` | 워밍업 필요 프레임 |
| --- | --- | --- | --- | --- | --- |
| HS (8k/4k/2k 공통) | 125 | 250 | 4000 | 12 | 2048 |
| FS (1k) | 1000 | 2000 | 20000 | 6 | 128 |
| `USB_SPEED_UNKNOWN`/서스펜드 | 0 | 0 | 0 | - | 즉시 리셋 |

속도 변경은 항상 `usbHidSofMonitorApplySpeedParams()`를 거치며, Prime이 캐시를 재사용하더라도 점수·워밍업 카운터는 초기화됩니다. *(V251005R8)*

---

## 4. 호출 흐름 지도
```
USBD_HID_SOF_ISR
  └─ usbHidMonitorSof(now_us)
        ├─ usbHidUpdateWakeUp()
        ├─ usbHidSofMonitorApplySpeedParams(dev_speed?)
        └─ usbRequestBootModeDowngrade(..., missed_frames_report, ...)  // ISR 16비트 포화 값을 큐로 전달

main loop (ap.c)
  └─ usbProcess()
        └─ usbBootModeSave()  // 큐 COMMIT 시

usb suspend/resume/reset
  └─ usbHidSofMonitorPrime(now, holdoff, warmup, speed)
```

- ISR에서는 `usbHidSofMonitorSyncTick()`으로 타임스탬프를 갱신한 뒤, 홀드오프 → 워밍업 → 안정 감시 → 임계 판단 순으로 진행합니다.
- `usbHidUpdateWakeUp()`이 서스펜드를 감지하면 점수·타임스탬프를 리셋하고 즉시 반환합니다.
- Prime은 동일 속도 재호출 시 캐시된 파라미터를 그대로 사용해 구조체 쓰기를 최소화합니다. *(V251005R8)*
- 다운그레이드 요청은 큐가 처리하며, ARM → COMMIT 단계에서 BootMode 저장과 리셋을 담당합니다.
- SOF 타임스탬프 비교 보조 함수(`usbHidTimeIsBefore`, `usbHidTimeIsAfterOrEqual`)는 인라인화되어 ISR 호출 비용을 줄였습니다. *(V251005R2)*
- 다운그레이드 타깃은 Enum 순차 증가 방식으로 계산되어 switch 분기가 제거되었습니다. *(V251005R7)*
- 안정 감시 단계에서만 `expected_us`를 읽어 워밍업 및 정상 프레임에서는 구조체 접근이 발생하지 않습니다. *(V251006R1)*

---

## 5. ISR 로직 요약 의사 코드
```
usbHidMonitorSof(now):
  if (usbHidUpdateWakeUp()) return

  if (pdev->dev_state != CONFIGURED):
    usbHidSofMonitorSyncTick(now)
    if (score != 0) score = 0                             // V251005R9 구성 해제 반복 시 불필요한 쓰기 제거
    return

  dev_speed = pdev->dev_speed
  if (dev_speed != monitor.active_speed)
    usbHidSofMonitorApplySpeedParams(dev_speed)

  usbHidSofMonitorSyncTick(now)
  if (now < holdoff_end) return

  interval = now - prev_tick
  if (!monitor.warmed_up):
    if (interval <= stable_threshold) warmup_good_frames++
    if (warmup_good_frames >= warmup_target) monitor.warmed_up = true
    return

  missed_frames = usbCalcMissedFrames(expected_us, interval)   // V251005R6 상수 분기 기반 누락 프레임 계산 공유 (안정 감시 단계에서만 expected_us 사용, V251006R1)
  missed_frames_report = clamp16(missed_frames)                // V251005R9 큐 전달용 16비트 포화 값 준비
  penalty = clamp(missed_frames - 1, 0, SCORE_CAP)
  next_score = score + penalty                                // V251005R8 8비트 덧셈으로 누락 패널티 누적
  if (score >= degrade_threshold or next_score >= degrade_threshold)
    trigger_downgrade = true                                   // V251005R8 단일 비교 기반 다운그레이드 판정
  else
    score = next_score

  if ((now - last_decay) >= decay_interval)
    score = max(score - 1, 0)

  if (trigger_downgrade)
    usbRequestBootModeDowngrade(next_mode, delta_us, expected_us, missed_frames_report)
```

---

## 6. 상황별 체크포인트
| 시나리오 | 필수 확인 함수 | 기대 상태 |
| --- | --- | --- |
| USB Configure/Reset | `usbHidSofMonitorPrime()` | `active_speed`가 최신 `pdev->dev_speed`로 초기화, 점수 0.
| 서스펜드 진입 | `usbHidUpdateWakeUp()` | `USB_HID_MONITOR_FLAG_SUSPENDED` 세트, 다음 SOF 즉시 반환.
| Resume 이후 첫 SOF | `usbHidMonitorSof()` | 워밍업부터 다시 누적.
| 다운그레이드 ARM | `usbRequestBootModeDowngrade()` | `ready_ms`가 현재 시간 + 확인 지연으로 세팅, `missed_frames` 캐시 유지.
| COMMIT 실패 | `usbProcess()` | 경고 로그 출력, 큐 초기화.

---

## 7. 유지보수 팁
- 감쇠/임계값 변경 시 `USB_SOF_MONITOR_SCORE_CAP`과 `USB_BOOT_MONITOR_CONFIRM_DELAY_MS`를 함께 검토합니다.
- 신규 BootMode를 추가하면 `usbHidResolveDowngradeTarget()`과 큐 초기화 루틴에서 모드 전환 테이블을 업데이트합니다.
- ISR에 추가 로직을 넣을 경우 `usbHidMonitorSof()`의 조기 반환 경로(서스펜드, 홀드오프, 워밍업)를 유지해 125µs 예산을 보호하십시오.
- 감쇠 타이머 비교는 `elapsed = now - last_decay` 형태의 unsigned 차분으로 동작하므로, `last_decay_us`를 갱신할 때 동일한 연산 형태를 유지해 래핑 안정성을 보장하십시오. *(V251005R5)*
- CLI 확장은 `log_pending` 플래그 흐름을 활용해 중복 로그를 방지합니다.

---

이 문서는 Codex가 `DEV_MONITOR` 구성요소의 흐름을 빠르게 복기하고 수정 지점을 정확히 찾을 수 있도록 설계된 참조 자료입니다.
