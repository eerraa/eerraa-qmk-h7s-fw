# DEV_BOOTMODE CODEX 레퍼런스

## 1. 브랜치 개요
- **기반 브랜치**: `DEV_BOOTMODE`
- **주요 목적**: USB 부트 모드(HS 8K/4K/2K, FS 1K) 선택값을 EEPROM에 영속화하고, 부팅 시점부터 HID/Composite 구성과 진단 로그가 해당 모드에 맞춰 동작하도록 일원화합니다.
- **핵심 소비자**: `usb.c`(모드 저장/로드 및 CLI), `usb_hid/usbd_hid.c` · `usb_cmp/usbd_cmp.c`(엔드포인트 bInterval/패킷 구성), `usbd_conf.c`(FS 강제 구동), `hw.c`(초기 로드), `qmk/port/port.h`(EEPROM 슬롯 정의).

## 2. 영향 범위 매트릭스
| 경로 | 주요 심볼/루틴 | 코드 영향 요약 |
| --- | --- | --- |
| `src/ap/modules/qmk/port/port.h` | `EECONFIG_USER_BOOTMODE` | QMK 사용자 EEPROM 4바이트 슬롯을 예약해 부트 모드 선택값을 저장합니다.
| `src/hw/hw.c` | `usbBootModeLoad()` | 부팅 초기화 시 EEPROM 동기화 직후 부트 모드를 로드해 이후 USB 초기화 흐름에 반영합니다.
| `src/hw/driver/usb/usb.[ch]` | `usbBootModeLoad/Store/SaveAndReset`, `usbBootModeGetHsInterval`, `cliBoot` | 모드 열거형, HS `bInterval` 인코딩, CLI `boot info/set` 명령 및 재부팅을 포함한 저장 시나리오를 제공합니다.
| `src/hw/driver/usb/usbd_conf.c` | `usbBootModeIsFullSpeed()` 분기 | HS PHY 사용 시에도 FS 1kHz 강제 구동(`PCD_SPEED_HIGH_IN_FULL`)을 선택할 수 있게 합니다.
| `src/hw/driver/usb/usb_hid/usbd_hid.c` | `usbBootModeGetHsInterval()` 활용 | HID/VIA/EXK 엔드포인트의 HS `bInterval`을 동적으로 설정하고, 폴링 계측 루틴의 샘플 윈도우를 부트 모드와 동기화합니다.
| `src/hw/driver/usb/usb_cmp/usbd_cmp.c` | `usbBootModeGetHsInterval()` 활용 | Composite 프로필의 각 인터페이스가 모드별 HS `bInterval`과 FS 1kHz 강제 값을 유지합니다.
| `src/hw/driver/usb/usb.c` | `usbBegin()` 로그 | USB 모드별 초기화 로그에 현재 부트 모드 레이블을 추가해 진단 가시성을 제공합니다.

## 3. 핵심 흐름 & 참조 포인트
### 3.1 영속화 및 초기화
- `usbBootModeLoad()`는 `EECONFIG_USER_BOOTMODE`에서 값을 읽고 유효 범위를 벗어나면 기본값 `USB_BOOT_MODE_HS_8K`로 복귀합니다.
- 부팅 경로: `hw.c` → `usbBootModeLoad()` → `usbInit()` → `usbBegin()` 순으로 모드가 전파됩니다.
- 저장 경로: `usbBootModeSaveAndReset()` → `resetToReset()` → 재부팅 후 `usbBootModeLoad()`로 적용을 확정합니다.

### 3.2 CLI 인터페이스
- `boot info`로 현재 저장된 모드를 확인하고, `boot set {8k|4k|2k|1k}`로 모드를 갱신합니다. 저장 성공 시 직후 시스템 리셋이 발생합니다.
- CLI 출력은 `usbBootModeLabel()`을 사용하며, 사용자 로그와 동일한 문자열을 공유합니다.

### 3.3 HID/Composite 동작
- HS `bInterval` 매핑: `USB_BOOT_MODE_HS_8K → 0x01`, `HS_4K → 0x02`, `HS_2K → 0x03`, `FS_1K → 0x01`.
- FS 강제 모드에서는 `usbd_conf.c`가 `PCD_SPEED_HIGH_IN_FULL`을 선택하고, 각 엔드포인트는 `HID_FS_BINTERVAL`(1ms)을 유지합니다.
- `usbHidMeasurePollRate()`는 부트 모드에 따라 계측 샘플 윈도우를 HS 8000 프레임 또는 FS 1000 프레임으로 자동 교체합니다.

### 3.4 진단 로그
- `usbBegin()` 각 분기(CDC/MSC/HID/CMP)와 CLI `boot` 명령은 선택된 모드를 `HS 8K`, `HS 4K`, `HS 2K`, `FS 1K` 레이블로 표기합니다.
- EEPROM 로드 실패 시 `usb.c`에서 `[!] usbBootModeLoad Fail` 로그가 발생하도록 준비되어 있습니다.

## 4. 버전 히스토리
### V250923R1 — BootMode 인프라 구축
- QMK EEPROM(`port.h`)에 부트 모드 슬롯을 정의하고 `usbBootModeLoad()`를 `hw.c` 초기화 흐름에 삽입했습니다.
- `usb.[ch]`에 모드 열거형, 저장/로드/질의 API, CLI `boot info/set` 명령, `usbBootModeSaveAndReset()` 재부팅 루틴을 도입했습니다.
- `usb_hid/usbd_hid.c`와 `usb_cmp/usbd_cmp.c`가 HS/FS 별 `bInterval`을 동적으로 반영하며, `usbBegin()` 로그가 부트 모드 레이블을 표시합니다.
- `usbd_conf.c`가 FS 강제 시나리오에서 `PCD_SPEED_HIGH_IN_FULL` 설정을 적용해 HS PHY + 1kHz 조합을 지원합니다.

### V250923R2 — Composite HID 패킷 유지
- `usb_cmp/usbd_cmp.c`가 FS 환경에서도 64바이트 패킷과 HS 3-트랜잭션 구성을 유지해 모드 전환 시 입력 손실을 방지합니다.

### V250924R1 — 폴링 계측 정합
- `usbHidMeasurePollRate()`가 부트 모드를 참조해 SOF 샘플 윈도우(1000 vs 8000)를 자동 선택하고, 버전 문자열을 `V250924R1`으로 갱신했습니다.

### V251009R1 — EEPROM 슬롯 재배치
- 사용자 EEPROM 재배치에 맞춰 `EECONFIG_USER_BOOTMODE` 오프셋을 `0x20`(32)으로 이동하고, 인디케이터 · SOCD · KKUK 슬롯을 선행 배치했습니다.
- `qmk/port/port.h`에 슬롯 오프셋 상수를 분리하고 `_Static_assert`로 사용자 영역 크기 내 배치를 검증합니다.
- `usb.c`의 부트 모드 저장/로드 경로가 신규 오프셋을 참조함을 명시해 향후 점검 시 혼선을 줄였습니다.

## 5. CODEX 점검 팁
- `usbBootModeGetHsInterval()` 호출이 추가되는 지점에서는 FS 강제 시 `HID_FS_BINTERVAL`이 유지되는지 동시에 확인하십시오.
- `usbBootModeSaveAndReset()`은 즉시 리셋을 수행하므로, CLI 확장 시 사용자 메시지 출력 후 리셋되도록 순서를 조정해야 합니다.
- HS/FS 모드별 상수를 손볼 경우 `usbd_conf.c`의 PHY 속도 설정, HID/Composite EP 설정, CLI 도움말 문자열이 함께 갱신되어야 합니다.
- EEPROM 슬롯 재배치가 필요하면 `EECONFIG_USER_BOOTMODE` 오프셋 변경과 `usbBootModeLoad()` 기본값 보정이 반드시 동반되어야 합니다.

---
이 문서는 CODEX가 `DEV_BOOTMODE` 브랜치에서 도입된 변경 사항을 신속하게 추적하고 재활용할 수 있도록 구성한 레퍼런스입니다.
