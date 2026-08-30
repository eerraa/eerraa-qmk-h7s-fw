# USB host contract

Genre: contract
Canonical for: what the host is shown and in what shape — interface and
endpoint layout, the 20-key report and boot-protocol size deviation, why NKRO
is not shipped, polling-mode ownership, the retired automatic USB recovery
path and why it must not be restored, bootloader-handoff detach, and the
main-loop periodic-work timer rule

## 1. Interface and endpoint layout

Shipped boards start `USB_HID_MODE` (`HW_USB_CMP` is 0 unless `_USE_HW_VCOM`
is defined). The HID configuration descriptor in
`src/hw/driver/usb/usb_hid/usbd_hid.c` declares three interfaces:

| Interface | Endpoint | wMaxPacketSize | subclass / protocol | Reports |
| --- | --- | --- | --- | --- |
| 0 | `HID_EPIN_ADDR` `0x81` IN | `HID_EPIN_SIZE` 64 B | 1 (BOOT) / 1 (Keyboard) | Keyboard IN, 22 B. No OUT endpoint; LED output is the 1 B HID SET_REPORT on EP0 |
| 1 | `HID_VIA_EP_IN` `0x84` IN / `HID_VIA_EP_OUT` `0x04` OUT | `HID_VIA_EP_SIZE` 32 B | 0 / 0 | VIA raw HID, 32 B each way |
| 2 | `HID_EXK_EP_IN` `0x85` IN | `HID_EXK_EP_SIZE` 8 B | 1 (BOOT) / 0 (none) | SYSTEM (`REPORT_ID_SYSTEM` 3, 3 B) / CONSUMER (`REPORT_ID_CONSUMER` 4, 3 B) / MOUSE (`REPORT_ID_MOUSE` 2, 6 B) |

The mouse report is 6 B (`report_mouse_t` with `MOUSE_SHARED_EP`) and fits
the existing 8 B EXK endpoint. `_Static_assert` in
`src/hw/driver/usb/usb_hid/usbd_hid.c` locks the report-descriptor sizes
to the QMK structs.

Advertised `bInterval` on that HID-only descriptor:

- HS (`USBD_HID_GetHSCfgDesc`): keyboard IN, VIA IN, VIA OUT, and EXK IN
  all take `usbBootModeGetHsInterval()`.
- FS (`USBD_HID_GetFSCfgDesc`): only keyboard IN is patched to
  `HID_FS_BINTERVAL` (1). VIA IN/OUT keep the static descriptor value 4;
  EXK keeps the static `HID_HS_BINTERVAL` (1).

`FS 1K` enumerates as Full Speed (`PCD_SPEED_HIGH_IN_FULL`). The HS 2/4/8K
modes enumerate as High Speed (`PCD_SPEED_HIGH`).

When `_USE_HW_VCOM` is on, `src/hw/driver/usb/usb_cmp/usbd_cmp.c` builds the
same three HID interfaces from the same size and report-descriptor-length
constants. HS keyboard `wMaxPacketSize` then ORs `(2U << 11)` (three
transactions per microframe); the HID-only descriptor does not.

## 2. Simultaneous keys are 20, not 6KRO

`HW_KEYS_PRESS_MAX` is 20 (`src/hw/hw_caps_keys.h`). No board overrides it.
Interface 0 has no `KEYBOARD_SHARED_EP`, so `report_keyboard_t` is mods(1) +
reserved(1) + `keys[20]` = `HID_KEYBOARD_REPORT_SIZE` / `KEYBOARD_REPORT_SIZE`
**22 B**. The report descriptor's `REPORT_COUNT` is `HW_KEYS_PRESS_MAX`.

A host that parses the report descriptor sees 20 key slots. A 21st key is
dropped: `add_key_byte()` in `src/ap/modules/qmk/port/protocol/report.c`
leaves the report unchanged when no empty slot remains. It does not send an
ErrorRollOver code.

## 3. Boot-protocol size deviation is known and kept

| | This firmware (interface 0) | HID Boot Keyboard |
| --- | --- | --- |
| IN report | 22 B — mods(1) + reserved(1) + keys[20] | 8 B — mods(1) + reserved(1) + keys[6] |
| Output report | 1 B (5 LED bits + 3 bits padding) | Same |
| Interface | subclass 1 (BOOT), protocol 1 (Keyboard) | Same |

The first 8 B of the 22 B IN report are the boot layout byte-for-byte. A
host that reads only 8 B sees the same modifiers, reserved byte, and first
six keys a boot keyboard would have sent.

Two deviations from boot protocol:

1. `USBD_HID_REQ_SET_PROTOCOL` stores `hhid->Protocol` and does not shrink
   the IN report. `usbHidSendReport()` always transmits
   `HID_KEYBOARD_REPORT_SIZE`. That field is not wired to QMK
   `keyboard_protocol`.
2. Interface 0 has no `USBD_HID_REQ_GET_REPORT` handler; the class SETUP
   default STALLs it (`USBD_CtlError`).

The remaining risk is transfer size, not content. `wMaxPacketSize` is 64
(`HID_EPIN_SIZE`). A host that arms an 8 B IN transfer against a 22 B
packet can babble-halt the endpoint.

BIOS/UEFI compatibility is therefore the 8 B prefix match, not a spec
guarantee. Do not blur that in this file or in release notes.

> **REFUSED:** shrinking the keyboard IN report to 8 B on boot protocol.
> **WHY:** the 8 kHz send path (retry queue and SOF drain) would have to
> become protocol-state variable length, and shipping images already run
> this 22 B report with no field failure report.
> **REOPENS:** a specific BIOS/KVM that fails; start with deviation 1.

> **REFUSED:** shipping NKRO (bitmap report, separate report ID).
> **WHY:** the default report already holds 20 keys with no toggle, while a
> 32 B NKRO report (`NKRO_REPORT_BITS` 30 plus ID and mods) would force EXK
> from 8 B to 32 B and grow the EXK retry queue for every user, including
> those who never enable it. A 6KRO↔NKRO toggle is a boot-compat versus
> rollover trade-off this layout does not have.
> **REOPENS:** field evidence that 20 keys is not enough. Reintroduction
> still means an extra report ID on EXK and tying `keyboard_protocol` to
> SET_PROTOCOL with `wIndex` 0.

NKRO was never offered. `NKRO_ENABLE` is not a compile definition
(`src/ap/modules/qmk/CMakeLists.txt`), so `keymap_config.nkro` has no
effect. Keeping 20-key reports is the status quo, not a regression.

## 4. 자동 USB 복구는 폐기됐다 — 되살리지 마라

없어진 것: SOF 간격 점수화와 warmup/timeout, 열거·속도·suspend 점수, 자동 8k → 4k → 2k → 1k
다운그레이드 큐, monitor EEPROM 토글, 그 토글의 VIA 채널 13 value id 3, 그리고 컴파일 타임
계측 유닛. `src/`에 **0건**이다.

없어진 것이 아니라 **폐기한 것**이고 이유는 둘이다.

1. **제품 계약.** 펌웨어는 안정성 점수나 stable/unstable 판정을 만들지 않고, 폴링 모드를
   자동으로 되돌리지 않는다. 모드 선택은 언제나 사용자 소유다(§5). 반대편은 앱 저장소의
   `the-via-eerraa/docs/adr/0002-h7s-usb-diagnostics.md`가 들고 있다. 관측은
   `docs/contract_via.md` §6의 읽기 전용 세션이 대신한다.
2. **그 코드가 원인 미상 정지를 냈다.** `USB_MONITOR_ENABLE`이 정의된 빌드에서 부팅 약 620초
   뒤 LED 토글까지 포함해 전부 멈추는 현상이 재현됐다. **런타임 토글이 OFF일 때도 재현됐고,
   빌드에서 매크로를 빼면 재현되지 않았다.** 계측을 넣으면 사라지는 Heisenbug였고 원인은 끝내
   확인되지 않았다. 즉 되살리는 쪽이 그 미해결 위험을 다시 들여오는 것이다.

> **REFUSED:** instability monitor 또는 자동 폴링 다운그레이드 복원.
> **WHY:** 위 두 이유가 함께 선다 — 앱과의 제품 계약을 깨고, 원인이 확인되지 않은 정지 경로를
> 되들인다.
> **REOPENS:** 없다. 관측이 더 필요하면 selector `0x07` 세션을 넓힌다.

**무는 것**: `python tools/era_doc_refs.py`의 `retired` 검사가 폐기 심볼이 `src/`에 다시
나타나면 실패한다. 그 목록이 곧 문서가 이 이름들을 "없는 것"으로 부를 수 있는 예외다.

EEPROM의 monitor 슬롯은 지우지 않고 `EECONFIG_USER_RESERVED_32`로 남겼다 — 뒤 슬롯 주소를
움직이지 않기 위해서다(`docs/contract_eeprom.md` §1).

지금 **항상 켜져 있는 것은 포화형 하드 카운터뿐**이다: keyboard/EXK report queue drop, USB
reset, HID configuration, suspend, speed change. RAM에만 있고 EEPROM에 쓰지 않는다.

매트릭스 개발 계측(`matrix_instrumentation.c`)은 사용자 USB 전달 진단과 **저장소도 활성 조건도
분리되어 있다.** 두 쪽의 수치를 합치지 않는다 — 재는 구간이 다르다.

## 5. Polling mode is user-owned

| enum | Link | HS `bInterval` (`usbBootModeGetHsInterval`) | VIA dropdown |
| --- | --- | --- | --- |
| `USB_BOOT_MODE_FS_1K` | FS 1 kHz | 1 (FS uses `HID_FS_BINTERVAL`) | 3 |
| `USB_BOOT_MODE_HS_2K` | HS 2 kHz | 3 | 2 |
| `USB_BOOT_MODE_HS_4K` | HS 4 kHz | 2 | 1 |
| `USB_BOOT_MODE_HS_8K` | HS 8 kHz | 1 | 0 |

Default is **FS 1 kHz**. `USB_BOOT_MODE_DEFAULT_VALUE` is
`USB_BOOT_MODE_FS_1K`; no board overrides it. All five boards set
`BOOTMODE_ENABLE`.

Channel 13 (`id_qmk_usb_polling`) in `src/ap/modules/qmk/port/bootmode.c`:
`id_qmk_usb_bootmode_select` (value 1) SET updates `pending_boot_mode`
only. `id_qmk_usb_bootmode_apply` (value 2) SET of a non-zero byte calls
`usbBootModeScheduleApply()`. `id_custom_save` is a no-op; persist is
Apply (`docs/contract_eeprom.md` §1).

The main loop (`src/ap/ap.c`) runs `usbProcess()`, which drains that queue
through `usbProcessBootModeApply()` → `usbBootModeSaveAndReset()`: write
`EECONFIG_USER_BOOTMODE`, wait at least `USB_BOOTMODE_APPLY_GRACE_MS` (40)
for the VIA response, then `usbProcessDeferredReset()` detaches
(`USB_RESET_DETACH_DELAY_MS` 100) and resets the MCU. Apply of the already
active mode still queues that reset.

CLI `boot info` / `boot set {1k|2k|4k|8k}` calls `usbBootModeSaveAndReset()`
directly — same persist-and-reset, no pending queue.

Only those user paths call the apply/reset APIs.
`src/ap/modules/qmk/port/era_usb_diagnostics.c` does not. Any automatic
step in `usbProcess()` besides user apply/reset violates §4.

Apply/reset tears down USB and reboots, so an in-flight diagnostic session
cannot continue on the same enumeration. How the host treats that boundary
is `the-via-eerraa/docs/adr/0002-h7s-usb-diagnostics.md` and
`docs/contract_via.md` §6.

## 6. 부트로더 → 펌웨어 인계는 100 ms 디태치가 필요하다

UF2 업로드 자체는 항상 성공하는데 부트로더 → 펌웨어 자동 전환이 간헐적으로 실패했다. 원인은
부트로더가 시스템 리셋이 아니라 **직접 점프**를 쓰면서 USB를 클럭 게이팅으로만 껐다는 것이다.
클럭 게이팅은 주변장치 레지스터를 리셋하지 않으므로 `DCTL.SDIS`가 0으로 남아 D+ 풀업과 HS
터미네이션이 유지된다. 호스트에게 장치는 분리된 것이 아니라 *붙어 있는데 응답만 없는* 상태이고,
그 위로 올라온 펌웨어는 영원히 열거되지 않는다.

**펌웨어가 보완할 수 있는 이유**: USB 분리/부착은 소프트웨어 인수인계가 아니라 D+/D- 선의
전기적 상태이고 `DCTL.SDIS` 비트 하나가 결정한다. USB 주변장치는 부트로더와 펌웨어가 공유하는
같은 하드웨어이며 점프 이후 소유자는 펌웨어다. 게다가 펌웨어는 이미 PCD init 끝에서 SDIS=1을
세우고 있었고 그 구간이 수백 µs로 너무 짧았을 뿐이다. 그래서 보완책은 새 동작 추가가 아니라
**기존 구간의 유지 시간 연장**이다 — `src/hw/driver/usb/usbd_conf.c`의 `USBD_LL_Start()`가
`USBD_BOOT_DETACH_HOLD_MS`(100 ms)만큼 SDIS를 유지한 뒤 PCD를 시작한다.

같은 기법의 선례가 이미 있다. VIA 리셋 경로가 `USB_RESET_DETACH_DELAY_MS`(100 ms)를 두고
리셋한다. 두 상수를 공유하지 않는 것은 전자가 "리셋 전 디태치 유예", 후자가 "부팅 시 디태치
유지"로 목적이 달라 각각 조정 가능해야 하기 때문이다.

**판별 코드를 넣지 않았다.** `DCFG.DAD != 0`으로 점프 진입을 판별할 수 있으나 PCD init이 클럭
인가와 코어 리셋(DAD를 0으로)을 한 호출에서 처리해 중복 초기화가 필요하다. 100 ms를 아끼려고
하드웨어 의존 코드를 넣지 않는다. `usbBegin()`은 부팅 시 1회만 호출되므로 블로킹 지연이 안전하다.

**한계**: 부트로더가 UF2 완료 처리 중 외장 플래시를 소거·복사하는 1.5~5초 동안 USB 스택이
멈추는 것은 펌웨어가 개입할 수 없다. 근본 해결은 부트로더 쪽(점프 대신 디태치 후 시스템 리셋)
이고 그것은 ST-LINK로만 갱신되므로 **이후 출고분에만** 적용된다. 이 펌웨어 보완책은 **기출하
보드용**이며 재열거 실패만 해결한다.

## 7. 주기 작업의 만료 판정을 캐시하지 마라

USB 폴링 루프와 같은 메인 루프에서 RGB 애니메이션·EEPROM 큐·키 스캔이 함께 돈다. 그 주기
작업들은 **16비트 타이머**(`sync_timer_read()`)로 만료를 판정하므로 비교 창이 약 32초다.

> **REFUSED:** rgblight에 이펙트 함수·주기 캐시와 "만료 선행 분기"를 다시 넣기.
> **WHY:** 그 구조에서 `next_timer_due`가 16비트 비교 창 밖으로 밀리거나 Velocikey·비활성
> 상태에서 캐시가 stale이 되면 애니메이션과 렌더가 **조용히 멈춘다.** 실제로 10분 안팎에
> 재현됐고, 캐시와 선행 분기를 완전히 걷어낸 뒤에야 사라졌다.
> **REOPENS:** 만료 판정을 32비트 시각으로 옮기고 wrap을 unsigned 차로 다루는 설계라면 다시
> 검토할 수 있다. 캐시 자체가 금지된 것이 아니라 **16비트 창 위의 캐시**가 금지된 것이다.

같은 규칙이 진단 세션에도 적용된다 — deadline은 unsigned 차와 signed deadline 비교로 다루고,
누계는 `UINT32_MAX`에서 포화시킨다. TIM5 wrap 주기는 4,295초다(`docs/contract_via.md` §6-2).
