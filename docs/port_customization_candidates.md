# 포트 커스터마이징 후보 메모 (V251016R8)

- Brick60 `config.h`에 `#define INDICATOR_ENABLE`을 선언해 키보드 전용 RGB 인디케이터 기능을 활성화했습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/config.h†L43-L45】
- `indicator_port.c`에서 인디케이터 LED 범위(0~29번 RGB 고정)와 호스트 LED 매핑을 직접 관리하도록 정리했습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/indicator_port.c†L12-L34】【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/indicator_port.c†L42-L50】
- `rgblight.c`에 인디케이터 범위 등록 API를 추가해 키보드 포트에서 Caps/Scroll/Num별 RGB 구간을 전달하도록 했습니다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L132-L153】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L203-L224】
- 인디케이터 범위를 RGB 오버레이로 적용하도록 변경해 Brick60처럼 전 LED를 공유하는 구조와 일반 키보드처럼 개별 LED를 사용하는 구조를 모두 지원합니다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L124-L161】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L268-L327】
- 포트 공통 계층(`port.h`)은 Brick60 사용자 EEPROM 슬롯 배치를 그대로 노출해 다른 하드웨어가 재정의할 수 있는 여지를 유지합니다.【F:src/ap/modules/qmk/port/port.h†L13-L18】

## 추가 점검이 필요한 후보 코드
- **Kill Switch 저장소**
  - `kill_switch.c`는 두 축의 킬스위치 구성을 EEPROM에 저장하기 위해 `EECONFIG_USER_KILL_SWITCH_LR/UD` 슬롯을 직접 사용합니다.【F:src/ap/modules/qmk/port/kill_switch.c†L45-L53】
  - 다른 하드웨어는 축 수나 저장 형식이 달라질 수 있으므로, 향후 키보드별 헤더에서 슬롯 위치와 기본값을 재정의하도록 분리할 필요가 있습니다.
- **KKUK 매크로 설정**
  - `kkuk.c`는 사용자 설정 저장을 위해 `EECONFIG_USER_KKUK` 슬롯을 사용합니다.【F:src/ap/modules/qmk/port/kkuk.c†L41-L50】
  - 향후 다른 키보드가 동일 슬롯을 재활용하지 않도록, 개별 포트 구성에서 슬롯 지정과 기본 동작을 오버라이드할 수 있는 구조를 고려해야 합니다.
- **USB BootMode 슬롯 공유**
  - USB 드라이버는 부트 모드 유지용으로 `EECONFIG_USER_BOOTMODE`를 참조합니다.【F:src/hw/driver/usb/usb.c†L12-L48】
  - 부트 모드를 지원하지 않는 기기는 슬롯을 비워두거나 다른 기능으로 재활용할 가능성이 있으므로, 포트 구성에서 해당 슬롯 정의를 조건부로 제공하는 방안을 검토할 가치가 있습니다.

## 후속 작업 제안
- 각 후보 기능에 대해 `config.h`와 개별 포트 소스(`indicator_port.c` 등)에서 필요한 매크로·콜백을 정의하도록 가이드라인을 정리합니다.
- 키보드 추가 시 EEPROM 슬롯 충돌을 방지하기 위해 `port.h`에 공통 기본값과 문서를 유지하고, 필요 시 키보드 전용 주석을 추가합니다.
