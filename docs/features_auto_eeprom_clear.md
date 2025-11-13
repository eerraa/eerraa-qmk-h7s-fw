# 자동 EEPROM 초기화( AUTO_EEPROM_CLEAR ) 가이드

## 1. 목적과 범위
- 펌웨어 업데이트나 대규모 레이아웃 변경 이후 사용자가 수동으로 EEPROM을 초기화하지 않아도 되도록, 빌드 타임 스위치로 전체 EEPROM을 포맷하고 기본값을 다시 쓰는 절차를 제공합니다.
- BootMode, USB instability monitor, VIA 사용자 데이터 등 EEPROM 의존 기능이 항상 초기화 이후 값을 다시 로드하도록 보장합니다.
- 대상 모듈: `src/hw/driver/eeprom_auto_clear.c`, `src/hw/hw.c`, `src/ap/modules/qmk/port/eeconfig_port.c`, `docs/features_bootmode.md`, `docs/features_instability_monitor.md`.

## 2. 구성 파일 & 빌드 매크로
| 경로 | 심볼/함수 | 설명 |
| --- | --- | --- |
| `src/hw/hw_def.h` | `AUTO_EEPROM_CLEAR_ENABLE`, `AUTO_EEPROM_CLEAR_COOKIE`, `AUTO_EEPROM_CLEAR_FLAG_MAGIC` | 빌드 타임 스위치와 쿠키/플래그 기본값을 정의합니다. 기본값은 `_DEF_FIRMWATRE_VERSION`(`VYYMMDDRn`)을 BCD로 변환한 값입니다. |
| `src/hw/driver/eeprom_auto_clear.c` | `eepromAutoClearCheck()` | 자동 초기화의 모든 로직이 구현되어 있습니다. `AUTO_EEPROM_CLEAR_ENABLE`이 0이면 단순히 true를 반환합니다. |
| `src/ap/modules/qmk/port/eeconfig_port.c` | `eeconfig_init_user_datablock()` | USER 데이터 초기화 시 BootMode/USB monitor 슬롯과 자동 초기화 플래그/쿠키를 갱신합니다. |
| `src/hw/hw.c` | `eepromAutoClearCheck()` 호출 | `eeprom_init()` 이후, BootMode/USB monitor가 로드되기 전에 자동 초기화를 수행합니다. |

## 3. EEPROM 슬롯 레이아웃
| 심볼 | 오프셋 (`EECONFIG_USER_DATABLOCK` 기준) | 크기 | 의미 |
| --- | --- | --- | --- |
| `EECONFIG_USER_BOOTMODE` | +28 | 4B | BootMode 저장소. `usbBootModeApplyDefaults()`가 기본값을 기록합니다. |
| `EECONFIG_USER_USB_INSTABILITY` | +32 | 4B | USB monitor 토글 저장소 (`usb_monitor_config_t`). |
| `EECONFIG_USER_EEPROM_CLEAR_FLAG` | +36 | 4B | 자동 초기화 완료 여부. `AUTO_EEPROM_CLEAR_FLAG_MAGIC`("VCLR")가 기록되면 완료로 간주합니다. |
| `EECONFIG_USER_EEPROM_CLEAR_COOKIE` | +40 | 4B | 마지막 초기화를 트리거한 버전 쿠키. 현재 빌드 쿠키와 다르면 다시 초기화합니다. |

USER 데이터 블록은 512바이트가 예약되어 있어 위 오프셋이 겹치지 않습니다.

## 4. 런타임 흐름
```
hwInit()
  ↳ eepromInit()
  ↳ eeprom_init()
  ↳ bootmode_init() / usb_monitor_init()
  ↳ eepromAutoClearCheck()
      ↳ sentinel 판독 (flag, cookie)
      ↳ 필요 시 전체 EEPROM 포맷
      ↳ eeconfig_disable()/eeconfig_init()/*_datablock() 재호출
      ↳ usbBootModeApplyDefaults() / usb_monitor_storage_apply_defaults()
      ↳ 플래그/쿠키 갱신 후 flush
      ↳ resetToReset() (성공 시 자동 리셋)
  ↳ usbBootModeLoad()
  ↳ usbInstabilityLoad()
```
- 자동 초기화는 BootMode/USB monitor 로드 전에 완료되며, 실패하더라도 `false`를 반환하지 않고 단순히 경고 로그만 남깁니다.
- 성공 시 `[  ] EEPROM auto clear : success ...` 로그 뒤 10 ms 지연 후 소프트 리셋을 실행합니다.

## 5. 알고리즘 세부 절차 (`eepromAutoClearCheck`)
1. **센티넬 판독** : `flag_addr`와 `cookie_addr`에서 32비트 값을 읽습니다. 플래그가 매직 값이고 쿠키가 현재 빌드 쿠키와 같으면 아무 작업도 하지 않습니다.
2. **플래그 리셋** : 플래그는 매직인데 쿠키가 다르면 플래그를 0으로 되돌리고 다시 검사를 진행합니다.
3. **EEPROM 포맷** : `eepromFormat()` 실패 시 경고 로그 후 false를 반환합니다.
4. **버퍼 재동기화** : `eeprom_init()`로 QMK EEPROM 미러를 재설정한 뒤 `eeconfig_disable()`/`eeconfig_init()`/`eeconfig_init_*()`를 호출해 코어, 키보드, 사용자 데이터 블록을 순서대로 초기화합니다.
5. **커스텀 기본값** : `usbBootModeApplyDefaults()`와 `usb_monitor_storage_apply_defaults()`를 호출하여 BootMode/USB monitor 슬롯을 초기화 직후 값으로 덮어씁니다. 이후 `eeprom_flush_pending()`으로 비동기 큐를 소진합니다.
6. **센티넬 갱신** : 플래그에 매직 값을 다시 쓰고, 쿠키에 현재 빌드 쿠키를 기록한 뒤 flush 합니다.
7. **리셋 예약** : 모든 쓰기가 끝나면 `[  ] EEPROM auto clear : scheduling reset` 로그 후 `resetToReset()`을 호출합니다.

## 6. BootMode & USB Monitor 연동
- USER 데이터가 플래시에서 지워지면 `eeconfig_init_user_datablock()`이 호출되어 BootMode/USB monitor 기본값을 즉시 기록합니다. 자동 초기화 루틴도 동일한 함수를 이용하므로, 별도의 버전 마이그레이션 코드를 중복 작성할 필요가 없습니다.
- BootMode 동작 전체는 `docs/features_bootmode.md`, USB monitor 동작은 `docs/features_instability_monitor.md`를 참고하십시오.

## 7. 사용 방법
1. 빌드 시 `AUTO_EEPROM_CLEAR_ENABLE=1`과 새로운 `_DEF_FIRMWATRE_VERSION`을 지정합니다. 쿠키를 수동으로 지정하려면 `-DAUTO_EEPROM_CLEAR_COOKIE=0xYYMMDDRR` 형태로 넘깁니다.
2. 대상 사용자가 기존 설정을 보존해야 한다면 해당 빌드 옵션을 끄고, 릴리스 노트에 수동 초기화 절차를 안내합니다.
3. 자동 초기화 빌드를 배포할 때는 첫 부팅 로그에 `[  ] EEPROM auto clear : begin ...` 메시지가 출력되는지 확인합니다.

## 8. 로그 & 트러블슈팅
| 로그 | 의미/대응 |
| --- | --- |
| `[  ] EEPROM auto clear : begin ...` | 플래그/쿠키가 일치하지 않아 초기화를 시작했습니다. |
| `[!] EEPROM auto clear : sentinel read fail` | EEPROM 드라이버에서 값을 읽지 못했습니다. I2C/Flash 구성을 확인하십시오. |
| `[!] EEPROM auto clear : format fail` | `eepromFormat()` 실패. 재시도 시에도 실패하면 AUTO_EEPROM_CLEAR를 비활성화하고 원인을 조사합니다. |
| `[!] EEPROM auto clear : cookie write fail` | 쿠키 저장 실패. 플래그를 0으로 되돌린 뒤 경고를 출력하므로 다음 부팅에서 다시 시도됩니다. |
| `[  ] EEPROM auto clear : scheduling reset` | 초기화가 성공적으로 끝났으며 곧 소프트 리셋이 실행됩니다. |

> 자동 초기화 빌드를 테스트할 때는, 한 번 부팅해 로그를 확인한 뒤 다시 부팅해 플래그/쿠키가 유지되어 재초기화가 발생하지 않는지 검증해야 합니다.
