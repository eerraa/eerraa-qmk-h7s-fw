================================================================
BRICK60 펌웨어 안내
================================================================

이 문서는 STM32H7S와 8,000 Hz USB 폴링을 사용하는 BRICK60 사용자를 위한 공식 펌웨어 안내입니다.

1. 펌웨어 파일 구성

   BRICK60-V251125R2.uf2
     - BRICK60 본체에 올리는 펌웨어 파일입니다.
     - 이 버전부터 디바운스 모드와 딜레이를 VIA에서 실시간으로 조정할 수 있으므로, 과거처럼 DEF/EAG 두 종류의 이미지를 따로 배포하지 않습니다. 이 하나의 UF2 파일만 사용하면 됩니다.

   BRICK60-V251125R2.JSON
     - VIA(usevia.app)에서 BRICK60을 인식하고, 디바운스/USB 설정/기타 기능을 노출하기 위한 Draft Definition 파일입니다.
     - 키맵을 편집하기 전에 반드시 한 번 로드해야 합니다.

2. 주요 기능 및 설정

2-1. USB POLLING 설정

   - VIA CONFIGURE 탭에서 왼쪽 메뉴의 SYSTEM을 선택합니다.
   - USB POLLING 항목에서 원하는 폴링 레이트[1 kHz (FS), 2 kHz (HS), 4 kHz (HS), 8 kHz (HS)]를 선택합니다.
   - 새로 플래싱한 직후의 기본 폴링 레이트는 1 kHz (FS)이며, 8 kHz (HS) 사용을 원한다면 반드시 이 메뉴에서 직접 8 kHz (HS)로 변경해야 합니다.
   - 화면 하단의 Apply 버튼을 누르면 선택한 폴링 레이트가 즉시 적용됩니다.
   - 일부 USB 허브나 케이블 환경에서는 HS 옵션의 폴링레이트에서 연결이 불안정해질 수 있습니다. 키보드가 오동작하는 것 같다면 1 kHz (FS) 옵션으로 변경하십시오.

2-2. USB 모니터링 기능

   - 동일한 SYSTEM 메뉴 안에 "Auto downgrade on USB unstable [BETA]" 항목이 있습니다.
   - 이 기능을 켜면, 펌웨어가 USB 오류를 감지했을 때 폴링 레이트를 자동으로 한 단계 낮춰 연결 안정성을 우선합니다.
   - 아직 베타 기능이므로, 폴링 레이트가 원치 않게 자주 바뀐다면 이 옵션을 꺼 두는 것을 권장합니다.

2-3. RGB 이펙트 확장

   - 기본적으로 QMK에서 제공하는 RGB 이펙트(솔리드/브리딩/레인보우/스네이크/나이트/그라데이션/트윙클 등)를 모두 제공합니다.
   - 여기에 Pulse on Press / Pulse off Press / Pulse on Press (Hold) / Pulse off Press (Hold) 네 가지 이펙트를 추가했습니다. 나머지 목록은 모두 QMK 오리지널 이펙트입니다.
   - VIA CONFIGURE 탭에서 LIGHTING 메뉴를 열고 Effect 드롭다운에서 원하는 모드를 선택할 수 있으며, Pulse on/off 계열을 선택한 경우 Effect Speed 슬라이더로 속도를 조절할 수 있습니다.
   - Velocikey: LIGHTING 메뉴에 Velocikey 토글이 추가되었으며, Enable 하면 Snake/Knight/Rainbow/Twinkle 등의 이펙트의 속도가 키 입력 속도에 따라서 변화합니다.

2-4. SOCD (Kill Switch)

   - VIA CONFIGURE 탭 → FEATURE 메뉴 안의 SOCD 항목에서 설정합니다.
   - KEY BIND 1은 좌/우 한 쌍, KEY BIND 2는 상/하 한 쌍을 켜고 끈 뒤, 각 쌍의 KEY 1 / KEY 2에 원하는 키를 지정합니다(예: A/D 또는 ←/→ 등).
   - 동작 방식: 두 키가 동시에 눌리면 마지막으로 눌린 키만 남기고 반대 키를 즉시 해제하며, 마지막 키를 뗄 때 반대 키가 여전히 눌려 있으면 자동으로 다시 눌러 줍니다.
   - 대전 격투 게임 등에서 상쇄(SOCD) 입력을 정리하고, 마지막 입력 우선 규칙을 만들고 싶을 때 사용합니다. 각 쌍은 서로 독립적으로 동작합니다.

2-5. Anti-Ghosting

   - VIA CONFIGURE 탭 → FEATURE 메뉴의 Anti-Ghosting 항목을 Enable 하면 동시 입력 반복 보정 기능이 켜집니다.
   - First Delay Time: 두 개 이상 기본 키를 누른 상태가 얼마나 지속되면 보정 모드로 진입할지 설정합니다(50~300 ms 범위).
   - Repeat Time: 보정 모드 활성화 후, 현재 눌린 키 묶음을 끊었다가 다시 눌러 주는 주기를 설정합니다(50~200 ms 범위).
   - 동작 방식: 두 개 이상 키를 지정한 시간 이상 누르고 있으면 모드가 켜지고, 설정한 주기마다 모든 눌린 키를 한 번 해제 후 다시 눌러 `ASDASD...`처럼 묶음 전체가 반복 입력됩니다. 처음부터 한 개만 누르는 경우에는 영향을 주지 않으며, SOCD에 묶어 둔 키는 모드 진입 판정에서 제외됩니다.
   - 여러 키를 누르다 한 개만 남을 때도 한 번 더 입력 상태를 새로 보내 주며, 모든 키를 떼면 모드가 종료됩니다.

2-6. 디바운스 설정 (DEBOUNCE)

   (1) 디바운스란 무엇인가?

       기계식 스위치를 누르거나 뗄 때, 실제로는 한 번만 동작했더라도 아주 짧은 시간 동안 전기 신호가 여러 번 튀는 현상이 발생할 수 있습니다. 이를 "채터링(chattering)"이라고 합니다.
       디바운스(Debounce)는 이런 짧은 튐을 일정 시간 동안 묶어서, 컴퓨터에는 "한 번만 눌린 것"처럼 전달되도록 정리해 주는 기능입니다.

       이 펌웨어는 기본적으로 디바운스를 내장하고 있으며, 추가로 VIA에서 런타임으로 디바운스 모드와 시간을 바꿀 수 있습니다.

   (2) DEBOUNCE 메뉴 위치

       VIA CONFIGURE 탭에서 FEATURE(또는 BRICK60 전용 설정 메뉴)를 선택하면 DEBOUNCE라는 항목이 보입니다.
       이곳에서 Debounce Mode와 각 모드별 시간을 설정할 수 있습니다.

   (3) Debounce Mode

       Debounce Mode에는 세 가지 모드가 있습니다.

       Balanced
         - 안정성과 반응 속도의 균형을 중시하는 기본형 모드입니다.
         - 키를 누르거나 뗄 때 모두, 변화 전과 후에 같은 길이의 디바운스 시간이 적용됩니다.
         - 예: 8 ms로 설정하면, 누르기 전 8 ms / 누른 후 8 ms, 떼기 전 8 ms / 뗀 후 8 ms를 모두 같은 값으로 사용합니다.

       Fast
         - 가능한 한 빠른 반응 속도를 중시하는 모드입니다.
         - 키를 누르거나 뗄 때, 상태가 변하는 그 순간에는 지연 없이 바로 반영하고, 그 이후 일정 시간 동안만 추가 변화를 막습니다(뒤쪽만 보호하는 "post-only" 방식).
         - 예: 7 ms로 설정하면, 누르거나 뗄 때 입력은 즉시 전달되지만, 그 뒤 7 ms 동안은 같은 키의 추가 튐을 무시합니다.

       Advanced
         - 키를 누를 때(Press)와 뗄 때(Release)를 서로 다른 방식으로 처리하는 고급 모드입니다.
         - 예를 들어 "누를 때는 최대한 빠르게, 뗄 때는 더 강하게 정리"하는 식의 튜닝이 가능합니다.
         - 스위치 상태나 본인의 타건 습관을 잘 알고 있을 때만 사용하는 것을 권장합니다.

   (4) Balanced 모드에서의 시간 설정

       항목 이름: Press & Release - delay before and after (same value)

       - 이 값은 누름과 뗌 모두에, 그리고 변화 전(pre)과 후(post) 모두에 동일하게 적용됩니다.
       - 예를 들어 9 ms로 설정하면,
         · 키를 누르기 직전 9 ms와 누른 뒤 9 ms 동안, 신호가 안정되도록 정리합니다.
         · 키를 뗄 때도 마찬가지로, 떼기 전 9 ms와 떼고 난 뒤 9 ms 동안 동일한 기준으로 처리합니다.
       - 일반적인 채터링 억제와 안정적인 타건감을 원한다면 Balanced 모드에서 5~10 ms 사이를 먼저 사용해 보길 권장합니다.

   (5) Fast 모드에서의 시간 설정

       항목 이름: Press & Release - delay after change (post-only)

       - 키 상태가 바뀌는 순간(누르기 혹은 떼기)은 바로 컴퓨터로 전달됩니다.
       - 그 뒤로만 설정한 시간(ms) 동안 보호 구간이 적용됩니다.
       - 예: 7 ms로 설정하면,
         · 키를 누른 순간 바로 입력되지만, 그 뒤 7 ms 동안 같은 키의 추가 변화는 무시됩니다.
         · 키를 뗄 때도 똑같이, 떼는 순간 바로 해제되지만, 이후 7 ms 동안은 같은 키의 떨림을 무시합니다.
       - 게임, 리듬 게임 등 빠른 반응이 중요한 환경에서 유용합니다. 값이 너무 낮으면 채터링이 보일 수 있으므로, 3~10 ms 사이에서 조정해 보세요.

   (6) Advanced 모드에서의 시간 설정

       Advanced 모드에서는 Press와 Release를 따로 설정할 수 있습니다.

       Press - delay after press (post-only cooldown)
         - 키를 누른 순간에는 지연 없이 바로 입력됩니다.
         - 누른 직후에만 설정한 시간(ms) 동안 쿨다운이 걸려, 같은 키의 추가 튐을 막습니다.
         - 예: 3 ms로 설정하면, 누르면 즉시 입력되지만 그 뒤 3 ms 동안은 같은 키의 변화가 무시됩니다.

       Release - delay before and after release (pre+post window)
         - 키를 떼는 시점을 기준으로 앞과 뒤에 모두 보호 구간을 둡니다.
         - 예: 4 ms로 설정하면, 떼기 전 4 ms와 떼고 난 뒤 4 ms를 하나의 "창(window)"로 묶어서, 그 안에서 발생하는 작은 튐을 하나의 깔끔한 해제 이벤트로 처리합니다.

   (7) 어떤 값을 써야 할지 모를 때의 권장 조합

       - 키보드나 스위치에 특별한 문제가 없다면
         · Debounce Mode: Balanced
         · Press & Release - delay before and after (same value): 5~10 ms
         조합을 먼저 추천합니다.

       - 게임 등에서 반응 속도가 더 중요하다면
         · Debounce Mode: Fast
         · Press & Release - delay after change (post-only): 3~10 ms
         로 시작한 뒤, 채터링이 보이면 값을 조금씩 올려 보십시오.

       - Advanced 모드는, 스위치의 채터링 패턴이나 본인의 타건 습관을 잘 알고 있는 사용자만 세밀 조정용으로 사용하는 것을 권장합니다.

2-7. 탭핑 설정 (TAPPING TERM, 실시간 변경)

   - 기존 펌웨어에서는 TAPPING_TERM이 고정이었으나, 현재 버전부터 VIA의 GLOBAL SETTINGS에 TAPPING 항목이 추가되어 런타임으로 조정할 수 있습니다.
   - VIA JSON의 UI 항목 이름과 역할:
     · Global Tapping Term: 탭으로 인식할 최대 시간(기본 200 ms). 이 시간이 지나면 홀드로 처리됩니다.
     · Permissive Hold: 이 옵션을 켜면, 탭핑 윈도우 내에 다른 키가 함께 눌리면 홀드로 간주해 롱프레스 동작을 더 쉽게 냅니다.
     · Hold on Other Key Press: 탭핑 중 다른 키가 눌린 순간 즉시 홀드로 전환합니다(타이밍에 상관없이 빠르게 홀드 처리).
     · Retro Tapping (Tap if unused): 탭핑 윈도우 안에 키를 눌렀다가 떼지 않아도, 다른 입력이 없고 타이머가 끝나면 탭으로 확정합니다(레트로 탭핑).
   - 값을 바꾼 뒤 SAVE(또는 VIA 저장)을 누르면 EEPROM에 저장되어 재부팅 후에도 유지됩니다.

2-8. 탭댄스(Tap Dance) 설정 & 키코드 사용법

   - 기본 개념: 슬롯 1~8(TD0~TD7)에 “어떤 키를 몇 번/얼마나 눌렀을 때 무엇을 보낼지”를 기록해 둔 뒤, 키맵에는 TD0~TD7을 배치만 하면 됩니다.
   - KC_* 키코드 전체 목록은 https://docs.qmk.fm/keycodes 에서 확인할 수 있습니다.
   - 설정 위치: VIA CONFIGURE → TAPDANCE 메뉴.
     · On Tap / On Hold / On Double Tap / Tap+Hold를 슬롯별로 지정합니다. 비워 두면 해당 동작은 실행되지 않고, 비어 있는 경우에는 Tap을 대신 사용하는 폴백이 적용됩니다.
     · Term은 슬롯별 탭/홀드 판정 시간(100~500 ms, 20 ms 스텝)으로, 글로벌 TAPPING_TERM과는 완전히 독립적입니다. 기본값은 200 ms입니다.
   - 적용/저장: 설정을 바꾸면 즉시 RAM에 반영되어 바로 시험할 수 있고, Save를 누르면 EEPROM에 저장되어 재부팅 후에도 유지됩니다.
   - 키맵 배치: VIA KEYMAP 탭 하단의 CUSTOM 섹션에 있는 “Tap Dance” 그룹에서 TD0~TD7을 골라 원하는 키 위치에 배치합니다. UI의 TD0~TD7이 내부 슬롯 0~7과 1:1로 연결됩니다.
   - 동작 예시 (Vial과 동일한 로직):
     · Hold가 비어 있으면 Tap을 대신 눌러 유지합니다.
     · Tap+Hold가 비어 있으면 Tap을 한 번 보내고 나서 Hold(없으면 Tap)를 길게 누릅니다.
     · Double Tap이 비어 있으면 Tap을 한 번 보내고 Tap을 길게 누릅니다.
     · 3번 연타하면 Tap×3, 4번 이상은 추가 Tap을 보냅니다. DOUBLE_SINGLE_TAP 패턴도 동일하게 처리합니다.
   - 빠른 시작 가이드 (초보자용):
     1) TAPDANCE 메뉴에서 TD0 슬롯에 On Tap=“A”, On Hold=“B”, Term=200 ms로 설정합니다.
     2) CONFIGURE 화면에서 원하는 위치에 TD0 키코드를 배치합니다.
     3) 해당 키를 짧게 누르면 A, 길게 누르면 B가 입력됩니다. 더블탭을 쓰고 싶다면 On Double Tap에 원하는 키를 채워 넣으면 됩니다.

3. VIA 사용 절차

   1) 브라우저에서 https://usevia.app 에 접속합니다.
   2) 상단의 SETTINGS 탭으로 이동한 뒤, "Show Design tab" 토글을 켭니다.
3) 새로 생긴 DESIGN 탭을 열고, 화면의 "Load Draft Definition" 버튼을 눌러 제공된 VIA.JSON 파일을 불러옵니다.
   4) JSON 로드가 완료되면 CONFIGURE 탭으로 돌아가 키맵, 레이어, 매크로 등을 설정합니다.
   5) SYSTEM 메뉴에서 USB POLLING 및 USB 모니터링 옵션을, LIGHTING 메뉴에서 RGB 이펙트를 조정합니다.
   6) DEBOUNCE 메뉴에서 위에서 설명한 디바운스 모드와 시간을 설정합니다.

4. 펌웨어 플래싱 절차

   1) 키보드를 부트로더 모드로 전환합니다.
      - Bootmagic 리셋: ESC 키(매트릭스 0,0)를 누른 채 USB 케이블을 연결합니다.
      - VIA 리셋: VIA CONFIGURE → SYSTEM 메뉴에서 "Jump To BOOT" 버튼을 누릅니다.
   2) PC에 새로운 이동식 디스크가 나타나면, 제공된 펌웨어.uf2 파일을 해당 디스크에 복사합니다.
   3) 복사가 끝나고 디스크가 자동으로 사라지면, 플래싱이 완료되고 키보드는 새 펌웨어로 재시작합니다.

5. EEPROM 초기화 및 키맵 백업 안내 (AUTO_FACTORY_RESET)

   AUTO_FACTORY_RESET 기능이 도입되어 "저장되어 있는 펌웨어 버전"과 "지금 실행 중인 펌웨어 버전"이 서로 다르면 EEPROM을 자동으로 초기화합니다.

   - 최초 부팅 시 한 번 EEPROM이 자동 초기화됩니다.
   - EEPROM은 키맵, 레이어, 매크로 사용자 설정을 담고 있기 때문에 이 과정에서 사용자 설정이 모두 공장 초기값으로 되돌아갑니다.

   따라서, 다른 버전의 펌웨어로 업데이트하기 전에 다음 절차로 키맵을 백업해 두는 것을 권장합니다.

   1) 펌웨어를 바꾸기 전에, VIA CONFIGURE 탭을 열고, 왼쪽 메뉴의 SAVE + LOAD 메뉴를 선택합니다.
   2) Save 버튼을 눌러 현재 키맵 구성을 파일로 저장합니다(예: brick60.layout.json).
   3) 새 펌웨어를 플래싱하고, 키보드가 자동으로 재부팅되면 AUTO_FACTORY_RESET이 실행돼 EEPROM이 초기화 됩니다.
   4) 다시 VIA CONFIGURE 탭을 열고, SAVE + LOAD 메뉴에서 Load 버튼을 눌러 앞에서 저장한 키맵 파일을 불러오면 이전 레이아웃을 그대로 복원할 수 있습니다.

   참고로, SYSTEM → CLEAN 메뉴의 EEPROM 정리 기능은 여전히 수동 초기화 용도로 남아 있습니다. 펌웨어 설정이 꼬인 것처럼 보일 때, 최후의 수단으로 수동 초기화를 실행한 뒤 같은 방법으로 설정을 다시 불러오면 됩니다.

================================================================
BRICK60 Firmware Guide
================================================================

This document is the official guide for BRICK60 (STM32H7S with 8,000 Hz USB polling). It explains how to flash the firmware and configure USB polling, debounce behavior, and keymaps using VIA.

1. Firmware files

   BRICK60-V251125R2.uf2
     - The firmware image that you flash onto the BRICK60 itself.
     - From this version onward, debounce mode and delay can be adjusted in real time in VIA, so there is no longer a separate DEF / EAG image. A single UF2 file is used for all configurations.

   BRICK60-V251125R2.JSON
     - Draft Definition file used by VIA (usevia.app) so that BRICK60 is correctly recognized and its debounce / USB / other options appear.
     - You must load this file once before you start editing the keymap.

2. Main features and configuration

2-1. USB POLLING

   - In VIA, open the CONFIGURE tab and select the SYSTEM menu on the left side.
   - In the USB POLLING section, choose the desired polling rate from the list [1 kHz (FS), 2 kHz (HS), 4 kHz (HS), 8 kHz (HS)].
   - Immediately after flashing, the default polling rate is 1 kHz (FS). If you want to use 8 kHz (HS), you must change it manually to 8 kHz (HS) in this menu.
   - Click Apply at the bottom of the page to apply the selected polling rate.
   - On some USB hubs, mainboards, or cables, HS polling options can be unstable. If the keyboard misbehaves, switch to 1 kHz (FS).

2-2. USB monitoring

   - In the same SYSTEM menu you will find the option "Auto downgrade on USB unstable [BETA]."
   - When this option is enabled, the firmware will automatically step the polling rate down one level when it detects repeated USB errors, prioritizing a stable connection.
   - Because this feature is still marked as beta, if the polling rate changes more frequently than you expect, it is recommended to turn this option off.

2-3. RGB effects

   - All standard QMK RGB effects are available (Solid/Breathing/Rainbow/Snake/Knight/Gradient/Twinkle, etc.).
   - In addition, four extra effects are added: Pulse on Press, Pulse off Press, Pulse on Press (Hold), Pulse off Press (Hold). Everything else in the Effect dropdown is the original QMK list.
   - Open the LIGHTING menu under the VIA CONFIGURE tab and select your preferred effect; for the Pulse on/off variants you can adjust the Effect Speed slider.
   - Velocikey: A Velocikey toggle is available in the LIGHTING menu. When enabled, speed-based effects such as Snake/Knight/Rainbow/Twinkle change their playback speed according to your typing speed. When disabled, they run at their built-in default speeds (as indicated by the effect name numbers).

2-4. SOCD (Kill Switch)

   - Configure this in VIA → CONFIGURE → FEATURE → SOCD.
   - KEY BIND 1 controls a left/right pair, KEY BIND 2 controls an up/down pair; enable each pair and assign any two keys to KEY 1 / KEY 2 (for example A/D or ←/→).
   - Behavior: when both keys in a pair are held, the most recently pressed key stays active while the opposite key is immediately released. When the last key is released, the opposite key is re-pressed automatically if it is still held down.
   - Useful for fighting games or any case where you want last-input-wins SOCD cleaning. The two pairs work independently.

2-5. Anti-Ghosting

   - Enable Anti-Ghosting in VIA → CONFIGURE → FEATURE to activate the simultaneous-key repeat helper.
   - First Delay Time sets how long two or more basic keys must be held before the helper mode starts (50–300 ms range).
   - Repeat Time sets how often, once active, the firmware releases and re-sends the currently held keys (50–200 ms range).
   - Behavior: after holding two or more keys past the delay, the firmware periodically clears and re-sends all held keys, producing `ASDASD...`-style repeated input for the whole chord. Single-key presses are unaffected, and keys assigned to SOCD pairs are ignored for the activation check.
   - If you go from multiple keys down to one, the firmware sends one more refresh; the mode ends when all keys are released.

2-6. Debounce settings (DEBOUNCE)

   (1) What is debounce?

       When you press or release a mechanical switch, the electrical contact can bounce several times in a very short period, even though you only intended a single action. This can appear as many very rapid on/off changes, which is commonly called "chattering."

       Debounce collects these tiny bounces inside a small time window and delivers them to the computer as one clean press or release event.

       The BRICK60 firmware has built-in debounce by default, and in addition you can change the debounce mode and timing at runtime in VIA.

   (2) Where to find the DEBOUNCE menu

       In the VIA CONFIGURE tab, open the FEATURE section (or the BRICK60-specific settings panel) and look for the DEBOUNCE group.
       This is where you choose the Debounce Mode and set the delay values for each mode.

   (3) Debounce Mode

       There are three modes available:

       Balanced
         - A basic mode that focuses on a good balance between stability and responsiveness.
         - Uses the same debounce time before and after both pressing and releasing a key.
         - Example: at 8 ms, the firmware applies 8 ms before and 8 ms after both press and release events.

       Fast
         - A mode aimed at achieving the fastest possible response.
         - When you press or release a key, the state change is sent immediately without delay. Only the short period after the change is protected (post-only).
         - Example: at 7 ms, the press or release is reported instantly, and any further changes on that key are ignored for 7 ms.

       Advanced
         - An advanced mode that treats press and release differently.
         - For example, you can tune it so that pressing is as fast as possible while releasing is filtered more strongly.
         - It is recommended for users who understand the condition of their switches and their own typing patterns.

   (4) Time setting in Balanced mode

       Label in VIA: Press & Release - delay before and after (same value)

       - This value is applied to both press and release, and both before (pre) and after (post) the change.
       - Example: at 9 ms,
         · Just before and just after a key press, the signal is stabilized for 9 ms so that small bounces are smoothed out.
         · The same 9 ms / 9 ms window is used before and after a key release.
       - If you want simple chatter suppression and a stable typing feel, it is recommended to start with Balanced at 5–10 ms.

   (5) Time setting in Fast mode

       Label in VIA: Press & Release - delay after change (post-only)

       - The moment the key state changes (press or release), the event is sent to the computer immediately.
       - Only the period after the change is protected for the specified time in milliseconds.
       - Example: at 7 ms,
         · When you press a key, the press is reported instantly, then further changes on that key are ignored for 7 ms.
         · When you release a key, the release is likewise reported instantly, and further changes are ignored for 7 ms.
       - This mode is useful for games and rhythm titles where fast response is important. If the delay is set too low, you may see chattering; try values in the range of roughly 3–10 ms.

   (6) Time settings in Advanced mode

       In Advanced mode, Press and Release are configured separately.

       Press - delay after press (post-only cooldown)
         - The press event is sent with no additional delay.
         - Only the period directly after the press is protected for the given time, acting as a short cooldown against extra bounces.
         - Example: at 3 ms, the key responds instantly when pressed, and then ignores further changes for 3 ms.

       Release - delay before and after release (pre+post window)
         - The release event is given a protection window both before and after the moment of release.
         - Example: at 4 ms, the 4 ms before and 4 ms after the release are treated as one window, and all small bounces in that window are merged into a single clean release event.

   (7) Recommended combinations if you are unsure

       - If your keyboard and switches do not show any obvious problems:
         · Debounce Mode: Balanced
         · Press & Release - delay before and after (same value): 5–10 ms

       - If faster reaction is more important (for example in games or rhythm titles):
         · Debounce Mode: Fast
         · Press & Release - delay after change (post-only): 3–10 ms
         Start from a low value, and raise it step by step if you see chatter or unwanted repeated input.

       - Advanced mode is intended as a fine-tuning tool for users who understand how their switches chatter and how they type.

2-7. Tapping settings (TAPPING TERM, runtime adjustable)

   - Earlier firmware versions used a fixed TAPPING_TERM. The current version adds a TAPPING section under VIA → GLOBAL SETTINGS so you can change it at runtime.
   - VIA JSON UI labels and their roles:
     · Global Tapping Term: Maximum time window (default 200 ms) for a tap; if you hold longer, it becomes a hold.
     · Permissive Hold: If another key is pressed during the tapping window, treat the key as a hold so long-press actions are easier to trigger.
     · Hold on Other Key Press: Immediately force a hold as soon as any other key is pressed, regardless of remaining tap time.
     · Retro Tapping (Tap if unused): If no other input happens before the window ends, a key still being held is counted as a tap (retro tapping behavior).
   - After adjusting values, click SAVE in VIA to store them to EEPROM so they persist across reboots.

2-8. Tap Dance configuration & keycode usage

   - Concept: assign “what to send” for slots 1–8 (TD0–TD7) and simply place TD0–TD7 on the keymap.
   - KC keycodes are listed at https://docs.qmk.fm/keycodes.
   - Where to set: VIA CONFIGURE → TAPDANCE.
     · On Tap / On Hold / On Double Tap / Tap+Hold per slot. If a field is left empty, that action is skipped and Tap is used as the fallback where applicable.
     · Term is the per-slot tap/hold window (100–500 ms, 20 ms step). It is completely independent of the global TAPPING_TERM. Default is 200 ms.
   - Apply/save: changes take effect immediately in RAM; click Save to store them to EEPROM so they persist after reboot.
   - Keymap placement: in VIA KEYMAP tab, scroll to the bottom CUSTOM section and select TD0–TD7 under the “Tap Dance” group, then place them on the layout. UI TD0–TD7 map 1:1 to internal slots 0–7.
   - Behavior (matches Vial Tap Dance):
     · If Hold is empty, the Tap key is held instead.
     · If Tap+Hold is empty, Tap is sent once, then Hold (or Tap) is held down.
     · If Double Tap is empty, Tap is sent once, then Tap is held.
     · Triple tap sends Tap×3; more than three sends extra Tap. DOUBLE_SINGLE_TAP is handled the same way as Vial.
   - Quick start example for newcomers:
     1) In TAPDANCE, set TD0: On Tap = “A”, On Hold = “B”, Term = 200 ms.
     2) Place TD0 on the desired key position in the keymap.
     3) A short press sends A; a long press sends B. To add a double-tap action, fill in On Double Tap.

3. How to use VIA

   1) Visit https://usevia.app in a web browser.
   2) Open the SETTINGS tab at the top and enable "Show Design tab."
3) Open the new DESIGN tab, click "Load Draft Definition," and load the provided VIA.JSON file.
   4) After the JSON has been loaded, go back to the CONFIGURE tab to edit keymaps, layers, and macros.
   5) Use the SYSTEM menu to adjust USB POLLING and USB monitoring, the LIGHTING menu to configure RGB effects, and the DEBOUNCE group to set the debounce modes and timings described above.

4. Firmware flashing procedure

   1) Put the keyboard into bootloader mode:
      - Bootmagic reset: hold the ESC key (matrix position 0,0) while plugging in the USB cable.
      - VIA reset: in VIA CONFIGURE → SYSTEM, click the "Jump To BOOT" button.
   2) When a new removable drive appears on the host PC, copy the provided firmware.uf2 file onto that drive.
   3) When the drive disappears automatically, flashing is complete and the keyboard restarts with the new firmware.

5. EEPROM reset and keymap backup (AUTO_FACTORY_RESET)

   This firmware enables AUTO_FACTORY_RESET. It compares the "firmware version stored in EEPROM" with the "version of the firmware currently running":

   - If the stored version and the running version are different, the EEPROM is automatically cleared once at the first boot after flashing.
   - Because the EEPROM stores user data such as keymaps, layers, and macro settings, this process resets all such settings to factory defaults.

   For this reason, you should back up your keymap before updating to a different firmware version:

   1) Before changing firmware, open the VIA CONFIGURE tab while the keyboard is still running the old version.
   2) In the left-side menu, open the SAVE + LOAD section:
      - Click Save to export your current layout to a file (for example brick60.layout.json).
   3) Flash the new firmware (the provided firmware.uf2). After the keyboard reboots, AUTO_FACTORY_RESET will run once and clear the EEPROM.
   4) Open the VIA CONFIGURE tab again, go back to the SAVE + LOAD section, and click Load to import the layout file you saved earlier. Your previous layout will then be restored.

   The SYSTEM → CLEAN menu is still available for manual EEPROM reset when necessary. If the configuration seems broken while you remain on the same firmware version, you can use manual EEPROM clean as a last resort and then restore your saved layout using the same SAVE + LOAD procedure.
