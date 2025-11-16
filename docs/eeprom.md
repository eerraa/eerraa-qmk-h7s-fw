# EEPROM 경로 가이드

본 문서는 V251112R8 시점의 EEPROM 전체 구조/정책을 정리한 가이드입니다. 현재 프로젝트는 **외부 I2C EEPROM(ZD24C128)** 만을 사용하므로, 내부 플래시 에뮬레이션 드라이버(`EEPROM_CHIP_EMUL`)는 동작 검증 대상에는 포함되지 않았음을 명시합니다. 다만 내부 경로에도 동일한 리팩터링이 반영되어 있으므로, 향후 필요 시 바로 테스트할 수 있도록 준비되어 있습니다.

## 1. 계층 구조 개요
- **QMK 비동기 큐 계층**: `src/ap/modules/qmk/port/platforms/eeprom.c`  
  - VIA/QMK가 요청한 바이트를 RAM 큐에 적재하고, SOF 기반 슬라이스(`EEPROM_WRITE_SLICE_MAX_US = 100us`)로 하위 드라이버에 전달합니다.
  - 큐는 총 `TOTAL_EEPROM_BYTE_COUNT + 1` 엔트리, 페이지 단위 버스트(32바이트)와 버스트 모드 추가 호출(`eeprom_get_burst_extra_calls()`)을 지원합니다.
- **하드웨어 드라이버**  
  - **외부 EEPROM (`src/hw/driver/eeprom/zd24c128.c`)**: 32바이트 페이지 버퍼, FastMode Plus(1MHz) I2C, Ready wait 로그. 현재 보드에서 사용되는 유일한 실 구현입니다.  
  - **플래시 에뮬 (`src/hw/driver/eeprom/emul.c`)**: STM32H7S 내장 플래시에 변수를 저장하며, 8비트 API와 비동기 Clean-up 상태 머신을 갖습니다. (테스트 미실시)
- **BootMode/AUTO_FACTORY_RESET 연동**: `usbBootModeWriteRaw()`와 `eeprom_apply_factory_defaults()`가 EEPROM 큐를 공유하여, BootMode·USB 모니터·센티넬이 동일 경로에서 초기화됩니다.

## 2. 주요 정책
1. **HS 8 kHz 유지**  
   - `eeprom_update()`는 매 호출마다 최대 100us의 실행 시간만 사용하여 USB 루프에 영향을 주지 않습니다.
   - 큐가 `EEPROM_WRITE_BURST_THRESHOLD`(512엔트리)을 넘으면 버스트 모드로 추가 호출을 허용해 처리량을 확보합니다.
2. **데이터 무손실 보장**  
   - `eeprom_write_byte()`는 큐 가득 참 시 최대 2ms까지 재시도하며, 실패 시 직접 쓰기 후 로그를 남깁니다.  
   - `qbufferAvailable()` 기반 하이워터 마크/오버플로 카운터를 CLI에서 확인할 수 있습니다.
3. **공용 초기화 흐름**  
   - VIA EEPROM CLEAR, AUTO_FACTORY_RESET 모두 `eepromScheduleDeferredFactoryReset()` → 재부팅 → `eeprom_apply_factory_defaults()` 경로를 공유합니다.  
   - BootMode/USB 모니터 기본값과 AUTO_CLEAR 센티넬도 동일 루틴에서 처리됩니다.
4. **클린업/Ready Wait 관측 가능성**  
   - `cli eeprom info`는 큐 상태 외에도 `emul cleanup busy/last/wait/cnt`를 출력합니다.  
     - 외부 EEPROM에서는 값이 항상 0입니다.  
     - 내부 플래시 에뮬 빌드 시 Clean-up 진행 상황을 실시간으로 모니터링할 수 있습니다.  
   - V251112R9에서는 동일 명령으로 `ready wait count/max/last`를 함께 노출하여, 부팅 로그 없이도 페이지 폴링 상태를 확인할 수 있습니다.  
   - 기본 빌드에서는 Ready wait 관련 로그를 출력하지 않고 CLI 통계로만 확인하며, `LOG_LEVEL_VERBOSE=1` 또는 `DEBUG_LOG_EEPROM=1`일 때만 `ready wait begin/done` 로그가 활성화됩니다.  
   - 부팅 완료 직후 `hwInit()`이 `[I2C] ready wait summary max=... count=... last=0x..` 한 줄을 남기므로, 기본 설정에서도 전체 통계를 빠르게 확인할 수 있습니다.

## 3. 함수/CLI 빠른 참조
| 계층 | 함수 | 설명 |
| --- | --- | --- |
| QMK | `eeprom_init()` | EEPROM 버퍼 및 큐 초기화 |
| QMK | `eeprom_update()` | SOF마다 호출되어 큐를 슬라이스 처리, `eepromIsErasing()`이 true면 즉시 대기 |
| QMK | `eeprom_write_byte()` | 큐 삽입/재시도/직접 쓰기 fallback |
| HW(ZD24C128) | `eepromWritePage()` | 32바이트 페이지 쓰기와 Ready wait |
| HW(ZD24C128) | `eepromIsErasing()` | 항상 false (클린업 개념 없음) |
| HW(Emul) | `eepromIsErasing()` | 비동기 Clean-up 진행 여부 반환 |
| CLI | `cli eeprom info` | 큐/클린업/Ready wait 통계 출력 |

## 4. 테스트 가이드 요약
1. `brick60` 키보드 설정으로 빌드 후 보드 플래시.
2. 부팅 직후 `cli eeprom info` 실행 → 큐 및 클린업 초기값 확인.
3. VIA에서 `docs/brick60.layout.json` 업로드 후 부팅 시 출력되는 `[I2C] ready wait summary ...`를 기록하고, `cli eeprom info`의 `ready wait count/max/last`가 기대대로 증가하는지 점검.
4. 필요 시 `cli eeprom write`로 AUTO_FACTORY_RESET 센티넬을 손상시키고 전원을 재투입, 재부팅 과정에서 큐가 안전하게 비워지는지 확인.
5. 내부 플래시 에뮬 빌드가 필요한 경우 `EEPROM_CHIP_EMUL` 설정으로 동일 절차를 반복해 Clean-up 계측이 갱신되는지 검증.

## 5. 장기 과제 및 현행 유지 항목
| 항목 | 상태 | 비고 |
| --- | --- | --- |
| 플래시 에뮬 멀티바이트 Unlock/Lock 최소화 | **장기 권장** | Clean-up 상태 머신과 오류 롤백을 재설계해야 하므로 안정성 우선으로 현행 유지 |
| VIA+USB Monitor 스트레스 테스트 자동화 | **장기 권장** | 현재는 수동 테스트 절차만 존재 |
| 내부 플래시 에뮬 경로 실기 검증 | **미실시** | 코드 수준 리팩터링 완료, 테스트 환경 부재로 미검증 |

## 6. 참고
- `src/ap/modules/qmk/port/platforms/eeprom.c` – 큐 슬라이스/버스트/리셋 경로
- `src/hw/driver/eeprom/zd24c128.c` – 외부 EEPROM 실제 드라이버
- `src/hw/driver/eeprom/emul.c` – 플래시 에뮬레이션(현 프로젝트에서는 비사용/미테스트)
- `src/hw/driver/eeprom_auto_factory_reset.c`, `src/hw/driver/usb/usb.c` – BootMode 및 AUTO_FACTORY_RESET 공용 처리
