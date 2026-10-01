======================================================================
H7S Firmware Guide
======================================================================

----------------------------------------------------------------------
한국어
----------------------------------------------------------------------

■ 펌웨어 업데이트

1. 키보드를 Bootloader 모드로 진입시킵니다.
2. PC에 부트로더 이동식 디스크가 나타나면 제공된 .uf2 파일을 복사합니다.
3. 복사가 끝나면 키보드가 자동으로 재시작합니다.

Bootloader 진입 방법은 다음 중 하나를 사용하십시오.
- 키보드 설정 화면의 SYSTEM -> BOOT -> Jump To BOOT
- 키맵에 배치한 QK_BOOT

일반 업데이트는 키맵·매크로·VIA 설정을 그대로 유지합니다. 저장 형식이 바뀐
릴리스만 첫 부팅에서 설정을 공장 초기값으로 되돌리며, 그 사실을 릴리스 안내에
적습니다. 업데이트나 EEPROM CLEAN 전에 VIA SAVE + LOAD로 키맵을 백업해
두십시오.
보드 이름과 일치하는 H7S UF2를 사용하고, 업데이트 뒤 SYSTEM -> VERSION에서
버전을 확인하십시오.

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

1. Put the keyboard into Bootloader mode.
2. When the bootloader removable drive appears, copy the provided .uf2 file.
3. The keyboard restarts automatically after the copy finishes.

Use either of these methods to enter the bootloader:
- SYSTEM -> BOOT -> Jump To BOOT in the keyboard configuration UI
- QK_BOOT if it is present in your keymap

A normal update keeps the keymap, macros and VIA settings. Only a release that
changes the stored format resets settings to factory defaults on first boot,
and its release notes say so. Back up the keymap with VIA SAVE + LOAD before
updating or running EEPROM CLEAN.
Use the H7S UF2 matching the board name and check SYSTEM -> VERSION
afterward.

■ Keyboard Configuration

For normal use, https://usekb.cc is recommended.
Each configuration menu has Help beside it, so a separate detailed manual is
not required.

If you want to use the official VIA app at https://usevia.app directly, see the
usevia.app/ folder in the distribution ZIP. Its usevia.txt explains how to load
the Draft Definition and describes the firmware-specific controls.
