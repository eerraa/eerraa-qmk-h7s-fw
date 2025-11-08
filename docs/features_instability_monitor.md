# USB 불안정성 모니터 Codex 레퍼런스

## 1. 기능 개요
- **목표**: HS 8kHz 기본 정책을 유지하면서 마이크로프레임 누락을 감지하고, 상황에 따라 HS 4k/2k 또는 FS 1kHz로 단계적 다운그레이드를 수행합니다.
- **핵심 컴포넌트**:
  1. **SOF 모니터**: USB 속도와 상태를 감시하면서 누락된 프레임을 점수화합니다.
  2. **다운그레이드 큐**: 감지된 불안정성을 기반으로 다음 부트 모드를 예약하고, 사용자에게 로그를 노출한 뒤 저장/리셋을 수행합니다.
  3. **BootMode 연동**: EEPROM에 저장된 부트 모드와 모니터 파라미터가 동기화되며, 재부팅 후에도 모니터 상태가 일관됩니다.
- **컴파일 제어**: // V251108R1: Brick60 `config.h`의 `USB_MONITOR_ENABLE` 매크로가 SOF 모니터 전체를 포함하거나 제외하며, BootMode 토글과 VIA 채널 13 구성을 함께 제어합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/config.h†L42-L49】
- **주요 소비자**: `src/hw/driver/usb/usb_hid/usbd_hid.c`, `src/hw/driver/usb/usb.[ch]`, `src/ap/ap.c`, `src/hw/hw.c`, `src/ap/modules/qmk/port/port.h`, `src/hw/hw_def.h`.

## 2. 파일 토폴로지 & 책임
| 경로 | 핵심 심볼/함수 | 책임 |
| --- | --- | --- |
| `src/ap/modules/qmk/port/port.h` | `EECONFIG_USER_BOOTMODE`, `EECONFIG_USER_USB_INSTABILITY` | // V251108R1: BootMode/USB 모니터 4B 슬롯을 각각 +28/+32 오프셋에 정의합니다. |
| `src/hw/hw.c` | `usbBootModeLoad()` | 부팅 초기화 단계에서 저장된 모드를 로드해 모니터 파라미터를 초기화합니다. |
| `src/hw/hw_def.h` | `_DEF_FIRMWATRE_VERSION` | 모니터 기능 릴리스 버전을 `V251109R1`로 명시합니다. |
| `src/hw/driver/usb/usb.h` | `usb_boot_mode_request_t`, `usbInstabilityLoad()` | 다운그레이드 큐와 VIA 토글 연동 API 프로토타입을 제공합니다. |
| `src/hw/driver/usb/usb.c` | `usbRequestBootModeDowngrade()`, `usbProcess()`, `usbInstabilityStore()` | 큐 상태 머신(ARMED→COMMIT)과 VIA 토글 저장/로드, 로그, 리셋 처리를 담당합니다. |
| `src/hw/driver/usb/usb_hid/usbd_hid.c` | `usbHidMonitorSof()`, `usbHidSofMonitorApplySpeedParams()` | SOF 모니터 점수 계산, 워밍업/홀드오프 관리, 속도별 파라미터 캐싱을 수행합니다. |
| `src/hw/driver/usb/usb_cmp/usbd_cmp.c` | HS/FS `bInterval` 유지 | 다운그레이드 후에도 안정적인 입력 패킷을 제공합니다. |
| `src/ap/ap.c` | `usbProcess()`, `usbHidMonitorBackgroundTick()` | 메인 루프에서 큐 상태 머신과 SOF 누락 감시를 주기적으로 서비스합니다.【F:src/ap/ap.c†L28-L46】
| `src/ap/modules/qmk/port/usb_monitor_via.[ch]` | `via_qmk_usb_monitor_command()`, `usb_monitor_storage_*()` | // V251108R1: VIA channel 13 value ID 3 토글을 EEPROM과 동기화하고 JSON 조건을 문서화합니다. |

## 3. 데이터 구조 스냅샷
### 3.1 `usb_sof_monitor_t`
- **필드**
  - `score`: 누락 프레임 가산 점수. 속도별 `event_score_cap`(HS 6점, FS 4점)까지 단일 이벤트를 누적하며 감쇠 주기마다 1점 감소합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L493-L557】
  - `expected_us`, `stable_threshold_us`: 속도별 기대 간격과 허용 오차.
  - `warmup_deadline_us`, `warmup_good_frames`: 모니터링 활성화 전 워밍업 조건.
  - `holdoff_end_us`: 장치 재개 직후 일정 시간 동안 감시를 중지합니다.
  - `decay_interval_us` / `slow_decay_interval_us`: 각각 빠른/느린 점수 감쇠 주기. HS는 4ms/12ms, FS는 20ms/60ms로 설정됩니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L523-L544】
  - `slow_score`, `slow_degrade_threshold`: 장기적인 드롭 빈도를 추적해 3~4회 이상 반복되면 다운그레이드를 보강합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L493-L557】
  - `no_sof_deadline_us`: 마지막 SOF 이후 허용되는 최대 간격(HS 8ms, FS 64ms). 초과 시 백그라운드 타이머가 강제 평가합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L495-L556】
  - `speed_change_count/suspend_count`, `speed_change_window_us/suspend_window_us`: 2초 창 내 재협상·서스펜드 발생 횟수를 기록해 과도한 변동을 감지합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L493-L556】
  - `persistent_score`, `persistent_threshold`: 속도/서스펜드 이벤트로 누적된 영속 점수를 추적해 임계(4점)를 넘으면 SOF 데이터가 초기화된 상황에서도 다운그레이드를 요청합니다.
  - `warmup_grace_active`, `warmup_grace_deadline_us`: 워밍업 완료 직후 100ms 이내에 변동이 발생하면 목표 프레임 수를 절반으로 낮춰 감시 재개까지 대기 시간을 줄입니다.
  - `degrade_threshold`: 빠른 점수 임계. HS 10점, FS 5점입니다.
  - `active_speed`: 현재 USB 속도 캐시.
- **상태 전이 이벤트**
  1. **속도/상태 변경**: 파라미터 캐시와 타이머를 초기화합니다.
  2. **워밍업 통과**: HS 2048프레임 또는 FS 128프레임이 안정 범위 내에 들어오면 감시를 시작합니다.
  3. **점수 가산**: 기대 간격 대비 초과 시간을 누락 프레임 수로 환산하여 점수를 부여합니다.
  4. **감쇠**: `decay_interval_us`마다 점수를 1씩 낮춥니다.
  5. **임계 도달**: `degrade_threshold` 또는 `slow_degrade_threshold` 이상이면 다운그레이드 큐에 요청을 보냅니다.
  6. **SOF 타임아웃**: SOF 인터럽트가 끊기면 백그라운드 훅(`usbHidMonitorBackgroundTick`)이 `no_sof_deadline_us`를 기준으로 지연을 점수화합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1393-L1540】
  7. **속도/서스펜드 누적**: `speed_change_count`와 `suspend_count`가 각각 4회/5회를 넘으면 `persistent_score`를 증가시켜 SOF 점수 초기화와 무관하게 다운그레이드를 유도합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1513-L1665】

### 3.3 `usb_enumeration_monitor_t`
- **필드**
  - `attempt_deadline_us`: `USBD_STATE_DEFAULT/ADDRESSED`에서 `CONFIGURED`에 진입해야 하는 마감 시각. 기본 250 ms.
  - `stable_since_us`: `CONFIGURED` 상태 유지 시간. 1초 이상 안정 시 열거 실패 점수를 1 감소시킵니다.
  - `fail_score`: 열거 실패 누적 점수(`USB_ENUM_MONITOR_SCORE_CAP`으로 캡).
  - `pending_state`: 현재 감시 중인 USB 장치 상태 스냅샷.
  - `waiting_config`: `CONFIGURED` 진입 대기 여부.
- **동작**
  1. `DEFAULT/ADDRESSED` 상태가 연속으로 유지되고 마감 시간이 지날 경우 `fail_score`를 1 증가시킵니다.
  2. `fail_score >= USB_ENUM_MONITOR_FAIL_THRESHOLD`가 되면 즉시 다운그레이드 큐에 이벤트를 전파합니다.
  3. `CONFIGURED` 상태가 1초 이상 유지되면 `fail_score`를 1 감소시켜 정상 복구 시 과도한 다운그레이드를 방지합니다.
  4. VIA 토글 등으로 모니터가 비활성화되어도 열거 추적은 계속 진행되며, 실제 다운그레이드는 모니터가 활성화된 경우에만 수행됩니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1513-L1603】
  5. `usbHidMonitorBackgroundTick()`이 항상 먼저 열거 상태를 처리하므로, 시나리오 1(초기화 실패 반복)에서도 펌웨어만으로 안정적인 다운그레이드 판단이 가능합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1605-L1698】

### 3.4 속도/서스펜드 열화 감시
- `usbHidMonitorHandleSpeedChange()`는 HS↔FS 변동이 2초 윈도우 내 4회를 넘으면 `persistent_score`를 올리고 워밍업 그레이스 모드를 켭니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1578-L1635】
- `usbHidMonitorHandleSuspend()`는 Selective Suspend가 2초 내 5회 이상 발생하면 동일하게 `persistent_score`를 증가시키며, `slow_score`는 1씩만 감소해 장기 누적을 보존합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1538-L1655】
- `persistent_score`가 임계(`persistent_threshold`)에 도달하면 다운그레이드 이벤트 유형(`sof/enum/speed/suspend`)이 `usbHidMonitorCommitDowngrade()`에 전달되어 로그에서 원인을 즉시 확인할 수 있습니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1546-L1615】

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
- `usbHidSofMonitorApplySpeedParams()`가 아래 테이블을 기반으로 파라미터를 캐싱합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L486-L547】

| USB 속도 | `expected_us` | `stable_threshold_us` | `decay_interval_us` | `degrade_threshold` | 워밍업 프레임 |
| --- | --- | --- | --- | --- | --- |
| HS (8k/4k/2k 공통) | 125 | 250 | 4000 | 12 | 2048 |
| FS (1k) | 1000 | 2000 | 20000 | 6 | 128 |
| 비구성/정지 | 0 | 0 | 0 | - | 즉시 리셋 |

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
- **V251009R6**: SOF 모니터 정의를 `USB_MONITOR_ENABLE` 가드로 분리해 USB 모니터를 독립 토글로 관리.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L473-L547】
- **V251009R7**: HID 전송 계층 계측 호출을 `_DEF_ENABLE_USB_HID_TIMING_PROBE` 조건으로 감싸 불필요한 큐/타이머 접근을 제거.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1145-L1159】
- **V251010R5**: `USB_MONITOR_ENABLE` 비활성 빌드에서도 HID 본체가 유지되도록 모니터 전용 블록을 조기 종료.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L486-L546】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1198-L1412】
- **V251010R6**: `USB_MONITOR_ENABLE` 기본값을 1로 고정해 릴리스 빌드에서 모니터를 상시 활성화.
- **V251108R1**: `USB_MONITOR_ENABLE`/`BOOTMODE_ENABLE`를 분리하고 VIA channel 13 value ID 3 토글·`EECONFIG_USER_USB_INSTABILITY` 슬롯·`usbInstabilityLoad/Store()`·`usb_monitor_via.[ch]`를 도입했습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/config.h†L42-L49】【F:src/ap/modules/qmk/port/usb_monitor_via.c†L5-L77】【F:src/hw/driver/usb/usb.c†L200-L278】
- **V251108R7**: `usbInstabilityLoad()`/`usbInstabilityStore()`가 현재 ON/OFF 상태를 `logPrintf()`로 출력해 CLI에서 즉시 확인할 수 있도록 했습니다.【F:src/hw/driver/usb/usb.c†L335-L375】

## 8. Codex 작업 체크리스트
1. SOF 모니터 파라미터를 수정하면 `USB_BOOT_MONITOR_CONFIRM_DELAY_MS` 및 `USB_SOF_MONITOR_*` 상수와의 관계를 재검토합니다.
2. 다운그레이드 로직을 확장할 때는 `usbHidResolveDowngradeTarget()`과 `usb_boot_mode_request_t` 초기화 경로를 함께 업데이트합니다.
3. `usbProcess()`는 IDLE 단계에서 즉시 반환하도록 설계되어 있으므로, 신규 로직을 추가할 때 조기 반환 조건을 유지합니다.
4. CLI 확장 시 `log_pending` 플래그가 중복 로그를 막는지 확인하고, 필요하면 새로운 상태 플래그를 추가합니다.
5. 모니터가 리셋을 유발하므로, 추가 로그는 리셋 직전에 출력되도록 순서를 조정합니다.

## 9. 조건부 컴파일 & 계측 상호작용
- // V251108R1: `USB_MONITOR_ENABLE`을 정의하지 않으면 `usbInstabilityLoad/Store()`와 SOF 모니터 전용 코드가 스텁으로 축소되어 다운그레이드 큐가 완전히 제외됩니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/config.h†L42-L49】【F:src/hw/driver/usb/usb.h†L90-L140】
- HID 계측 매크로 `_DEF_ENABLE_USB_HID_TIMING_PROBE`가 0이면 `usbHidInstrumentation*` 함수가 인라인 스텁으로 대체되어 큐 깊이·타이머 접근이 제거됩니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L11-L68】
- `USB_MONITOR_ENABLE`이 정의된 빌드에서만 `usbHidInstrumentationNow()`가 항상 `micros()`를 반환하여 SOF 모니터가 필요한 타임스탬프를 유지하며, VIA 토글이 꺼지면 SOF 핸들러가 즉시 반환하여 추가 오버헤드를 방지합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L26-L33】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1035-L1098】
- // V251108R8: `USBD_HID_SOF()`는 모니터 토글과 계측 매크로를 점검한 뒤 필요한 경우에만 `micros()`를 호출하여 SOF ISR 시간을 더 줄입니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1074-L1100】
- 매트릭스 계층의 계측 매크로가 꺼져 있더라도 모니터는 HID 내부 타임스탬프만으로 작동하므로, Poll Rate 계측 활성 여부와 독립적으로 안정성 감시가 유지됩니다.【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L13-L45】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1198-L1412】

## 10. 대표 빌드 시나리오
- **릴리스 기본(S1)**: `_DEF_ENABLE_MATRIX_TIMING_PROBE=0`, `_DEF_ENABLE_USB_HID_TIMING_PROBE=0`, `USB_MONITOR_ENABLE=1`. SOF 모니터만 활성화되어 프레임마다 `micros()`를 1회 호출하며 다운그레이드 상태 머신만 실행합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/config.h†L42-L49】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1198-L1412】
- **HID 진단 활성(S4)**: `_DEF_ENABLE_USB_HID_TIMING_PROBE=1`로 전환하면 큐 깊이 스냅샷·폴링 초과 측정을 수행하지만, 모니터와 타임스탬프 소스를 공유하여 중복 `micros()` 호출을 피합니다.【F:src/hw/hw_def.h†L9-L18】【F:src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.h†L11-L68】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1145-L1188】
- **풀 계측(S5)**: `_DEF_ENABLE_MATRIX_TIMING_PROBE=1`과 `_DEF_ENABLE_USB_HID_TIMING_PROBE=1`을 동시에 활성화하면 매트릭스 스캔 타임스탬프가 HID 계층으로 전달되어 Poll Rate 분석과 연결되며, SOF 모니터는 동일 타임스탬프를 사용해 다운그레이드를 판단합니다.【F:src/ap/modules/qmk/port/matrix_instrumentation.h†L13-L45】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1198-L1412】

## 11. VIA & EEPROM 구성 (Brick60)
- // V251108R1: `usb_monitor_config_t`는 1바이트 enable 플래그와 3바이트 예약 필드를 가지며, `EECONFIG_USER_USB_INSTABILITY`(데이터 블록 +32) 슬롯에 저장됩니다.【F:src/ap/modules/qmk/port/port.h†L18-L24】
- `usb_monitor_via.c`는 `EECONFIG_DEBOUNCE_HELPER`를 사용해 EEPROM 읽기/플러시 헬퍼(`usb_monitor_storage_*`)를 생성하고, VIA channel 13 value ID 3 토글을 `usbInstabilityStore()/IsEnabled()`로 연결합니다.【F:src/ap/modules/qmk/port/usb_monitor_via.c†L5-L77】【F:src/hw/driver/usb/usb.c†L200-L278】
- Brick60 VIA 정의(`BRICK60-H7S-VIA.JSON`)는 "USB POLLING" 블록에서 BootMode/USB 모니터 의존성을 라벨에 노출하며, `USB_MONITOR_ENABLE` 또는 `BOOTMODE_ENABLE`를 끈 빌드에서는 해당 블록을 삭제해야 합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/json/BRICK60-H7S-VIA.JSON†L248-L292】
