# USB 부트 모드 Codex 레퍼런스 (V251009R9)

## 1. 파일 개요
- **핵심 파일**
  - `src/hw/driver/usb/usb.c`: 부트 모드 영속화, HS `bInterval`/기대 간격 캐시, 다운그레이드 큐 FSM을 담당합니다.【F:src/hw/driver/usb/usb.c†L23-L327】【F:src/hw/driver/usb/usb.c†L624-L674】
  - `src/hw/driver/usb/usb.h`: `UsbBootMode_t` 열거형과 외부 API(`usbBootModeGet*`, `usbRequestBootModeDowngrade()`, `usbCalcMissedFrames()`)를 정의합니다.【F:src/hw/driver/usb/usb.h†L61-L107】
  - `src/ap/modules/qmk/port/port.h`: `EECONFIG_USER_BOOTMODE` 슬롯을 선언해 QMK EEPROM에 선택값을 저장합니다.【F:src/ap/modules/qmk/port/port.h†L13-L22】
  - `src/hw/driver/usb/usb_hid/usbd_hid.c`: 폴링 계측 및 엔드포인트 `bInterval` 설정에서 부트 모드 캐시를 소비합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L656-L677】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1310-L1344】
- **연관 흐름 요약**
  1. 부팅 시 `hw.c` → `usbBootModeLoad()` → `usbBootModeRefreshCaches()` → `usbBegin()` 순으로 모드가 로드·로그됩니다.【F:src/hw/hw.c†L54-L76】【F:src/hw/driver/usb/usb.c†L86-L149】【F:src/hw/driver/usb/usb.c†L350-L377】
  2. USB 불안정성 감지(`usbHidMonitorSof()`)가 `usbRequestBootModeDowngrade()`를 호출하면, `usbProcess()`가 큐를 소비해 저장/리셋을 수행합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1359-L1610】【F:src/hw/driver/usb/usb.c†L209-L327】
  3. CLI `boot info/set`는 `usbBootModeSaveAndReset()`을 통해 모드 변경을 즉시 영속화합니다.【F:src/hw/driver/usb/usb.c†L198-L207】【F:src/hw/driver/usb/usb.c†L624-L674】

## 2. 부트 모드 테이블과 캐시 구조
| 심볼 | 정의 | 주요 값 | 설명 | 위치 |
| --- | --- | --- | --- | --- |
| `UsbBootMode_t` | `HS 8K/4K/2K`, `FS 1K` | `USB_BOOT_MODE_*` | 영속화되는 폴링 프로필 열거형 | `usb.h` L61-L68【F:src/hw/driver/usb/usb.h†L61-L68】 |
| `usb_boot_mode_name[]` | `const char *[USB_BOOT_MODE_MAX]` | `"HS 8K"` 등 | 로그 및 CLI 표시 문자열 | `usb.c` L27-L32【F:src/hw/driver/usb/usb.c†L27-L32】 |
| `usb_boot_mode_hs_interval_table[]` | `uint8_t[USB_BOOT_MODE_MAX]` | `0x01/0x02/0x03` | HS EP `bInterval` 매핑 테이블 | `usb.c` L34-L39【F:src/hw/driver/usb/usb.c†L34-L39】 |
| `usb_boot_mode_expected_interval_table[]` | `uint16_t[USB_BOOT_MODE_MAX]` | `125/250/500/1000` us | SOF 기대 간격 매핑 테이블 | `usb.c` L41-L46【F:src/hw/driver/usb/usb.c†L41-L46】 |
| `usb_boot_mode_hs_interval_cache` | `uint8_t` | 초기값 `0x01` | 부트 모드 변경 시 즉시 갱신되는 HS `bInterval` 캐시 | `usb.c` L48-L99【F:src/hw/driver/usb/usb.c†L48-L100】 |
| `usb_boot_mode_expected_interval_cache` | `uint16_t` | 초기값 `125` | SOF 기대 간격 캐시(`usbCalcMissedFrames()` 입력) | `usb.c` L48-L100【F:src/hw/driver/usb/usb.c†L48-L100】 |
| `usb_boot_mode_request_t` | 큐 구조체 | `stage`, `next_mode`, `delta_us`, `expected_us`, `missed_frames` 등 | 다운그레이드 FSM 상태·로그 컨텍스트 보관 | `usb.c` L72-L112【F:src/hw/driver/usb/usb.c†L72-L112】 |

- `usbBootModeRefreshCaches()`는 현재 모드에 따른 테이블 값을 캐시에 복제해 ISR 및 서비스 루프에서 분기 없이 접근하도록 합니다. 기본값 범위를 벗어나면 `HS 8K`로 롤백합니다.【F:src/hw/driver/usb/usb.c†L86-L100】
- `usbBootModeRequestReset()`은 큐를 `IDLE`로 초기화하고 측정치를 모두 0으로 비웁니다.【F:src/hw/driver/usb/usb.c†L102-L112】

## 3. EEPROM 영속화 및 초기화 플로우
1. **로드 (`usbBootModeLoad()`)**
   - `EECONFIG_USER_BOOTMODE`에서 32비트 값을 읽고, 범위 벗어나면 `USB_BOOT_MODE_HS_8K`로 대체합니다.【F:src/ap/modules/qmk/port/port.h†L13-L22】【F:src/hw/driver/usb/usb.c†L137-L149】
   - 모드를 전역에 저장한 뒤 `usbBootModeRefreshCaches()`로 파생 캐시를 동기화하고, `[  ] USB BootMode : ...` 로그를 남깁니다.【F:src/hw/driver/usb/usb.c†L86-L149】
2. **질의 (`usbBootModeGet*`)**
   - `usbBootModeGet()`은 현재 열거형을 그대로 반환합니다.【F:src/hw/driver/usb/usb.c†L153-L156】
   - `usbBootModeGetHsInterval()` / `usbBootModeGetExpectedIntervalUs()`는 캐시 값을 즉시 반환해 ISR 경로 분기를 제거합니다.【F:src/hw/driver/usb/usb.c†L163-L171】
3. **저장 (`usbBootModeStore()` / `usbBootModeSaveAndReset()`)**
   - 동일 모드면 조기 종료, 새 모드 저장 시 EEPROM에 기록 후 캐시를 갱신합니다.【F:src/hw/driver/usb/usb.c†L173-L207】
   - `usbBootModeSaveAndReset()`은 저장 성공 시 `resetToReset()`을 호출해 즉시 리셋합니다.【F:src/hw/driver/usb/usb.c†L198-L207】
4. **CLI 연동**
   - `usbInit()`에서 `cliAdd("boot", cliBoot)`로 명령을 노출합니다.【F:src/hw/driver/usb/usb.c†L330-L346】
   - `boot set {8k|4k|2k|1k}`는 `usbBootModeSaveAndReset()`을 호출하고, `boot info`는 현재 저장된 모드를 레이블로 출력합니다.【F:src/hw/driver/usb/usb.c†L624-L674】

## 4. 다운그레이드 큐와 불안정성 모니터 연계
### 4.1 큐 인터페이스
- `usbRequestBootModeDowngrade(mode, measured_delta_us, expected_us, missed_frames)`
  - 유효성 검사: 모드 범위, `expected_us`/`missed_frames`가 0이 아니어야 합니다. 실패 시 `USB_BOOT_DOWNGRADE_REJECTED`를 반환합니다.【F:src/hw/driver/usb/usb.c†L209-L225】
  - `IDLE` 단계: 확인 지연(`USB_BOOT_MONITOR_CONFIRM_DELAY_MS`)과 타임아웃을 설정하고 로그 플래그와 함께 `ARMED`로 전이합니다.【F:src/hw/driver/usb/usb.c†L231-L248】
  - `ARMED` 단계: 최신 측정값을 갱신하고 `ready_ms`에 도달하면 `COMMIT`으로 승격합니다.【F:src/hw/driver/usb/usb.c†L250-L267】
- `usbProcess()`
  - `ARMED`: 최초 한 번 경고 로그를 출력하고, 타임아웃이 지나면 요청을 초기화합니다.【F:src/hw/driver/usb/usb.c†L276-L305】
  - `COMMIT`: 확정 로그 후 `usbBootModeSaveAndReset()`을 호출하며, 실패 시 에러 로그를 남깁니다.【F:src/hw/driver/usb/usb.c†L307-L327】

### 4.2 SOF 모니터 연동 포인트
- `usbHidMonitorSof()`가 다운그레이드 조건을 만족하면 FSM을 ARM/CONFIRM 단계로 전환하고, 필요 시 확인 지연을 연장합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1359-L1610】
- 폴링 계측 루틴(`usbHidMeasurePollRate()`)은 `usbBootModeGetExpectedIntervalUs()`로 기대 간격을 가져와 HS 8000프레임/FS 1000프레임 윈도우를 유지합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1310-L1344】

## 5. 런타임 소비자와 레퍼런스 포인트
| 모듈 | 사용 함수/심볼 | 역할 |
| --- | --- | --- |
| `usbd_hid.c` | `usbBootModeGetHsInterval()`, `usbBootModeGetExpectedIntervalUs()` | HID/VIA/EXK EP `bInterval` 설정 및 폴링 계측 윈도우 계산에 사용됩니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L656-L677】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1310-L1344】 |
| `usbd_cmp.c` | `usbBootModeGetHsInterval()` | Composite 인터페이스별 HS `bInterval`을 동기화합니다.【F:src/hw/driver/usb/usb_cmp/usbd_cmp.c†L860-L934】 |
| `usbd_conf.c` | `usbBootModeIsFullSpeed()` | FS 1kHz 강제 시 PHY 속도를 `PCD_SPEED_HIGH_IN_FULL`로 전환합니다.【F:src/hw/driver/usb/usbd_conf.c†L341-L358】 |
| `usb.c` | `usbBootModeLabel()` | `usbBegin()` 로그, CLI 응답, 다운그레이드 로그에 공통 레이블을 제공합니다.【F:src/hw/driver/usb/usb.c†L128-L135】【F:src/hw/driver/usb/usb.c†L288-L377】 |

## 6. 확장 및 검증 체크리스트
1. **새 부트 모드 추가 시**
   - `UsbBootMode_t` 열거형과 `USB_BOOT_MODE_MAX`를 확장하고, 이름/HS `bInterval`/기대 간격 테이블을 동시에 업데이트합니다.【F:src/hw/driver/usb/usb.h†L61-L88】【F:src/hw/driver/usb/usb.c†L27-L100】
   - `usbBootModeRefreshCaches()` 기본값 분기, CLI 문자열, SOF 모니터 분기를 함께 점검합니다.【F:src/hw/driver/usb/usb.c†L86-L112】【F:src/hw/driver/usb/usb.c†L624-L674】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1310-L1610】
2. **EEPROM 슬롯 변경 시**
   - `EECONFIG_USER_BOOTMODE` 오프셋을 갱신하고, 마이그레이션 필요 여부를 검토합니다.【F:src/ap/modules/qmk/port/port.h†L13-L22】【F:src/hw/driver/usb/usb.c†L137-L207】
3. **다운그레이드 정책 수정 시**
   - `USB_BOOT_MONITOR_CONFIRM_DELAY_MS`, FSM 단계, 로그 포맷이 `usbRequestBootModeDowngrade()`와 `usbProcess()`에서 일관되게 유지되는지 확인합니다.【F:src/hw/driver/usb/usb.h†L70-L88】【F:src/hw/driver/usb/usb.c†L209-L327】
4. **디버깅 팁**
   - 캐시 불일치 시 `usbBootModeRefreshCaches()` 호출 누락 여부를 확인합니다.【F:src/hw/driver/usb/usb.c†L86-L112】
   - 다운그레이드 큐가 반복 재ARM되면 `ready_ms`와 `timeout_ms` 계산이 기대한 지연(2s/4s)인지 점검합니다.【F:src/hw/driver/usb/usb.c†L235-L305】
   - FS 강제 모드 테스트에서는 `usbBootModeIsFullSpeed()`가 true로 유지되는지와 `usbd_conf.c`가 `PCD_SPEED_HIGH_IN_FULL`을 선택하는지 확인합니다.【F:src/hw/driver/usb/usb.c†L158-L166】【F:src/hw/driver/usb/usbd_conf.c†L341-L358】

---
이 문서는 Codex가 USB 부트 모드 구조를 빠르게 파악하고, 불안정성 모니터·CLI·EEPROM 흐름을 일관되게 유지하면서 확장하도록 돕기 위한 레퍼런스입니다.
