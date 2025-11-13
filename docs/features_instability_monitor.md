# USB 불안정성 모니터 가이드

## 1. 목적과 범위
- HS 8 kHz를 기본 정책으로 유지하되, 마이크로프레임 누락·속도 재협상·열거 실패·서스펜드 남용을 감지하여 BootMode 다운그레이드를 자동 수행합니다.
- 모니터는 `USB_MONITOR_ENABLE` 매크로가 정의된 빌드에서만 컴파일되며, VIA channel 13 토글과 EEPROM 저장소를 통해 런타임 제어가 가능합니다.
- 대상 모듈: `src/hw/driver/usb/usb_hid/usbd_hid.c`, `src/hw/driver/usb/usb.c`, `src/ap/ap.c`, `src/ap/modules/qmk/port/{port.h,usb_monitor.c}`.

## 2. 구성 파일 & 빌드 매크로
| 경로 | 핵심 심볼 | 설명 |
| --- | --- | --- |
| `src/ap/modules/qmk/port/port.h` | `EECONFIG_USER_USB_INSTABILITY`, `usb_monitor_config_t` | 4바이트 EEPROM 슬롯(+32)과 구성 구조체를 정의합니다.
| `src/ap/modules/qmk/port/usb_monitor.c` | `EECONFIG_DEBOUNCE_HELPER`, `via_qmk_usb_monitor_command()` | VIA channel 13 value ID 3 토글을 EEPROM과 동기화합니다.
| `src/hw/driver/usb/usb.h` | `usbInstabilityLoad/Store/IsEnabled` | 모니터 토글 API 선언과 빌드 가드를 제공합니다.
| `src/hw/driver/usb/usb.c` | `usbInstability*`, `usbRequestBootModeDowngrade()`, `usbProcess()` | VIA 토글 캐시, 다운그레이드 큐, 메인 루프 상태 머신을 구현합니다.
| `src/hw/driver/usb/usb_hid/usbd_hid.c` | `usbHidMonitor*`, `usbHidResolveDowngradeTarget()` | SOF/열거/속도/서스펜드 이벤트를 측정하고 점수를 계산합니다.
| `src/ap/ap.c` | `usbProcess()`, `usbHidMonitorBackgroundTick()` | 메인 루프에서 큐 상태와 SOF 누락 감시를 주기적으로 호출합니다.

> **매크로 정리**
> - `USB_MONITOR_ENABLE` : 모니터 전체를 포함. 꺼지면 SOF 핸들러가 즉시 반환하고 `usbInstabilityIsEnabled()`는 false입니다.
> - `_DEF_ENABLE_USB_HID_TIMING_PROBE` : 모니터가 `micros()`를 호출할 수 있도록 하며, 계측 비활성 시에도 `usbHidInstrumentationNow()`는 `micros()`를 반환하도록 조건부로 구현되어 있습니다.

## 3. API 레퍼런스
| 함수 / 열거형 | 위치 | 설명 |
| --- | --- | --- |
| `usb_monitor_config_t` | `src/ap/modules/qmk/port/port.h` | VIA가 토글할 enable 비트와 예약 필드를 포함합니다.
| `bool usbInstabilityLoad(void)` | `src/hw/driver/usb/usb.c` | EEPROM 값을 읽어 캐시하고 현재 상태를 로그로 출력합니다.
| `bool usbInstabilityStore(bool enable)` | `src/hw/driver/usb/usb.c` | 토글 값을 갱신하고 EEPROM 플러시를 예약한 뒤 로그를 남깁니다.
| `bool usbInstabilityIsEnabled(void)` | `src/hw/driver/usb/usb.c` | 인터럽트/메인 루프에서 모니터 활성 여부를 빠르게 확인합니다.
| `void usb_monitor_storage_*()` | `src/ap/modules/qmk/port/usb_monitor.c` | `EECONFIG_DEBOUNCE_HELPER`로 생성된 init/flag/flush 헬퍼 함수군입니다.
| `void via_qmk_usb_monitor_command(uint8_t *data, uint8_t length)` | `src/ap/modules/qmk/port/usb_monitor.c` | VIA channel 13 value ID 3 요청을 토글 API와 연결합니다.
| `void usbHidMonitorSof(uint32_t now_us)` | `src/hw/driver/usb/usb_hid/usbd_hid.c` | SOF ISR에서 호출되어 프레임 간격, 워밍업, 점수 감쇠를 계산합니다.
| `void usbHidMonitorProcessDelta(uint32_t now_us, uint32_t delta_us)` | `src/hw/driver/usb/usb_hid/usbd_hid.c` | HS/FS별 허용 오차와 비교해 점수를 누적하고 타임아웃을 갱신합니다.
| `void usbHidMonitorPrimeTimeout(uint32_t now_us)` | `src/hw/driver/usb/usb_hid/usbd_hid.c` | SOF 누락 감시 타임아웃과 holdoff 타이머를 갱신합니다.
| `bool usbHidMonitorCommitDowngrade(...)` | `src/hw/driver/usb/usb_hid/usbd_hid.c` | `usbRequestBootModeDowngrade()`를 호출하고, 필요 시 로그를 출력합니다.
| `void usbHidMonitorTrackEnumeration(...)` | `src/hw/driver/usb/usb_hid/usbd_hid.c` | 열거 실패 횟수(구성 전 detach)를 점수화합니다.
| `void usbHidMonitorHandleSpeedChange/HandleSuspend(uint32_t now_us)` | `src/hw/driver/usb/usb_hid/usbd_hid.c` | 1초/1.5초 창 내 재협상·서스펜드 횟수를 추적해 persistent 점수를 올립니다.
| `void usbHidMonitorBackgroundTick(uint32_t now_us)` | `src/hw/driver/usb/usb_hid/usbd_hid.c` | 메인 루프에서 SOF 누락 감시, 열거 상태 갱신을 수행합니다.
| `usb_boot_downgrade_result_t usbRequestBootModeDowngrade(...)` | `src/hw/driver/usb/usb.c` | 모니터가 전달한 측정값을 큐 상태로 전환합니다.
| `void usbProcess(void)` | `src/hw/driver/usb/usb.c` | 모니터 큐(`boot_mode_request`)가 활성 상태일 때 로그 및 저장/리셋을 실행합니다.

## 4. 데이터 구조
### 4.1 `usb_sof_monitor_t`
| 필드 | 설명 |
| --- | --- |
| `prev_tick_us` | 마지막 SOF 타임스탬프. 다음 프레임과의 차이를 계산합니다.
| `last_decay_us` / `slow_last_decay_us` | 빠른 점수와 느린 점수 감쇠를 수행하는 기준 시각.
| `holdoff_end_us` | 다운그레이드 직후 일정 시간(설정상 2초) 동안 감시를 중단합니다.
| `warmup_deadline_us` / `warmup_good_frames` / `warmup_target_frames` | 워밍업 기간에 필요한 정상 프레임 수와 타임아웃을 추적합니다.
| `no_sof_deadline_us` | `expected_us * USB_SOF_MONITOR_NO_SOF_TIMEOUT_FACTOR`로 계산된 SOF 누락 감시 타임아웃.
| `expected_us` / `stable_threshold_us` | HS 125µs / FS 1000µs 기본값과 허용 오차(HS 180µs, FS 1500µs).
| `decay_interval_us` / `slow_decay_interval_us` | HS 4ms/12ms, FS 20ms/60ms로 구성된 감쇠 주기.
| `speed_change_window_us` / `suspend_window_us` | 속도/서스펜드 이벤트 창. 각각 1s/1.5s를 마이크로초 단위로 보관합니다.
| `warmup_grace_deadline_us` / `warmup_grace_active` | 워밍업 직후 200ms 동안 grace 모드를 켤 시점을 추적합니다.
| `degrade_threshold` / `slow_degrade_threshold` | 빠른 점수 10/5, 느린 점수 4/3 임계값.
| `event_score_cap`, `score`, `slow_score` | 단일 이벤트 점수 상한과 누적 점수.
| `speed_change_count`, `suspend_count` | 창 안에서 발생한 이벤트 횟수.
| `persistent_score`, `persistent_threshold` | 재협상/서스펜드 반복 시 추가 다운그레이드를 유도하는 점수.
| `active_speed`, `warmup_complete` | 현재 USB 속도, 워밍업 완료 여부 캐시.

### 4.2 `usb_enumeration_monitor_t`
| 필드 | 설명 |
| --- | --- |
| `attempt_deadline_us` | 장치가 구성을 완료하기 전 대기 가능한 시간(기본 250ms) 상한.
| `stable_since_us` | 열거 이후 안정 상태로 머문 시간.
| `fail_score` | 재시도 실패 횟수를 5점까지 누적합니다.
| `pending_state` | 현재 USB Device 상태(`USBD_STATE_DEFAULT`, `ADDRESSED`, `CONFIGURED`)를 기록합니다.
| `waiting_config` | 아직 구성이 완료되지 않았음을 나타내는 플래그.

### 4.3 `usb_monitor_config_t`
- `enable` (1 byte) : VIA 토글에서 직접 읽고 쓰는 필드.
- `reserved[3]` : 향후 확장용. VIA는 항상 0을 기록합니다.

## 5. 이벤트 흐름
1. **SOF ISR (`USBD_HID_SOF`)**
   - `usbHidInstrumentationNow()` → `usbInstabilityIsEnabled()`를 확인.
   - 활성화된 경우 `usbHidMonitorSof(now_us)` 호출.
   - `usbHidMonitorSof()`는 속도 변화 감지, 워밍업/holdoff 적용 후 `usbHidMonitorProcessDelta()`를 호출해 점수를 계산합니다.
2. **점수 평가**
   - 점수가 임계치를 넘으면 `usbHidResolveDowngradeTarget()`으로 목표 모드를 결정하고 `usbHidMonitorCommitDowngrade()`에서 `usbRequestBootModeDowngrade()`를 호출합니다.
   - `usbRequestBootModeDowngrade()`가 `ARMED`를 반환하면 로그 대기 상태가 되고, `ready_ms` 이후 다시 호출되어 `COMMIT`으로 전환됩니다.
3. **백그라운드 감시 (`usbHidMonitorBackgroundTick`)**
   - 메인 루프에서 750ms 워밍업과 SOF 누락 타임아웃을 체크합니다.
   - 열거/속도/서스펜드 이벤트 창을 만료시키고 필요 시 persistent 점수를 감소시킵니다.
4. **메인 루프 처리 (`usbProcess`)**
   - 다운그레이드 큐 단계에 따라 로그를 출력하고, `usbBootModeSaveAndReset()`으로 EEPROM 저장 → 유예 리셋을 실행합니다.

## 6. 점수 & 파라미터 요약
| 항목 | 값 |
| --- | --- |
| 워밍업 최소 프레임 | HS 2048, FS 128 |
| 워밍업 타임아웃 | 50ms holdoff + 2000ms = 2050ms |
| 빠른 점수 임계 | HS 10, FS 5 |
| 느린 점수 임계 | HS 4, FS 3 |
| 영구 점수 임계 | 3 (속도/서스펜드 이벤트 3회) |
| SOF 누락 타임아웃 | `expected_us * 64` (HS 8ms, FS 64ms) |
| 열거 실패 임계 | 250ms 안에 구성 실패 3회 → 다운그레이드 |
| 재시도 지연 | 다운그레이드 실패 시 50ms holdoff |

## 7. VIA & EEPROM 동작
- `via_qmk_usb_monitor_command()`는 `id_custom_set_value` 수신 시 enable 비트를 저장하고, 응답을 항상 현재 상태로 에코합니다.
- 토글 변경 시 `usbInstabilityStore()`는 즉시 EEPROM 쓰기를 예약하고 로그를 남깁니다.
- VIA channel 13 JSON 라벨에는 "USB POLLING" 문구로 BootMode/VIA 의존성을 명시하며, `USB_MONITOR_ENABLE`이 꺼진 빌드에서는 항목을 제거합니다.

## 8. 디버깅 & 체크리스트
1. 모니터가 과도하게 다운그레이드하면 `logPrintf`로 출력되는 이벤트 라벨(`SOF`, `ENUM`, `SPEED`, `SUSPEND`)을 확인해 어떤 경로에서 점수가 발생했는지 파악합니다.
2. SOF 타이머가 전혀 동작하지 않는다면 `_DEF_ENABLE_USB_HID_TIMING_PROBE`와 `USB_MONITOR_ENABLE` 매크로가 모두 켜져 있는지 확인합니다.
3. `usbProcess()`가 빈번히 실행되어 메인 루프를 점유하면 `usbHasPendingService()`에서 감시하는 세 큐(Apply, Monitor, Reset) 중 어디가 true인지 로그를 추가해 추적합니다.
4. VIA 토글 후에도 로그가 변하지 않으면 `eeconfig_flag_usb_monitor(true)`가 호출되었는지 확인하고, EEPROM 쓰기 실패 여부를 `eeconfig_flush_usb_monitor()` 반환값으로 점검합니다.
5. 모니터가 비활성화된 상태에서도 `_DEF_ENABLE_USB_HID_TIMING_PROBE=1`이면 `usbHidInstrumentationNow()`가 `micros()`를 호출하므로, 불필요한 타임스탬프 비용을 피하려면 계측 매크로를 끕니다.

## 9. 로그 메시지 예시
- `[  ] USB Monitor : ON/OFF` : EEPROM 로드 결과.
- `[!] USB Poll 불안정 감지 : 기대 125 us, 측정 420 us (검증 대기)` : SOF 점수가 `ARMED` 상태로 진입.
- `[!] USB Poll 모드 다운그레이드 -> HS 4K` : `COMMIT` 단계에서 실제 저장이 이루어짐.
- `[!] VIA RX queue overflow : <count>` : VIA 큐가 가득 차면 모니터가 토글 요청을 놓칠 수 있으므로 큐 깊이를 조정해야 합니다.
- `[!] USB Monitor Toggle -> OFF` : 호스트 요청으로 모니터를 비활성화했음을 나타냅니다.

> **운영 팁**: 장시간 테스트 중에는 `usbHidMonitorBackgroundTick()`이 호출되는지 `apMain()` 루프에 임시 토글 로그를 넣어 확인하십시오. SOF 누락 감시는 백그라운드 틱에 의존하므로, 루프가 8ms 이상 멈추지 않도록 다른 작업의 블로킹을 피해야 합니다.
