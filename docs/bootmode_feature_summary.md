# BOOTMODE 기능 요약

## V250923R1
- **EEPROM 초기화 및 부팅 시 적용**
  - `src/hw/hw.c`에서 `eeprom_init()`과 `usbBootModeLoad()`를 부팅 단계에 추가하여 QMK EEPROM 이미지를 동기화하고, 저장된 USB 부트 모드 설정을 자동으로 불러옵니다.
- **부트 모드 영속화 및 조회 API**
  - `src/hw/driver/usb/usb.c`와 `src/hw/driver/usb/usb.h`에 `usbBootModeLoad`, `usbBootModeGet`, `usbBootModeIsFullSpeed`, `usbBootModeGetHsInterval`, `usbBootModeSaveAndReset` 등의 함수를 정의하여 원하는 USB 폴링 속도를 EEPROM에 저장하고 재부팅 시 적용할 수 있게 했습니다.
- **CLI 지원**
  - `boot info`, `boot set 8k/4k/2k/1k` 명령을 통해 현재 모드 확인 및 변경이 가능하도록 `cliBoot` 핸들러를 추가했습니다.
- **동적 HID 폴링 간격**
  - `src/hw/driver/usb/usb_hid/usbd_hid.c`와 `src/hw/driver/usb/usb_cmp/usbd_cmp.c`에서 HS 모드일 때 `usbBootModeGetHsInterval()` 값을 사용해 키보드·VIA·EXK 엔드포인트의 `bInterval`을 동적으로 설정하고, FS 모드에서는 1kHz 간격을 유지합니다.
- **로그 및 진단 개선**
  - 모든 USB 모드(`usbBegin`) 초기화 로그와 CLI 출력에 선택된 BootMode 이름을 추가하여 동작 상태를 확인하기 쉽게 했습니다.

## V250923R2
- **Composite HID 엔드포인트 패킷 크기 유지**
  - `src/hw/driver/usb/usb_cmp/usbd_cmp.c`에서 FS 모드에서도 64바이트 패킷 크기를 유지하고, HS 모드에서는 3 트랜잭션 구성을 유지하여 BootMode 전환 시에도 안정적인 데이터 전송 용량을 확보했습니다.

## V250924R1
- **폴링 속도 측정 창 동기화**
  - `src/hw/driver/usb/usb_hid/usbd_hid.c`의 `usbHidMeasurePollRate()`에서 BootMode 설정에 맞춰 샘플링 윈도우를 1kHz(FS) 또는 8kHz(HS)로 자동 조정하여 폴링 속도 통계를 보다 정확하게 측정합니다.
- **펌웨어 버전 식별자 업데이트**
  - `src/hw/hw_def.h`의 `_DEF_FIRMWATRE_VERSION` 값을 `V250924R1`으로 갱신하여 새로운 BootMode 개선 사항을 반영합니다.

---
이 문서는 CODEX가 BOOTMODE 브랜치에서 도입된 핵심 변경사항을 빠르게 파악할 수 있도록 버전별로 정리한 요약본입니다.
