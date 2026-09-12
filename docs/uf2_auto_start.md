# UF2 업로드 후 자동 시작 실패 — 원인과 해결 (V260824R1)

대상: `ERA-QMK-H7S-BOOT` (STM32H7S3, 내장 HS PHY, TinyUSB MSC)
관련 커밋: UF2 완료 처리를 점프에서 시스템 리셋으로 전환
관련 저장소: [eerraa-qmk-h7s-fw](https://github.com/eerraa/eerraa-qmk-h7s-fw)

---

## 1. 증상

UF2 부트로더에 펌웨어를 드래그&드롭으로 업로드한 뒤,

- **업로드는 항상 정상**적으로 완료된다 (외장 플래시 내용은 올바름).
- 그런데 **부트로더에서 빠져나와 펌웨어가 자동으로 시작될 때도 있고, 그냥 무반응으로 남을 때도 있다.**
- 무반응 상태에서 **USB 케이블을 뽑았다 다시 꽂으면 업로드한 펌웨어가 정상 부팅**된다.
- 재현 조건을 특정할 수 없다 (간헐적).

---

## 2. 근본 원인

### 2-1. 점프는 USB를 켠 채로 클럭만 끊고 펌웨어를 시작한다 (주원인)

`bootJumpFirm()`은 시스템 리셋이 아니라 **직접 점프**로 펌웨어를 시작한다.
그 직전에 호출되는 `bspDeInit()` → `usbDeInit()`이 하는 일은 클럭 게이팅뿐이었다.

```c
void usbDeInit(void)
{
  __HAL_RCC_USB_OTG_HS_CLK_DISABLE();   // 클럭만 차단
  __HAL_RCC_USBPHYC_CLK_DISABLE();
  HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
}
```

RCC 클럭 게이팅은 주변장치를 리셋하지 않는다. OTG 코어는 무장(`GAHBCFG.GINT`)·연결
(`DCTL.SDIS` = 0) 상태 그대로, PHY는 HS 세션 도중에 멈춘 채 펌웨어에 넘어간다.
펌웨어의 `HAL_PCD_Init()`은 클럭을 다시 켜고 코어 소프트리셋을 거는데, 그렇게
넘겨받은 코어는 리셋을 완료하지 못한다. HAL은 `HAL_USB_TIMEOUT`만큼 기다린 뒤
(약 10 s) 실패를 돌려주고, 펌웨어는 `Error_Handler()`에서 멈춘다.
→ **무반응.**

BRICK60 실기(2026-09-12): 진단 빌드가 부팅이 USB 초기화까지 온 것, `HAL_PCD_Start()`에
이르지 못한 것, 약 10 s 뒤의 정지를 RGB로 보였다. 펌웨어가 클럭을 켠 직후 OTG 코어와
USBPHYC를 RCC로 리셋하고 시작하도록 한 빌드는 5회 연속 정상 열거됐다.

이전 판은 원인을 "호스트가 디태치를 인지하지 못한다"로 적었다. 그 가설로 만든 펌웨어
보완책(부팅 시 `SDIS` 100 ms 유지, 펌웨어 V260824R2)은 효과가 없었다. 실패 지점이
그 유지보다 앞에 있기 때문이다. 그 서술은 이 절로 대체한다.

### 2-2. 수 초짜리 플래시 작업이 USB 콜백 안에서 실행됐다 (별개 결함)

`uf2_flash_complete()`는 `tud_msc_write10_complete_cb()`, 즉 `tud_task()` 안에서
호출되는데 거기서 `bootUpdateFirm()`을 직접 수행했다.

- `qspiErase()`는 **64KB 블록 단위**(`W25Q16JV_SECTOR_SIZE = 0x10000`)로 소거한다.
  약 328KB 펌웨어면 6블록, W25Q16JV 64KB 블록 소거는 typ 150ms / max 2000ms.
- 여기에 328KB 페이지 프로그램과 CRC 검증까지 더하면 **1.5~5초** 동안
  `tud_task()`가 완전히 멈춘다. 그동안 모든 제어/벌크 전송이 무응답이 된다.

이 정지는 호스트가 MSC 장치에 타임아웃을 겪게 할 뿐, 자동 시작 실패의 원인은 아니다.
간헐로 보였던 이유는 확정되지 않았고, 확인된 실기에서는 매번 실패했다.

### 2-3. 비대칭이 가리키는 것

| 경로 | 인계 시 OTG/PHY 상태 | 결과 |
|---|---|---|
| 전원 인가(콜드부트) → 펌웨어 | `bootUp()`이 `usbInit()`보다 **먼저** 실행 → USB를 켠 적 없음 | 항상 정상 |
| 케이블 재삽입 | 전원 사이클 = 리셋 상태 | 항상 정상 |
| 펌웨어 → 부트로더 | 펌웨어가 `HAL_NVIC_SystemReset()` 사용 | 항상 정상 |
| **부트로더 → 펌웨어 (UF2 직후)** | **켠 채 클럭만 끊고 점프** | **실패** |

정상 동작하는 세 경로는 전부 OTG 코어와 PHY가 리셋 상태에서 시작한다. 실패하는
경로만 살아 있던 세션을 물려받는다.

---

## 3. 해결

**UF2 완료·이젝트 시 점프 대신 시스템 리셋으로 펌웨어를 시작한다.**
`bootUp()`에 이미 존재하던 `MODE_BIT_UPDATE` 경로를 재사용한다. 시스템 리셋이 OTG
코어와 USBPHYC를 리셋 상태로 되돌리므로 펌웨어는 콜드부트와 같은 주변장치를 받는다.

```
UF2 드롭 → 마지막 블록 → tag flush
        → MODE_BIT_UPDATE 예약 (RTC 백업 레지스터 DR3), CSW 정상 응답
        → +500ms : tud_disconnect()   ← DCTL.SDIS, 실제 전기적 디태치
        → +100ms : resetToReset()
        → bootUp() : USB 켜기 전에 bootUpdateFirm() → bootJumpFirm()
        → 펌웨어 시작 (전원 재인가와 완전히 동일한 경로)
```

### 변경 파일

| 파일 | 내용 |
|---|---|
| `src/ap/modules/uf2/uf2.c` | `uf2RequestFirmStart()` 신설. `uf2_flash_complete()`는 플래시 복사 대신 `MODE_BIT_UPDATE` 예약만 수행. `uf2Update()`를 3단계(대기 → 디태치 → 리셋) 상태머신으로 변경 |
| `src/ap/modules/uf2/uf2.h` | `uf2RequestFirmStart()` 선언 |
| `src/ap/modules/uf2/uf2_msc.c` | 이젝트 경로도 `bootJumpFirm()` → `uf2RequestFirmStart(false)`로 통일. CSW를 정상 응답한 뒤 리셋한다 |
| `src/hw/driver/usb/usb.c` | `usbDeInit()`이 클럭 차단 전에 `tud_disconnect()` + 100ms 대기를 수행. **콜드부트에서는 USB 클럭이 미인가 상태이므로 `is_init` 가드 필수** (무가드 시 버스폴트) |
| `src/hw/hw_def.h` | `_DEF_FIRMWATRE_VERSION` → `V260824R1` |

### 설계상 주의점

- 대기 구간을 블로킹 `delay()`가 아닌 상태머신으로 둬서, 그동안 `tud_task()`가
  계속 돌아 마지막 CSW가 정상 전송된다.
- `MODE_BIT_UPDATE`는 RTC 백업 레지스터(`HW_RTC_BOOT_MODE` = `RTC_BKP_DR3`)에
  기록되므로 시스템 리셋을 넘어 보존된다. `resetInit()`이 읽은 뒤 0으로 지우므로
  리셋 루프가 발생하지 않는다.
- `usbDeInit()`의 `tud_disconnect()`는 반드시 `is_init` 가드 안에 둔다.
  콜드부트 경로(`bootUp()` → `bootJumpFirm()` → `bspDeInit()`)에서는
  `usbInit()`이 아직 실행되지 않아 USB 클럭이 꺼져 있고, 그 상태에서
  `DCTL` 레지스터에 접근하면 버스폴트가 발생한다.

### 부수적으로 함께 해결된 문제

- **한 세션 내 2회 업로드 시 CRC 손상**: `_flash_crc` / `_flash_len` / `_wr_state`가
  세션 간에 초기화되지 않아 두 번째 업로드의 태그 CRC가 깨졌다. 이제 매 업로드가
  리셋으로 끝나므로 statics가 항상 초기 상태다.
- **업로드 직후 케이블 분리 시 업데이트 유실**: `MODE_BIT_UPDATE`가 백업
  레지스터에 남아 다음 전원 인가 시 업데이트가 완료된다.

### 회귀 위험

업데이트가 실패하면 리셋 후 `bootUp()`이 `bootJumpFirm()` 실패 →
`bootVerifyUpdate()` 재시도 → 그래도 실패 시 Boot Mode로 진입해 UF2 드라이브가
다시 뜬다. 재업로드 가능 상태가 유지되므로 기존 대비 후퇴는 없다
(리셋 1회가 끼어들 뿐).

---

## 4. 검증 방법

1. **UART 로그** — `HW_LOG_CH = HW_UART_CH_SWD`(UART1, 115200).
   업로드 후 다음 순서가 찍히면 정상이다.
   ```
   [  ] uf2 update reserved
   (리셋)
   [  ] bootUpdateFirm()
   [OK]
   [  ] bootJumpFirm()
        addr : 0x...
   [ Firmware Begin... ]
   ```
2. **호스트** — 업로드 직후 UF2 드라이브가 사라지고 키보드가 새로 열거되는지.
3. **반복** — 연속 10회 이상 업로드해 자동 시작 100% 재현 확인.
4. **회귀** — 콜드부트, ESC 홀드 부트로더 진입, 펌웨어에서 부트로더 진입(VIA),
   드라이브 수동 이젝트 4개 경로 확인.

---

## 5. 기존 출하 보드 대응

UF2는 외장 플래시의 FIRM / UPDATE 영역만 기록하므로 **UF2로는 부트로더를 갱신할 수 없다**
(부트로더는 내부 플래시 `0x08000000`, 갱신에 ST-LINK 필요).
따라서 이 수정은 **이후 출고분에만** 적용된다.

이미 출하된 보드는 펌웨어 쪽에서 해결한다. 점프 이후 USB 주변장치의 소유자는 펌웨어이므로
펌웨어가 클럭을 켠 직후 `GAHBCFG.GINT`로 물려받은 코어를 알아보고 OTG 코어와 USBPHYC를
RCC로 리셋한 뒤 100 ms 유예를 두고 평소대로 초기화한다(펌웨어 V260912R1,
`src/hw/driver/usb/usbd_conf.c`의 `HAL_PCD_MspInit()`). 이전 보완책 V260824R2의
부팅 시 `SDIS` 100 ms 유지는 실패 지점 뒤에 있어 효과가 없었고 제거됐다.

| | 자동 시작 실패 (2-1) | 업데이트 중 USB 정지 (2-2) |
|---|---|---|
| 부트로더 수정 (본 문서) | O | O |
| 펌웨어 V260912R1 | O | X |

두 수정은 양립한다. 이 부트로더는 리셋 상태의 코어를 넘기므로 펌웨어의 분기는 아무 일도
하지 않는다.
