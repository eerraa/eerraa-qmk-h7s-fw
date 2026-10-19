# ERA-QMK-H7S-BOOT — 에이전트 진입점

STM32H7S3용 TinyUSB MSC/UF2 부트로더다. 원본 프로젝트와 포크 출처, 사용자용 빌드 명령은 `README.md`가 소유한다.

## 시작과 작업 경계

- 공통 Git·승인·문서 규약·검증 원칙은 루트 AGENTS.md를 따른다. 이 문서의 상대 경로와 빌드·검사 명령은 현재 부트로더 컴포넌트 디렉터리 기준이다.
- 문서 작업은 문서와 문서 포인터만 바꾼다. 별도 제품 변경 권한 없이는 소스, 펌웨어 버전, 플래시 동작을 바꾸지 않는다.
- `LICENSE`, 벤더 라이선스/원본 안내와 `README.md`의 upstream 출처를 보존한다.
- 펌웨어 컴포넌트는 호환 확인에 필요한 경우만 조회하고, 수정 범위에 포함되면 해당 AGENTS를 먼저 읽는다.

## 작업 라우터

| 작업 | 필요한 계약·문서 | 첫 소스·설정 앵커 | 관련 검증 |
| --- | --- | --- | --- |
| 빌드·툴체인 | `README.md` §Build | `CMakeLists.txt`, `tools/arm-none-eabi-gcc.cmake`, `src/bsp/ldscript/STM32H7S3V8TX_BOOT.ld` | 빌드 입력 변경 시 README의 CMake 경로를 실제 실행 |
| UF2 자동 시작·복구 | `docs/uf2_auto_start.md` | `src/ap/modules/uf2/uf2.c`의 `uf2RequestFirmStart()`/`uf2Update()`, `src/ap/modules/uf2/uf2_msc.c`, `src/ap/ap.c`의 `bootUp()` | 문서 포인터와 소스 심볼 대조; 제품 변경 시 영향 범위 빌드/실기 검증 |
| 부트 인계·플래시 레이아웃 | `docs/uf2_auto_start.md`의 storage/compatibility 절 | `src/hw/hw_def.h`, `src/ap/modules/boot/boot.c`, `src/hw/driver/reset.c` | 주소·크기·버전은 소스에서 확인; 실기기 미실행을 PASS로 쓰지 않음 |
| USB 종료·H7S 펌웨어 호환 | `docs/uf2_auto_start.md`의 compatibility 절 | `src/hw/driver/usb/usb.c`; 짝 펌웨어의 `docs/contract_usb.md` §6 및 `src/hw/driver/usb/usbd_conf.c::HAL_PCD_MspInit()` | 양쪽 현재 revision을 명시해 정적 대조; HIL은 실제 실행했을 때만 판정 |
| 문서·원본·라이선스 | `AGENTS.md`, `README.md`, `docs/` | 해당 문서와 `LICENSE`, 벤더 `LICENSE.txt` | 로컬 링크/경로 확인, `git diff --check` |
