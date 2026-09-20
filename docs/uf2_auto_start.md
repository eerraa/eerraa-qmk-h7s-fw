# UF2 자동 시작·복구 계약

Genre: contract
Canonical for: UF2 완료·이젝트 후 펌웨어 시작 방식, 실패 복구, 구 부트로더와 H7S 펌웨어 사이의 USB 인계 호환 경계

## 1. 요구

- UF2 전송 완료 또는 MSC 이젝트 뒤에는 활성 USB 세션에서 펌웨어로 직접 점프하지 않고 MCU reset을 거쳐 시작한다. H7S의 OTG core/USBPHYC 상태를 TinyUSB 세션에서 그대로 넘기면 펌웨어의 HAL USB 초기화가 실패할 수 있으므로 reset 상태의 주변장치를 인계하는 것이 요구다.
- MSC 완료 콜백 안에서 긴 erase/copy를 수행하지 않는다. 업데이트 예약 뒤 TinyUSB가 마지막 상태 응답을 처리할 기회를 유지하고, 실제 업데이트는 reset 이후 boot 경로가 수행한다.
- 업데이트 예약은 reset을 넘어 보존되어야 하며 boot 시작 시 한 번 소비·제거되어 reset loop를 만들지 않아야 한다.
- USB deinit의 전기적 detach는 USB가 실제 초기화된 경우에만 수행한다. cold boot의 `bootUp()` 경로는 USB clock이 켜지기 전 펌웨어로 갈 수 있으므로 미초기화 상태의 USB register 접근은 허용하지 않는다.
- 업데이트 검증 또는 펌웨어 jump가 실패하면 boot mode와 재업로드 경로가 남아야 한다. 실패한 이미지를 실행하거나 복구 불가능 상태로 진행하지 않는다.

대기 시간, 상태 번호, boot-mode bit, 주소·크기·제품 버전 같은 구현 값은 아래 소스가 소유하며 이 문서에 복제하지 않는다.

## 2. 소스 소유자

| 관심사 | 정본 진입점 |
| --- | --- |
| UF2 payload/tag 기록과 reset 예약·detach 상태머신 | `src/ap/modules/uf2/uf2.c` — `uf2_flash_write()`, `uf2_flash_flush()`, `uf2RequestFirmStart()`, `uf2Update()` |
| MSC 완료·이젝트 진입 | `src/ap/modules/uf2/uf2_msc.c` — `tud_msc_write10_complete_cb()`, `tud_msc_start_stop_cb()` |
| reset 후 update 소비, 검증 실패 재시도와 Boot Mode fallback | `src/ap/ap.c` — `bootUp()` |
| boot mode의 RTC 보존·1회 소비와 system reset | `src/hw/driver/reset.c` — `resetInit()`, `resetSetBootMode()`, `resetToReset()` |
| UPDATE→FIRM 복사·검증과 SRAM jump | `src/ap/modules/boot/boot.c` — `bootUpdateFirm()`, `bootJumpFirm()` |
| USB 초기화 여부 가드와 deinit | `src/hw/driver/usb/usb.c` — `usbDeInit()` |
| 제품 버전·FLASH/SRAM 레이아웃 | `src/hw/hw_def.h`; boot linker layout은 `src/bsp/ldscript/STM32H7S3V8TX_BOOT.ld` |

## 3. Storage와 H7S 펌웨어 호환 경계

- UF2 payload와 tag는 `FLASH_ADDR_UPDATE` 쪽에 기록되고 boot 경로가 검증 후 FIRM 영역으로 복사한다. UF2 경로는 내부 flash의 bootloader 영역을 갱신하지 않는다. 정확한 주소와 크기는 `src/hw/hw_def.h`가 소유한다. bootloader 자체 교체는 UF2가 아니라 외부 programmer(ST-LINK) 작업이다.
- 이미 출하된 구 bootloader처럼 활성 TinyUSB 상태를 clock-gate만 한 뒤 직접 jump하는 경우를 위해 H7S 펌웨어는 inherited USB block 복구를 유지한다. 그 요구의 상대 소유자는 `eerraa-qmk-h7s-fw/docs/contract_usb.md` §6이고 구현 진입점은 `src/hw/driver/usb/usbd_conf.c::HAL_PCD_MspInit()`이다.
- 현재 bootloader의 reset 인계와 펌웨어 측 inherited-USB 복구는 양립해야 한다. reset 상태로 넘어오면 펌웨어의 복구 분기는 개입하지 않아야 하고, 구 bootloader 상태를 받으면 펌웨어가 OTG/USBPHYC를 정상 초기 상태로 되돌려야 한다.
- `HAL_PCD_Init()` 이후의 단순 boot-time detach hold는 구 bootloader 호환 해결책으로 취급하지 않는다. 실패 지점이 그보다 앞이므로 해당 접근을 복구 계약으로 되살리지 않는다.

## 4. 검증 경계

문서/포인터 변경은 위 경로와 심볼의 실재 여부, 로컬 링크, `git diff --check`를 확인한다. 제품 동작을 바꾼 경우에만 영향 범위 빌드와 필요한 하드웨어 검증을 추가한다.

실기기 acceptance가 필요한 변경에서는 최소한 다음 경로를 구분해 확인한다: UF2 완료 후 자동 시작, MSC 수동 이젝트, cold boot, ESC bootloader 진입, 펌웨어가 요청한 bootloader 진입, 업데이트 검증 실패 뒤 재업로드 가능 상태. 자동 시작 안정성 판정은 연속 10회 이상의 UF2 업로드에서 모두 성공해야 한다. 실행하지 않은 HIL은 PASS로 보고하지 않는다.
