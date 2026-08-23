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

### 2-1. 점프 직전에 USB를 전기적으로 분리하지 않았다 (주원인)

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

RCC 클럭 게이팅은 주변장치 레지스터를 리셋하지 않는다. 따라서 `DCTL.SDIS`는 0으로
남고 **PHY의 D+ 풀업 / HS 터미네이션이 그대로 유지된다.** 호스트 입장에서는
장치가 분리된 것이 아니라 *붙어 있는데 응답만 안 하는* 상태다.
`bspDeInit()`의 `delay(100)`은 "붙어 있는 채로 100ms 침묵"일 뿐 디태치가 아니었다.

이어서 펌웨어가 올라오면 ST HAL이 `HAL_PCD_Init()` 끝에서 `USB_DevDisconnect()`
(SDIS=1)를 세우고 곧바로 `HAL_PCD_Start()`가 `USB_DevConnect()`(SDIS=0)로 되돌린다.
그 사이는 **수백 마이크로초**로, 호스트/허브의 디바운스 시간(100ms)에 한참 못 미친다.

결과적으로 호스트는 디태치를 인지하지 못한 채 예전 MSC 장치를 계속 상대하고,
OTG 코어 소프트리셋으로 장치 주소가 0으로 돌아간 펌웨어는 영원히 열거되지 않는다.
→ **무반응.**

### 2-2. 수 초짜리 플래시 작업이 USB 콜백 안에서 실행됐다 (증폭 요인)

`uf2_flash_complete()`는 `tud_msc_write10_complete_cb()`, 즉 `tud_task()` 안에서
호출되는데 거기서 `bootUpdateFirm()`을 직접 수행했다.

- `qspiErase()`는 **64KB 블록 단위**(`W25Q16JV_SECTOR_SIZE = 0x10000`)로 소거한다.
  약 328KB 펌웨어면 6블록, W25Q16JV 64KB 블록 소거는 typ 150ms / max 2000ms.
- 여기에 328KB 페이지 프로그램과 CRC 검증까지 더하면 **1.5~5초** 동안
  `tud_task()`가 완전히 멈춘다. 그동안 모든 제어/벌크 전송이 무응답이 된다.

### 2-3. 왜 간헐적이었나

두 원인이 맞물려 결과가 호스트 상태에 좌우됐다.

- 2-2의 정지 구간에서 **호스트가 SCSI 타임아웃으로 MSC 장치 스택을 먼저 내려버리면**
  → 이후 펌웨어의 재접속이 "새 장치 부착"으로 보여 **성공**.
- 호스트가 참고 기다렸으면 → 옛 스택이 살아있는 채로 펌웨어가 올라와 **실패**.
- 2-1의 수백 µs SDIS 글리치를 루트허브 포트가 래치했는지 여부도 매번 달랐다.

소거 시간은 플래시 개체·이미지 크기·호스트 컨트롤러에 따라 편차가 커서
사실상 코인플립이 됐다.

### 2-4. 비대칭이 결정적 증거

| 경로 | USB 상태 | 결과 |
|---|---|---|
| 전원 인가(콜드부트) → 펌웨어 | `bootUp()`이 `usbInit()`보다 **먼저** 실행 → USB를 켠 적 없음 | 항상 정상 |
| 케이블 재삽입 | 전원 사이클 = PHY 리셋 | 항상 정상 |
| 펌웨어 → 부트로더 | 펌웨어가 `HAL_NVIC_SystemReset()` 사용 (+100ms 디태치) | 항상 정상 |
| **부트로더 → 펌웨어 (UF2 직후)** | **점프. 디태치 없음.** | **간헐 실패** |

정상 동작하는 세 경로는 전부 "USB가 붙은 적이 없거나, 하드웨어 리셋으로 PHY가
초기화된" 경우다. 실패하는 경로만 유일하게 살아있는 USB 세션을 물려받았다.

---

## 3. 해결

**UF2 완료·이젝트 시 점프 대신 시스템 리셋으로 펌웨어를 시작한다.**
`bootUp()`에 이미 존재하던 `MODE_BIT_UPDATE` 경로를 재사용한다.

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

이미 출하된 보드에는 펌웨어 측 보완책을 배포한다. 펌웨어가 자신의 USB 초기화
과정에서 `HAL_PCD_Init()`이 세워둔 SDIS를 100ms 유지하면, 부트로더를 고치지 않고도
호스트가 디태치를 인지하게 만들 수 있다. USB 주변장치는 부트로더와 펌웨어가
공유하는 동일 하드웨어이고, 점프 이후에는 펌웨어가 그 레지스터의 소유자이기 때문이다.

단, 보완책은 **2-1(재열거 실패)만 해결하고 2-2(업데이트 중 USB 정지)는 해결하지 못한다.**
근본 해결은 본 부트로더 수정이다.

| | 2-1 재열거 실패 | 2-2 업데이트 중 USB 정지 |
|---|---|---|
| 부트로더 수정 (본 문서) | O | O |
| 펌웨어 보완책 | O | X |
