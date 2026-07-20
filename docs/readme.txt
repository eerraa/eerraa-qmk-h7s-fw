================================================================
펌웨어 안내
================================================================

1. 펌웨어 파일 구성

   키보드이름-V251127R1.uf2
     - 키보드 본체에 올리는 펌웨어 파일입니다.
     - 디바운스 모드/딜레이를 VIA에서 실시간 조정할 수 있으므로 추가 변형 이미지는 제공하지 않습니다. 이 하나의 UF2만 사용하면 됩니다.

   키보드이름-VIA-V251127R1.JSON
     - VIA(usevia.app)에서 키보드를 인식하고, 디바운스/USB 설정/기타 기능을 노출하기 위한 Draft Definition 파일입니다.
     - 키맵을 편집하기 전에 반드시 한 번 로드해야 합니다.

2. 주요 기능 및 설정

2-1. USB POLLING 설정

   - VIA CONFIGURE 탭 → SYSTEM 메뉴에서 원하는 폴링 레이트[1 kHz (FS), 2 kHz (HS), 4 kHz (HS), 8 kHz (HS)]를 선택합니다.
   - 기본 폴링 레이트는 1 kHz (FS)이며, 8 kHz (HS)를 쓰려면 메뉴에서 직접 선택해야 합니다.
   - Apply 버튼을 누르면 즉시 적용됩니다. 일부 허브/케이블 환경에서는 HS에서 불안정할 수 있으므로 문제가 보이면 1 kHz (FS)로 낮추십시오.

2-2. USB 모니터링 기능

   - SYSTEM 메뉴의 "Auto downgrade on USB unstable [BETA]"를 켜면 오류 감지 시 폴링 레이트를 자동으로 한 단계 낮춥니다.
   - 베타 기능이므로 원치 않게 자주 바뀐다면 끄는 것을 권장합니다.

2-3. RGB 이펙트 확장

   - QMK 기본 RGB 이펙트(솔리드/브리딩/레인보우/스네이크/나이트/그라데이션/트윙클 등)에 더해 Pulse on/off Press (Hold 포함) 4종을 추가했습니다.
   - VIA CONFIGURE → LIGHTING에서 Effect를 선택하고, Pulse on/off 계열은 Effect Speed로 속도를 조절합니다.
   - Velocikey 토글을 켜면 Snake/Knight/Rainbow/Twinkle 등이 입력 속도에 따라 가속됩니다.

2-4. SOCD (Kill Switch)

   - VIA CONFIGURE → FEATURE → SOCD에서 설정합니다.
   - KEY BIND 1은 좌/우, KEY BIND 2는 상/하 한 쌍을 Enable 후 KEY 1/KEY 2에 원하는 키를 지정합니다.
   - 두 키가 동시에 눌리면 마지막 입력만 남기고 반대 키를 해제하며, 마지막 키를 떼면 반대 키가 계속 눌린 경우 자동으로 다시 눌러 줍니다.

2-5. Anti-Ghosting

   - VIA CONFIGURE → FEATURE → Anti-Ghosting을 Enable 하면 동시 입력 반복 보정이 켜집니다.
   - First Delay Time: 두 개 이상 기본 키를 누른 상태가 얼마나 지속되면 모드로 진입할지(50~300 ms).
   - Repeat Time: 모드 활성화 후 묶음 전체를 해제/재입력하는 주기(50~200 ms).

2-6. 디바운스 설정 (DEBOUNCE)

   (1) 디바운스란?
       스위치 신호가 짧게 튀는 채터링을 일정 시간 묶어 정리합니다. 펌웨어에 기본 내장되어 있으며 VIA에서 모드/시간을 바꿀 수 있습니다.

   (2) 메뉴 위치
       VIA CONFIGURE → FEATURE(또는 키보드 전용 설정) → DEBOUNCE.

   (3) 모드
       Balanced: 누름/뗌 전후 모두 같은 시간 적용.
       Fast: 변화 순간은 즉시 반영, 이후 일정 시간만 보호(post-only).
       Advanced: 누를 때(post-only)와 뗄 때(pre+post window)를 개별 설정.

   (4) Balanced 시간
       Press & Release - delay before and after (same value)
       예: 9 ms → 누르기/떼기 전후 각각 9 ms를 적용.

   (5) Fast 시간
       Press & Release - delay after change (post-only)
       예: 7 ms → 변화는 즉시 반영, 이후 7 ms 동안 같은 키의 튐 무시.

   (6) Advanced 시간
       Press - delay after press (post-only cooldown): 변화 즉시 반영, 이후 설정 시간만 보호.
       Release - delay before and after release (pre+post window): 떼기 전/후를 한 창으로 묶어 정리.

   (7) 권장 조합
       - 일반 용도: Balanced, 5~10 ms
       - 빠른 반응 우선: Fast, 3~10 ms (채터링 시 값 증가)
       - Advanced는 채터링 패턴을 알고 있을 때만 사용 권장.

2-7. 탭핑 설정 (TAPPING TERM, 실시간 변경)

   - VIA GLOBAL SETTINGS의 TAPPING 섹션에서 조정합니다.
   - 항목: Global Tapping Term(기본 200 ms), Permissive Hold, Hold on Other Key Press, Retro Tapping.
   - 값을 바꾼 뒤 SAVE(또는 VIA 저장)을 누르면 EEPROM에 보존됩니다.

2-8. 탭댄스(Tap Dance) 설정 & 키코드 사용법

   - 기본 개념: 슬롯 1~8(TD0~TD7)에 “어떤 키를 몇 번/얼마나 눌렀을 때 무엇을 보낼지”를 기록한 뒤, 키맵에 TD0~TD7만 배치합니다.
   - 지정 가능한 키코드는 https://docs.qmk.fm/keycodes 참고.
   - 설정 위치: VIA CONFIGURE → TAPDANCE 메뉴.
     · On Tap / On Hold / On Double Tap / Tap+Hold를 슬롯별로 지정. 비워 두면 해당 동작은 실행되지 않으며, 비어 있으면 Tap을 대신 사용하는 폴백이 적용됩니다.
     · Term은 슬롯별 탭/홀드 판정 시간(100~500 ms, 20 ms 스텝)으로 글로벌 TAPPING_TERM과 독립적입니다. 기본값 200 ms.
   - 적용/저장: 변경 시 즉시 RAM에 반영되며, Save를 누르면 EEPROM에 저장되어 재부팅 후에도 유지됩니다.
   - 키맵 배치: VIA KEYMAP 탭 하단 CUSTOM 섹션의 “Tap Dance” 그룹에서 TD0~TD7을 골라 배치합니다. UI TD0~TD7은 내부 슬롯 0~7과 1:1로 연결됩니다.
   - 동작 예시 (Vial과 동일한 로직):
     · Hold가 비어 있으면 Tap을 대신 눌러 유지합니다.
     · Tap+Hold가 비어 있으면 Tap을 한 번 보내고 Hold(없으면 Tap)를 길게 누릅니다.
     · Double Tap이 비어 있으면 Tap을 한 번 보내고 Tap을 길게 누릅니다.
     · 3번 연타하면 Tap×3, 4번 이상은 추가 Tap을 보냅니다. DOUBLE_SINGLE_TAP도 동일하게 처리합니다.
   - 빠른 시작 예시:
     1) TAPDANCE에서 TD0 슬롯에 On Tap=“KC_A”, On Hold=“MO(1)”, Term=200 ms로 설정.
     2) CONFIGURE 화면에서 원하는 위치에 TD0를 배치.
     3) 짧게 누르면 A, 길게 누르면 레이어1. 더블탭 동작이 필요하면 On Double Tap을 추가합니다.

3. VIA 사용 절차

   1) https://usevia.app 접속.
   2) SETTINGS 탭 → "Show Design tab" 토글 활성화.
   3) DESIGN 탭 → "Load Draft Definition"에서 제공된 VIA JSON 로드.
   4) CONFIGURE 탭으로 돌아와 키맵/레이어/매크로를 설정.
   5) SYSTEM에서 USB POLLING 및 모니터링, LIGHTING에서 RGB, DEBOUNCE에서 디바운스 모드/시간을 조정합니다.

4. 펌웨어 플래싱 절차

   1) 키보드를 부트로더 모드로 전환:
      - Bootmagic 리셋: ESC 키(매트릭스 0,0)를 누른 채 USB를 연결.
      - VIA 리셋: VIA CONFIGURE → SYSTEM의 "Jump To BOOT" 버튼을 클릭.
   2) PC에 표시된 이동식 디스크에 제공된 UF2 파일을 복사.
   3) 디스크가 자동으로 사라지면 플래싱 완료 후 키보드가 재시작합니다.

5. EEPROM 초기화 및 키맵 백업 (AUTO_FACTORY_RESET)

   AUTO_FACTORY_RESET은 EEPROM에 저장된 펌웨어 버전과 실행 중인 버전이 다를 때 자동 초기화를 수행합니다.

   - 최초 부팅 시 한 번 EEPROM이 자동 초기화됩니다.
   - 키맵/레이어/매크로 설정이 공장 초기값으로 돌아가므로, 업데이트 전 키맵을 백업해 두는 것을 권장합니다.

   백업 절차:
   1) VIA CONFIGURE → SAVE + LOAD에서 Save로 현재 키맵을 파일로 저장(예: keyboard.layout.json).
   2) 새 펌웨어 플래싱 후 자동 초기화가 실행되면,
   3) 같은 메뉴에서 Load로 앞서 저장한 키맵을 불러와 복원합니다.
   SYSTEM → CLEAN의 EEPROM 정리 기능은 수동 초기화 용도로 남아 있습니다.

================================================================
Firmware Guide
================================================================

1. Firmware package contents

   KeyboardName-V251127R1.uf2
     - Flash this UF2 onto the keyboard.
     - Debounce mode/delay are runtime-configurable in VIA, so no alternate images are shipped—use this single UF2.

   KeyboardName-VIA-V251127R1.JSON
     - VIA Draft Definition so the keyboard is recognized and USB/debounce/feature controls appear.
     - Load it once before editing the keymap.

2. Key features and settings

2-1. USB POLLING

   - VIA CONFIGURE → SYSTEM: choose 1 kHz (FS), 2/4/8 kHz (HS).
   - Default is 1 kHz (FS); select 8 kHz (HS) manually if needed.
   - Apply takes effect immediately; if HS is unstable on your hub/cable, fall back to 1 kHz (FS).

2-2. USB monitoring

   - SYSTEM → "Auto downgrade on USB unstable [BETA]" lowers polling by one step when errors are detected.
   - If it changes too often, turn it off.

2-3. RGB effects

   - Includes all stock QMK effects plus four extra Pulse on/off Press (with Hold variants).
   - VIA CONFIGURE → LIGHTING: pick Effect; adjust Effect Speed for Pulse modes.
   - Velocikey toggle accelerates effects like Snake/Knight/Rainbow/Twinkle based on typing speed.

2-4. SOCD (Kill Switch)

   - VIA CONFIGURE → FEATURE → SOCD.
   - Enable KEY BIND 1 (L/R) and KEY BIND 2 (U/D), assign KEY 1/KEY 2 (e.g., A/D or arrows).
   - When both in a pair are pressed, the latest press wins; releasing the last key re-applies the opposite key if it was held.

2-5. Anti-Ghosting

   - VIA CONFIGURE → FEATURE → Anti-Ghosting → Enable.
   - First Delay Time: how long multiple basic keys must be held before entering the mode (50–300 ms).
   - Repeat Time: interval to release/repress the whole bundle (50–200 ms).

2-6. Debounce settings (DEBOUNCE)

   (1) What is debounce?
       It groups short switch bounces (chatter) into clean events. Built in, and adjustable in VIA.

   (2) Menu location
       VIA CONFIGURE → FEATURE (or keyboard-specific panel) → DEBOUNCE.

   (3) Modes
       Balanced: same delay before/after press and release.
       Fast: applies only after the change (post-only).
       Advanced: separate press post-only and release pre+post window.

   (4) Balanced timing
       Press & Release - delay before and after (same value)
       Example: 9 ms → applies 9 ms before/after both press and release.

   (5) Fast timing
       Press & Release - delay after change (post-only)
       Example: 7 ms → event is instant; further changes on that key are ignored for 7 ms.

   (6) Advanced timing
       Press - delay after press (post-only cooldown): instant event, protects for the set time after.
       Release - delay before and after release (pre+post window): merges bounces in the window around release.

   (7) Recommended combinations
       - General use: Balanced, 5–10 ms
       - Faster response: Fast, 3–10 ms (raise if chatter appears)
       - Advanced is for users who understand their switch behavior.

2-7. Tapping settings (TAPPING TERM, runtime adjustable)

   - VIA GLOBAL SETTINGS → TAPPING.
   - Labels: Global Tapping Term (default 200 ms), Permissive Hold, Hold on Other Key Press, Retro Tapping.
   - Click SAVE in VIA to store to EEPROM.

2-8. Tap Dance configuration & keycode usage

   - Concept: assign “what to send” to slots 1–8 (TD0–TD7), then place TD0–TD7 on the keymap.
   - KC keycodes are listed at https://docs.qmk.fm/keycodes.
   - Where to set: VIA CONFIGURE → TAPDANCE.
     · On Tap / On Hold / On Double Tap / Tap+Hold per slot. Empty fields skip that action and use Tap as a fallback where applicable.
     · Term is a per-slot tap/hold window (100–500 ms, 20 ms step), independent of the global TAPPING_TERM. Default 200 ms.
   - Apply/save: changes take effect immediately in RAM; click Save to store to EEPROM so they persist after reboot.
   - Keymap placement: in VIA KEYMAP, open the CUSTOM section and choose TD0–TD7 under “Tap Dance,” then place them. UI TD0–TD7 map 1:1 to internal slots 0–7.
   - Behavior (matches Vial Tap Dance):
     · If Hold is empty, Tap is held instead.
     · If Tap+Hold is empty, Tap is sent once, then Hold (or Tap) is held.
     · If Double Tap is empty, Tap is sent once, then Tap is held.
     · Triple tap sends Tap×3; more than three sends additional Tap. DOUBLE_SINGLE_TAP behaves the same.
   - Quick start example:
     1) In TAPDANCE, set TD0: On Tap = “KC_A”, On Hold = “MO(1)”, Term = 200 ms.
     2) Place TD0 on the desired key position.
     3) Short press sends A; long press switches to layer 1. Add On Double Tap if needed.

3. How to use VIA

   1) Go to https://usevia.app.
   2) Open the SETTINGS tab and enable "Show Design tab."
   3) Open DESIGN, click "Load Draft Definition," and load the provided VIA JSON.
   4) Return to CONFIGURE to edit keymaps, layers, and macros.
   5) Use SYSTEM for USB POLLING/monitoring, LIGHTING for RGB, and DEBOUNCE for debounce modes/timings.

4. Firmware flashing procedure

   1) Enter bootloader mode:
      - Bootmagic reset: hold ESC (matrix 0,0) while plugging in USB.
      - VIA reset: VIA CONFIGURE → SYSTEM → "Jump To BOOT."
   2) When the removable drive appears, copy the provided UF2 onto it.
   3) When the drive disappears, flashing is done and the keyboard restarts with the new firmware.

5. EEPROM reset and keymap backup (AUTO_FACTORY_RESET)

   AUTO_FACTORY_RESET compares the firmware version stored in EEPROM with the running version and auto-resets when they differ.

   - On first boot, EEPROM is reset once.
   - Because keymap/layer/macro settings return to factory defaults, back up your layout before updating.

   Backup steps:
   1) In VIA CONFIGURE → SAVE + LOAD, click Save to export the current layout (e.g., keyboard.layout.json).
   2) Flash the new firmware; automatic reset will run.
   3) Load the saved layout to restore it. The SYSTEM → CLEAN EEPROM function remains for manual reset if needed.
