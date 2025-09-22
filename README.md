# eerraa-qmk-h7s-fw

## 프로젝트 개요
- STM32H7S 계열 MCU를 기반으로 한 커스텀 키보드 펌웨어입니다.
- MCU의 USB High-speed PHY만을 사용하여 8,000Hz 인터럽트 기반 통신을 목표로 합니다.
- QMK 모듈을 통합해 기존 QMK 생태계의 키맵과 기능을 최대한 재사용합니다.

## 하드웨어 특성 요약
1. **마이크로컨트롤러**: STM32H7S 시리즈 (High-performance, ARM Cortex-M7 코어).
2. **USB 구성**: 내장 USB High-speed PHY 단독 사용, Full-speed PHY는 비활성화 상태입니다.
3. **통신 주기**: 8,000Hz 인터럽트 루프로 입력 신호를 고속 처리합니다.
4. **보드 식별자**: `_DEF_BOARD_NAME`은 `BARAM-QMK-H7S-FW`로 정의되어 있습니다.

## Codex 온보딩 체크리스트
1. `src/hw/hw_def.h`에서 사용 중인 하드웨어 기능 플래그와 최신 펌웨어 버전을 확인합니다.
2. `src/main.c`의 엔트리 포인트와 `src/ap/ap.c`의 애플리케이션 루프를 훑어 전체 실행 흐름을 이해합니다.
3. USB High-speed 경로는 `src/hw/driver`와 `src/ap/modules/qmk` 내 구현을 함께 추적합니다.
4. 필요한 경우 `tools/uf2/uf2conv.py`를 활용해 빌드 산출물을 UF2 포맷으로 변환합니다.

## 폴더 및 파일 가이드
### 루트 디렉터리
- `CMakeLists.txt`: 펌웨어 전체 빌드 설정과 대상 MCU, 링크 스크립트 구성을 선언합니다.
- `LICENSE`: 프로젝트 라이선스(오픈소스 사용 조건)를 명시합니다.
- `README.md`: Codex를 위한 온보딩 가이드(현재 문서)입니다.
- `prj/`: IDE 및 개발 환경 설정 파일을 모아둔 디렉터리입니다.
- `src/`: 펌웨어 소스 코드가 위치한 핵심 디렉터리입니다.
- `tools/`: 빌드 도구, 플래시 유틸리티 및 지원 스크립트를 포함합니다.

### `prj/`
- `vscode/`: VS Code 워크스페이스 구성을 제공합니다.
  - `baram-45k.code-workspace`: 45키 레이아웃 개발용 워크스페이스.
  - `baram-60mx-6.25u.code-workspace`: 60% 레이아웃 기반 프로젝트 설정.

### `src/`
- `main.c`: 시스템 초기화와 RTOS/메인 루프 진입점을 제공합니다.
- `main.h`: 전역적으로 공유되는 메인 진입점 헤더.
- `ap/`: 애플리케이션 계층 로직.
  - `ap.c`, `ap.h`, `ap_def.h`: 애플리케이션 루프, 상태 정의, 상수 선언을 포함합니다.
  - `modules/qmk/`: QMK 포팅 레이어.
    - `keyboards/`: QMK 키보드별 설정 및 키맵 래퍼.
    - `port/`: STM32H7S에 맞춘 QMK 포팅 코드.
    - `quantum/`: QMK 핵심 로직 중 프로젝트 커스텀 버전.
- `bsp/`: 보드 지원 패키지.
  - `device/`: STM32H7S 주변장치 초기화 및 보드별 설정.
  - `ldscript/`: 링커 스크립트와 메모리 맵 정의.
  - `startup/`: 부트업 시퀀스와 벡터 테이블 초기화 코드.
- `common/`: 공용 유틸리티와 하드웨어 추상화 레이어.
  - `core/`: 시스템 코어 유틸리티, 타이밍, 로깅 지원.
  - `hw/`: 하드웨어 인터페이스 정의와 공용 헤더(`def.h`, `err_code.h`).
- `hw/`: 하드웨어 드라이버 계층.
  - `hw.c`, `hw.h`: 하드웨어 초기화 진입점과 API 선언.
  - `hw_def.h`: 하드웨어 기능 플래그, 펌웨어 버전, 보드 이름 등을 정의합니다.
  - `driver/`: LED, UART, USB 등 개별 주변장치 드라이버.
- `lib/`: 외부 라이브러리.
  - `ST/`: STMicroelectronics에서 제공하는 HAL/LL 라이브러리 코드.
  - `lib8tion/`: QMK에서 사용하는 공용 유틸리티 라이브러리.

### `tools/`
- `arm-none-eabi-gcc.cmake`: ARM 크로스 컴파일러 툴체인 경로를 정의합니다.
- `W25Q16JV_BARAM-QMK-H7S.stldr`: W25Q16 시리얼 플래시용 ST-LINK 로더 스크립트.
- `uf2/`: UF2 변환 도구와 MCU 패밀리 정의.
  - `uf2conv.py`: 바이너리/ELF를 UF2 포맷으로 변환하는 파이썬 스크립트.
  - `uf2families.json`: 지원 MCU 패밀리 식별자 테이블.

## 빌드 & 테스트 워크플로
1. CMake Configure: `cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/sirind/brick60'`
2. CMake Build: `cmake --build build -j10`

## 추가 메모
- 빌드 전에 `build/` 디렉터리가 깨끗한지 확인하면 캐시 충돌을 예방할 수 있습니다.
- USB High-speed 경로에 의존하므로, 디버깅 시 HS PHY 클럭 설정을 반드시 점검하세요.

<!-- ['V250922R1'] Codex용 프로젝트 가이드 갱신 -->
