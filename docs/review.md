# 펌웨어 로직 리뷰 (V251124R2)

## 범위
- 부팅 경로(main → ap → qmk)와 BootMode/VIA 경로, USB instability monitor, AUTO_FACTORY_RESET, 키 스캔·디바운스·탭핑 설정, RGB 인디케이터, EEPROM 큐를 검토했습니다.

## 주요 발견사항
1. **USB 모니터 토글 시 다운그레이드/리셋 큐가 유지됨**  
   - 위치: `src/hw/driver/usb/usb.c` (usbProcess/usbInstabilityStore 주변)  
   - 내용: VIA에서 USB 모니터를 끈 뒤에도 이미 ARM/COMMIT 상태인 `boot_mode_request`/`usb_reset_request`가 그대로 처리되어 예상치 못한 다운그레이드·재부팅이 발생할 수 있습니다. 런타임 비활성화 시 큐를 즉시 초기화하거나 `usbProcessBootModeDowngrade()` 실행을 토글 상태에 연동해야 합니다.  
   - 영향: 모니터 OFF를 선택한 사용자가 즉시 효과를 기대해도 2초 후 강제 리셋/모드 저장이 진행될 수 있습니다.

2. **Composite 모드에서 USB 모드 캐시가 잘못 저장됨**  
   - 위치: `src/hw/driver/usb/usb.c:618-638`  
   - 내용: `USB_CMP_MODE` 분기에서도 `is_usb_mode`가 `USB_CDC_MODE`로 기록됩니다. CLI `usb info` 등에서 모드가 CDC로 잘못 보고되고, 향후 모드 분기 로직 추가 시 오동작 위험이 있습니다.  
   - 개선: `is_usb_mode = USB_CMP_MODE;`로 보정.

3. **키 스캔 초기화 실패를 감지하지 않음**  
   - 위치: `src/hw/driver/keys.c:32-46`  
   - 내용: GPIO/DMA/타이머 초기화 실패 여부를 무시하고 항상 true를 반환합니다. 일부 HAL 호출 실패 시에도 키 스캔이 시작된 것처럼 진행되어 디버깅이 어려워집니다.  
   - 개선: 각 초기화 결과를 확인해 실패 시 로그를 남기고 false를 반환하도록 변경.

4. **EEPROM 큐 flush가 실패 시 무한 루프**  
   - 위치: `src/ap/modules/qmk/port/platforms/eeprom.c:72-148`  
   - 내용: `eepromWritePage()`가 연속 실패하면 큐가 비워지지 않아 `eeprom_flush_pending()`이 영구 루프에 빠집니다(특히 AUTO_FACTORY_RESET 시).  
   - 개선: flush에 타임아웃/재시도 한계를 두고 오류를 상위로 전달해 부팅이 완전히 멈추지 않게 처리 필요.
