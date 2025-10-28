# Brick60 RGB 인디케이터 이관 계획

## 1. 현재 구조 요약
- Brick60은 30개의 WS2812를 단일 열로 배치하여 동일한 LED 뱅크를 언더글로우와 Caps Lock 인디케이터가 동시에 사용하도록 설정돼 있습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/config.h†L4-L11】 이 뱅크는 QMK의 `rgblight_driver`가 WS2812 포팅 계층을 통해 제어합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/driver/rgblight_drivers.c†L1-L22】
- 현재 Caps Lock 인디케이터는 키보드 포트 계층에서 직접 WS2812 버퍼를 채우고, 전역 `rgblight_override_enable` 플래그로 RGB 라이트 태스크를 강제로 우회시키는 방식으로 구현되어 있습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port.c†L45-L106】【F:src/ap/modules/qmk/port/override.h†L1-L5】【F:src/ap/modules/qmk/port/override.c†L1-L3】
- `rgblight_set()`과 `rgblight_timer_task()`는 위 전역 플래그를 확인해 작동을 중단하므로, 인디케이터가 켜져 있을 때 기본 이펙트 렌더링이 완전히 정지합니다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L898-L945】【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1047-L1150】
- VIA 커스텀 채널은 Caps Lock 인디케이터만을 대상으로 하는 `id_qmk_led_caps_channel`을 사용하며, JSON 정의 역시 채널 6번을 통해 토글·밝기·색상을 전송합니다.【F:src/ap/modules/qmk/quantum/via.h†L107-L121】【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/json/BRICK60-H7S-VIA.JSON†L16-L36】
- 신규 VIA JSON은 `id_custom_channel`(0)을 사용해 Caps/Scroll/Num Lock 중 연동 대상을 선택하고, 밝기·색상을 범용 값 ID로 설정하도록 바뀌었습니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/json/BRICK60-H7S-VIA-NEW.JSON†L17-L111】
- 사용자 EEPROM은 LED 인디케이터 구성을 4바이트 슬롯(`EECONFIG_USER_LED_CAPS`)에 저장하도록 예약돼 있으며, Scroll Lock 슬롯도 함께 확보돼 있습니다.【F:src/ap/modules/qmk/port/port.h†L13-L20】
- `rgb_matrix` 계층은 효과 렌더링 직후에 `rgb_matrix_indicators()`를 호출해 기본 효과 위에 인디케이터를 덮어씌우는 구조를 이미 제공하므로, rgblight에서도 유사한 파이프라인을 도입할 수 있습니다.【F:src/ap/modules/qmk/quantum/rgb_matrix/rgb_matrix.c†L360-L438】

## 2. 요구사항 및 설계 고려사항
1. LED 인디케이터 제어를 `rgblight.c` 내부로 이관해 오리지널 QMK와 유사한 흐름을 구축하고, 정적/동적 효과 모두에서 Caps·Scroll·Num Lock 상태를 덮어씌워야 합니다.
2. 정적 효과에서는 인디케이터 활성 시 LED를 비우고, 상태가 해제되면 원래 모드·밝기로 즉시 복구되어야 합니다(기존 `rgblight_mode_noeeprom()` 재호출 요구와 동일).
3. 동적 효과에서는 불필요한 애니메이션 연산을 생략하고 곧바로 인디케이터 컬러를 출력해야 하므로, 효과 렌더 루프 진입 전에 조기 탈출 훅이 필요합니다.
4. VIA는 새로운 JSON 스키마(`id_qmk_custom_ind_*`)를 처리하며, 사용자가 선택한 잠금 키 타입·밝기·색상을 EEPROM에 저장하고 부팅 시 복원해야 합니다.
5. 기존 Caps Lock 전용 채널과 `override.*` 의존성은 제거해 모듈 경계를 단순화하고, 포트 계층 파일명을 인디케이터 중심으로 재구성해야 합니다.
6. 변경 사항에 맞춰 펌웨어 버전을 갱신하고, 각 코드 수정 블록에 `// VYYMMDDRn ...` 변경 이력 주석을 추가해야 합니다.【F:src/hw/hw_def.h†L1-L20】

## 3. 세부 구현 계획

### 3.1 `rgblight` 내부 인디케이터 파이프라인 구축
1. `rgblight.c`에 인디케이터 상태/구성 구조체를 추가합니다. 구성에는 대상 잠금 키(0=Off, 1=Caps, 2=Scroll, 3=Num), HSV 컬러, 독립 밝기 값을 포함해 4바이트 이하로 유지합니다.
2. `rgblight.h`에 인디케이터 전용 API(예: `rgblight_indicator_update_config()`, `rgblight_indicator_apply_host_led(led_t state)`)를 선언해 키보드 포트 계층에서 설정/호출할 수 있게 합니다.
3. `rgblight_set()` 내부에서는 인디케이터 활성 여부를 먼저 평가하고, 활성화된 경우 RGB 버퍼를 초기화한 뒤 인디케이터 컬러로 채워 `rgblight_driver.setleds()`를 호출합니다. 이때 기존 효과 계산 분기를 건너뛰고, 정적 모드 복원을 위해 `rgblight_indicator_set_restore_pending()` 같은 상태 플래그를 남깁니다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L898-L945】
4. `rgblight_timer_task()`는 인디케이터가 켜져 있으면 애니메이션 로직을 건너뛰고, 필요한 경우 단 한 번만 `rgblight_set()`을 호출하도록 플래그를 사용합니다. 이로써 동적 효과에서 불필요한 계산을 피합니다.【F:src/ap/modules/qmk/quantum/rgblight/rgblight.c†L1047-L1150】
5. 인디케이터가 꺼질 때는 정적 효과에서 원래 모드·밝기를 복원하기 위해 `rgblight_mode_noeeprom(rgblight_config.mode)` 혹은 효과 재적용 헬퍼를 호출합니다. 복원 직후에는 재호출 루프를 방지할 재진입 방지 플래그를 초기화합니다.
6. 기존 전역 `rgblight_override_enable` 플래그와 `override.{c,h}`를 제거하고, 관련 include를 정리해 의존성을 줄입니다.【F:src/ap/modules/qmk/port/override.h†L1-L5】【F:src/ap/modules/qmk/port/override.c†L1-L3】
7. Brick60의 전체 LED 범위(0~29)를 인디케이터가 독점하므로, 하드코딩 대신 `RGBLIGHT_LED_COUNT`와 선택된 범위 매크로를 사용해 rgblight 내부에서 일관되게 접근합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/config.h†L4-L11】

### 3.2 키보드 포트 계층 리팩토링
1. `led_port.c/.h`를 `indicator_port.c/.h`로 리네이밍하고, 파일 내부를 인디케이터 로직·VIA 로직·EEPROM 관리 함수로 분리합니다. 필요 시 VIA 처리를 별도 `indicator_via.c`로 분할해 유지보수를 용이하게 합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port.c†L1-L198】
2. EEPROM 헬퍼 매크로(`EECONFIG_DEBOUNCE_HELPER`)를 새 구조체에 맞게 갱신하고, 초기화 시 기본 HSV/잠금 키 선택을 캡슐화합니다. Scroll Lock 슬롯은 예비용으로 유지하거나 새로운 설정 필드 확장에 대비합니다.【F:src/ap/modules/qmk/port/port.h†L13-L20】
3. `led_init_ports()`에서는 EEPROM 값을 읽어 rgblight 인디케이터 구성 API로 전달하고, 기본 효과 복원을 보장하기 위해 초기화 직후 `rgblight_indicator_sync_state()` 호출을 추가합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port.c†L51-L61】
4. `led_update_ports()`는 더 이상 WS2812 드라이버를 직접 호출하지 않고, 호스트 LED 상태를 새로운 rgblight 인디케이터 API로 전달하여 출력 경로를 일원화합니다. 상태 변화 감시 및 모드 복원은 rgblight 측에서 수행하므로, 포트 계층에는 상태 비교·EEPROM 갱신만 남깁니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port.c†L63-L106】
5. VIA 요청 처리 함수는 `indicator_port_via_command()` 등으로 명명하고, 값 ID 해석(선택/밝기/색상)과 EEPROM 업데이트 후 즉시 rgblight 구성에 반영하는 경로를 유지합니다. Flush 동작은 기존 디바운스 헬퍼를 활용해 주기적으로 수행합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/led_port.c†L108-L198】
6. 리팩토링에 따라 새 파일이 빌드 대상에 포함되는지 확인하고, 필요하면 해당 키보드 전용 CMake/빌드 스크립트를 갱신합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/config.cmake†L1-L10】

### 3.3 VIA 프로토콜 및 JSON 연동
1. `via.h`에서 `id_qmk_led_caps_channel`·`id_qmk_led_scroll_channel`을 제거하고, 인디케이터 값 ID를 새 enum(`id_qmk_custom_ind_selec` 등)으로 정의합니다. 기존 채널 번호 충돌이 없도록 `id_custom_channel`을 그대로 사용합니다.【F:src/ap/modules/qmk/quantum/via.h†L107-L133】
2. `via_custom_value_command_kb()`는 채널 0 수신 시 인디케이터 처리기로 위임하도록 갱신하고, 기타 기능(버전, 시스템, SOCD 등)은 기존 분기 구조를 유지합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/port/via_port.c†L6-L53】
3. 구형 VIA JSON과의 호환성이 필요하지 않기 때문에 기존 분기는 제거합니다. (예: 채널 6 기반 사용자)
4. 신규 JSON(`BRICK60-H7S-VIA-NEW.JSON`)을 기본 배포본으로 교체하고, 문서/릴리스 노트에 인디케이터 선택·밝기·색상 옵션 추가 사실을 명시합니다.【F:src/ap/modules/qmk/keyboards/era/sirind/brick60/json/BRICK60-H7S-VIA-NEW.JSON†L17-L111】

### 3.4 버전 관리 및 부가 조정
1. `_DEF_FIRMWATRE_VERSION`을 이번 작업에 맞춰 `V251012R2`(가칭) 등 새로운 식별자로 올리고, 모든 수정 파일 최상단 또는 주요 변경 지점에 동일 버전의 변경 이력 주석을 남깁니다.【F:src/hw/hw_def.h†L1-L20】
2. 인디케이터 관련 문서가 필요하다면 `docs/`에 간단한 사용 가이드(예: VIA 설정법)를 추가해 사용자 혼선을 줄입니다.
3. 코드 스타일 규칙(2칸 들여쓰기, 중괄호 개행 등)과 기존 주석 컨벤션을 유지하며, 불필요한 `#include` 정리와 정적 분석 경고 확인을 병행합니다.
4. PR 작성시 AGENTS.md에 따라 작성합니다.

## 4. 테스트 및 검증 계획
1. **기능 점검**: Caps/Scroll/Num Lock을 각각 토글하여 인디케이터가 선택된 잠금 키에만 반응하는지, 해제 즉시 원래 RGB 효과가 복구되는지 확인합니다.
2. **동적 효과 확인**: Breathing·Rainbow 등 동적 모드에서 인디케이터가 켜지는 동안 애니메이션이 일시 중단되고, 꺼지면 프레임 지연 없이 재개되는지 관찰합니다.
3. **정적 효과 복원**: Static Light 모드에서 인디케이터가 종료되면 이전 색상/밝기가 정확히 복구되는지 확인합니다.
4. **VIA 연동**: 신규 JSON을 로드한 VIA에서 인디케이터 종류/밝기/색상 변경이 즉시 반영되고 EEPROM 재부팅 후에도 유지되는지 테스트합니다.
5. **빌드 검증**: `cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/sirind/brick60'` → `cmake --build build -j10` → `rm -rf build` 순으로 정상 빌드·정리되는지 확인합니다.
6. **회귀 방지**: RGBLIGHT_SLEEP, RGBlight 레이어, SOCD/KKUK 등 다른 VIA 채널 기능과의 상호작용에 문제가 없는지 수동 테스트 및 로그 검토를 수행합니다.

## 5. 리스크 및 추가 고려사항
- 정적/동적 효과 복원 로직이 잘못되면 사용자가 설정한 RGB 모드가 인디케이터 종료 후 초기화될 수 있으므로, 상태 플래그·복원 경로에 대한 단위 테스트 혹은 광범위 수동 점검이 필요합니다.
- VIA 구버전 사용자에게는 새로운 JSON 적용 안내가 선행돼야 하며, 임시 호환 코드를 유지한다면 추후 제거 일정을 문서화해야 합니다.
- 인디케이터 처리 로직이 rgblight 공통 코드에 들어가기 때문에, 다른 키보드 포팅층에서 동일 코드를 재사용할 여지가 있는지 검토하고 공통 API로 승격할지 향후 협의가 필요합니다.
