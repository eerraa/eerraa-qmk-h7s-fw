======================================================================
H7S Firmware Guide
======================================================================

----------------------------------------------------------------------
한국어
----------------------------------------------------------------------

■ 펌웨어 업데이트

V261004R1은 USB 진단 대신 조회 시점의 폴링 설정을 표시하며, 폴링 변경 시
VIA 후속 통신을 처리한 뒤 재부팅합니다. 공식 VIA의 Tap Dance는 기본 모드 편집만
제공하고 고급 설정은 https://usekb.cc 에서 편집합니다.
V261004R1은 MOUSE 정밀 설정을 위해 저장 형식을 변경했습니다. 이전 버전에서
업데이트하면 키맵·매크로·모든 설정이 초기화됩니다. 먼저 백업하십시오.

V261002R1은 MAY65 매트릭스 확장에 따라 EEPROM 형식을 변경했습니다.
이전 형식에서 처음 업데이트하면 모든 보드의 키맵·매크로·설정이 초기화됩니다.
먼저 SAVE + LOAD로 백업하고,
백업 파일에 포함되지 않는 설정과 탭댄스는 따로 기록하십시오.
MAY65는 5×15에서 5×16으로 바뀌어 기존 키맵 백업을 그대로 불러올 수 없습니다.
기존 백업을 참고하여 키맵을 수동으로 다시 설정하십시오.
이 릴리스부터 메이커별 USB 식별자를 사용합니다. 메이커와 보드 이름이 모두
일치하는 ZIP을 사용하고, 업데이트 후 브라우저에서 기기 연결을 다시 허용하십시오.

1. 키보드를 Bootloader 모드로 진입시킵니다.
2. PC에 부트로더 이동식 디스크가 나타나면 제공된 .uf2 파일을 복사합니다.
3. 복사가 끝나면 키보드가 자동으로 재시작합니다.

Bootloader 진입 방법은 다음 중 하나를 사용하십시오.
- 키보드 설정 화면의 SYSTEM -> BOOT -> Jump To BOOT
- 키맵에 배치한 QK_BOOT
- 왼쪽 위 키를 누른 채 USB 연결

일반 업데이트는 키맵·매크로·VIA 설정을 그대로 유지합니다. 저장 형식이 바뀐
릴리스만 첫 부팅에서 설정을 공장 초기값으로 되돌리며, 그 사실을 릴리스 안내에
적습니다. 업데이트나 EEPROM CLEAN 전에 VIA SAVE + LOAD로 키맵을 백업해
두십시오.
보드 이름과 일치하는 H7S UF2를 사용하고, 업데이트 뒤 SYSTEM -> VERSION에서
버전을 확인하십시오.
USB 식별자(VID/PID)가 바뀐 펌웨어로 업데이트하면 브라우저가 키보드를 새 장치로
보므로, 설정 화면에서 키보드 연결을 한 번 다시 허용하십시오.

■ 키보드 설정

일반 사용자는 https://usekb.cc 를 권장합니다.
각 설정 메뉴 옆에 Help가 있으므로 별도의 상세 설명서 없이 기능과 설정 방법을
확인할 수 있습니다.

공식 VIA(https://usevia.app)를 직접 사용하려면 배포 ZIP의 usevia.app/ 폴더를
참고하십시오. 그 폴더의 usevia.txt에 Draft Definition을 불러오는 방법과
펌웨어 기능별 설명이 들어 있습니다.


----------------------------------------------------------------------
English
----------------------------------------------------------------------

■ Firmware Update

V261004R1 replaces USB diagnostics with the last-read polling setting and
allows VIA follow-up traffic before restarting after a polling change. Official
VIA edits basic Tap Dance slots; use https://usekb.cc for advanced settings.
V261004R1 changes the stored MOUSE format for precise settings. Updating from
an older version resets all keymaps, macros and settings. Back up first.

V261002R1 changes the EEPROM format to expand the MAY65 matrix. The first
upgrade from the previous format resets keymaps, macros and settings on every
board. Back up with SAVE + LOAD first, and separately record settings and
Tap Dance actions that the backup file does not include.
MAY65 changes from 5×15 to 5×16, so its previous keymap backup cannot be loaded
directly. Use the old backup as a reference to configure the keymap manually.
This release uses maker-specific USB identities. Choose the ZIP matching both
the maker and board, then authorize the keyboard again in the browser.

1. Put the keyboard into Bootloader mode.
2. When the bootloader removable drive appears, copy the provided .uf2 file.
3. The keyboard restarts automatically after the copy finishes.

Use one of these methods to enter the bootloader:
- SYSTEM -> BOOT -> Jump To BOOT in the keyboard configuration UI
- QK_BOOT if it is present in your keymap
- Hold the top-left key while plugging in USB

A normal update keeps the keymap, macros and VIA settings. Only a release that
changes the stored format resets settings to factory defaults on first boot,
and its release notes say so. Back up the keymap with VIA SAVE + LOAD before
updating or running EEPROM CLEAN.
Use the H7S UF2 matching the board name and check SYSTEM -> VERSION
afterward.
If an update changes the keyboard's USB identity (VID/PID), the browser treats
it as a new device; authorize the keyboard once more in the configuration page.

■ Keyboard Configuration

For normal use, https://usekb.cc is recommended.
Each configuration menu has Help beside it, so a separate detailed manual is
not required.

If you want to use the official VIA app at https://usevia.app directly, see the
usevia.app/ folder in the distribution ZIP. Its usevia.txt explains how to load
the Draft Definition and describes the firmware-specific controls.
