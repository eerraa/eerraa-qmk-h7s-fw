# 검증 매뉴얼

Genre: manual
Canonical for: 보드 없이 돌릴 수 있는 검증과 그 명령, 각 검사가 무엇을 무는가, 실기에서만
판정되는 것과 그것을 읽는 법, 증상별 확인 순서

## 1. 세 가지를 돌린다

| 명령 | 무엇을 무는가 | 언제 owe하는가 |
| --- | --- | --- |
| `python tools/era_doc_refs.py` | 문서–코드 정합 9종(`docs/MAP.md` §8) | **문서만 고쳤어도 돌린다** |
| `pwsh -NoProfile -File tools/era_via_host_tests/run.ps1` | VIA 값 계층·진단 봉투·단일 TX 생산자 | VIA 값 계층, 진단, `via_hid.c`, `via.c`를 건드렸을 때 |
| `cmake` 빌드 (§4) | 컴파일과 링크, 정적 단언, 크기 | 소스를 건드렸을 때 |

**소스를 건드리지 않은 변경은 빌드를 owe하지 않는다.** 그렇게 말하는 것이 검증 진술이다.

검사기 자신을 고쳤으면 `python tools/era_doc_refs_selftest.py`를 돌린다. 검사마다 결함을
하나씩 심어 검사기가 그것을 그 이름으로 잡는지 보고 원본을 되돌린다 — **통과하는 검사기가
무언가를 보고 있다는 뜻은 아니기 때문이다.**

클론마다 한 번 `git config core.hooksPath hooks` 하면 `hooks/pre-commit`이 커밋 전에
`python tools/era_doc_refs.py`를 돌린다. CI 워크플로는 없다.

## 2. 툴체인 전제 (Windows)

문서 검사기는 Python 3만 있으면 어디서든 돈다. 호스트 테스트는 mingw gcc **절대경로**가
`tools/era_via_host_tests/run.ps1`에 박혀 있어 그 경로가 없는 머신에서는 돌지 않는다. 문서
검사기를 그 스크립트에 합치지 않은 이유가 이것이다 — 툴체인이 없는 곳에서도 문서 정합은
검사되어야 한다.

ARM 빌드는 툴체인이 PATH에 없다. Git Bash 기준:

```bash
export PATH="/d/baram-fw-tools_exe/arm_toolchain/arm_gcc/gcc-arm-none-eabi-10.3-2021.10/bin:$PATH"
export PATH="/d/baram-fw-tools_exe/arm_toolchain/make/xpack-windows-build-tools-4.4.0-1/bin:$PATH"
```

`-DKEYBOARD_PATH='/keyboards/...'`는 Git Bash가 Windows 경로로 바꿔 버리므로
`MSYS_NO_PATHCONV=1`이 필요하다. 그런데 그 변수는 `ARM_TOOLCHAIN_DIR` 변환도 함께 막으므로
**`ARM_TOOLCHAIN_DIR`만은 Windows 형식(`D:/...`)으로** 준다. MSYS 형식으로 주면 툴체인 cmake가
"is not a full path to an existing compiler tool"로 configure에서 실패한다.

## 3. 호스트 테스트가 무는 것

보드 없이 mingw gcc로 해당 유닛만 호스트에서 빌드해 돌린다.

- **exact-ms / legacy 왕복 벡터** — `docs/contract_via.md` §3의 비대칭(legacy GET이 저장값을
  바꾸지 않는다)을 포함한다.
- **state sync 봉투** — 32 B 전 구간 검사와 `ERA_STATE_SYNC_STATUS_INVALID` 팔.
- **MOUSE 단위 환산** — 최고 속도 반올림, 램프 시간 보존, 가속 off의 첫 스텝 고정, 클램프
  정직성(담기지 않는 조합은 요청보다 짧은 값을 돌려준다).
- **진단 봉투** — capability/tag/reserved/version, 세션 수명, multi-chunk 일관성, stale
  sequence, 정규화 histogram, 하드 이벤트, deadline wrap, 포화, RAM 상한.
- **단일 TX 생산자** — `check_single_producer.py`가 소스를 직접 읽는다
  (`docs/contract_via.md` §1).

## 4. 빌드

```powershell
cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/keynetix/may65' -G "MinGW Makefiles"
cmake --build build -j10
```

보드 경로는 `docs/MAP.md` §3의 다섯 중 하나다. UF2 변환은 CMake 타깃 안에서 자동으로 처리된다
(`tools/uf2/uf2conv.py`, family `0xFFFF0002`). 한 보드가 통과하면 나머지도 대개 통과하지만,
보드의 `<board>/config.h`나 `<board>/port/`를 건드렸으면 그 보드를 직접 빌드한다.

**크기 비교는 같은 보드·같은 툴체인끼리만 한다.** RAM/FLASH 증감을 보고할 때는 비교 기준
버전을 함께 적는다.

## 5. 실기에서만 판정되는 것

무엇이 아직 미검증인지는 `docs/state_open.md`가 든다. 여기 있는 것은 **읽는 법**이다.

**진단 수치를 비교하는 축**은 `docs/contract_via.md` §6-1이 정한다. 절대 µs는 열거마다
재추첨되는 위상을 포함하므로 회차 비교에 쓸 수 없다.

**부팅 카운터는 실시간 값이 아니다.** 두 번 오해를 샀으므로 적어 둔다.

- `Since firmware boot` 계열은 **스냅샷 값**이다. 세션이 새 스냅샷을 만들 때만 갱신되므로,
  이벤트 전후로 화면을 읽어도 새 세션을 돌리지 않으면 같은 옛 값을 두 번 본다.
- **폴링 모드 Apply는 MCU를 재부팅시켜 카운터를 0으로 되돌린다.** 모드를 바꾼 직후 런의
  카운터가 1/1/1/0인 것은 정상이다.
- 따라서 **어떤 이벤트가 계수됐는지 보려면 이벤트를 일으킨 뒤 짧은 세션을 새로 돌린다.**

**모드를 바꾸면 연결이 끊긴다.** 진행 중인 세션은 앱이 중단으로 처리하고, 재연결 후 새 세션을
돌려 비교한다. 펌웨어 버전이 다른 기록은 같은 비교군에 넣지 않는다.

## 6. 증상별 확인 순서

| 증상 | 확인 |
| --- | --- |
| UF2를 올렸는데 자동으로 시작하지 않는다 | 케이블 재삽입으로 부팅되면 인계 디태치 문제다 — `docs/contract_usb.md` §6. 구 부트로더 보드에서 재현되는지 본다. |
| BIOS/UEFI에서 키가 안 먹는다 | USB POLLING을 1 kHz (FS)로 낮춰 본다. 그래도 안 되면 `docs/contract_usb.md` §3의 편차 둘을 의심한다. |
| 20키를 넘겨도 더 안 들어간다 | 정상이다. `docs/contract_usb.md` §2. |
| 커서가 전혀 안 움직인다 | 키맵에 Mouse 키코드를 배치했는지 본다. VIA KEYMAP 탭 기본 목록에 있다. |
| 가속 Off인데 너무 빠르다 | `mousekey_config_apply_runtime()`이 `time_to_max == 0`에서 배수 1을 넣는지 본다 — `docs/contract_via.md` §7-3. |
| 가속을 2.0초로 골랐는데 1.3초로 표시된다 | Steps Per Second가 200 /s면 램프 상한이 1.275초다. 되읽기가 정직한 것이 계약이다 — `docs/contract_via.md` §7-2. |
| 램프가 주기를 바꿀 때 같이 변한다 | interval SET 경로에서 램프 읽기/재설정이 빠졌다. |
| legacy 드롭다운으로 130 ms를 보냈는데 120 ms로 저장된다 | 정상이다. legacy SET만 20 ms 격자로 내린다 — `docs/contract_via.md` §3. |
| 값이 재부팅 후 사라진다 | VIA에서 SAVE를 눌러야 flush된다 — `docs/contract_eeprom.md` §1. |
| VIA 응답이 없거나 다른 요청의 응답이 온다 | TX 생산자가 둘로 늘어난 것을 의심한다. `check_single_producer.py`를 돌린다. |
| 펌웨어에 있는 기능이 VIA 화면에 없다 | 보드 JSON에 그 채널이 없는 것이다. `python tools/era_doc_refs.py`의 `menu` 검사가 잡는다. |
| 부팅이 LED 점멸에서 멈춘다 | EEPROM 자동 초기화가 3회 실패했다 — `docs/contract_eeprom.md` §2. |
| EEPROM 쓰기가 느리거나 유실이 의심된다 | `cli eeprom info`의 큐 하이워터/오버플로 카운터를 본다. |
| 저장된 폴링 모드와 부팅 로그가 다르다 | `boot info`로 EEPROM 캐시를 확인한다. 자동으로 되돌리는 경로는 없다 — `docs/contract_usb.md` §4. |
