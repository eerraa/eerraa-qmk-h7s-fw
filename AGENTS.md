# ERA-QMK-H7S-BOOT — 에이전트 진입점

STM32H7S3용 TinyUSB MSC/UF2 부트로더다. 원본 프로젝트와 포크 출처, 사용자용 빌드 명령은 `README.md`가 소유한다.

문서 규약은 `eerraa-agent-docs` v2의 commit `4bd84f45dd3e970bf25505059431058efbe7680a`에 고정한다.
- 규약: https://github.com/eerraa/eerraa-agent-docs/blob/4bd84f45dd3e970bf25505059431058efbe7680a/AGENT_DOCS_CONVENTION.md
- 채택 절차: https://github.com/eerraa/eerraa-agent-docs/blob/4bd84f45dd3e970bf25505059431058efbe7680a/ADOPTION.md
중앙 문서는 문서체계 변경·채택 검토 때만 조회한다. 일반 개발은 이 로컬 지침으로 시작한다.

## 시작과 작업 경계

- 실제 root/branch/HEAD/status와 관련 diff를 먼저 확인하고 사용자 변경을 보존한다.
- 구현 동작·상수·버전·메모리 주소는 현재 소스와 설정이 소유한다. 제품 요구·안전·호환 조건이 소스와 어긋나면 문서를 소스에 맞춰 완화하지 말고 충돌로 다룬다.
- 문서 작업은 문서와 문서 포인터만 바꾼다. 별도 제품 변경 권한 없이는 소스, 펌웨어 버전, 플래시 동작을 바꾸지 않는다.
- `LICENSE`, 벤더 라이선스/원본 안내와 `README.md`의 upstream 출처를 보존한다.
- 짝 펌웨어 `eerraa-qmk-h7s-fw`는 호환 확인에 필요한 경우만 읽고, 상대 변경은 그 저장소의 별도 작업 범위로 시작한다.

## 작업 라우터

| 작업 | 필요한 계약·문서 | 첫 소스·설정 앵커 | 관련 검증 |
| --- | --- | --- | --- |
| 빌드·툴체인 | `README.md` §Build | `CMakeLists.txt`, `tools/arm-none-eabi-gcc.cmake`, `src/bsp/ldscript/STM32H7S3V8TX_BOOT.ld` | 빌드 입력 변경 시 README의 CMake 경로를 실제 실행 |
| UF2 자동 시작·복구 | `docs/uf2_auto_start.md` | `src/ap/modules/uf2/uf2.c`의 `uf2RequestFirmStart()`/`uf2Update()`, `src/ap/modules/uf2/uf2_msc.c`, `src/ap/ap.c`의 `bootUp()` | 문서 포인터와 소스 심볼 대조; 제품 변경 시 영향 범위 빌드/실기 검증 |
| 부트 인계·플래시 레이아웃 | `docs/uf2_auto_start.md`의 storage/compatibility 절 | `src/hw/hw_def.h`, `src/ap/modules/boot/boot.c`, `src/hw/driver/reset.c` | 주소·크기·버전은 소스에서 확인; 실기기 미실행을 PASS로 쓰지 않음 |
| USB 종료·H7S 펌웨어 호환 | `docs/uf2_auto_start.md`의 compatibility 절 | `src/hw/driver/usb/usb.c`; 짝 펌웨어의 `docs/contract_usb.md` §6 및 `src/hw/driver/usb/usbd_conf.c::HAL_PCD_MspInit()` | 양쪽 현재 revision을 명시해 정적 대조; HIL은 실제 실행했을 때만 판정 |
| 문서·원본·라이선스 | `AGENTS.md`, `README.md`, `docs/` | 해당 문서와 `LICENSE`, 벤더 `LICENSE.txt` | 로컬 링크/경로 확인, `git diff --check` |

## 종료

관련 검증과 최종 diff/공백 검토 후 자신의 변경만 관심사 단위로 commit한다. 요청 없는 push/tag/PR/배포/실기기 조작은 하지 않는다.
