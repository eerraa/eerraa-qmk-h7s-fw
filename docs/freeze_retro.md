# 프리징 이슈 회고 (USB_MONITOR_ENABLE 빌드)

## 개요
- 대상 보드: STM32H7S (HS 8000 Hz 기본) / Brick60 VIA 빌드
- 현상: `USB_MONITOR_ENABLE`이 정의된 상태에서 부팅 후 약 620 s(10분 20초) 경과 시 키보드가 완전히 정지. LED 토글 포함 모든 동작이 멈춰 CPU/SysTick 정지로 추정됨.
- 전제: USB 모니터 런타임 값이 OFF일 때도 재현. `USB_MONITOR_ENABLE`를 빌드에서 제거하면 재현되지 않음.

## 주요 버전 타임라인
- **V251123R7**: `src/ap/ap.c`에 1초 주기 USB 스냅샷 로그 추가(usbDebugGetState), LED 0.5 s 토글로 CPU 생존 확인. `src/hw/driver/usb/usb.c`에 디버그 스냅샷 구조체 추가.
- **V251123R8**: `src/bsp/device/stm32h7rsxx_it.c` SysTick에서 heartbeat 미갱신 2 s 이상 시 `[F] Main stalled` 로그 추가, `src/bsp/bsp.c`/`bsp.h`에 heartbeat 상태 관리 추가. 펌웨어 버전 `_DEF_FIRMWARE_VERSION "V251123R8"` (`src/hw/hw_def.h`).
- **V251124R1**: 런타임 모니터 OFF일 때 `micros()` 호출조차 우회하도록 래퍼 추가.
  - `src/ap/ap.c`: `usbHidMonitorBackgroundService()` 호출로 변경.
  - `src/hw/driver/usb/usb_hid/usbd_hid.h`/`.c`: `usbHidMonitorBackgroundService()` 신규. `usbInstabilityIsEnabled()==false`면 즉시 리턴. 모니터 ON일 때만 `usbHidMonitorBackgroundTick()` 내부 로직 실행.
- **V251124R2** (현재): 프리징 재현 테스트용 계측 롤백.
  - V251123R7의 1초 주기 USB 스냅샷/LED 주기 토글 제거, LED는 부팅 후 0.5 s 경과 시 1회 OFF로 복원 (`src/ap/ap.c`).
  - V251123R8 heartbeat/SysTick 헬스체크 제거 (`src/bsp/bsp.c`, `src/bsp/bsp.h`, `src/bsp/device/stm32h7rsxx_it.c`).
  - 펌웨어 버전 `_DEF_FIRMWARE_VERSION "V251124R2"` (`src/hw/hw_def.h`).

## 재현 조건(과거)
- 빌드 플래그: `USB_MONITOR_ENABLE` 정의.
- 런타임 설정: USB 모니터 OFF, BootMode HS/FS 무관(1 kHz도 재현). 입력/서스펜드 여부 무관.
- 시점: 약 620 s 경과 후 LED 토글 포함 전체 정지. 다운그레이드/리셋 큐 로그, Fault 로그 없음. LED 멈춤 → IRQ/클럭까지 정지 추정.

## 계측/관측 결과 요약
- V251123R7/8 계측 도입 후에는 디버그 로그가 반복(usb mon=OFF stage=0 reset=0)되나, 프리징 직전까지 특이 로그 없음.
- Fault 로그(Hard/Mem/Bus/Usage) 추가 후에도 정지 시점에 로그 없음.
- V251123R8에서 SysTick stall 로거 추가 후 11분 이상 테스트에서 재현 불가(비재현). Heisenbug 의심.
- V251124R1/ R2에서도 모든 조합(HW_LOG_ENABLE_DEFAULT=0/1, USB 모니터 ON/OFF, 1 kHz/8 kHz)에서 프리징 재현 안 됨.

## 추정/미해결점
- 타이밍/배치 의존 Heisenbug 가능성: `USB_MONITOR_ENABLE` 정의로 코드/링커 배치가 달라지고, 계측 추가가 우연히 회피 요인이 되었을 수 있음.
- 모니터 OFF 경로라도 `micros()`/SOF ISR 접근이 있었다는 점에서, 타이머/클럭/IRQ 경합 문제 가능성. V251124R1에서 OFF 시 완전 우회.
- 정확한 원인은 미확인. 계측 제거(R2)에서도 재현이 없으므로 회피 요인이 사라진 것인지, 근본 원인이 사라졌는지는 불명.

## 파일별 핵심 포인트
- `src/ap/ap.c`:
  - 현재(R2): 부팅 후 `_DEF_LED1` ON → 500 ms 경과 시 1회 OFF. 메인 루프에서 `usbHidMonitorBackgroundService()` 호출.
  - R7 시절: 0.5 s 주기 토글 + 1 s 주기 USB 상태 로그.
- `src/hw/driver/usb/usb_hid/usbd_hid.c`:
  - `usbHidMonitorBackgroundService()`가 모니터 런타임 OFF 시 즉시 리턴. ON일 때만 `usbHidMonitorBackgroundTick(micros())` 실행.
  - SOF ISR 경로 `USBD_HID_SOF`에서 모니터 ENABLE + 런타임 ON일 때만 `usbHidMonitorSof()` 호출.
- `src/bsp/device/stm32h7rsxx_it.c`:
  - R8에서 SysTick 2 s stall 로거가 있었으나 R2에서 제거됨.
- `src/hw/hw_def.h`:
  - `_DEF_FIRMWARE_VERSION` 확인. R8에서 V251123R8, 현재 V251124R2.

## 향후 재발 시 조사 가이드
1. **빌드/토글 확인**: `USB_MONITOR_ENABLE` 정의 여부와 `usbInstabilityIsEnabled()` 런타임 값 확인. VIA 토글이 EEPROM에 반영되는지 `usbInstabilityLoad()` 로그 확인 (`src/hw/hw.c`).
2. **계측 단계적 활성화**:
   - R7/R8 계측(USB 상태 주기 로그, SysTick stall)만 개별적으로 다시 활성화해 재현 여부 비교. 각각의 활성화 포인트는 위 파일 참조.
   - 모니터 OFF 우회(usbHidMonitorBackgroundService) 제거 시 재현되는지 확인.
3. **클럭/타이머 관측**:
   - 정지 시 `micros()`(TIM5) 증분 여부, SysTick 핸들러 진입 여부, USB suspend/resume 로그(`usbd_conf.c`)를 추가 계측.
4. **링커/배치 영향**:
   - `USB_MONITOR_ENABLE`를 비정의 빌드와 비교해 `.map`/`.elf`에서 TIM/USB/PCD 관련 ISR 위치나 BSS/STACK 배치 차이를 확인.
5. **로그 최소화 재현**:
   - HW_LOG_ENABLE_DEFAULT=0, 모니터 OFF, 1 kHz로 장시간(≥15분) 반복. 재현 시점 직전 로그가 없는지 확인.

## 결론/상태
- V251124R2까지는 프리징 재현 없음. 근본 원인은 미확인.
- 재발 대비를 위해 위 가이드로 단계적 계측/비교 테스트 필요.
