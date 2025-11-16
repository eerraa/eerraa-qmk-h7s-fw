# BootMode 서브시스템 가이드

## 1. 목적과 범위
- STM32H7S HS PHY 기반 펌웨어에서 USB 폴링 주기를 강제 고정하고, EEPROM/VIA/CLI/USB 모니터가 동일한 상태를 공유하도록 보장합니다.
- HS 8k/4k/2kHz 및 FS 1kHz 모드 간 전환, EEPROM 기본값 관리, VIA Apply 큐, USB monitor 다운그레이드 절차를 모두 기술합니다.
- 대상 모듈: `src/hw/hw.c`, `src/hw/driver/usb/usb.[ch]`, `src/hw/driver/usb/usb_hid/usbd_hid.c`, `src/ap/modules/qmk/port/{bootmode.c,eeconfig_port.c}`, `docs/features_instability_monitor.md`.

## 2. 구성 파일 & 빌드 매크로
| 경로 | 주요 심볼/함수 | 설명 |
| --- | --- | --- |
| `src/hw/hw.c` | `bootmode_init()`, `usbBootModeLoad()` | 부팅 초기에 EEPROM과 램 캐시를 동기화하고 기본값을 강제합니다. |
| `src/hw/driver/usb/usb.h` | `UsbBootMode_t`, `USB_BOOT_MODE_DEFAULT_VALUE`, `bootmode_init()` | 열거형, 기본값 매크로, 다운그레이드 API를 선언합니다. 기본 모드는 FS 1kHz이며 보드에서 재정의할 수 있습니다. |
| `src/hw/driver/usb/usb.c` | `usbBootMode*`, `boot_mode_apply_request`, `usbRequestBootModeDowngrade()` | EEPROM 저장소, Apply 큐, 다운그레이드 상태 머신, CLI 명령을 구현합니다. |
| `src/hw/driver/usb/usb_hid/usbd_hid.c` | `usbHidResolveDowngradeTarget()` | 모니터 이벤트를 받으면 8k→4k→2k→1k 순으로 다음 모드를 계산합니다. |
| `src/ap/modules/qmk/port/bootmode.c` | `via_qmk_usb_bootmode_command()` | VIA channel 13 value ID 1/2를 BootMode API로 연결하고, JSON 값 ↔ 열거형 값을 변환합니다. (V251113R1) |
| `src/ap/modules/qmk/port/eeconfig_port.c` | `eeconfig_init_user_datablock()` | USER 데이터가 초기화될 때 BootMode/USB monitor 슬롯 기본값을 다시 씁니다. |
| `src/hw/driver/usb/usb_hid/usbd_hid_instrumentation.c` | `usbBootModeIsFullSpeed()` | 모드별 샘플 윈도우/`bInterval`을 결정합니다. |

> 빌드 시 `BOOTMODE_ENABLE`이 꺼져 있으면 모든 API가 스텁으로 치환되고 VIA/CLI에서도 해당 항목을 노출하지 않습니다.

## 3. 런타임 흐름
```
hwInit()
  ↳ eepromInit() / eeprom_init()                  // QMK EEPROM 베이스라인
  ↳ bootmode_init()                              // EEPROM 범위 밖 값은 즉시 기본값으로 갱신
  ↳ usb_monitor_init()
  ↳ eepromAutoFactoryResetCheck() (선택)                // AUTO_FACTORY_RESET_ENABLE일 때만
  ↳ usbBootModeLoad()                            // EEPROM → RAM 캐시
  ↳ usbInstabilityLoad()
  ↳ usbInit()/usbBegin()

apMain()
  ↳ usbProcess()                                 // Apply 큐 및 다운그레이드 큐 처리
      ↳ usbProcessBootModeApply()
      ↳ usbProcessBootModeDowngrade()
      ↳ usbProcessDeferredReset()
  ↳ usbHidMonitorBackgroundTick()
  ↳ qmkUpdate()
```
- `bootmode_init()`은 `usbBootModeApplyDefaults()`를 호출하지 않고, EEPROM 슬롯이 비어 있거나 손상된 경우에만 기본값을 기록합니다.
- `usbBootModeLoad()`는 `[  ] USB BootMode : <라벨>` 로그를 출력하여 현재 상태를 CLI 없이도 확인할 수 있습니다.

## 4. API 요약
### 4.1 코어 API (`src/hw/driver/usb/usb.c`)
| 함수 | 설명 |
| --- | --- |
| `bool usbBootModeLoad(void)` | EEPROM 값을 읽어 `usb_boot_mode` 캐시에 저장하고 로그를 남깁니다. 범위를 벗어나면 즉시 기본값을 기록합니다. |
| `UsbBootMode_t usbBootModeGet(void)` | 현재 모드를 반환합니다. IRQ/메인 루프 모두에서 호출됩니다. |
| `bool usbBootModeIsFullSpeed(void)` | FS 1 kHz 모드인지 빠르게 판별합니다. `usbd_conf.c`와 계측 모듈이 사용합니다. |
| `uint8_t usbBootModeGetHsInterval(void)` | HID/Composite 엔드포인트의 HS `bInterval` 값을 돌려줍니다. |
| `bool usbBootModeStore(UsbBootMode_t mode)` | EEPROM에 즉시 씁니다. 동일 값이면 조용히 true를 반환합니다. |
| `bool usbBootModeSaveAndReset(UsbBootMode_t mode)` | 저장 후 `usbScheduleGraceReset()`을 호출해 최소 40ms의 응답 유예 후 리셋을 예약합니다. |
| `bool usbBootModeScheduleApply(UsbBootMode_t mode)` | 인터럽트 문맥에서도 호출 가능한 Apply 큐. 메인 루프에서만 저장/리셋이 발생합니다. |
| `usb_boot_downgrade_result_t usbRequestBootModeDowngrade(...)` | USB monitor가 호출하는 상태 머신. `IDLE → ARMED → COMMIT`를 통해 로그와 저장을 제어합니다. |

### 4.2 VIA/CLI 래퍼
| 위치 | 함수 | 설명 |
| --- | --- | --- |
| `src/ap/modules/qmk/port/bootmode.c` | `via_qmk_usb_bootmode_command()` | channel 13 value ID 1/2 요청을 BootMode API로 포워딩. JSON 값(8k→4k→2k→1k)을 `bootmode_decode_via_value()`로 열거형에 맞춥니다. |
| `src/hw/driver/usb/usb.c` | `cliBoot()` | `boot info`, `boot set {8k|4k|2k|1k}` 명령을 제공하며 Apply 직후 자동 리셋을 예약합니다. |

### 4.3 저장/기본값 훅
| 위치 | 책임 |
| --- | --- |
| `src/ap/modules/qmk/port/eeconfig_port.c` | USER 데이터가 재초기화될 때 `usbBootModeApplyDefaults()`를 호출해 슬롯을 기본값으로 채우고, 플래그/쿠키도 갱신합니다. |
| `src/hw/driver/eeprom_auto_factory_reset.c` | AUTO_FACTORY_RESET_ENABLE 빌드에서 EEPROM을 포맷한 뒤 `usbBootModeApplyDefaults()`를 호출합니다. |

## 5. 데이터 구조
### 5.1 `UsbBootMode_t` ( `src/hw/driver/usb/usb.h` )
| 열거형 값 | 의미 | 비고 |
| --- | --- | --- |
| `USB_BOOT_MODE_FS_1K` | HS PHY + FS 1 kHz 폴링. 기본값. |
| `USB_BOOT_MODE_HS_2K` | HS 2 kHz. |
| `USB_BOOT_MODE_HS_4K` | HS 4 kHz. |
| `USB_BOOT_MODE_HS_8K` | HS 8 kHz. |
| `USB_BOOT_MODE_MAX` | 범위 검사용. |

> V251115R1부터 EEPROM에는 bit31이 1인 새 인코딩(`flag | mode`)이 기록됩니다. 이전 버전의 0~3 값은 부팅 시 자동으로 FS→HS 순서와 일치하도록 변환 후 재기록됩니다.

### 5.2 VIA 인코딩 (V251113R1)
| VIA dropdown 값 | 표시 문자열 | 변환 결과 |
| --- | --- | --- |
| `0` | "8 kHz (HS)" | `USB_BOOT_MODE_HS_8K` |
| `1` | "4 kHz (HS)" | `USB_BOOT_MODE_HS_4K` |
| `2` | "2 kHz (HS)" | `USB_BOOT_MODE_HS_2K` |
| `3` | "1 kHz (FS)" | `USB_BOOT_MODE_FS_1K` |

`bootmode_encode_via_value()`가 RAM 캐시 → VIA 응답, `bootmode_decode_via_value()`가 VIA 설정 → 열거형으로 변환하여 JSON 순서를 바꾸지 않고도 호환됩니다.

### 5.3 Apply 큐 (`boot_mode_apply_request`)
- `pending`, `mode` 필드만 가지는 `static volatile` 구조체입니다.
- 인터럽트(예: VIA RAW HID)에서 Apply를 요청하면 큐에 기록하고, `usbProcessBootModeApply()`가 메인 루프에서 한 번만 처리합니다.
- 동일 모드가 이미 대기 중이면 true만 반환하여 불필요한 리셋을 막습니다.

### 5.4 다운그레이드 큐 (`usb_boot_mode_request_t`)
| 필드 | 역할 |
| --- | --- |
| `stage` | `IDLE/ARMED/COMMIT`. ARM 상태에서는 로그를 한 번만 출력하고, 일정 시간이 지나면 COMMIT으로 승격됩니다. |
| `next_mode` | 다운그레이드가 확정될 때 적용할 타깃 모드. |
| `delta_us` / `expected_us` | USB monitor가 보고한 실제/기대 SOF 간격. 로그 출력과 조건 비교에 사용됩니다. |
| `ready_ms` / `timeout_ms` | ARM ↔ COMMIT 시점 관리. 2초 지연(`USB_BOOT_MONITOR_CONFIRM_DELAY_MS`)을 기준으로 동작합니다. |

### 5.5 EEPROM 슬롯 ( `EECONFIG_USER_BOOTMODE` )
- USER 데이터 블록 +28바이트 위치에 32비트 값으로 저장합니다. 범위를 벗어나면 즉시 `USB_BOOT_MODE_DEFAULT_VALUE`로 복원합니다.
- EEPROM 자동 초기화나 USER 데이터 리셋이 일어나면 `usbBootModeApplyDefaults()`가 호출되어 기본값을 다시 씁니다.

## 6. CLI & VIA 상호작용
- CLI `boot info`는 현재 라벨을 출력합니다. `boot set Xk` 명령은 문자열을 열거형으로 매핑한 뒤 저장/리셋을 예약합니다.
- VIA channel 13 value ID 1은 선택 UI이며, EEPROM에는 쓰지 않고 `pending_boot_mode` 캐시만 갱신합니다.
- VIA value ID 2 (Apply 토글)는 1을 쓰는 순간 `usbBootModeScheduleApply()`가 호출되어 동일 값이라도 리셋이 예약됩니다. 응답 패킷은 요청 값을 그대로 돌려줍니다.
- VIA value ID 3은 USB monitor 토글이므로 BootMode와 동일 채널에서 처리되지만, `via_handle_usb_polling_channel()`에서 BootMode/Monitor를 분기합니다.

## 7. USB 모니터 연동
- 모니터가 이벤트를 감지하면 `usbHidResolveDowngradeTarget()`으로 현 모드보다 낮은 모드를 계산합니다. 순서는 8k→4k→2k→1k입니다.
- `usbRequestBootModeDowngrade()`는 ARM 단계에서 한 번, COMMIT 단계에서 한 번 로그를 출력합니다. COMMIT 단계에서 `usbBootModeSaveAndReset()`을 호출하며 실패 시 `[!] USB Poll 모드 저장 실패`가 발생합니다.
- 다운그레이드 후에는 `usbBootModeRequestReset()`으로 큐를 비우고 다음 이벤트를 기다립니다.

## 8. 로그 & 트러블슈팅
| 로그 | 의미/대응 |
| --- | --- |
| `[  ] USB BootMode : HS 8K` | EEPROM 로드 성공. VIA/CLI 없이도 현재 모드를 확인할 수 있습니다. |
| `[!] USB Poll 불안정 감지 : 기대 ...` | USB monitor가 ARM 상태로 진입했습니다. 네트워크/케이블 상태를 확인하세요. |
| `[!] USB Poll 모드 다운그레이드 -> HS 4K` | 실제 저장 및 리셋이 예약되었습니다. |
| `[!] USB BootMode apply 실패` | EEPROM 쓰기 또는 리셋 예약 실패. EEPROM 드라이버 로그를 확인합니다. |
| `[!] usbBootModeLoad Fail` | EEPROM에서 값을 읽지 못했습니다. `eepromInit()` 또는 하드웨어 문제 가능성이 높습니다.

> BootMode 관련 변경 후에는 `cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/sirind/brick60' && cmake --build build -j10` 로 빌드하여 `_DEF_FIRMWARE_VERSION`과 로그 문자열이 일치하는지 확인하십시오.
