# DEV_MONITOR CODEX 레퍼런스

Codex가 USB 불안정성 탐지 로직을 빠르게 파악하도록 **핵심 심볼 · 호출 흐름 · 파라미터 테이블**만 남기고 정리했습니다. 필요 시 각 섹션을 독립적으로 참조할 수 있도록 구성했습니다.

---

## 1. 빠른 레이더(파일 ↔ 책임)
| 파일 | 주요 심볼 | 역할 요약 |
| --- | --- | --- |
| `src/hw/driver/usb/usb_hid/usbd_hid.c` | `usbHidMonitorSof`, `usbHidSofMonitorPrime`, `usbHidSofMonitorApplySpeedParams` | SOF ISR, 초기화, 속도 캐시 및 점수 계산.
| `src/hw/driver/usb/usb.c` | `usbRequestBootModeDowngrade`, `usbProcess`, `usbBootModeSave` | 다운그레이드 큐, 확인 지연, BootMode 저장.
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
  3. 간격 초과 → 점수 가산(`clamp(0~4)`), 감쇠 타이머에 따라 1점 감소.
  4. `score >= degrade_threshold` → 다운그레이드 큐에 요청하며 누락 프레임 수를 함께 캐시.

### 2.2 `usb_boot_mode_request_t` (다운그레이드 큐)
- **필드**: `stage`(IDLE→ARMED→COMMIT), `next_mode`, `delta_us`, `expected_us`, `missed_frames`, `ready_ms`, `timeout_ms`, `log_pending`.
- **동작 요약**
  - `ARMED`: 첫 로그 출력 후 `USB_BOOT_MONITOR_CONFIRM_DELAY_MS` 대기.
  - `COMMIT`: BootMode 저장 성공 시 리부트 → `usbHidSofMonitorPrime()` 재호출.
  - 실패 경로는 경고 로그 출력 후 큐 리셋.
  - `usbProcess()`는 `stage == IDLE`일 때 즉시 반환.

---

## 3. 파라미터 레퍼런스 (`sof_monitor_params[]`)
| USB 속도 | `expected_us` | `stable_threshold_us` | `decay_interval_us` | `degrade_threshold` | 워밍업 필요 프레임 |
| --- | --- | --- | --- | --- | --- |
| HS (8k/4k/2k 공통) | 125 | 250 | 4000 | 12 | 2048 |
| FS (1k) | 1000 | 2000 | 20000 | 6 | 128 |
| `USB_SPEED_UNKNOWN`/서스펜드 | 0 | 0 | 0 | - | 즉시 리셋 |

속도 변경은 항상 `usbHidSofMonitorApplySpeedParams()`를 거치며, 파라미터 재적용 시 점수·워밍업 카운터가 초기화됩니다.

---

## 4. 호출 흐름 지도
```
USBD_HID_SOF_ISR
  └─ usbHidMonitorSof(now_us)
        ├─ usbHidUpdateWakeUp()
        ├─ usbHidSofMonitorApplySpeedParams(dev_speed?)
        └─ usbRequestBootModeDowngrade(..., missed_frames, ...)  // 임계 초과 시 누락 프레임 전달

main loop (ap.c)
  └─ usbProcess()
        └─ usbBootModeSave()  // 큐 COMMIT 시

usb suspend/resume/reset
  └─ usbHidSofMonitorPrime(now, holdoff, warmup, speed)
```

- ISR에서는 `usbHidSofMonitorSyncTick()`으로 타임스탬프를 갱신한 뒤, 홀드오프 → 워밍업 → 안정 감시 → 임계 판단 순으로 진행합니다.
- `usbHidUpdateWakeUp()`이 서스펜드를 감지하면 점수·타임스탬프를 리셋하고 즉시 반환합니다.
- 다운그레이드 요청은 큐가 처리하며, ARM → COMMIT 단계에서 BootMode 저장과 리셋을 담당합니다.

---

## 5. ISR 로직 요약 의사 코드
```
usbHidMonitorSof(now):
  if (usbHidUpdateWakeUp()) return

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

  delta_frames = clamp((interval - expected_us) / expected_us, 0, 4)
  score = min(score + delta_frames, SCORE_CAP)
  missed_frames = delta_frames + 1

  if (now - last_decay >= decay_interval)
    score = max(score - 1, 0)

  if (score >= degrade_threshold)
    usbRequestBootModeDowngrade(next_mode, delta_us, expected_us, missed_frames)
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
- CLI 확장은 `log_pending` 플래그 흐름을 활용해 중복 로그를 방지합니다.

---

이 문서는 Codex가 `DEV_MONITOR` 구성요소의 흐름을 빠르게 복기하고 수정 지점을 정확히 찾을 수 있도록 설계된 참조 자료입니다.
