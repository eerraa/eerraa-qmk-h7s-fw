# BootMode 서브시스템 가이드 (V260823R2)

## 1. 정책

STM32H7S 내장 HS PHY에서 FS 1 kHz, HS 2/4/8 kHz를 지원합니다. 기본값은 FS 1 kHz이며 모드 변경은 VIA 또는 CLI에서 사용자가 명시적으로 적용할 때만 EEPROM 저장과 재부팅이 일어납니다.

USB 상태, 진단 결과, SOF 간격 때문에 모드가 자동으로 바뀌는 경로는 없습니다. 진단과 수동 비교는 `docs/features_usb_diagnostics.md`를 참고하십시오.

## 2. 구성

| 위치 | 책임 |
| --- | --- |
| `src/hw/driver/usb/usb.[ch]` | `UsbBootMode_t`, EEPROM 캐시, 사용자 apply 큐, 응답 유예 reset |
| `src/hw/driver/usb/usbd_conf.c` | FS/HS PCD 속도 선택 |
| `src/hw/driver/usb/usb_hid/usbd_hid.c` | 모드에 따른 HID endpoint `bInterval` |
| `src/ap/modules/qmk/port/bootmode.c` | VIA channel 13 value ID 1/2와 mode 변환 |
| `src/ap/modules/qmk/port/eeconfig_port.c` | USER 초기화 시 FS 1 kHz 기본값 기록 |

`BOOTMODE_ENABLE`이 없는 빌드에서는 API가 스텁이며 메뉴도 노출하지 않습니다.

## 3. 모드와 wire 값

| enum | 실제 모드 | HS `bInterval` | VIA dropdown |
| --- | --- | --- | --- |
| `USB_BOOT_MODE_FS_1K` | FS 1 kHz | FS endpoint interval 1 | 3 |
| `USB_BOOT_MODE_HS_2K` | HS 2 kHz | 3 | 2 |
| `USB_BOOT_MODE_HS_4K` | HS 4 kHz | 2 | 1 |
| `USB_BOOT_MODE_HS_8K` | HS 8 kHz | 1 | 0 |

`EECONFIG_USER_BOOTMODE`는 USER block +28 위치의 32비트 값입니다. 범위를 벗어나면 `USB_BOOT_MODE_DEFAULT_VALUE`(기본 FS 1 kHz)를 기록합니다.

## 4. VIA 적용 흐름

```
channel 13 / value 1 SET
  → pending_boot_mode만 변경 (저장/재부팅 없음)

channel 13 / value 2 SET=1
  → usbBootModeScheduleApply()
  → apMain() / usbProcess()
  → usbProcessBootModeApply()
  → usbBootModeSaveAndReset()
  → EEPROM queue 기록
  → VIA 응답을 위한 최소 40 ms 유예
  → USB detach 100 ms 후 MCU reset
```

동일 mode를 고르면 EEPROM 쓰기는 생략할 수 있지만 Apply 요청의 재열거 의도는 유지됩니다. 이 큐와 reset은 사용자 명시 경로이며 진단 subsystem이 호출하지 않습니다.

## 5. API

| 함수 | 의미 |
| --- | --- |
| `usbBootModeLoad()` | EEPROM → RAM cache, 손상 시 기본값 복원 |
| `usbBootModeGet()` | 현재 cache 반환 |
| `usbBootModeIsFullSpeed()` | FS 1 kHz 여부 |
| `usbBootModeGetHsInterval()` | endpoint descriptor용 HS interval |
| `usbBootModeStore()` | 유효 mode를 EEPROM queue에 기록 |
| `usbBootModeScheduleApply()` | 사용자 apply를 main loop로 defer |
| `usbBootModeSaveAndReset()` | 저장 후 응답 유예 reset 예약 |

CLI는 `boot info`, `boot set {1k|2k|4k|8k}`를 제공합니다. CLI set도 사용자 명시 요청으로 동일 저장/reset 경로를 사용합니다.

## 6. 점검

- 부팅 로그 `[  ] USB BootMode : FS 1K`가 저장 상태와 일치하는지 확인합니다.
- VIA value ID 3 또는 Auto downgrade control은 더 이상 정의하지 않습니다.
- 각 mode 적용은 연결을 끊으므로 실행 중 진단 세션은 앱에서 중단 처리하고, 재연결 후 새 세션으로 비교합니다.
- `usbProcess()`에는 사용자 apply/reset 큐 외의 자동 downgrade 단계가 없어야 합니다.
