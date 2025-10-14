# USB 불안정성 모니터 Codex 레퍼런스

## 1. 기능 개요
- **목표**: HS 8kHz 기본 정책을 유지하면서 마이크로프레임 누락을 감지하고, 상황에 따라 HS 4k/2k 또는 FS 1kHz로 단계적 다운그레이드를 수행합니다.
- **핵심 컴포넌트**:
  1. **SOF 모니터**: USB 속도와 상태를 감시하면서 누락된 프레임을 점수화합니다.
  2. **다운그레이드 큐**: 감지된 불안정성을 기반으로 다음 부트 모드를 예약하고, 사용자에게 로그를 노출한 뒤 저장/리셋을 수행합니다.
  3. **BootMode 연동**: EEPROM에 저장된 부트 모드와 모니터 파라미터가 동기화되며, 재부팅 후에도 모니터 상태가 일관됩니다.
- **컴파일 제어**: `_USE_USB_MONITOR` 매크로가 SOF 모니터 전체를 포함하거나 제외하며, V251009R6부터 HID 계측 매크로와 완전히 독립적으로 동작합니다.
- **주요 소비자**: `src/hw/driver/usb/usb_hid/usbd_hid.c`, `src/hw/driver/usb/usb.[ch]`, `src/ap/ap.c`, `src/hw/hw.c`, `src/ap/modules/qmk/port/port.h`, `src/hw/hw_def.h`.

## 2. 파일 토폴로지 & 책임
| 경로 | 핵심 심볼/함수 | 책임 |
| --- | --- | --- |
| `src/ap/modules/qmk/port/port.h` | `EECONFIG_USER_BOOTMODE` | 다운그레이드 큐가 사용할 EEPROM 슬롯을 BootMode와 공유합니다. |
| `src/hw/hw.c` | `usbBootModeLoad()` | 부팅 초기화 단계에서 저장된 모드를 로드해 모니터 파라미터를 초기화합니다. |
| `src/hw/hw_def.h` | `_DEF_FIRMWATRE_VERSION` | 모니터 기능 릴리스 버전을 `V250924R4`로 명시합니다. |
| `src/hw/driver/usb/usb.h` | `usb_boot_mode_request_t` | 다운그레이드 큐 구조체와 상태 열거형을 선언합니다. |
| `src/hw/driver/usb/usb.c` | `usbRequestBootModeDowngrade()`, `usbProcess()`, `cliBoot` | 큐 상태 머신(ARMED→COMMIT), 로그, CLI, 리셋 처리를 담당합니다. |
| `src/hw/driver/usb/usb_hid/usbd_hid.c` | `usbHidMonitorSof()`, `usbHidSofMonitorApplySpeedParams()` | SOF 모니터 점수 계산, 워밍업/홀드오프 관리, 속도별 파라미터 캐싱을 수행합니다. |
| `src/hw/driver/usb/usb_cmp/usbd_cmp.c` | HS/FS `bInterval` 유지 | 다운그레이드 후에도 안정적인 입력 패킷을 제공합니다. |
| `src/ap/ap.c` | `usbProcess()` 호출 | 메인 루프에서 큐 상태 머신을 주기적으로 서비스합니다. |

## 3. 데이터 구조 스냅샷
### 3.1 `usb_sof_monitor_t`
- **필드**
  - `score`: 누락 프레임 가산 점수. 최대 4점까지 상승하며 감쇠 주기마다 1점 감소합니다.
  - `expected_us`, `stable_threshold_us`: 속도별 기대 간격과 허용 오차.
  - `warmup_deadline_us`, `warmup_good_frames`: 모니터링 활성화 전 워밍업 조건.
  - `holdoff_end_us`: 장치 재개 직후 일정 시간 동안 감시를 중지합니다.
  - `decay_interval_us`: 감쇠 주기. 속도별로 2000µs(HS) 또는 8000µs(FS).
  - `degrade_threshold`: 다운그레이드 트리거 점수(기본 3점).
  - `active_speed`: 현재 USB 속도 캐시.
- **상태 전이 이벤트**
  1. **속도/상태 변경**: 파라미터 캐시와 타이머를 초기화합니다.
  2. **워밍업 통과**: HS 2048프레임 또는 FS 128프레임이 안정 범위 내에 들어오면 감시를 시작합니다.
  3. **점수 가산**: 기대 간격 대비 초과 시간을 누락 프레임 수로 환산하여 점수를 부여합니다.
  4. **감쇠**: `decay_interval_us`마다 점수를 1씩 낮춥니다.
  5. **임계 도달**: `degrade_threshold` 이상이면 다운그레이드 큐에 요청을 보냅니다.

### 3.2 `usb_boot_mode_request_t`
- **필드**
  - `stage`: `USB_BOOT_MODE_REQ_STAGE_IDLE/ARMED/COMMIT` 상태 머신.
  - `next_mode`: 적용 예정 부트 모드.
  - `delta_us`, `expected_us`: 현재 모드 대비 변화된 폴링 주기 정보를 로그에 활용합니다.
  - `ready_ms`, `timeout_ms`: ARM 단계의 확인 지연(기본 2초)과 타임아웃.
  - `log_pending`: 로그 중복을 막는 플래그.
- **동작**
  1. `usbRequestBootModeDowngrade()`가 IDLE 상태에서 호출되면 `ARMED`로 진입하고 확인 지연 타이머를 설정합니다.
  2. `usbProcess()`가 주기적으로 호출되어 타이머를 확인하고, 지연이 끝나면 `COMMIT` 단계로 상승합니다.
  3. `COMMIT` 단계에서 `usbBootModeSaveAndReset()`이 실행되어 EEPROM 저장 후 시스템을 리셋합니다.
  4. 실패하거나 타임아웃이면 큐가 초기화되고 경고 로그가 출력됩니다.

## 4. 속도별 파라미터 테이블
| USB 속도 | `expected_us` | `stable_threshold_us` | `decay_interval_us` | `degrade_threshold` | 워밍업 프레임 |
| --- | --- | --- | --- | --- | --- |
| HS (8k/4k/2k 공통) | 125 | 250 | 2000 | 3 | 2048 |
| FS (1k) | 1000 | 2000 | 8000 | 3 | 128 |
| 비구성/정지 | 0 | 0 | 0 | - | 즉시 리셋 |
- `usbHidSofMonitorApplySpeedParams()`가 위 테이블을 기반으로 파라미터를 캐싱합니다.

## 5. 제어 흐름
```
usbHidSOF_ISR()
  └─ usbHidMonitorSof()
       ├─ 속도 변화 감지 → 파라미터 캐시 갱신
       ├─ 워밍업 / 홀드오프 조건 검사
       ├─ 누락 프레임 계산 → score 가산
       └─ score ≥ degrade_threshold → usbRequestBootModeDowngrade()

apMainLoop()
  └─ usbProcess()
       ├─ stage == IDLE → 즉시 반환
       ├─ stage == ARMED → 확인 지연 경과 확인, 최초 로그 출력
       └─ stage == COMMIT → usbBootModeSaveAndReset()
```

## 6. CLI & 로그
| 메시지 | 발생 지점 | 의미 |
| --- | --- | --- |
| `[USB] SOF unstable: target=HS 4K (+125µs)` | `usbHidMonitorSof()` → `usbRequestBootModeDowngrade()` | 다운그레이드 ARM 진입 시점 로그. |
| `[USB] SOF downgrade confirm` | `usbProcess()` | 확인 지연이 만료되어 COMMIT 단계로 전환됨을 알립니다. |
| `[!] USB Poll 모드 저장 실패` | `usbProcess()` | EEPROM 저장 실패 또는 리셋 실패 시 경고. |
| `boot info` / `boot set ...` | `usb.c` CLI | 모니터와 동일한 BootMode 슬롯을 사용하여 사용자 제어를 제공합니다. |

## 7. 버전 히스토리 (요약)
- **V250923R1**: BootMode 영속화 기반 구축, EEPROM 슬롯 정의, HID/Composite 모드 연동.
- **V250923R2**: Composite 전송 용량 보강으로 모드 전환 시 입력 손실 최소화.
- **V250924R1**: 폴링 계측 샘플 윈도우 자동 전환.
- **V250924R2**: SOF 감시와 다운그레이드 큐의 ARM/COMMIT 상태 머신 도입.
- **V250924R3**: 워밍업 750ms, 홀드오프, 점수 감쇠 최적화 및 큐 타임아웃 로직 추가.
- **V250924R4**: 속도별 파라미터 캐싱 최적화, `_DEF_FIRMWATRE_VERSION`을 `V250924R4`로 갱신.

## 8. Codex 작업 체크리스트
1. SOF 모니터 파라미터를 수정하면 `USB_BOOT_MONITOR_CONFIRM_DELAY_MS` 및 `USB_SOF_MONITOR_*` 상수와의 관계를 재검토합니다.
2. 다운그레이드 로직을 확장할 때는 `usbHidResolveDowngradeTarget()`과 `usb_boot_mode_request_t` 초기화 경로를 함께 업데이트합니다.
3. `usbProcess()`는 IDLE 단계에서 즉시 반환하도록 설계되어 있으므로, 신규 로직을 추가할 때 조기 반환 조건을 유지합니다.
4. CLI 확장 시 `log_pending` 플래그가 중복 로그를 막는지 확인하고, 필요하면 새로운 상태 플래그를 추가합니다.
5. 모니터가 리셋을 유발하므로, 추가 로그는 리셋 직전에 출력되도록 순서를 조정합니다.
