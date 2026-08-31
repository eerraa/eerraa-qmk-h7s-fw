======================================================================
Firmware Quick Guide
======================================================================

----------------------------------------------------------------------
펌웨어
----------------------------------------------------------------------

EERRAA H7S 펌웨어입니다. STM32H7S3(내장 HS PHY) 키보드용이며
MAY65, BRICK60, BRICK65, SCULPTUREI, INTIGRITY80 보드가 한 소스를
공유합니다. 버전은 260901R1입니다.

USB는 HS 8/4/2 kHz와 FS 1 kHz를 지원하며 기본은 FS 1 kHz입니다.
그 밖에 TAPDANCE, SOCD, KKUK, DEBOUNCE, TAPPING, MOUSE,
20키 동시 입력, RGB·인디케이터, RGB 절전, EEPROM 초기화,
USB 전달 진단을 제공합니다.


----------------------------------------------------------------------
제공 파일
----------------------------------------------------------------------

■ .uf2
   키보드에 올리는 펌웨어 파일입니다. 빌드가 만든 이름은
   키보드이름-V260901R1.uf2 입니다.

■ VIA JSON
   VIA(usevia.app)가 키보드를 인식하도록 하는 Draft Definition 파일입니다.
   VIA에서 키맵이나 FEATURE/TAPDANCE/SYSTEM 메뉴를 쓰려면 먼저
   로드해야 합니다.

■ via_keycodes.txt
   TAPDANCE 입력창에 넣을 수 있는 keycode 예시 문서입니다.
   VIA는 최신 QMK keycode 이름을 모두 인식하지 못할 수 있으므로,
   KC_A, MO(1), MT(MOD_LSFT,KC_A) 같은 값을 직접 입력할 때 먼저
   이 파일을 확인하세요.


----------------------------------------------------------------------
펌웨어 업데이트
----------------------------------------------------------------------

1. 키보드를 Bootloader 모드로 진입시킵니다.
2. PC에 부트로더 이동식 디스크가 나타나면 .uf2 파일을 복사합니다.
3. 잠시 후 드라이브가 사라지고 업데이트가 완료됩니다.

Bootloader 진입 방법:
- VIA: CONFIGURE -> SYSTEM -> BOOT에서 Jump To BOOT를 켜면 곧바로
  부트로더로 들어갑니다. 켜는 즉시 동작하며, 이 토글은 화면에 항상
  꺼진 것으로 보입니다. 끄면 아무 일도 일어나지 않습니다.
- 리셋 키코드: 키맵에 QK_BOOT이 배치되어 있을 때 사용합니다.

펌웨어를 바꾸면 저장된 키맵과 설정이 공장 초기값으로 돌아갑니다.
업데이트 전에 VIA의 SAVE + LOAD에서 키맵을 백업하십시오.


----------------------------------------------------------------------
VIA 사용
----------------------------------------------------------------------

1. https://usevia.app 접속
2. SETTINGS -> Show Design tab 활성화
3. DESIGN -> Load Draft Definition에서 제공된 VIA JSON 로드
4. CONFIGURE에서 키맵과 기능 설정
5. 설정을 보존하려면 VIA의 Save 기능 사용

레이어는 0~7, 여덟 개입니다.


----------------------------------------------------------------------
주요 기능
----------------------------------------------------------------------

■ TAPDANCE
   한 키에 네 가지 동작을 담습니다.

   - On Tap         짧게 눌림으로 판정됐을 때 입력되는 키코드.
   - On Hold        길게 눌림으로 판정됐을 때 입력되는 키코드.
   - On Double Tap  Term 안에 두 번 탭했을 때 입력되는 키코드.
   - Tap+Hold       한 번 탭한 뒤 이어서 길게 눌렀을 때 입력되는 키코드.
   - Term (ms)      어느 동작인지 판단하기까지 기다리는 시간.

   VIA CONFIGURE -> TAPDANCE에서 TD0~TD7을 설정하고, KEYMAP -> TAPDANCE의
   같은 TD 키를 원하는 위치에 배치하십시오. 입력 예시는
   via_keycodes.txt에 있습니다.

■ SOCD
   반대 방향키가 함께 눌리면 마지막에 누른 키만 남기는 Last Input Wins
   기능입니다. VIA CONFIGURE -> FEATURE -> SOCD에서 KEY BIND 1은 좌/우,
   KEY BIND 2는 상/하 한 쌍을 지정하십시오. 마지막 키를 떼면 반대 키가
   계속 눌린 경우 그 키를 다시 눌러 줍니다.

■ KKUK   ※ 이전 이름: Anti-Ghosting
   여러 키를 계속 누르고 있으면 눌린 키 전체를 주기적으로 다시
   입력합니다. 예를 들어 A, S, D를 계속 누르면 일반 입력의 "asdddd..."
   대신 "asdasdasd..."처럼 입력될 수 있습니다. 문자열 매크로가 아니며,
   매트릭스 고스팅 방지와도 무관해 이름을 바꿨습니다.
   VIA CONFIGURE -> FEATURE -> KKUK에서 켜고 Delay/Repeat 값을
   조정하십시오. SOCD에 지정한 키는 제외됩니다.

■ DEBOUNCE
   VIA CONFIGURE -> FEATURE -> DEBOUNCE에서 스위치 채터링을 조정합니다.
   기본값은 Balanced, 5 ms이며 채터링이 보일 때만 조금씩 늘리십시오.

■ TAPPING
   VIA CONFIGURE -> FEATURE -> TAPPING에서 Mod-Tap과 Layer-Tap의
   짧게/길게 누름 판정 시간을 조정합니다. 기본값은 200 ms입니다.

■ MOUSE (마우스 키)
   VIA CONFIGURE -> FEATURE -> MOUSE에서 조정합니다. 키맵에 마우스 키가
   없으면 아무 영향이 없습니다.

   - Cursor Acceleration   시작 속도에서 최고 속도까지 걸리는 시간.
                           Off면 시작 속도로 고정됩니다.
   - Cursor Start Speed    키를 누른 직후 한 스텝의 이동 거리.
   - Cursor Top Speed      가속이 끝난 뒤 한 스텝의 이동 거리.
   - Cursor Speed          가속이 Off일 때의 한 스텝 이동 거리.
   - Cursor Steps Per Second  초당 이동 스텝 수. 클수록 부드러워지고
                           가속 시간은 그대로입니다.
   - Wheel Rate            초당 스크롤 스텝 수.
   - Wheel Acceleration    누르고 있는 동안 스크롤이 빨라지는 정도.

■ 동시 입력
   별도 설정 없이 최대 20키까지 동시에 입력됩니다. 켜고 끄는 옵션은
   없습니다. 오래된 BIOS나 KVM처럼 기본 키보드만 읽는 환경에서는 앞
   6키가 동작하므로 부팅에는 문제가 없습니다.

■ LIGHTING (조명)
   VIA CONFIGURE -> LIGHTING에서 밝기, 효과, 속도와 색을 조정합니다.
   QMK 기본 이펙트에 더해 Pulse on/off Press (Hold 포함) 4종이 있습니다.
   Velocikey를 켜면 Snake/Knight/Rainbow/Twinkle 등이 입력 속도에 따라
   가속됩니다. INDICATOR는 Caps/Scroll/Num Lock 표시를 지정합니다.
   MAY65는 공장 초기화 뒤 언더글로우가 꺼진 상태로 시작합니다. LIGHTING에서
   켤 수 있습니다.

■ RGB 절전
   키 입력이 없는 시간이 지나면 RGB를 끕니다.
   기본값은 10분이며, 키를 누르면 RGB가 다시 켜집니다.
   VIA CONFIGURE -> FEATURE -> SLEEP에서 1/3/5/10/30/60분 중 고릅니다.
   PC가 절전되거나 연결이 사라지면 RGB도 절전 상태로 들어갑니다.

■ USB POLLING
   VIA CONFIGURE -> SYSTEM -> USB POLLING에서 1 kHz (FS), 2/4/8 kHz (HS)를
   고른 뒤 Apply를 누르면 키보드가 재시작되며 새 속도로 돌아옵니다.
   기본은 1 kHz (FS)입니다. 연결이 불안정하면 1 kHz (FS)로 낮추십시오.

■ VERSION
   VIA CONFIGURE -> SYSTEM -> VERSION에서 현재 펌웨어 날짜와 리비전을
   확인합니다. 이 펌웨어는 26년 09월 01일 R1입니다. 메뉴에서 값을 바꿔도
   펌웨어는 바뀌지 않습니다.

■ USB 전달 진단
   전용 ERA VIA 앱의 DIAGNOSTICS에서 10/30/60초 측정을 시작할 수
   있습니다(기본 30초). usevia.app에는 이 메뉴가 없습니다.
   실제 HID 전달 지연과 USB 이벤트만 보여 주며, 펌웨어가 폴링을 자동으로
   바꾸거나 결과를 저장하지는 않습니다.

■ EEPROM CLEAN
   키맵과 모든 설정을 초기화합니다.

   VIA CONFIGURE -> SYSTEM -> CLEAN에서 10초 안에 확인 토글 세 개를 모두
   켜면 실행됩니다. 저장된 모든 데이터가 초기화되고 키보드가 재시작됩니다.
   10초를 넘기면 켜 둔 토글이 저절로 풀립니다.

   실행 전 SAVE + LOAD에서 키맵을 백업하십시오.


======================================================================
Firmware Quick Guide
======================================================================

----------------------------------------------------------------------
Firmware
----------------------------------------------------------------------

This is EERRAA H7S firmware for STM32H7S3 (on-chip HS PHY) keyboards.
MAY65, BRICK60, BRICK65, SCULPTUREI, and INTIGRITY80 share one source
tree. The version is 260901R1.

USB supports HS 8/4/2 kHz and FS 1 kHz; the default is FS 1 kHz. It also
adds TAPDANCE, SOCD, KKUK, DEBOUNCE, TAPPING, MOUSE, 20-key rollover,
RGB and indicator lighting, RGB sleep, EEPROM reset, and USB delivery
diagnostics.


----------------------------------------------------------------------
Files
----------------------------------------------------------------------

■ .uf2
   Firmware file to flash to the keyboard. The build name looks like
   KeyboardName-V260901R1.uf2.

■ VIA JSON
   Draft Definition file for VIA(usevia.app). Load it first to make VIA show
   KEYMAP, FEATURE, TAPDANCE, and SYSTEM controls.

■ via_keycodes.txt
   Keycode examples for TAPDANCE fields. VIA may not recognize every latest
   QMK keycode name, so check this file before entering values such as KC_A,
   MO(1), or MT(MOD_LSFT,KC_A).


----------------------------------------------------------------------
Firmware Update
----------------------------------------------------------------------

1. Enter Bootloader mode.
2. When the bootloader removable drive appears, copy the .uf2 file onto it.
3. The drive disappears when flashing is complete.

Bootloader options:
- VIA: turn on CONFIGURE -> SYSTEM -> BOOT -> Jump To BOOT and the keyboard
  enters the bootloader at once. It acts the moment you switch it on, and the
  toggle always reads back off. Turning it off does nothing.
- Reset keycode: use QK_BOOT if it is placed in the keymap.

A firmware change returns the keymap and every setting to factory defaults.
Back up the keymap from VIA SAVE + LOAD first.


----------------------------------------------------------------------
Using VIA
----------------------------------------------------------------------

1. Open https://usevia.app.
2. Enable SETTINGS -> Show Design tab.
3. Load the provided VIA JSON in DESIGN -> Load Draft Definition.
4. Configure keymap and features in CONFIGURE.
5. Use VIA Save when you want settings to persist.

There are eight layers, numbered 0 through 7.


----------------------------------------------------------------------
Key Features
----------------------------------------------------------------------

■ TAPDANCE
   Four actions on one key.

   - On Tap         Keycode sent when the press is judged a short one.
   - On Hold        Keycode sent when the press is judged a long one.
   - On Double Tap  Keycode sent when the key is tapped twice inside Term.
   - Tap+Hold       Keycode sent when a tap is followed by holding the key.
   - Term (ms)      How long the keyboard waits before deciding which one.

   Configure TD0~TD7 in VIA CONFIGURE -> TAPDANCE, then place the matching
   TD key from KEYMAP -> TAPDANCE. See via_keycodes.txt for input examples.

■ SOCD
   Last Input Wins: when opposite direction keys are held together, only the
   latest press stays. Assign left/right on KEY BIND 1 and up/down on KEY
   BIND 2 in VIA CONFIGURE -> FEATURE -> SOCD. Releasing the last key puts
   the opposite key back down if it is still held.

■ KKUK   (formerly named Anti-Ghosting)
   Keep several keys held and the whole held set is typed again on a timer.
   Holding A, S and D can produce "asdasdasd..." instead of ordinary
   "asdddd...". It is not a string macro and has nothing to do with matrix
   ghosting, hence the new name. Enable it in VIA CONFIGURE -> FEATURE ->
   KKUK and adjust Delay/Repeat as needed. Enabled SOCD keys are excluded.

■ DEBOUNCE
   Adjust switch chatter filtering in VIA CONFIGURE -> FEATURE -> DEBOUNCE.
   The default is Balanced, 5 ms; increase it only when chatter appears.

■ TAPPING
   Adjust Mod-Tap and Layer-Tap timing in VIA CONFIGURE -> FEATURE ->
   TAPPING. The default is 200 ms.

■ MOUSE
   Set it in VIA CONFIGURE -> FEATURE -> MOUSE. It has no effect unless mouse
   keys are in your keymap.

   - Cursor Acceleration   Time from start speed to top speed. Off holds the
                           start speed.
   - Cursor Start Speed    Distance per step the instant you press the key.
   - Cursor Top Speed      Distance per step once acceleration has finished.
   - Cursor Speed          Distance per step while acceleration is Off.
   - Cursor Steps Per Second  Move steps per second. Higher is smoother and
                           does not change the acceleration time.
   - Wheel Rate            Scroll steps per second.
   - Wheel Acceleration    How much scrolling speeds up while you hold.

■ Simultaneous keys
   Up to 20 keys at once, always on. There is no setting to turn it off.
   On an old BIOS or KVM that only reads a basic keyboard, the first 6 keys
   still work, so booting is fine.

■ LIGHTING
   Adjust brightness, effects, speed, and colour in VIA CONFIGURE -> LIGHTING.
   Stock QMK effects plus four Pulse on/off Press modes (with Hold variants)
   are available. Velocikey accelerates Snake/Knight/Rainbow/Twinkle with
   typing speed. INDICATOR assigns Caps, Scroll, or Num Lock status LEDs.
   MAY65 starts with underglow off after a factory reset; turn it on in
   LIGHTING.

■ RGB sleep
   RGB turns off after a period with no key input.
   The default is 10 minutes, and pressing a key turns RGB back on.
   Choose 1/3/5/10/30/60 minutes in VIA CONFIGURE -> FEATURE -> SLEEP.
   If the PC sleeps or the connection goes away, RGB also enters sleep.

■ USB POLLING
   Choose 1 kHz (FS) or 2/4/8 kHz (HS) in VIA CONFIGURE -> SYSTEM ->
   USB POLLING, then Apply. The keyboard restarts at the new rate. The
   default is 1 kHz (FS). If the link is unstable, drop back to 1 kHz (FS).

■ VERSION
   VIA CONFIGURE -> SYSTEM -> VERSION shows the firmware date and revision.
   This firmware is 26-09-01 R1. Changing the menu does not change the
   firmware.

■ USB delivery diagnostics
   In the dedicated ERA VIA app, open DIAGNOSTICS and run a 10/30/60-second
   session (30 seconds by default). usevia.app does not have this menu.
   It reports HID delivery latency and USB events only. Firmware never
   changes polling by itself or stores the results.

■ EEPROM CLEAN
   Erases the keymap and every setting.

   Switch all three confirm toggles in VIA CONFIGURE -> SYSTEM -> CLEAN
   within ten seconds to run it. Everything stored is erased and the
   keyboard restarts. Miss the ten seconds and the toggles you switched
   clear themselves.

   Back up your keymap from SAVE + LOAD first.
