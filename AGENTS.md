# 에이전트 작업 가이드 (Codex · Claude Code 공용)

본 문서는 전체 저장소에 적용되며, 아래 순서대로 지침을 확인하십시오.

## 1. 필수 응답 규칙
- 모든 답변, 커밋 메시지, PR 본문은 **반드시 한국어**로 작성합니다.
- 저장소 탐색은 §3의 지식그래프 질의로 후보를 좁힌 뒤 필요한 파일만 엽니다.

## 2. 프로젝트 개요
- 대상 보드: STM32H7S3 (내장 HS PHY) — 기본 USB 폴링 주기 8000Hz.
- 지원 속도: HS 8k/4k/2kHz, FS 1kHz. 정책은 FS 우선입니다.
- 주요 기능: USB instability monitor, 단계적 폴링 다운그레이드 큐, QMK 포팅층, VIA/Vial 지원.
- 지원 키보드(5종, `src/ap/modules/qmk/keyboards/` 하위): `era/keynetix/may65`(최근 작업 보드), `era/intigrity80`, `era/sirind/{brick60,brick65,sculpturei}`
- 현재 `_DEF_FIRMWARE_VERSION`: **V260821R1** (기준은 항상 `src/hw/hw_def.h`)

## 3. 지식그래프 (graphify) — 코드 탐색 1순위
이 리포에는 지식그래프가 `graphify-out/`에 커밋되어 있습니다. 스코프는 `.graphifyignore`가 정의하며, "실제 컴파일되는 코드 + 프로젝트 문서"만 포함합니다.
- 세션 시작 시 `python tools/graphify/bootstrap.py`를 실행합니다 (Claude Code는 `.claude/settings.json`의 SessionStart hook이 자동 실행). git hook 설치, 그래프 동기화, 상태 출력을 수행합니다.
- 코드 구조 질문은 먼저 `graphify query "<질문>"`으로 답합니다. 두 심볼의 관계는 `graphify path "A" "B"`, 단일 개념 설명은 `graphify explain "X"`.
- 코드 수정 후에는 `graphify update .`로 그래프를 갱신합니다 (AST-only, LLM/API 비용 없음). 커밋 시에는 post-commit hook이 자동 갱신합니다.
- hook 갱신으로 `graphify-out/` 파일이 dirty해지는 것은 정상이며, 다음 커밋에 함께 포함합니다.
- 전체 아키텍처 조망은 `graphify-out/GRAPH_REPORT.md`를 참조합니다.
- `src/ap/modules/qmk/CMakeLists.txt`의 컴파일 파일 목록을 바꾸면 `.graphifyignore`도 함께 갱신합니다.

## 4. 작업 전 체크리스트
1. `_DEF_FIRMWARE_VERSION`과 보드 매크로를 `src/hw/hw_def.h`에서 확인합니다.
2. 엔트리 경로는 `src/main.c → src/ap/ap.c` 흐름을 중심으로 파악합니다.
3. QMK 구조는 `src/ap/modules/qmk/{port,keyboards,quantum}` 순으로 확인합니다.
4. 다음 경로는 **고위험 파일군**으로 분류하여 변경 시 집중 리뷰합니다.
   - `src/ap/modules/qmk/port/sys_port.*`
   - `src/hw/driver/`

## 5. 변경 이력 규칙
- 코드 변경마다 **사용자로부터 전달받은 코드 버전**(`VYYMMDDRn`)으로 `// VYYMMDDRn ...` 형식의 변경 이력 주석을 추가합니다.
- 변경된 코드 버전에 맞추어 `_DEF_FIRMWARE_VERSION`를 업데이트 합니다.
  PATH: src/hw/hw_def.h
- 사용자가 코드 버전을 명시하지 않은 상태에서 작업을 시작해야 한다면, 임의의 코드 버전을 하나 지정해 변경 이력에 표기합니다.
  그리고 즉시 사용자에게 공식 버전 확인을 요청합니다.

## 6. 코드 스타일 요약
- 들여쓰기는 공백 두 칸을 사용합니다.
- 함수 및 제어문 중괄호는 새 줄에 배치합니다.
- 조건문은 `if (cond) {`와 같이 괄호 앞뒤에 공백을 둡니다.
- 연산자는 `a + b` 형태로 좌우에 공백을 두되, 기존 코드가 공백 없이 연속 호출(`millis()-pre_time`)을 사용한다면 주변 컨벤션을 유지합니다.
- `#define` 상수는 대문자로 작성하고 값, 주석 사이 정렬을 맞춥니다.
- 단일 행 주석은 `//`를 사용하며, 변경 이력 주석은 한국어로 남깁니다.
- 함수 선언과 정의 사이에는 한 줄을 비워 가독성을 확보합니다.

## 7. 빌드 및 테스트
- 개발 환경: Windows 11 + PowerShell/Git Bash 기준 (WSL/Linux에서도 동일 명령 동작). ARM GCC Toolchain, CMake 3.13 이상, Python3 필요.
```bash
cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/keynetix/may65'
cmake --build build -j10
```
- Windows에서 MinGW 사용 시 `-G "MinGW Makefiles"`를 추가합니다.
- 별도의 **빌드 테스트 실행 명령**이 없다면 빌드 테스트는 생략합니다.
- UF2 변환은 CMake 타깃 내부에서 자동으로 처리됩니다 (`tools/uf2/uf2conv.py`, family `0xFFFF0002`).
- JSON/VIA 파일 검증은 `jq` 또는 Python 표준 JSON 파서를 사용합니다. 도구별 경로 전제(WSL 심볼릭 링크 등)는 두지 않습니다.

## 8. 디렉터리 힌트
- `src/` : 펌웨어 소스 및 라이브러리 전반
- `src/ap/` : 애플리케이션 계층과 QMK 포팅
- `src/hw/` : 하드웨어 추상화 및 펌웨어 버전 정의
- `src/bsp/` : 보드 초기화, 클럭, 스타트업 코드
- `src/lib/` : 벤더 라이브러리 (ST HAL/CMSIS/USB Device Library, lib8tion)
- `tools/` : 빌드·UF2 보조 스크립트, graphify 부트스트랩(`tools/graphify/`)
- `docs/` : 결정 기록·실측 회고·기능 운영 가이드 (§11 참조)

## 9. PR 작성 지침
- PR 제목과 본문은 한국어로 작성합니다.
- 변경 요약과 테스트 결과를 명시하고, 현재 펌웨어 버전 문자열(예: `V260821R1`)을 포함합니다.
- 후속 PR이라면 기존 요약을 유지한 채 의미 있는 변경만 추가로 기술합니다.

## 10. 추가 주의사항
- USB 모니터는 구성 직후 50ms 홀드오프 후 HS 2048 프레임(약 256ms)/FS 128 프레임을 채우거나 2.05초 타임아웃 중 먼저 도달하면 활성화됩니다. 다운그레이드 ARM→COMMIT 지연은 2초입니다.
- VIA에서 USB 모니터를 런타임 비활성화하면 대기 중인 다운그레이드·리셋 큐도 함께 초기화됩니다 (V251124R3, `usbMonitorResetQueues`).
- 타이머/USB 경로를 변경할 때는 8000Hz 스케줄링이 유지되는지 검증합니다.
- 장기 실행 타이머를 건드릴 경우 USB instability monitor의 워밍업/타임아웃 조건을 함께 확인하면 리그레션을 줄일 수 있습니다.
- QMK 업스트림 병합 시 `quantum/`을 먼저 비교하고, 이후 `port/`에서 플랫폼 수정을 재적용합니다.
- 디버그 로그는 `src/hw/hw.c` 초기화 루틴과 연동되어 있으므로, 버전 문자열 출력 경로를 수정할 때는 전역 영향 범위를 검토하세요.

## 11. 문서 체계와 참조 가이드
문서 역할 분담 원칙:
- **코드 구조·호출 관계**는 지식그래프(§3)로 질의합니다. 구조 설명만을 위한 문서는 새로 만들지 않습니다.
- **docs/** 에는 코드에서 복원 불가능한 것만 둡니다: 결정 기록(`DECISIONS.md`), 실측·회고(`freeze_retro.md`), 기능별 운영·트러블슈팅 가이드(`features_*.md`, `eeprom.md`, `rgblight.md`), 릴리스 동봉 사용자 안내(`readme.txt`).
- 세션 인수인계, 결정, 실측 결과는 에이전트 개인 메모리가 아니라 `docs/DECISIONS.md`에 기록합니다 (Codex·Claude 간 공유 필요).
- 릴리스 시 `docs/readme.txt`의 버전 표기를 함께 갱신합니다.

작업별 참조 목록 (그래프 질의로 부족할 때):
- **펌웨어 버전/로그 문자열**: `src/hw/hw_def.h`의 `_DEF_FIRMWARE_VERSION`, `src/hw/hw.c`의 `hwInit()` 버전 출력
- **엔트리 포인트/메인 루프**: `src/main.c`의 `main()`, `src/ap/ap.c`의 `apInit()/apMain()`, 초기 설정은 `src/ap/ap_def.h`
- **QMK 포팅 공통 계층**: `src/ap/modules/qmk/port/`의 `sys_port.*`, `matrix*.c`, `ver_port.c`, `via_hid.*`, 플랫폼별 HAL 연동은 `port/platforms/`
- **키보드별 키맵/설정**: `src/ap/modules/qmk/keyboards/era/<vendor>/<board>/` (config.h, info.json, keymap.c, json/의 VIA JSON, port/)
- **USB instability monitor & 폴링 다운그레이드 큐**: `src/hw/driver/usb/usb_hid/usbd_hid.c`, `src/hw/driver/usb/usb.[ch]` — 문서: `docs/features_instability_monitor.md`
- **USB 클래스/엔드포인트**: `src/hw/driver/usb/`(`usbd_conf.c`, `usb_hid/`, `usb_cdc/`, `usb_cmp/`, `usb_hid/usbd_hid_instrumentation.c`), CDC 상위 계층은 `src/hw/driver/cdc.c`
- **EEPROM/BootMode/자동 초기화**: `src/hw/driver/eeprom/`, `src/hw/driver/eeprom_auto_factory_reset.c`, `src/ap/modules/qmk/port/port.h`의 `EECONFIG_USER_BOOTMODE` — 문서: `docs/eeprom.md`, `docs/features_bootmode.md`, `docs/features_auto_factory_reset.md`
- **키 입력/디바운스**: `src/hw/driver/keys.c`, `src/ap/modules/qmk/port/matrix*.c` — 문서: `docs/features_keyinput.md`
- **Tap Dance / Tapping**: `src/ap/modules/qmk/port/tapdance.c`, `quantum/process_keycode/process_tap_dance.c` — 문서: `docs/features_tapdance.md`, `docs/features_tapping.md`
- **LED/RGB**: `src/hw/driver/led.c`, `src/hw/driver/ws2812.c`, QMK 헬퍼는 `src/ap/modules/qmk/quantum/rgblight/` — 문서: `docs/rgblight.md`
- **로깅/CLI**: `src/hw/driver/log.c`, `src/hw/driver/uart.c`, USB CLI 진입점은 `src/hw/driver/usb/usb.c`의 `cliBoot`
- **빌드/툴체인**: `tools/` 디렉터리 일체 (CMake 헬퍼, UF2 변환, ST-LINK 로더)
