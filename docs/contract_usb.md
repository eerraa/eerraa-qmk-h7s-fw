# USB 호스트 계약

Genre: contract
Canonical for: 호스트에 무엇을 어떤 모양으로 보고하는가 — 인터페이스·엔드포인트 구성, 20키
리포트와 부트 규격 편차, NKRO를 넣지 않는 이유, 폴링 모드 소유권, **폐기한 자동 복구와
되살리면 안 되는 이유**, 부트로더 인계 디태치, 메인 루프 주기 작업의 타이머 규칙

## 1. 인터페이스 구성

| 인터페이스 | 엔드포인트 | 크기 | subclass / protocol | 내용 |
| --- | --- | --- | --- | --- |
| 0 | `HID_EPIN_ADDR` `0x81` IN | 64 B | 1 (BOOT) / 1 (Keyboard) | 키보드. **20키 배열, IN 22 B** |
| 1 | `HID_VIA_EP_IN` `0x84` IN / `HID_VIA_EP_OUT` `0x04` OUT | 32 B | 0 / 0 | VIA raw HID |
| 2 | `HID_EXK_EP_IN` `0x85` IN | 8 B | 1 / 0 | SYSTEM(ID 3, 3 B) / CONSUMER(ID 4, 3 B) / MOUSE(ID 2, 6 B) |

`bInterval`은 세 인터페이스 모두 `usbBootModeGetHsInterval()`을 따르므로 선택한 폴링 레이트가
그대로 적용된다. 마우스 리포트는 6 B라 **기존 8 B 엔드포인트에 그대로 들어간다** — 엔드포인트
크기도 FIFO 배분도 바뀌지 않았다. 디스크립터가 선언한 크기와 QMK 구조체 크기는
`src/hw/driver/usb/usb_hid/usbd_hid.c`의 `_Static_assert` 셋이 컴파일 시점에 고정한다.
Composite(CDC+HID) 모드의 `src/hw/driver/usb/usb_cmp/usbd_cmp.c`는 같은 상수
(`HID_EXK_EP_SIZE`, `HID_EXK_REPORT_DESC_SIZE`)에서 디스크립터를 조립하므로 두 모드가 자동으로
일치한다.

## 2. 동시 입력은 20키다 — 6KRO가 아니다

`HW_KEYS_PRESS_MAX`가 20이라(5개 보드 모두 override 없음) 인터페이스 0의 IN 리포트는
mods(1) + reserved(1) + keys[20] = **22 B**다. 빌드 이미지에서 추출한 디스크립터의
`95 14 75 08`(REPORT_COUNT = 0x14 = 20)이 이를 확증한다. 리포트 디스크립터를 파싱하는 호스트는
20키를 그대로 인식하고, 21번째 키는 `add_key_byte()`가 빈 슬롯을 찾지 못해 **조용히 무시**한다
(ErrorRollOver를 보내지 않는다).

## 3. 부트 규격 편차는 알려진 상태이고 유지한다

| | 이 펌웨어 (인터페이스 0) | HID Boot Keyboard 규격 |
| --- | --- | --- |
| IN 리포트 | 22 B — mods(1) + reserved(1) + keys[20] | 8 B — mods(1) + reserved(1) + keys[6] |
| OUT 리포트 | 1 B (LED 5비트 + 패딩 3) | 동일 |
| 인터페이스 선언 | subclass 1(BOOT), protocol 1(Keyboard) | 동일 |

BIOS에서 동작해 온 이유는 **22 B의 앞 8 B가 부트 포맷과 바이트 단위로 같기** 때문이다. 8 B만
읽는 호스트는 부트 키보드가 보낸 것과 같은 데이터를 받아 앞 6키가 정확히 동작한다.

편차는 둘이다. ① `USBD_HID_REQ_SET_PROTOCOL`(boot)을 받아도 리포트를 8 B로 줄이지 않는다 —
전송 길이는 항상 `HID_KEYBOARD_REPORT_SIZE`다. ② 인터페이스 0의 GET_REPORT에 핸들러가 없어
STALL된다. **남는 위험은 내용이 아니라 전송 크기다** — 호스트가 8 B TD로 IN을 걸면 22 B 응답이
babble이 되어 엔드포인트가 halt될 수 있다. 다만 EFI HID 드라이버를 포함한 대부분의 호스트
스택은 `wMaxPacketSize`(64)로 버퍼를 잡으므로 통과한다.

> **REFUSED:** boot protocol에서 리포트를 8 B로 절단하도록 고치기.
> **WHY:** 재시도 큐와 SOF 드레인이 얽힌 8 kHz 송신 경로를 프로토콜 상태에 따라 가변 길이로
> 바꿔야 하는데, 출시 이미지가 이 동작으로 현장에서 쓰이고 실패 보고가 없다.
> **REOPENS:** 특정 BIOS/KVM에서 실패가 보고되면 ①부터 검토한다.

따라서 **"BIOS 호환"은 규격상 보장이 아니라 앞 8 B 일치에 기댄 실무적 호환이다.** 이 구분을
문서와 릴리스 노트에서 흐리지 않는다.

> **REFUSED:** NKRO(240키, 별도 리포트 ID) 도입.
> **WHY:** 기본 상태가 이미 전환 없이 20키라 기능적 실익이 없는 반면, NKRO 리포트 32 B를
> 실으려면 EXK 엔드포인트를 8 B → 32 B로 키워야 해서 켜지 않는 사용자까지 재열거와 재시도 큐
> RAM 1,152 B → 4,224 B를 부담한다. 6KRO↔NKRO 토글은 "부트 호환이냐 동시입력이냐"의 교환을
> 사용자에게 떠넘기는 장치인데 지금 구성에는 그 교환 자체가 없다.
> **REOPENS:** 20키로 부족하다는 실사용 근거. 재도입하면 EXK에 리포트 ID를 추가하고
> `keyboard_protocol`을 SET_PROTOCOL `wIndex==0`에 연결하는 방식이 그대로 유효하다.

NKRO 토글은 이 펌웨어에서 **제공된 적이 없다.** `NKRO_ENABLE`이 정의된 적이 없어
`keymap_config.nkro` 비트는 아무 효과가 없었다. 즉 위 결정은 현상 유지이지 회귀가 아니다.

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

## 5. 폴링 모드는 사용자 소유다

| enum | 실제 모드 | HS `bInterval` | VIA dropdown |
| --- | --- | --- | --- |
| `USB_BOOT_MODE_FS_1K` | FS 1 kHz | (FS endpoint interval 1) | 3 |
| `USB_BOOT_MODE_HS_2K` | HS 2 kHz | 3 | 2 |
| `USB_BOOT_MODE_HS_4K` | HS 4 kHz | 2 | 1 |
| `USB_BOOT_MODE_HS_8K` | HS 8 kHz | 1 | 0 |

기본은 **FS 1 kHz**다. 채널 13 value 1 SET은 pending 값만 바꾸고, value 2 SET=1이
`usbBootModeScheduleApply()`를 걸어 메인 루프의 `usbProcessBootModeApply()`가 EEPROM에 기록한 뒤
VIA 응답을 위한 최소 40 ms와 USB 디태치 100 ms를 두고 MCU를 리셋한다. 같은 모드를 골라도
재열거 의도는 유지한다. CLI `boot info` / `boot set {1k|2k|4k|8k}`도 같은 경로를 쓴다.

**이 큐와 리셋을 호출하는 것은 사용자 경로뿐이다.** 진단 서브시스템은 이 API를 부르지 않는다.
`usbProcess()`에 사용자 apply/reset 외의 자동 단계가 있으면 §4 위반이다.

모드를 바꾸면 연결이 끊기므로 진행 중인 진단 세션은 앱이 중단 처리하고, 재연결 후 새 세션으로
비교한다.

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
