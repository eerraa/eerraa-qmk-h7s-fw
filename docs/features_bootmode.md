# BootMode 기능 Codex 레퍼런스

## 1. 기능 개요
- **목표**: USB 부트 모드(HS 8K/4K/2K, FS 1K)를 EEPROM에 저장하고, 부팅 시점부터 모든 USB 경로가 일관된 폴링 주기를 사용하도록 만듭니다.
- **동작 요약**:
  1. 부팅 직후 EEPROM에서 마지막으로 저장된 부트 모드를 읽습니다.
  2. USB 초기화 시 HS/FS 속도와 `bInterval`이 모드에 맞춰 재구성됩니다.
  3. CLI를 통해 모드를 변경하면 즉시 EEPROM에 저장하고 재부팅하여 설정을 확정합니다.
- **주요 소비자**: `src/hw/driver/usb/usb.[ch]`, `src/hw/driver/usb/usbd_conf.c`, `src/hw/driver/usb/usb_hid/usbd_hid.c`, `src/hw/driver/usb/usb_cmp/usbd_cmp.c`, `src/hw/hw.c`, `src/ap/modules/qmk/port/port.h`.

## 2. 파일 토폴로지 & 책임
| 경로 | 핵심 심볼/함수 | 책임 |
| --- | --- | --- |
| `src/ap/modules/qmk/port/port.h` | `EECONFIG_USER_BOOTMODE` | 사용자 EEPROM에 4바이트 슬롯을 확보합니다. |
| `src/hw/hw.c` | `usbBootModeLoad()` | 부팅 초기화 단계에서 EEPROM과 RAM을 동기화합니다. |
| `src/hw/driver/usb/usb.h` | `usb_boot_mode_t` 열거형, `usbBootModeGetHsInterval()` 외 | 부트 모드 정의, HS `bInterval` 계산, CLI 프로토타입을 제공합니다. |
| `src/hw/driver/usb/usb.c` | `usbBootModeLoad/Store/SaveAndReset()`, `usbBegin()` 로그 | 저장/로드, CLI 명령 처리, 부트 모드별 초기화 로그를 담당합니다. |
| `src/hw/driver/usb/usbd_conf.c` | `usbBootModeIsFullSpeed()` | HS PHY에서도 FS 1kHz를 강제할 수 있도록 PCD 속도를 재설정합니다. |
| `src/hw/driver/usb/usb_hid/usbd_hid.c` | `usbBootModeGetHsInterval()` 호출부 | HID 엔드포인트 `bInterval`과 폴링 계측 샘플 크기를 부트 모드에 맞춥니다. |
| `src/hw/driver/usb/usb_cmp/usbd_cmp.c` | `usbBootModeGetHsInterval()` 호출부 | Composite 인터페이스의 `bInterval`을 모드별로 재구성합니다. |
| `src/ap/modules/qmk/port/usb_bootmode_via.[ch]` | `via_qmk_usb_bootmode_command()` | // V251108R1: Brick60 VIA channel 13 value ID 1/2를 BootMode API와 연동하고, 토글 비활성 빌드에서는 JSON 블록을 제거해야 함을 명시합니다. |

## 3. 데이터 모델 & 유틸리티
- **열거형 `usb_boot_mode_t`**
  - `USB_BOOT_MODE_HS_8K`, `HS_4K`, `HS_2K`, `FS_1K` 순으로 정의되며, CLI와 로그에서 동일한 레이블을 사용합니다.
  - `usbBootModeGetHsInterval(mode)`는 HS 계열 모드에 대해 `bInterval`을 `0x01/0x02/0x03`으로 변환하고, FS 모드에서는 `HID_FS_BINTERVAL`을 반환합니다.
- **EEPROM 슬롯**
  - `EECONFIG_USER_BOOTMODE`는 4바이트입니다.
  - 로드 시 값이 범위 밖이면 기본값 `USB_BOOT_MODE_HS_8K`로 복구합니다.
- **CLI 헬퍼**
  - `usbBootModeLabel(mode)`는 `HS 8K`, `HS 4K`, `HS 2K`, `FS 1K` 문자열을 제공합니다.
- **다운그레이드 큐 지연**
  - 모니터가 다운그레이드를 예약하면 `USB_BOOT_MONITOR_CONFIRM_DELAY_MS`(2초) 동안 검증 대기 상태에 머무르고, 동일 시간만큼 타임아웃을 부여해 반복 리셋을 방지합니다.【F:src/hw/driver/usb/usb.h†L61-L88】【F:src/hw/driver/usb/usb.c†L177-L266】

## 4. 제어 흐름
```
resetToReset()
  └─ hwInit()
       └─ usbBootModeLoad()          // EEPROM → RAM
       └─ usbInit()
            └─ usbBegin()
                 ├─ 로그에 현재 부트 모드 출력
                 ├─ HID/Composite 엔드포인트에 HS interval 적용
                 └─ FS 강제 시 PCD 속도 `PCD_SPEED_HIGH_IN_FULL`
```
- **CLI 저장 흐름**
```
cliBoot "set <mode>"
  └─ usbBootModeStore(mode)          // EEPROM 기록
  └─ usbBootModeSaveAndReset()
       └─ 로그 출력 후 시스템 리셋
```
- **모드 적용 시점**: 재부팅 이후 `usbBootModeLoad()`가 다시 호출되면서 새로운 모드가 확정됩니다.

## 5. HID/Composite 및 계측 연동
- HID, VIA, EXK 엔드포인트의 HS `bInterval`은 부트 모드에 따라 동적으로 설정됩니다.
- `usbHidMeasurePollRate()`는 모드가 HS 계열이면 8000프레임, FS이면 1000프레임을 샘플링하여 폴링 주기를 계산합니다.
- Composite 프로필(`usb_cmp/usbd_cmp.c`)은 FS 강제 모드에서도 HS 구성을 최대한 유지하여 입력 손실을 방지합니다.

## 6. 로그 & CLI 명령
| 명령/로그 | 위치 | 설명 |
| --- | --- | --- |
| `boot info` | `usb.c` CLI | 저장된 모드와 레이블을 출력합니다. |
| `boot set {8k|4k|2k|1k}` | `usb.c` CLI | 모드를 저장하고 즉시 리셋을 트리거합니다. |
| `[USB] boot-mode HS 8K` | `usbBegin()` | 초기화가 완료될 때 현재 모드를 기록합니다. |
| `[!] usbBootModeLoad Fail` | `usb.c` | EEPROM에서 유효한 값을 읽지 못했을 때 경고합니다. |

## 7. 버전 히스토리 (요약)
- **V250923R1**: BootMode 인프라 도입, EEPROM 슬롯 정의, 저장/로드 API, CLI, HID/Composite 연동.
- **V250923R2**: Composite FS 환경에서도 HS 패킷 구성을 유지하도록 보강.
- **V250924R1**: 폴링 계측 샘플 윈도우가 부트 모드에 따라 자동 전환되도록 조정.

## 8. Codex 작업 체크리스트
1. `usbBootModeGetHsInterval()`을 호출하는 신규 엔드포인트는 FS 모드에서 `HID_FS_BINTERVAL`을 유지하는지 확인합니다.
2. CLI 확장 시 `usbBootModeSaveAndReset()`이 즉시 리셋을 수행하므로 사용자 메시지를 리셋 전에 모두 출력합니다.
3. EEPROM 슬롯 변경이 필요하면 `port.h`와 `usbBootModeLoad()` 기본값을 함께 수정합니다.
4. HS/FS 파라미터를 조정하면 `usbd_conf.c`, HID/Composite 엔드포인트, CLI 도움말 문자열을 동일하게 업데이트합니다.

## 9. 조건부 컴파일 & 모니터 연동
- // V251108R1: Brick60는 `config.h`에서 `BOOTMODE_ENABLE`을 정의해 BootMode 경로를 포함하며, `USB_MONITOR_ENABLE`이 켜져 있을 때 강제로 BootMode를 켜도록 파생 가드를 추가했습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/config.h†L42-L49】
- // V251108R1: `usbd_hid.c`의 SOF 모니터 블록은 `USB_MONITOR_ENABLE`가 정의된 빌드에서만 컴파일되며, VIA 토글(`usbInstabilityIsEnabled()`)이 `false`면 SOF 핸들러에서 즉시 반환합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1035-L1098】
- 모니터 매크로나 VIA 토글이 꺼져 있으면 `usbProcess()` 상태 머신이 no-op으로 유지되어 CLI `boot set`만으로 모드를 바꿀 수 있습니다.【F:src/hw/driver/usb/usb.c†L200-L278】

## 10. VIA 연동 (Brick60)
- // V251108R1: channel 13 value ID 1(부트 모드 선택)은 런타임 폴링 레이트를 바꾸지 않고 보류 값(`pending_boot_mode`)만 갱신하며, value ID 2(Apply 토글)가 1이 될 때 `usbBootModeSaveAndReset()`을 호출해 CLI `boot set`과 동일한 EEPROM 저장→리셋 흐름을 수행합니다.【F:src/ap/modules/qmk/port/usb_bootmode_via.c†L5-L64】
- `BRICK60-H7S-VIA.JSON`의 "USB POLLING" 블록은 `BOOTMODE_ENABLE`/`USB_MONITOR_ENABLE`이 비활성화된 빌드에서는 파일에서 제거해야 하며, 기본 JSON은 해당 의존성을 라벨에 명시합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/json/BRICK60-H7S-VIA.JSON†L248-L292】
