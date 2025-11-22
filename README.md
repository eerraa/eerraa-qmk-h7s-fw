# ERA-QMK-H7S-FW

## 프로젝트 개요
ERA-QMK-H7S-FW는 STM32H7S 마이크로컨트롤러 기반 커스텀 키보드를 위해 설계된 고성능 펌웨어입니다. HS USB 8kHz 폴링을 기본으로 하며, USB instability monitor와 단계적 폴링 다운그레이드 큐를 통해 고속 입력 안정성을 확보합니다. QMK 포팅층이 포함되어 있어 기존 QMK 기반 설정과 호환되며, `src/main.c → src/ap/ap.c` 경로를 따라 애플리케이션 로직이 동작합니다.

이 프로젝트는 https://github.com/chcbaram/baram-qmk-h7s에서 포트되었습니다.

## 디렉터리 구조
- `src/` : 펌웨어 소스와 하드웨어 추상화 계층이 위치합니다.
- `src/ap/` : 애플리케이션 계층과 QMK 포팅 코드가 포함됩니다.
- `src/hw/` : 보드별 하드웨어 정의와 드라이버를 제공합니다.
- `src/bsp/` : 보드 초기화, 클럭 설정, 스타트업 코드를 제공합니다.
- `docs/` : 펌웨어 관련 문서 및 참고 자료가 있습니다.
- `tools/` : 빌드 및 UF2 변환을 지원하는 스크립트가 있습니다.

## 빌드 준비
1. ARM GCC Toolchain과 CMake 3.20 이상을 설치합니다.
2. Python과 `pip`을 설치한 뒤 필요한 경우 QMK 스크립트 의존성을 설치합니다.
3. 본 저장소를 클론한 뒤 프로젝트 루트에서 아래 절차를 수행합니다.

## CMake 설정 및 빌드
### Mac/Linux
```bash
cmake -S . -B build -DKEYBOARD_PATH='/keyboards/<vendor>/<keyboard_name>'
```

### Windows (MinGW)
```bash
cmake -S . -B build -G "MinGW Makefiles" -DKEYBOARD_PATH='/keyboards/<vendor>/<keyboard_name>'
```

`KEYBOARD_PATH`에는 `/keyboards/` 디렉터리 아래에 존재하는 대상 키보드 경로를 지정합니다. 예를 들어 Brick60을 빌드하려면 `/keyboards/era/sirind/brick60`을 입력합니다.

### 공통 빌드 단계
설정이 완료되면 다음 명령으로 펌웨어를 컴파일합니다.
```bash
cmake --build build -j10
```

빌드가 완료되면 CMake가 자동으로 UF2 이미지를 생성하며, 생성된 결과물을 지원 보드에 플래시할 수 있습니다.
