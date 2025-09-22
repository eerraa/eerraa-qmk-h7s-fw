# eerraa-qmk-h7s-fw Codex 가이드

## 프로젝트 한눈에 보기
- 이 프로젝트는 STM32H7S 계열 MCU를 기반으로 하는 QMK 파생 펌웨어입니다.
- MCU의 USB High-speed PHY만을 사용하며, Full-speed PHY는 사용하지 않습니다.
- USB High-speed 연결에서 8,000Hz 인터럽트 통신을 달성하는 것을 목표로 설계되었습니다.
- 빌드는 CMake 기반으로 구성되며, Codex가 코드를 탐색할 때 핵심 진입점은 `src/main.c` 와 `src/ap/ap.c` 입니다.

## Codex를 위한 탐색 가이드
1. **엔트리 포인트**: `src/main.c`에서 시스템 초기화가 이뤄지고, `apInit()` 및 `apMain()`을 통해 애플리케이션 로직이 호출됩니다.
2. **애플리케이션 계층**: `src/ap/` 디렉터리는 QMK 모듈과 사용자 로직을 결합합니다. 특히 `src/ap/modules/qmk/` 아래에서 키맵, 포트 계층, 퀀텀 계층을 확인할 수 있습니다.
3. **하드웨어 추상화**: `src/hw/` 및 `src/bsp/` 폴더는 보드 지원 패키지와 드라이버를 제공합니다. USB High-speed PHY 구성 및 각종 주변장치 초기화 코드는 이 영역에 집중되어 있습니다.
4. **공통 유틸리티**: `src/common/`은 로깅, 오류 코드, 코어 유틸리티를 관리하여 모듈 간 공통 로직을 제공합니다.
5. **외부 라이브러리**: `src/lib/`에는 ST HAL과 타사 라이브러리가 포함되어 있어, 필요 시 해당 폴더로 이동해 참조하십시오.

## 프로젝트 구조 및 역할
| 경로 | 내용 |
| --- | --- |
| `/CMakeLists.txt` | 전체 프로젝트를 구성하는 최상위 CMake 스크립트. 타깃과 서브디렉터리를 정의합니다. |
| `/README.md` | 프로젝트 개요 및 Codex용 가이드 문서입니다. |
| `/prj/vscode/*.code-workspace` | VS Code에서 사용되는 워크스페이스 설정 파일입니다. |
| `/src/main.c` | 펌웨어의 엔트리 포인트로, 보드 초기화와 애플리케이션 루프 진입을 담당합니다. |
| `/src/main.h` | `main.c` 관련 전역 선언을 제공합니다. |
| `/src/ap/` | 애플리케이션 계층. `ap.c`/`ap.h`가 상위 제어를, `modules/`가 세부 기능을 담당합니다. |
| `/src/ap/modules/qmk/` | QMK 통합 레이어. `keyboards/`에 키보드 레이아웃, `port/`에 하드웨어 포팅 코드, `quantum/`에 QMK 코어가 위치합니다. |
| `/src/bsp/` | 보드 지원 패키지. `device/`에 MCU 설정, `ldscript/`에 링커 스크립트, `startup/`에 스타트업 코드를 포함합니다. |
| `/src/common/` | 프로젝트 전반에서 사용하는 공통 정의(`def.h`), 오류 코드(`err_code.h`), 코어 유틸리티(`core/`), 하드웨어 공통 로직(`hw/`)을 제공합니다. |
| `/src/hw/` | 하드웨어 드라이버 초기화부. `hw.c`/`hw.h`로 하드웨어 초기화 흐름을 관리하고, `driver/`에 개별 드라이버가 위치합니다. |
| `/src/lib/ST/` | STM32 HAL 및 CMSIS 관련 라이브러리를 포함합니다. |
| `/src/lib/lib8tion/` | QMK에서 사용하는 유틸리티 라이브러리입니다. |
| `/tools/arm-none-eabi-gcc.cmake` | 교차 컴파일러 설정을 정의하는 CMake 툴체인 파일입니다. |
| `/tools/W25Q16JV_BARAM-QMK-H7S.stldr` | 외부 플래시 프로그래밍에 사용되는 ST-LINK 로더 설정입니다. |
| `/tools/uf2/` | UF2 변환 스크립트와 보드 식별 정보가 포함되어 있어, 빌드 산출물을 UF2 포맷으로 변환할 때 사용됩니다. |

## 빌드 및 테스트 절차
1. **Configure**
   ```bash
   cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/sirind/brick60'
   ```
2. **Build**
   ```bash
   cmake --build build -j10
   ```

## 추가 메모
- USB High-speed PHY 설정과 8kHz 인터럽트 핸들링은 주로 `src/hw/driver/usb` 및 관련 HAL 구성에서 관리됩니다.
- 새로운 하드웨어 기능을 확장할 경우 `_DEF_FIRMWATRE_VERSION`을 업데이트하고, 변경된 코드에 "['버전'] 변경 설명" 주석을 추가하세요.
- Codex가 자동으로 코드를 탐색할 때는 위 표를 기준으로 관심 영역을 빠르게 파악할 수 있습니다.
