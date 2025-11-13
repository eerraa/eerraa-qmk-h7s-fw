# USB 불안정성 모니터 가이드

## 1. 목적과 범위
- 기본 정책인 HS 8 kHz 폴링을 유지하되, 마이크로프레임 누락·속도 재협상·열거 실패·과도한 서스펜드를 감시해 자동으로 폴링 모드를 낮춥니다.
- 모니터 토글은 EEPROM/VIA를 통해 지속되고, BootMode 다운그레이드 큐( `usbProcess()` )와 긴밀하게 연동됩니다.
- 대상 모듈: `src/hw/driver/usb/usb_hid/usbd_hid.c`, `src/hw/driver/usb/usb.c`, `src/ap/ap.c`, `src/ap/modules/qmk/port/{usb_monitor.c,port.h}`, `docs/features_bootmode.md`.

## 2. 구성 파일 & 빌드 매크로
| 경로 | 주요 심볼/함수 | 설명 |
| --- | --- | --- |
| `src/ap/modules/qmk/port/port.h` | `EECONFIG_USER_USB_INSTABILITY`, `usb_monitor_config_t` | VIA 토글을 EEPROM USER 블록(+32B) 슬롯에 저장합니다. |
| `src/ap/modules/qmk/port/usb_monitor.c` | `usb_monitor_storage_*`, `via_qmk_usb_monitor_command()` | VIA channel 13 value ID 3 토글 처리, EEPROM 디바운스 헬퍼. 기본값은 OFF입니다. |
| `src/hw/driver/usb/usb.h` | `usbInstabilityLoad/Store/IsEnabled` | 런타임 토글 캐시와 빌드 가드를 정의합니다. |
| `src/hw/driver/usb/usb.c` | `usbInstability*`, `usbRequestBootModeDowngrade()`, `usbProcess()` | VIA 토글 캐시, 다운그레이드 큐, 메인 루프 상태 머신을 담당합니다. |
| `src/hw/driver/usb/usb_hid/usbd_hid.c` | `usbHidMonitor*` | SOF ISR/백그라운드 감시, 점수 계산, 이벤트 윈도우, BootMode 다운그레이드 요청. |
| `src/ap/ap.c` | `usbProcess()`, `usbHidMonitorBackgroundTick()` | 메인 루프에서 큐 상태와 SOF 누락 감시를 주기적으로 호출합니다. |

> `USB_MONITOR_ENABLE`이 정의되지 않은 빌드에서는 모든 API가 스텁으로 치환되며, VIA UI에서 해당 항목을 숨기는 것이 권장됩니다.

## 3. 저장 및 VIA 인터페이스
- `usb_monitor_config_t`는 `enable` 1바이트 + 예약 필드(3B)로 구성됩니다. 값이 0/1 외 범위면 `usb_monitor_apply_defaults_locked()`가 호출되어 OFF로 복원됩니다.
- `usb_monitor_storage_*()` 함수군은 `EECONFIG_DEBOUNCE_HELPER` 매크로로 생성되어, EEPROM 기록을 지연하고 중복 쓰기를 방지합니다.
- `via_qmk_usb_monitor_command()`는 channel 13 value ID 3에 매핑되어 있으며 toggle/set/get 모두 `usbInstabilityStore()` / `usbInstabilityIsEnabled()` API를 사용합니다.

## 4. 감시 아키텍처
```
USB SOF ISR (8000 Hz)
  ↳ usbHidMonitorSof(now_us)
      ↳ 상태 변화 체크 (CONFIGURED / SUSPENDED)
      ↳ SOF 간격 누적 & 점수 계산
      ↳ 시점별 타임아웃(holdoff, warmup, no-sof) 갱신
      ↳ 필요 시 usbHidMonitorProcessDelta()
          ↳ usbHidMonitorCommitDowngrade() → usbRequestBootModeDowngrade()

apMain()
  ↳ usbProcess()                         // 다운그레이드 큐 처리, 저장/리셋 실행
  ↳ usbHidMonitorBackgroundTick(micros())
      ↳ usbHidMonitorRefreshEventWindows()
      ↳ usbHidMonitorTrackEnumeration()
      ↳ usbHidMonitorProcessDelta() (SOF 누락 감지)
```
- SOF ISR 경로는 `USB_MONITOR_ENABLE`과 `usbInstabilityIsEnabled()`가 모두 true일 때만 동작합니다.
- 백그라운드 틱은 약 1 kHz 주기로 호출되어, 장시간 SOF가 중단되거나 USB 장치가 재열거되는 경우를 감시합니다.

## 5. 점수 및 타임아웃
### 5.1 주요 상수 (`src/hw/driver/usb/usb_hid/usbd_hid.c`)
| 상수 | 값 | 설명 |
| --- | --- | --- |
| `USB_SOF_MONITOR_CONFIG_HOLDOFF_MS` | 50 ms | 구성 직후/속도 전환 직후 감시 제외 구간. |
| `USB_SOF_MONITOR_WARMUP_FRAMES_HS` / `FS` | 2048 / 128 | 각 속도에서 안정성을 확인하기 위해 필요한 좋은 프레임 수. |
| `USB_SOF_MONITOR_NO_SOF_TIMEOUT_FACTOR` | 64 | `expected_us * 64` 동안 SOF가 없으면 누락 이벤트로 처리합니다. |
| `USB_SOF_MONITOR_SPEED_WINDOW_US` | 1 s | 이 시간 안에 3회 이상 속도 재협상이 발생하면 비정상으로 간주합니다. |
| `USB_SOF_MONITOR_SUSPEND_WINDOW_US` | 1.5 s | 이 창 안에 3회 이상 Selective Suspend가 반복되면 비정상으로 간주합니다. |
| `USB_SOF_MONITOR_PERSISTENT_THRESHOLD` | 3 | 속도/서스펜드 이벤트가 연달아 발생할 때 다운그레이드를 트리거하는 임계치. |
| `USB_SOF_MONITOR_RECOVERY_DELAY_US` | 50 ms | 다운그레이드 실패 시 재시도까지 기다리는 시간. |

### 5.2 점수 체계
| 점수 | 설명 |
| --- | --- |
| `score` | 빠른 누락 감지. SOF 간격 위반 시 이벤트 크기만큼 증가하며 감쇠는 프레임 간격 기반입니다. |
| `slow_score` | 장시간 간헐적 누락을 추적. 별도 감쇠 주기를 사용합니다. |
| `persistent_score` | 속도 변화/서스펜드 이벤트가 반복되면 증가합니다. 윈도우가 만료되면 자동으로 리셋됩니다. |

## 6. 이벤트 유형
| 이벤트 | 발생 조건 | 다운그레이드 경로 |
| --- | --- | --- |
| `USB_MONITOR_EVENT_SOF` | SOF 간격이 허용치보다 크거나 SOF가 멈춤 | 즉시 ARM/COMMIT 상태 머신으로 진입합니다. |
| `USB_MONITOR_EVENT_ENUM` | 구성 이전에 세 번 연속 detach (열거 실패) | 백그라운드 틱에서 감지 후 `usbRequestBootModeDowngrade()` 호출. |
| `USB_MONITOR_EVENT_SPEED` | 1초 내 3회 이상 속도 재협상 | Persistent 점수가 임계에 도달하면 다운그레이드. |
| `USB_MONITOR_EVENT_SUSPEND` | 1.5초 내 3회 이상 Selective Suspend | Persistent 점수가 임계에 도달하면 다운그레이드. |

각 이벤트는 로그 레이블(`usbHidMonitorEventLabel()`)과 함께 출력되어 문제 원인을 즉시 파악할 수 있습니다.

## 7. BootMode 다운그레이드 파이프라인
1. 모니터가 다음 모드를 계산 (`usbHidResolveDowngradeTarget()` → 8k→4k→2k→1k 순).
2. `usbHidMonitorCommitDowngrade()`가 `usbRequestBootModeDowngrade()`를 호출하여 큐 상태를 ARM/COMMIT으로 전환합니다.
3. ARM 단계에서 `[!] USB Poll 불안정 감지 ... (검증 대기)` 로그가 출력되고, 2초의 확인 지연이 시작됩니다.
4. COMMIT 단계에 도달하면 `[!] USB Poll 모드 다운그레이드 -> ...` 로그가 출력되고 `usbBootModeSaveAndReset()`이 호출됩니다.
5. `usbProcess()`는 EEPROM 저장 성공 후 `usbScheduleGraceReset()`으로 40 ms 유예 리셋을 예약하고 큐를 초기화합니다.

## 8. 로그 & 트러블슈팅
| 로그 | 의미/대응 |
| --- | --- |
| `[  ] USB Monitor : ON/OFF` | EEPROM 로드 결과. VIA에서 상태가 올바르게 반영되는지 확인합니다. |
| `[!] USB Monitor downgrade (SOF/SPEED/SUSPEND/ENUM)` | 해당 이벤트가 임계치를 넘었습니다. 케이블, 허브, 호스트 전원 관리 설정을 점검하세요. |
| `[!] USB Poll 모드 저장 실패` | EEPROM 쓰기 실패. `usbBootModeSaveAndReset()` 경로를 확인합니다. |
| `[!] USB Monitor Toggle -> ...` | VIA 토글이 성공적으로 저장되었습니다. |

### 운영 체크리스트
1. 모니터를 끌 때는 VIA JSON에서도 "Auto downgrade" 항목을 숨겨 혼선을 방지합니다.
2. 새로운 USB 클래스를 추가할 경우 `usbHidMonitorSof()`가 호출될 수 있도록 SOF ISR 경로(`USBD_LL_SOF`)를 유지하세요.
3. 펌웨어 변경으로 모니터가 과도하게 트리거되면 `logPrintf()`에 표시되는 이벤트/점수 정보를 기준으로 허용 오차를 조정합니다.

> 모니터 경로를 수정한 뒤에는 실제 PC/허브 조합에서 5분 이상 HS 8kHz 상태를 유지하며 로그가 깨끗한지 확인하는 것이 좋습니다.
