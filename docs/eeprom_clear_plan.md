# EEPROM 전체 초기화 자동화 계획

## 1. 배경 및 목표
- 펌웨어 업데이트 직후 EEPROM 데이터를 강제 초기화해야 하는 릴리스가 늘어나면서, 사용자가 수동으로 `qmk clear eeprom` 혹은 VIA 리셋을 수행하지 않아도 되도록 자동화가 필요합니다.
- `AUTO_EEPROM_CLEAR_ENABLE` 빌드에서만 동작하며, 부팅이 USB 초기화·VIA 로직보다 앞서 실행될 때 모든 EEPROM 슬롯(코어 + KB + USER)을 기본값으로 되돌린 뒤 안전하게 마이그레이션 로그를 남기는 것이 목표입니다.
- 신규 절차는 BootMode, USB instability monitor, Indicator 등 EEPROM 기반 서브시스템이 항상 초기화 이후 값을 다시 로드하도록 보장해야 합니다.

## 2. 요구 조건 정리
1. **조건 1 – 빌드 가드**: `AUTO_EEPROM_CLEAR_ENABLE`가 선언된 바이너리에서만 자동 초기화 루틴을 링크합니다. 기본 빌드에서는 완전히 컴파일 제외하여 실수로 EEPROM이 지워지는 일을 막습니다.
2. **조건 2 – EEPROM 플래그**: `EECONFIG_USER_DATABLOCK` 내에 플래그 슬롯을 두고, 값이 `0`이면 전체 EEPROM을 포맷하고 `1`로 갱신합니다. 이미 `1`인 경우에는 초기화를 건너뜁니다.
3. **조건 3 – 쿠키 버전 관리**: 모든 자동 초기화 빌드는 반드시 `AUTO_EEPROM_CLEAR_COOKIE` 값을 정의하며, 부팅 시 EEPROM에 저장된 쿠키와 불일치하면 플래그를 0으로 되돌리고 다시 초기화를 실행합니다. 쿠키는 릴리스마다 바꿔야 하며, 같은 쿠키를 재사용하면 중복 초기화가 발생하지 않습니다.
4. **상세 요구**: 초기화 성공 여부를 로그에 기록하고, 실패 시에는 USB/CLI를 제한하는 대신 경고만 출력합니다. 또한, OTA나 SWD 업데이트 후에도 쿠키 비교만으로 반복 초기화가 자동으로 트리거되어야 합니다.

## 3. 현행 동작 점검
| 단계 | 위치 | 설명 |
| --- | --- | --- |
| 하드웨어 EEPROM 초기화 | `src/hw/hw.c` → `eepromInit()` | STM32H7S FLASH 기반 emulated EEPROM 초기화·로그 출력. |
| QMK EEPROM 미들웨어 | `src/hw/hw.c` → `eeprom_init()` | `platforms/eeprom.c` 경유로 QMK `eeconfig_init_*`가 사용 가능한 상태를 만듭니다. |
| 부팅 직후 EEPROM 소비자 | `usbBootModeLoad()`, `usbInstabilityLoad()`, 각종 `eeconfig_*` | EEPROM이 초기화되기 전에 호출되면 기본값이 아닌 이전 데이터를 읽습니다. |
| 수동 초기화 | `cli qmk clear eeprom`, VIA, 커스텀 CLI | 사용자가 직접 실행해야 하며, 강제 업데이트 시 실수가 잦습니다. |

## 4. 설계 제안

### 4.1 빌드 제어 매크로
- `hw_def.h` 또는 상위 CMake 옵션에 `AUTO_EEPROM_CLEAR_ENABLE`를 정의합니다. 기본값은 0이며, 단계 1에서 전역 매크로로 추가되었습니다.
- `AUTO_EEPROM_CLEAR_COOKIE`는 **필수** 매크로입니다. 현재 구현(V251112R2)은 `_DEF_FIRMWATRE_VERSION`이 따르는 `VYYMMDDRn` 포맷을 그대로 BCD(YY|MM|DD|Rn)로 변환해 기본 쿠키(`0xYYMMDDRn`)를 생성하며, 빌드 옵션으로 재정의할 수도 있습니다. 예) `V251112R1` → `0x25111201`, `V251112R2` → `0x25111202`.

### 4.2 센티넬 슬롯 설계
| 심볼 | 오프셋 제안 | 크기 | 의미 |
| --- | --- | --- | --- |
| `EECONFIG_USER_EEPROM_CLEAR_FLAG` | `EECONFIG_USER_DATABLOCK + 36` | 4B | `0x00000000` 또는 `0xFFFFFFFF` → 미초기화, `0x56434C52`(“VCLR”) → 초기화 완료 |
| `EECONFIG_USER_EEPROM_CLEAR_COOKIE` | `+40` | 4B | 마지막으로 초기화를 수행한 쿠키. 부팅 시점 쿠키와 다르면 플래그를 리셋 후 재초기화 |
- Brick60 설정에서 USER 데이터는 512바이트가 예약되어 있으므로 위 오프셋은 기존 슬롯(0~35B)과 충돌하지 않습니다.

### 4.3 부팅 흐름
```
hwInit()
  ↳ eepromInit()
  ↳ eeprom_init()
  ↳ eepromAutoClearCheck()  // 신규
      ↳ (조건 충족 시) eepromLowLevelFormat()
      ↳ eeconfig_init() / eeconfig_init_quantum()
      ↳ 커스텀 EEconfig 헬퍼 재플러시
      ↳ 플래그/쿠키 갱신
  ↳ usbBootModeLoad()
  ↳ usbInstabilityLoad()
```
- 자동 초기화는 USB/CDC/BootMode 로드 이전에 끝나야 하며, 실패 시에도 이후 흐름이 계속되도록 `bool` 반환값만 확인합니다.

### 4.4 초기화 루틴 세부 절차
1. **빌드 조건 확인**: `#ifdef AUTO_EEPROM_CLEAR_ENABLE` 경로에서만 함수 본문을 유지합니다.
2. **센티넬 판독**: `eepromRead()`로 플래그와 쿠키를 읽습니다. 값이 `0x56434C52`가 아니거나 쿠키가 일치하지 않으면 초기화를 예약합니다.
3. **저수준 포맷**: `eepromFormat()`을 호출해 물리 EEPROM 전역을 삭제합니다. 실패 시 로그를 `[!] EEPROM format fail` 형태로 남깁니다.
4. **버퍼 재동기화**: 포맷 이후 `eeprom_init()`을 다시 호출해 QMK EEPROM 미러 버퍼와 하드웨어 상태를 0으로 맞춥니다. 이렇게 해야 이후 `eeconfig_init_*()`가 동일한 기준에서 동작합니다.
5. **QMK EEconfig 재설정**:
   - `eeconfig_disable()` → `eeconfig_init()`.
   - `eeconfig_init_kb_datablock()`/`eeconfig_init_user_datablock()`를 호출하여 512B 사용자 슬롯을 0으로 초기화합니다.
   - Indicator, USB monitor 등 `EECONFIG_DEBOUNCE_HELPER` 기반 모듈은 `eeconfig_init_*()`와 `eeconfig_flush_*()`를 통해 기본값을 다시 써 줍니다.
6. **플래그/쿠키 기록**: 초기화 성공 시 `0x56434C52`(“VCLR”)와 현재 빌드의 `AUTO_EEPROM_CLEAR_COOKIE` 값을 함께 기록합니다. 이 과정에서 쿠키 쓰기가 실패하면 플래그도 0으로 되돌려 다음 부팅에서 재시도하게 합니다.
7. **로그**: `logPrintf("[  ] EEPROM auto clear : DONE (cookie=0x%08X)\n");` 형태로 결과를 남깁니다.

### 4.5 쿠키 기반 반복 초기화 전략
1. **릴리스별 고유 쿠키**: 빌드 스크립트가 `_DEF_FIRMWATRE_VERSION`과 동기화된 쿠키를 자동으로 생성하면, 버전 문자열만 바꿔도 새로운 쿠키가 주입되어 초기화가 보장됩니다.
2. **재시도 로직**: 부팅 시 EEPROM의 쿠키와 빌드 쿠키를 비교해 다르면 즉시 플래그를 `0x00000000`으로 덮어쓰고, 동일 부팅 사이클 내에서 곧바로 `eepromAutoClearCheck()`를 다시 진입시켜 포맷을 반복합니다.
3. **선택적 스킵**: 개발자가 같은 쿠키를 명시적으로 재사용하면 이미 초기화된 장비를 다시 포맷하지 않고도 디버깅이 가능합니다.
4. **마이그레이션 확장**: 쿠키 슬롯을 복수 개로 확장하면 BootMode, Indicator, VIA JSON 등 개별 서브시스템의 마이그레이션 상태도 동일 메커니즘으로 추적할 수 있습니다.

## 5. 구현 단계 체크리스트
### 5.1 단계 1 – 빌드 심볼 및 슬롯 준비 (완료: V251112R1)
1. `src/hw/hw_def.h`에 `AUTO_EEPROM_CLEAR_ENABLE`, 자동 초기화 플래그 매직, `_DEF_FIRMWATRE_VERSION` 기반 기본 쿠키 생성 매크로(`AUTO_EEPROM_CLEAR_COOKIE_DEFAULT`)를 선언했습니다.
2. `src/ap/modules/qmk/port/port.h`에 `EECONFIG_USER_EEPROM_CLEAR_FLAG`/`COOKIE` 매크로를 추가해 USER 데이터 블록에 슬롯을 예약했습니다.
3. 향후 필요 시 `-DAUTO_EEPROM_CLEAR_COOKIE=0x...` 옵션으로 기본 쿠키를 덮어쓸 수 있으며, 현재 구현은 `_DEF_FIRMWATRE_VERSION`을 BCD로 변환하는 헬퍼로 자동 생성합니다.

### 5.2 단계 2 – 센티넬 로직 및 헬퍼 구현 (완료: V251112R2)
1. `src/hw/driver/eeprom_auto_clear.c`/`.h`에 `bool eepromAutoClearCheck(void)`를 추가했습니다. `AUTO_EEPROM_CLEAR_ENABLE`이 1일 때만 전체 로직이 컴파일되며, 기본값(0)에서는 즉시 true를 반환합니다.
2. 함수는 플래그/쿠키를 `eepromRead()`로 읽어 비교하고, 쿠키 불일치 시 즉시 플래그를 `AUTO_EEPROM_CLEAR_FLAG_RESET`으로 돌려 다음 부팅에서도 재시작할 수 있게 합니다.
3. 포맷 단계는 `eepromFormat()` → `eeprom_init()` 순으로 진행해 하드웨어/버퍼 상태를 동기화한 뒤 `eeconfig_disable()`/`eeconfig_init()`/`eeconfig_init_{kb,user}_datablock()`를 호출합니다.
4. 플래그·쿠키는 `eepromWrite()`로 직접 기록하며, 실패 시 플래그를 0으로 되돌리고 `[!] EEPROM auto clear` 로그를 남긴 뒤 false를 반환합니다.
5. 정상 완료 시 `[  ] EEPROM auto clear : success (cookie=0xXXXXXXXX)` 로그를 출력해 이후 단계(부팅 경로 통합)에서 재사용할 수 있는 상태 코드를 제공합니다.

### 5.3 단계 3 – 부팅 경로 통합 및 로깅 (완료: V251112R3)
1. `src/hw/hw.c`의 `hwInit()`에서 `eeprom_init()` 직후 `eepromAutoClearCheck()`를 호출하도록 연결했습니다. 반환값은 `(void)` 처리해 릴리스/디버그 빌드 모두에서 경고가 발생하지 않습니다.
2. 성공/실패 로그는 `eepromAutoClearCheck()` 내부에서 출력되며(`"[  ] EEPROM auto clear : success..."`, `"[!] ... fail"`), `AUTO_EEPROM_CLEAR_ENABLE`이 0인 빌드에서는 함수가 즉시 true를 반환해 추가 로그가 남지 않습니다.
3. BootMode/USB Monitor 등 EEPROM 소비자 호출 순서를 점검해, 모두 자동 초기화 이후에 실행되도록 유지했습니다. (현재 호출 순서는 `eepromAutoClearCheck()` → `usbBootModeLoad()` → `usbInstabilityLoad()` 순서로 보장됩니다.)

### 5.4 단계 4 – 모듈 후처리 및 개발자 도구
1. Indicator, USB monitor, BootMode 등의 `eeconfig_init_*()` 호출부를 점검해 자동 초기화 직후 기본값이 즉시 반영되도록 보완합니다. 필요하면 각 모듈에 “auto-clear hook”을 추가합니다.
2. 개발 편의를 위해 CLI(예: `qmk auto_clear status/reset`)를 추가하여 플래그/쿠키 값을 조회·초기화할 수 있게 합니다.
3. 문서 업데이트: `docs/features_bootmode.md`, `docs/features_instability_monitor.md`, 릴리스 노트에 자동 초기화 조건과 쿠키 사용법을 기재합니다.
4. 테스터 가이드를 작성해 OTA/UF2 업데이트 시 쿠키 로그를 확인하는 방법을 안내합니다.

## 6. 테스트 및 검증 계획
- **Cold boot 시나리오**: 펌웨어를 플래싱한 뒤 첫 부팅 로그에서 `[  ] EEPROM auto clear`가 출력되고, VIA 설정/BootMode가 기본값으로 리셋되는지 확인합니다.
- **재부팅 시나리오**: 같은 바이너리를 두 번째로 부팅했을 때 플래그가 `VCLR`라면 추가 초기화가 발생하지 않아야 합니다.
- **쿠키 강제 업데이트**: `AUTO_EEPROM_CLEAR_COOKIE` 값을 변경한 빌드로 업데이트하면 다시 포맷이 실행되는지 확인합니다.
- **에러 경로**: `eepromFormat()` 실패를 강제로 트리거(테스트 훅 or 모의)하여, 실패 로그가 출력되고 부팅이 계속되는지 확인합니다.
- **VIA/CLI 연동**: 자동 초기화 이후 `qmk` CLI가 정상적으로 동작하며, Indicator/USB monitor 상태가 기본값으로 복귀했는지 점검합니다.

## 7. 추가 아이디어
1. **CLI 토글 제공**: `cli qmk auto_clear enable/disable` 명령을 추가하여, 개발 중에도 센티넬을 손쉽게 0/1로 바꿀 수 있게 합니다.
2. **릴리스별 적용율 통계**: `logBoot()`나 USB CLI에 쿠키 값을 표시해 현장에서 특정 버전의 자동 초기화가 실제 수행되었는지 추적합니다.
3. **UF2 메타데이터 연동**: 빌드 스크립트(`tools/`)에서 `AUTO_EEPROM_CLEAR_COOKIE`를 UF2 헤더에 기록하여, 배포 문서에서 어떤 쿠키가 포함되었는지 자동으로 표기하도록 합니다.
