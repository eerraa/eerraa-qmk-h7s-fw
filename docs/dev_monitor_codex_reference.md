# DEV_MONITOR CODEX 레퍼런스

## 1. 브랜치 개요
- **기반 브랜치**: `DEV_MONITOR`
- **주요 목적**: USB SOF 안정성 모니터, 단계적 폴링 속도 다운그레이드 큐, BootMode 영속화 기반을 통합해 HS 8kHz 기본 정책을 안전하게 유지합니다.
- **핵심 소비자**: `usb_hid/usbd_hid.c`(SOF 모니터링, 점수 감쇠, 다운그레이드 결정), `usb.c`(비동기 큐와 CLI), `ap.c`(서비스 루프), `hw.c`/`hw_def.h`(초기화와 버전 태깅), `qmk/port/port.h`(EEPROM 슬롯).

## 2. 영향 범위 매트릭스
| 경로 | 주요 심볼/루틴 | 코드 영향 요약 |
| --- | --- | --- |
| `src/ap/modules/qmk/port/port.h` | `EECONFIG_USER_BOOTMODE` | BootMode 선택값을 사용자 EEPROM에 저장할 슬롯을 예약합니다.
| `src/hw/hw.c` | `usbBootModeLoad()` | 부팅 직후 저장된 BootMode를 로드해 SOF 모니터 및 큐가 올바른 초기 상태로 시작하게 합니다.
| `src/hw/driver/usb/usb.[ch]` | `usbRequestBootModeDowngrade`, `usbProcess()`, `cliBoot` | 다운그레이드 요청 큐 상태 머신(ARMED/COMMIT), 로그, CLI 명령, 재부팅 루틴을 제공합니다.
| `src/hw/driver/usb/usb_hid/usbd_hid.c` | `usbHidMonitorSof()`, `usbHidSofMonitorApplySpeedParams()` | 마이크로프레임 간격 기반 점수 계산, 워밍업/홀드오프/감쇠 정책, 속도별 파라미터 캐싱을 담당합니다.
| `src/hw/driver/usb/usb_cmp/usbd_cmp.c` | HS/FS `bInterval` 유지 | 다운그레이드 시에도 안정적인 패킷 크기를 보장합니다.
| `src/hw/driver/usb/usbd_conf.c` | `usbBootModeIsFullSpeed()` 분기 | HS PHY에서 FS 1kHz 강제 모드를 선택할 수 있습니다.
| `src/hw/hw_def.h` | `_DEF_FIRMWATRE_VERSION` | 모니터 기능 릴리스를 `V250924R4`까지 태깅합니다.
| `src/ap/ap.c` | `usbProcess()` 호출 | 메인 루프에서 큐 상태 머신을 주기적으로 서비스합니다.

## 3. 상태 머신 & 데이터 구조 스냅샷
### 3.1 SOF 모니터(`usb_sof_monitor_t`)
- 필드 핵심: `score`, `holdoff_end_us`, `warmup_deadline_us`, `warmup_good_frames`, `expected_us`, `stable_threshold_us`, `decay_interval_us`, `degrade_threshold`, `active_speed`.
- 전이 이벤트:
  1. **상태 변화 감지**: USB 장치 상태/속도가 바뀌면 캐시를 초기화하고 워밍업 타이머를 재시작합니다.
  2. **워밍업**: HS 2048프레임 또는 FS 128프레임이 정상 범위(`stable_threshold_us`) 안에서 누적되면 모니터링을 본격 시작합니다.
  3. **점수 가산**: 기대 간격(`expected_us`) 대비 초과 시 누락 프레임 수를 환산해 최대 4점까지 가산, 감쇠 주기(`decay_interval_us`)마다 1점씩 감소합니다.
  4. **임계 도달**: `degrade_threshold` 이상이면 다음 단계 부트 모드를 산출하고 큐에 다운그레이드를 요청합니다.

### 3.2 다운그레이드 큐(`usb_boot_mode_request_t`)
- 필드 핵심: `stage`(IDLE→ARMED→COMMIT), `next_mode`, `delta_us`, `expected_us`, `ready_ms`, `timeout_ms`, `log_pending`.
- `ARMED` 단계에서 첫 로그를 출력하고 2초 확인 지연(`USB_BOOT_MONITOR_CONFIRM_DELAY_MS`)을 기다립니다.
- 확인 지연이 경과하면 `COMMIT`으로 승격되어 저장/리셋을 수행하고, 실패 시 `[!] USB Poll 모드 저장 실패` 로그 후 큐를 초기화합니다.
- `usbProcess()`는 IDLE 상태에서 반환하여 메인 루프 오버헤드를 최소화합니다.

## 4. 속도별 파라미터 레퍼런스
| USB 속도 | `expected_us` | `stable_threshold_us` | `decay_interval_us` | `degrade_threshold` | 워밍업 프레임 |
| --- | --- | --- | --- | --- | --- |
| HS (8k/4k/2k 공통) | 125 | 250 | 2000 | 3 | 2048 |
| FS (1k) | 1000 | 2000 | 8000 | 3 | 128 |
| 비구성/정지 | 0 | 0 | 0 | - | - (즉시 리셋) |
- 모든 속도 전환은 `usbHidSofMonitorApplySpeedParams()`에서 처리하며, 속도가 바뀌면 워밍업 타이머와 점수가 초기화됩니다.

## 5. 버전 히스토리
### V250923R1 — BootMode 영속화 기반 마련
- `port.h`에 BootMode 슬롯을 추가하고 `hw.c` 초기화 루틴에서 `usbBootModeLoad()`를 호출해 저장된 모드를 즉시 반영합니다.
- `usb.[ch]`에 BootMode 열거형, CLI `boot info/set`, 저장/로드 API, 진단 로그를 추가합니다.
- `usb_hid/usbd_hid.c`가 HID/VIA/EXK 엔드포인트의 `bInterval`을 모드별로 설정하고 전환 로그를 남깁니다.
- `usbd_conf.c`가 FS 강제 시나리오에서 HS PHY 설정을 적절히 조정합니다.

### V250923R2 — Composite 전송 용량 보강
- `usb_cmp/usbd_cmp.c`에서 모드 전환 중에도 64B 패킷과 HS 3-트랜잭션 구성을 유지해 입력 손실을 방지합니다.

### V250924R1 — 폴링 계측 정합
- `usbHidMeasurePollRate()`가 BootMode를 참조해 샘플 윈도우(8000 vs 1000)를 자동 선택합니다.

### V250924R2 — SOF 감시 & 다운그레이드 큐 도입
- `usbHidMonitorSof()`가 SOF 간격을 계측하고 누락된 마이크로프레임 수에 따라 점수를 가산합니다.
- `usbHidResolveDowngradeTarget()`과 `usbRequestBootModeDowngrade()`로 단계적 모드 하향과 ARM/COMMIT 단계 큐를 구성합니다.
- `usbProcess()`와 `ap.c` 메인 루프가 큐를 서비스하며, 로그와 타임아웃 관리를 처리합니다.

### V250924R3 — 워밍업/홀드오프 기반 안정화
- 구성 직후 750ms 홀드오프, 재개/실패 지연, 점수 감쇠 초기화가 도입되어 초기 진동 및 일시 정지 복귀 시 오탐을 줄입니다.
- 큐 타임아웃 경과 시 자동 초기화하고, ARM/COMMIT 단계별 로그가 추가되었습니다.

### V250924R4 — 속도별 파라미터 캐싱 & 임계 최적화
- `usbHidSofMonitorApplySpeedParams()`가 속도별 기대 간격/안정 임계/감쇠 주기/다운그레이드 점수를 캐싱해 ISR 분기를 줄입니다.
- 속도 변경·재개 시 캐시와 워밍업 타이머를 갱신하여 다운그레이드 판단의 일관성을 높입니다.
- `_DEF_FIRMWATRE_VERSION`이 `V250924R4`로 갱신되어 모니터 최적화를 식별합니다.

## 6. CODEX 점검 팁
- SOF 모니터 파라미터를 수정할 때는 `USB_BOOT_MONITOR_CONFIRM_DELAY_MS`와 `USB_SOF_MONITOR_*` 상수의 상호 의존성을 반드시 검토하십시오.
- 다운그레이드 큐는 리셋을 동반하므로, 신규 모드 추가 시 `usbHidResolveDowngradeTarget()`과 `usb_boot_mode_request_t` 초기화 경로를 함께 업데이트해야 합니다.
- `usbProcess()`는 IDLE 상태에서 즉시 반환하도록 설계되어 있으므로, 추가 로직을 삽입할 때 조기 반환 조건을 유지하여 메인 루프 부하를 최소화하십시오.
- CLI 확장 시 `log_pending` 플래그가 중복 로그를 방지하므로, 새로운 로그 포인트를 넣을 때는 해당 플래그 흐름을 참고하세요.

---
이 문서는 CODEX가 `DEV_MONITOR` 브랜치의 변경 사항을 빠르게 재구성하고 후속 작업에 활용할 수 있도록 정리한 레퍼런스입니다.
