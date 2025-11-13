# BootMode 서브시스템 가이드

## 1. 목적과 범위
- STM32H7S HS PHY 기반 펌웨어에서 USB 폴링 주기를 명시적으로 고정하고, EEPROM/VIA/CLI/USB 모니터가 같은 진실 소스를 바라보도록 보장합니다.
- HS 8k/4k/2kHz와 FS 1kHz 모드 간 전환뿐 아니라, 모니터에 의해 자동 다운그레이드가 예약될 때의 확인 절차까지 설명합니다.
- 대상 모듈: `src/hw/driver/usb/usb.[ch]`, `src/hw/hw.c`, `src/hw/driver/usb/usb_hid/usbd_hid.c`, `src/ap/modules/qmk/port/{port.h,usb_bootmode_via.c}`, `src/ap/ap.c`.

## 2. 구성 파일 & 빌드 매크로
| 경로 | 주요 심볼 | 책임 |
| --- | --- | --- |
| `src/ap/modules/qmk/port/port.h` | `EECONFIG_USER_BOOTMODE` | EEPROM 사용자 데이터 블록 +28 바이트 슬롯을 BootMode 저장소로 예약합니다.
| `src/hw/hw.c` | `usbBootModeLoad()` | `hwInit()` 초기에 EEPROM → RAM 동기화와 부트 로그를 수행합니다.
| `src/hw/driver/usb/usb.h` | `UsbBootMode_t`, `USB_BOOT_MODE_DEFAULT_VALUE`, `bootmode_init()` | 열거형/상수, 기본값 매크로·초기화 진입점, 다운그레이드 인터페이스를 선언합니다. `USB_BOOT_MODE_DEFAULT_VALUE`는 기본적으로 FS 1kHz이며 보드에서 재정의할 수 있습니다. |
| `src/hw/driver/usb/usb.c` | `usbBootMode*`, `usbRequestBootModeDowngrade()`, `usbProcess()` | 저장/적용/큐 상태 머신과 CLI, 로그, 리셋 지연 제어를 담당합니다.
| `src/hw/driver/usb/usbd_conf.c` | `usbBootModeIsFullSpeed()` 사용 | PCD 속도를 `PCD_SPEED_HIGH_IN_FULL`로 강제하거나 HS 모드를 유지합니다.
| `src/hw/driver/usb/usb_hid/usbd_hid.c` | `usbHidResolveDowngradeTarget()` | 모니터에서 폴링 모드 전환 목표를 결정하고 다운그레이드를 요청합니다.
| `src/ap/modules/qmk/port/usb_bootmode_via.c` | `via_qmk_usb_bootmode_command()` | VIA channel 13 value ID 1/2를 BootMode API와 동기화합니다.
| `src/ap/ap.c` | `usbProcess()` 호출 | 메인 루프에서 BootMode Apply 큐·다운그레이드 큐·리셋 큐를 서비스합니다.

> 현재 전역 기본 부트 모드는 FS 1kHz이며, 다른 보드는 `USB_BOOT_MODE_DEFAULT_VALUE`를 자신에게 맞는 `UsbBootMode_t` 값으로 재정의해 사용합니다.

> **빌드 가드**
> - `BOOTMODE_ENABLE`이 꺼지면 모든 API가 스텁으로 치환되고, VIA/CLI에서도 BootMode 관련 항목을 노출하지 않습니다.
> - `USB_MONITOR_ENABLE`이 켜져 있어야 모니터 자동 다운그레이드가 컴파일됩니다. 모니터가 비활성화되면 `usbRequestBootModeDowngrade()` 호출이 스텁으로 치환됩니다.

## 3. API 레퍼런스
| 함수 / 열거형 | 위치 | 설명 |
| --- | --- | --- |
| `UsbBootMode_t` | `src/hw/driver/usb/usb.h` | `HS_8K/4K/2K`, `FS_1K` 값을 제공하고 CLI·VIA·로그에서 동일한 라벨을 재사용합니다.
| `bool usbBootModeLoad(void)` | `src/hw/driver/usb/usb.c` | EEPROM 값을 읽고 범위 검증 후 RAM/로그를 갱신합니다.
| `UsbBootMode_t usbBootModeGet(void)` | `src/hw/driver/usb/usb.c` | 현재 RAM 캐시에 저장된 모드를 반환합니다.
| `bool usbBootModeIsFullSpeed(void)` | `src/hw/driver/usb/usb.c` | FS 1kHz 요청 여부를 확인하여 PCD/엔드포인트 구성에 사용합니다.
| `uint8_t usbBootModeGetHsInterval(void)` | `src/hw/driver/usb/usb.c` | HID/Composite 엔드포인트 HS `bInterval` 값을 선택합니다.
| `bool usbBootModeStore(UsbBootMode_t mode)` | `src/hw/driver/usb/usb.c` | EEPROM에 선택값을 기록하고 RAM 캐시를 업데이트합니다.
| `bool usbBootModeSaveAndReset(UsbBootMode_t mode)` | `src/hw/driver/usb/usb.c` | 저장 후 `usbScheduleGraceReset()`을 호출해 리셋을 예약합니다.
| `bool usbBootModeScheduleApply(UsbBootMode_t mode)` | `src/hw/driver/usb/usb.c` | 인터럽트 문맥에서 Apply 요청을 큐에 적재하고, 메인 루프에서만 리셋을 실행하도록 합니다.
| `usb_boot_downgrade_result_t usbRequestBootModeDowngrade(...)` | `src/hw/driver/usb/usb.c` | 모니터가 측정한 SOF 간격과 목표 모드를 바탕으로 ARM/COMMIT 단계를 제어합니다.
| `void usbProcess(void)` | `src/hw/driver/usb/usb.c` | BootMode Apply, 다운그레이드, 지연 리셋 큐 중 처리할 항목이 있을 때만 상태 머신을 실행합니다.
| `void via_qmk_usb_bootmode_command(uint8_t *data, uint8_t length)` | `src/ap/modules/qmk/port/usb_bootmode_via.c` | VIA channel 13 value ID 1(선택)과 2(Apply)를 BootMode API로 라우팅합니다.
| `bool usbScheduleGraceReset(uint32_t delay_ms)` | `src/hw/driver/usb/usb.c` | CLI/VIA 응답이 송신될 수 있도록 지연 리셋을 요청합니다.

## 4. 핵심 데이터 구조
### 4.1 `usb_boot_mode_request_t` (`src/hw/driver/usb/usb.c`)
| 필드 | 설명 |
| --- | --- |
| `stage` (`usb_boot_mode_request_stage_t`) | `IDLE → ARMED → COMMIT` 상태를 정의합니다.
| `log_pending` | 단계 전환 시 로그를 한 번만 출력하도록 제어합니다.
| `next_mode` | 다운그레이드 후 적용할 타깃 모드.
| `delta_us` / `expected_us` | 모니터가 전달한 실제/기대 SOF 간격.
| `ready_ms` | 2차 확인(검증 대기) 가능 시각. 기본 `now + USB_BOOT_MONITOR_CONFIRM_DELAY_MS`.
| `timeout_ms` | 검증 대기가 만료되는 시각. 만료 시 큐를 초기화합니다.

### 4.2 `boot_mode_apply_request`
- `pending`, `mode` 필드만을 가지는 `static volatile` 구조체로, 인터럽트에서 Apply 요청을 기록한 뒤 메인 루프가 단일 항목만 처리하도록 합니다.
- 동일 모드 중복 요청은 큐를 새로 만들지 않고 true를 반환해 불필요한 리셋을 방지합니다.

### 4.3 `usb_boot_downgrade_result_t`
- `REJECTED/ARMED/CONFIRMED` 세 단계를 구분하여 모니터가 이벤트를 중복 보고하지 않도록 합니다.

### 4.4 EEPROM 슬롯 (`EECONFIG_USER_BOOTMODE`)
- 4바이트 `uint32_t`로 저장하며, 범위 밖 값은 `USB_BOOT_MODE_DEFAULT_VALUE`(기본 FS 1kHz)로 복구합니다.
- VIA/CLI/모니터는 항상 `usbBootModeStore()` 경유로 쓰기 때문에 별도의 CRC는 사용하지 않습니다.

## 5. 제어 흐름
```
reset → hwInit()
  ↳ usbBootModeLoad()
  ↳ usbInit()/usbBegin()               // 초기 모드 로그
      ↳ usb_hid/usbd_conf : HS/FS 파라미터 구성

apMain() 루프
  ↳ usbProcess()
      ↳ usbProcessBootModeApply()      // VIA/CLI Apply 요청 처리
      ↳ usbProcessBootModeDowngrade()  // 모니터 큐 (ARMED/COMMIT)
      ↳ usbProcessDeferredReset()      // 응답 송신 후 리셋
```
- CLI `boot info/set` → `usbBootModeStore()` → `usbScheduleGraceReset()` 순으로 동작하며, 최소 40ms의 응답 송신 유예를 둡니다.
- 모니터는 `usbRequestBootModeDowngrade()`가 `COMMIT`을 반환했을 때만 `usbBootModeSaveAndReset()`을 호출하고, 실패 시 로그를 남깁니다.
- VIA channel 13 value ID 2는 **무조건** `usbBootModeScheduleApply()`를 호출하여 현재 값과 동일하더라도 리셋이 이루어집니다.

## 6. CLI & VIA 상호작용
- `boot info` : 현재 모드 라벨을 출력.
- `boot set {8k|4k|2k|1k}` : 문자열을 파싱하여 `UsbBootMode_t` 값으로 변환한 뒤 저장/리셋을 예약합니다.
- VIA channel 13:
  - value ID 1 = BootMode 선택. EEPROM에는 즉시 쓰지 않고 `pending_boot_mode`로만 캐시합니다.
  - value ID 2 = Apply 토글. 1로 쓰면 `usbBootModeScheduleApply()`가 호출되고, 응답 패킷은 원본 값을 그대로 에코합니다.
  - `id_custom_save` 명령은 no-op 처리하여 VIA UI에서 저장 버튼을 눌러도 오류가 발생하지 않습니다.
- JSON (`BRICK60-H7S-VIA.JSON`)의 "USB POLLING" 블록은 `BOOTMODE_ENABLE` 또는 `USB_MONITOR_ENABLE`이 꺼진 빌드에서는 제거해야 합니다.

## 7. 운영 체크리스트
1. 새로운 HID 엔드포인트를 추가할 때는 반드시 `usbBootModeGetHsInterval()`을 사용하여 `bInterval`을 결정합니다.
2. `usbProcess()`에 로직을 추가할 경우, 큐가 모두 비어 있으면 즉시 반환해야 메인 루프 주기가 유지됩니다.
3. EEPROM 슬롯 레이아웃을 변경하면 `port.h`, `usbBootModeLoad()`, VIA JSON 문서까지 동시에 업데이트합니다.
4. 모니터와 연동되는 코드를 건드렸다면 `USB_BOOT_MONITOR_CONFIRM_DELAY_MS` 및 HS/FS 샘플 윈도우가 기대대로 유지되는지 확인합니다.
5. 리셋 전에 반드시 `USBD_Stop()`/`USBD_DeInit()` → `USB_RESET_DETACH_DELAY_MS` 지연 순서를 지켜야 호스트가 안전하게 디바이스 분리를 감지합니다.

## 8. 로그 & 트러블슈팅
- `[  ] USB BootMode : HS 8K` : EEPROM 로드 성공.
- `[!] USB Poll 불안정 감지 ... (검증 대기)` : 모니터가 ARMED 상태에 들어갔음을 의미합니다. 같은 메시지에서 `next_mode`를 확인할 수 있습니다.
- `[!] USB Poll 모드 저장 실패` : EEPROM 쓰기 실패. 전원이 불안정한지, I2C 잠금을 확인해야 합니다.
- `[!] USB BootMode apply 실패` : `usbBootModeSaveAndReset()`이 false를 반환한 케이스. 주로 EEPROM 오류 또는 리셋 스케줄러 고장입니다.

> **팁**: BootMode 경로를 수정한 후에는 `cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/sirind/brick60' && cmake --build build -j10` 로 빌드하여 `_DEF_FIRMWATRE_VERSION`과 로그 문자열이 일치하는지 확인한 뒤 `rm -rf build`로 정리하십시오.
