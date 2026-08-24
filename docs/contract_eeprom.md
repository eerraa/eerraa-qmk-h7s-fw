# 영속 상태 계약

Genre: contract
Canonical for: EEPROM에 무엇이 어떻게 남는가 — USER 슬롯 주소 불변과 유효성 판정, 저장 시점,
버전 쿠키와 자동 팩토리 리셋의 파급, 쓰기 경로가 8 kHz 예산을 지키는 방법

배치 표(오프셋·크기·심볼)는 `docs/MAP.md` §5에 있고 소스에서 다시 계산된다. 여기 있는 것은
그 표가 왜 그 모양이어야 하는가다.

## 1. 슬롯 주소는 한 번 배포되면 움직이지 않는다

- **기능을 폐기해도 슬롯은 지우지 않고 예약으로 남긴다.** `EECONFIG_USER_RESERVED_32`가 그
  사례다 — 폐기한 monitor 토글의 자리이며 읽지도 쓰지도 않는다.
  **왜**: 오프셋을 당기면 이미 필드에 나간 EEPROM에서 뒤 슬롯이 전부 다른 필드로 읽힌다. 버전
  쿠키가 올라가는 릴리스라면 초기화되지만, **쿠키를 올리지 않는 릴리스가 실제로 있다**(§2).
- 새 슬롯은 뒤에 붙인다. USER 블록은 512 B(`EECONFIG_USER_DATA_SIZE`)를 예약한다.
- 각 슬롯은 시그니처와 버전을 든다 — `"GTAP"`, `"TDAN"`, `"MUSK"`. 불일치나 범위 밖이 감지되면
  init에서 기본값으로 덮고 dirty 표시를 남겨 다음 flush에 정상값이 기록되게 한다.
- **유효성 판정 범위는 슬롯마다 다르고, 그것이 의도다.** TAPPING·TAPDANCE는 범위까지 보고
  손상 슬롯을 통째로 되돌린다. MOUSE는 시그니처와 버전만 보고 범위는 필드별 클램프에 맡긴다 —
  열 개의 독립된 손잡이라 하나가 벗어났다고 나머지 아홉을 버릴 이유가 없다.
- MOUSE 슬롯은 페이지가 6개만 보여도 **raw 10개를 전부 저장한다.** 나머지를 나중에 여는 일이
  마이그레이션이 아니라 정의 변경이 되도록 하기 위해서다.

**저장 시점**: VIA `id_custom_set_value`는 런타임과 RAM 이미지만 바꾸고, EEPROM 기록은
`id_custom_save`(VIA의 SAVE 버튼)에서 flush될 때 일어난다. 사용자가 SAVE를 누르지 않으면
재부팅 시 이전 저장값으로 롤백된다 — 이건 결함이 아니라 VIA의 계약이다.

## 2. 버전 쿠키를 올리면 모든 기기의 EEPROM이 초기화된다

`AUTO_FACTORY_RESET_COOKIE`의 기본값은 `_DEF_FIRMWARE_VERSION`에서 파생된다. 부팅 시
`EECONFIG_USER_EEPROM_CLEAR_FLAG`(`AUTO_FACTORY_RESET_FLAG_MAGIC` = "VCLR")와
`EECONFIG_USER_EEPROM_CLEAR_COOKIE`를 읽어 쿠키가 다르면 EEPROM 전체를 포맷하고
`eeprom_apply_factory_defaults()`로 기본값을 다시 쓴다.

따라서 **버전 문자열을 올리는 것은 동적 키맵·매크로·VIA 설정까지 전부 초기화하는 결정이다.**
`AUTO_FACTORY_RESET_ENABLE`이 켜진 보드에 새 이미지를 올리면 첫 부팅에서 그렇게 된다.

이 파급에서 두 가지가 따라 나온다.

- **상수만 바꾸면 아무 일도 일어나지 않는 값이 있다.** `RGBLIGHT_DEFAULT_ON` 같은 공장
  초기값은 `eeconfig_update_rgblight_default()`가 팩토리 초기화 경로에서만 소비하므로, 버전을
  올리지 않으면 기존 기기의 저장값이 계속 이긴다. 기본값 변경은 **반드시 버전 상승과 짝이다.**
- **출시된 보드는 전역 쿠키를 공유한다.** 그래서 릴리스마다 "이번 초기화를 누가 맞는가"를
  따로 판단해야 한다. MAY65의 underglow 기본값을 바꿀 때 초기화를 수용할 수 있었던 것은 그
  보드가 아직 판매 전이었기 때문이고, 그 면제는 MAY65 한정이었다.

반대로 **JSON만 바뀌는 릴리스는 버전을 올리지 않는다.** 채널·value id·EEPROM 배치·펌웨어
코드가 그대로면 재빌드도 초기화도 필요 없고, 이미 플래시된 키보드도 새 JSON만 불러오면 된다.
MOUSE 메뉴 추가와 KKUK 개명이 그런 릴리스였다.

**실패하면 부팅을 막는다.** 초기화는 최대 3회 재시도하며 매 실패마다 `_DEF_LED1`이 세 번
점멸한다. 세 번 모두 실패하면 `hwInit()`이 false를 반환하고 메인이 LED 점멸 상태로 정지한다 —
**반쯤 초기화된 EEPROM으로 조용히 부팅하지 않는 것**이 계약이다.

VIA의 EEPROM 초기화 명령은 `eepromScheduleDeferredFactoryReset()`으로 센티넬을 지우고 지연
리셋을 예약하므로, 다음 부팅에서 자동 초기화와 **완전히 같은 공용 루틴**을 탄다. 경로가 둘로
갈라지지 않는 것이 이 설계의 요점이다.

## 3. 쓰기 경로는 8 kHz 예산 안에서 돈다

VIA/QMK의 쓰기는 RAM 큐에 적재되고 `eeprom_update()`가 호출마다 **최대
`EEPROM_WRITE_SLICE_MAX_US`(100 µs)**만 써서 하위 드라이버로 넘긴다. 큐가
`EEPROM_WRITE_BURST_THRESHOLD`(512 엔트리)를 넘으면 버스트 모드로 추가 호출을 허용해 처리량을
확보한다.

**왜 계약인가**: 이 슬라이스가 깨지면 USB 8 kHz 루프가 밀린다. EEPROM 경로에 동기 쓰기를
넣거나 슬라이스를 늘리는 변경은 폴링 예산을 직접 깎는 변경이다.

큐가 가득 차면 최대 2 ms까지 재시도하고, 그래도 안 되면 직접 쓰기로 떨어뜨린 뒤 로그를 남긴다.
데이터를 조용히 버리지 않는다. 하이워터 마크와 오버플로 카운터는 `cli eeprom info`로 본다.

실제로 쓰이는 드라이버는 외부 I2C EEPROM(`src/hw/driver/eeprom/zd24c128.c`, 32 B 페이지)
하나다. 내부 플래시 에뮬레이션(`src/hw/driver/eeprom/emul.c`)은 같은 API를 구현하고 코드
수준 리팩터링도 되어 있으나 **실기 검증이 없다**(`docs/state_open.md`).

USB 진단은 EEPROM을 쓰지 않는다. 세션도 카운터도 RAM에만 있다(`docs/contract_usb.md` §4).
