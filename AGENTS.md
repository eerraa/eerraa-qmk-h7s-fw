# Codex 작업 가이드

본 문서는 전체 저장소에 적용되며, 아래 순서대로 지침을 확인하십시오.

## 1. 필수 응답 규칙
- 모든 답변, 커밋 메시지, PR 본문은 **반드시 한국어**로 작성합니다.
- 저장소 탐색은 사용자 요청 범위와 아래 §11 참조 경로에 맞춰 필요한 파일만 열람합니다. 추가로 열람이 필요하면 사용자에게 사전 요청합니다.

## 2. 프로젝트 개요
- 대상 보드: STM32H7S (내장 HS PHY) — 기본 USB 폴링 주기 8000Hz.
- 지원 속도: HS 8k/4k/2kHz, FS 1kHz. 정책은 FS 우선입니다.
- 주요 기능: USB instability monitor (마이크로프레임 격차 감시, V250924R4), 단계적 폴링 다운그레이드 큐, QMK 포팅층.
- 현재 `_DEF_FIRMWARE_VERSION`: **V251124R6**

## 3. 작업 전 체크리스트
1. `_DEF_FIRMWARE_VERSION`과 보드 매크로를 `src/hw/hw_def.h`에서 확인합니다.
2. 엔트리 경로는 `src/main.c → src/ap/ap.c` 흐름을 중심으로 파악합니다.
3. QMK 구조는 `src/ap/modules/qmk/{port,keyboards,quantum}` 순으로 확인합니다.
4. 다음 경로는 **고위험 파일군**으로 분류하여 변경 시 집중 리뷰합니다.
   - `src/ap/modules/qmk/port/sys_port.*`
   - `src/hw/driver/`

## 4. 변경 이력 규칙
- 코드 변경마다 **사용자로부터 전달받은 코드 버전**(`VYYMMDDRn`)으로 `// VYYMMDDRn ...` 형식의 변경 이력 주석을 추가합니다.
- 변경된 코드 버전에 맞추어 `_DEF_FIRMWARE_VERSION`를 업데이트 합니다.
  PATH: src/hw/hw_def.h
- 사용자가 코드 버전을 명시하지 않은 상태에서 작업을 시작해야 한다면, 임의의 코드 버전을 하나 지정해 변경 이력에 표기합니다.
  그리고 즉시 사용자에게 공식 버전 확인을 요청합니다.

## 5. 코드 스타일 요약
- 들여쓰기는 공백 두 칸을 사용합니다.
- 함수 및 제어문 중괄호는 새 줄에 배치합니다.
- 조건문은 `if (cond) {`와 같이 괄호 앞뒤에 공백을 둡니다.
- 연산자는 `a + b` 형태로 좌우에 공백을 두되, 기존 코드가 공백 없이 연속 호출(`millis()-pre_time`)을 사용한다면 주변 컨벤션을 유지합니다.
- `#define` 상수는 대문자로 작성하고 값, 주석 사이 정렬을 맞춥니다.
- 단일 행 주석은 `//`를 사용하며, 변경 이력 주석은 한국어로 남깁니다.
- 함수 선언과 정의 사이에는 한 줄을 비워 가독성을 확보합니다.

## 6. 빌드 및 테스트
```bash
cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/sirind/brick60'
cmake --build build -j10
```
- 별도의 **빌드 테스트 실행 명령**이 없다면 빌드 테스트는 생략합니다.
- UF2 변환은 CMake 타깃 내부에서 자동으로 처리됩니다.

## 7. 디렉터리 힌트
- `src/` : 펌웨어 소스 및 라이브러리 전반
- `src/ap/` : 애플리케이션 계층과 QMK 포팅
- `src/hw/` : 하드웨어 추상화 및 펌웨어 버전 정의
- `src/bsp/` : 보드 초기화, 클럭, 스타트업 코드
- `tools/` : 빌드 및 UF2 보조 스크립트

## 8. PR 작성 지침
- PR 제목과 본문은 한국어로 작성합니다.
- 변경 요약과 테스트 결과를 명시하고, 현재 펌웨어 버전 문자열(예: `V250928R5`)을 포함합니다.
- 후속 PR이라면 기존 요약을 유지한 채 의미 있는 변경만 추가로 기술합니다.

## 9. 추가 주의사항
- USB 모니터는 구성 직후 50ms 홀드오프 후 HS 2048 프레임(약 256ms)/FS 128 프레임을 채우거나 2.05초 타임아웃 중 먼저 도달하면 활성화됩니다. 다운그레이드 ARM→COMMIT 지연은 2초입니다.
- VIA에서 USB 모니터를 비활성화해도 이미 ARM/COMMIT된 다운그레이드·리셋 큐는 처리됩니다. 비활성화 요청 시 큐를 초기화하도록 수정 필요.
- 타이머/USB 경로를 변경할 때는 8000Hz 스케줄링이 유지되는지 검증합니다.
- QMK 업스트림 병합 시 `quantum/`을 먼저 비교하고, 이후 `port/`에서 플랫폼 수정을 재적용합니다.

## 10. 참고 팁
- 저장소 전반의 디버그 로그는 `src/hw/hw.c` 초기화 루틴과 연동되어 있으므로, 버전 문자열 출력 경로를 수정할 때는 전역 영향 범위를 검토하세요.
- 장기 실행 타이머를 건드릴 경우 USB instability monitor의 워밍업/타임아웃 조건을 함께 확인하면 리그레션을 줄일 수 있습니다.

## 11. 작업별 참조 가이드
- **펌웨어 버전 및 로그 문자열 조정**: `src/hw/hw_def.h`의 `_DEF_FIRMWARE_VERSION`, `src/hw/hw.c`의 `hwInit()` 버전 출력; 배경 설명은 `docs/features_instability_monitor.md`.
- **엔트리 포인트 및 메인 루프**: `src/main.c`의 `main()`, `src/ap/ap.c`의 `apInit()/apMain()` 흐름, 초기 설정은 `src/ap/ap_def.h`.
- **QMK 포팅 공통 계층**: `src/ap/modules/qmk/port/`의 `sys_port.*`, `matrix*.c`, `ver_port.c`, 플랫폼별 HAL 연동은 `port/platforms/`.
- **키보드별 키맵/설정**: `src/ap/modules/qmk/keyboards/<vendor>/<board>/`, VIA/VIAL 처리는 `src/ap/modules/qmk/port/via_hid.*`.
- **USB instability monitor & 폴링 다운그레이드 큐**: `src/hw/driver/usb/usb_hid/usbd_hid.c`, `src/hw/driver/usb/usb.[ch]`, 관련 문서는 `docs/features_instability_monitor.md`.
- **USB 클래스/엔드포인트 정의**: `src/hw/driver/usb/` 하위 `cdc.c`, `usb_hid/`, `usbd_hid_instrumentation.c`; 보조 타이머 설계는 `docs/review_usb_backup_timer.md`.
- **EEPROM/BootMode 및 설정 유지**: `src/hw/driver/eeprom/`, `src/ap/modules/qmk/port/port.h`의 `EECONFIG_USER_BOOTMODE`, 백그라운드 설명은 `docs/features_bootmode.md`.
- **키 입력 경로와 디바운스**: `src/hw/driver/keys.c`, `src/ap/modules/qmk/port/matrix*.c`, 개요는 `docs/features_keyinput.md`.
- **LED 및 RGB 인디케이터**: `src/hw/driver/led.c`, `src/hw/driver/ws2812.c`, QMK 헬퍼는 `src/ap/modules/qmk/quantum/rgblight/`, 설계 문서는 `docs/rgblight_indicator_refactoring.md`, `docs/indicator_plan.md`.
- **로깅 및 CLI**: `src/hw/driver/log.c`, `src/hw/driver/uart.c`, USB CLI 진입점은 `src/hw/driver/usb/usb.c`의 `cliBoot`.
- **빌드/툴체인 스크립트**: `tools/` 디렉터리 일체 (CMake 헬퍼, UF2 변환).
