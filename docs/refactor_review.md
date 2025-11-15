# EEPROM 시스템 리팩토링 재검토 (GPT5.1)

## 1. 검토 전제와 절차
- Codex GPT5.1 환경에서 EEPROM 경로 전체를 다시 읽고, 이전 문서의 주장과 실제 코드(`src/ap/modules/qmk/port/platforms/eeprom.c`, `src/hw/driver/eeprom/*`, `src/hw/driver/usb/usb.c`, `src/ap/modules/qmk/port/sys_port.c`, `src/hw/driver/eeprom_auto_factory_reset.c`)를 교차 확인했습니다.
- 검토 목적은 **오버헤드/인터럽트 감소, 코드 간략화, 유지보수성 향상**이며, HS 8 kHz 스케줄과 USB Instability Monitor 정책을 해치지 않는지 우선 확인했습니다.
- 의도치 않은 대규모 변경을 피하기 위해, **성능 이득이 명확하고 부작용이 관리 가능한 항목만 “필수 조치”로 분류**했습니다. 개선 제안마다 판단 근거와 예상 부작용을 병기했습니다.

## 2. 범위
- 하드웨어 EEPROM 드라이버: `src/hw/driver/eeprom/*`, `src/hw/driver/eeprom_auto_factory_reset.c`.
- QMK 포팅 계층: `src/ap/modules/qmk/port/platforms/eeprom.c`, VIA 시스템 포트(`src/ap/modules/qmk/port/sys_port.c`), USB Monitor 저장 경로(`src/ap/modules/qmk/port/usb_monitor.c`).
- EEPROM을 사용하는 부트모드 및 USB 모니터: `src/hw/driver/usb/usb.c`, 관련 문서(`docs/features_auto_factory_reset.md`, `docs/features_bootmode.md`, `docs/features_instability_monitor.md`).

## 3. 판단 기준 및 부작용 고려 프레임
1. **성능/오버헤드**: 8 kHz 루프에서 추가 대기나 인터럽트 마스킹이 발생하면 필수 조치 후보로 올렸습니다.
2. **데이터 신뢰성**: EEPROM 큐 누락·미러 불일치처럼 즉시 사용자 설정에 영향을 주는 항목은 보수적으로 “반드시 수정”으로 분류했습니다.
3. **메모리 안전성**: 스택 VLA, 장시간 폴링 등은 펌웨어 다운타임을 유발하므로 높은 우선순위입니다.
4. **부작용 평가**: 각 개선안에 대해 스케줄러 장기 점유, 리셋 타이밍 변화 등 잠재 리스크를 나열하고, 완화 가능 여부를 정리했습니다.

## 4. 세부 진단 및 판단

### 4.1 QMK EEPROM 비동기 계층 (`src/ap/modules/qmk/port/platforms/eeprom.c`)
1. **큐 오버플로 시 데이터 손실 (필수)**  
   - 증상: `eeprom_write_byte()`가 `qbufferWrite()` 반환값을 무시합니다(라인 134-143). 큐 최대치(총 4 KB +1) 도달 시 추가 기록이 조용히 드롭됩니다.  
   - 부작용 고려: 실패 시 즉시 플러시하거나 호출 스레드를 잠시 대기시키면 루프 시간이 늘어날 수 있으나, 현재도 1바이트 처리라 영향이 미미합니다.  
   - 판단: 사용자 설정 손실은 허용 불가 → **반드시 수정**. 재시도 루프에 타임아웃을 두고, 실패 시 로그를 남겨 추적성을 확보해야 합니다.

2. **플러시 처리량 제한(1바이트)으로 인한 큐 적체 (필수)**  
   - 증상: `eeprom_update()`가 루프마다 1바이트만 쓰며, `eeprom_task()`는 SOF마다 한 번 호출됩니다. VIA에서 수백 바이트 블록을 쓰면 큐가 쉽게 포화되고 USB 모니터 루틴까지 지연됩니다.  
   - 부작용 고려: 한 루프에서 과도하게 많은 바이트를 비우면 8000 Hz 타이밍을 해칠 수 있으므로, “최대 N바이트/최대 M us”와 같은 슬라이스 한도를 두어야 합니다.  
   - 판단: 처리량 향상 없이는 큐 드롭 버그도 해결되지 않으므로 **동시에 개선**해야 합니다.

3. **VLA 기반 `eeprom_update_block()` (필수)**  
   - 증상: `uint8_t read_buf[len];` (라인 198 이하)이 사용자 입력 길이만큼 스택을 잠식합니다. VIA RAW HID 명령이 최대 4 KB까지 len을 밀어 넣을 수 있어 ISR/USB 스택과 충돌합니다.  
   - 부작용 고려: 고정 버퍼를 도입하면 반복 비교가 늘어나지만, chunk 비교 방식으로 분기하면 추가 오버헤드는 DMA/I2C 대기보다 작습니다.  
   - 판단: 스택 오버런 리스크가 크므로 **필수 수정**.

4. **VIA 초기화 로직 중복 및 장기 블로킹 (필수 → V251112R3에서 해결)**  
- 증상: `eeprom_task()`가 `is_req_clean`일 때 `eeprom_flush_pending()`을 여러 번 호출하여 큐가 빌 때까지 루프를 점유했고, 동일 동작이 `eeprom_auto_factory_reset`와 중복 구현되어 유지보수가 어려웠습니다.  
   - 해결: `eeprom_apply_factory_defaults()` 공용 헬퍼 도입으로 AUTO_CLEAR와 VIA 초기화가 동일 경로를 공유하며, 실제 측정에서도 두 경로가 모두 `queue max = 13`, `queue ofl = 0`으로 수렴했습니다.  
   - 잔여 리스크: 초기화 중 플러시가 반복 호출되는 구조는 그대로이므로, 향후 비동기 알림 방식으로 전환할 때 리셋 타이밍을 재조정해야 합니다.

### 4.2 하드웨어 EEPROM 드라이버 (`src/hw/driver/eeprom/emul.c`, `src/hw/driver/eeprom/zd24c128.c`)
1. **플래시 에뮬 96비트 연산 남용 (필수)**  
   - 증상: `eepromReadByte()`/`eepromWriteByte()`가 각각 `EE_ReadVariable96bits`/`EE_WriteVariable96bits`를 호출(라인 86-143)하면서 실제로는 1바이트만 사용합니다.  
   - 부작용 고려: 8비트 API를 복구하거나 12바이트 캐시를 두면 코드 복잡도가 소폭 증가하지만, HAL Unlock/Lock 빈도가 감소해 전체 루프 대기 시간이 줄어듭니다.  
   - 판단: 쓰기당 최대 수백 µs 절감과 플래시 마모 감소 효과가 확실하므로 **필수**.

2. **Clean-up 대기 500 ms 바쁜 루프 (필수)**  
   - 증상: `EE_CleanUp_IT()` 후 `is_erasing`이 false가 될 때까지 최대 500 ms 대기(라인 123-138)하며, 그동안 USB 폴링이 멈춥니다.  
   - 부작용 고려: 비동기화 시 마지막 몇 바이트가 실패할 수 있으므로 `eeprom_update()`에서 `is_erasing`을 감지해 재시도하도록 설계해야 합니다.  
   - 판단: 인터럽트/USB 모니터를 동시에 중단하는 현재 구조는 위험 → **필수** 개선.

3. **ZD24C128 단일 바이트 쓰기 (권장)**  
   - 증상: `i2cWriteA16Bytes()` 호출 후 100 ms까지 `i2cIsDeviceReady()`를 폴링(라인 93-118). 장시간 덮어쓰기 시 루프 지연이 커집니다.  
   - 부작용 고려: 페이지 쓰기는 버퍼 관리가 추가 필요하며, VIA가 비정렬 주소로 쓰는 경우 파편화될 수 있습니다.  
   - 판단: HS USB 기판에서 외부 I2C EEPROM을 쓰는 SKU가 제한적이라면 후순위 가능. **권장이지만 필수는 아님**으로 분류했습니다.

4. **`eepromWrite()` 루프 내부 Unlock/Lock 반복 (권장)**  
   - 증상: 다중 바이트 쓰기에서 바이트마다 Unlock/Lock을 반복합니다(라인 167-181, 139-152).  
   - 부작용 고려: 멀티바이트 API 도입 시 오류 롤백 처리가 복잡해지나, 상위 큐가 이미 순서를 보장합니다.  
   - 판단: 기존 Clean-up 문제를 해결한 뒤 접근해도 늦지 않아 **권장**으로 둡니다.

### 4.3 BootMode & 자동 초기화 (`src/hw/driver/usb/usb.c`, `src/hw/driver/eeprom_auto_factory_reset.c`)
1. **BootMode 직접 쓰기 → 미러 불일치 (필수)**  
   - 증상: `usbBootModeWriteRaw()`가 `eepromWrite()`를 호출하고 `eeprom_buf`를 갱신하지 않습니다(라인 234-256). 그 결과 `eeprom_read_dword()`가 여전히 오래된 값을 반환하여, QMK 계층의 동작이 실제 저장값과 어긋납니다.  
   - 부작용 고려: BootMode 저장을 `eeprom_update_dword()`로 바꾸면 큐를 타게 되어 적용 지연이 발생할 수 있습니다. 따라서 BootMode 저장 후 즉시 `eeprom_buf`를 직접 갱신하거나, `eeprom_update_dword()` 사용 시 “즉시 재부팅 시큐언스” 앞에서 플러시 확인을 넣어야 합니다.  
   - 판단: 설정 불일치는 장애로 간주 → **필수**.

2. **AUTO_FACTORY_RESET와 VIA 클린 경로의 중복 (권장)**  
   - 증상: 두 경로 모두 BootMode/USB 모니터 기본값과 센티넬을 각각 기록합니다(`src/hw/driver/eeprom_auto_factory_reset.c` 58-110, `src/ap/modules/qmk/port/platforms/eeprom.c` 60-86).  
   - 부작용 고려: 공용 헬퍼 생성 시 리셋 시점이 바뀔 수 있으니, “센티넬 → 사용자 데이터 → 리셋” 순서를 유지하도록 주의해야 합니다.  
   - 판단: 유지보수성 향상을 위해 권장하나, 기능 결함은 아니므로 **권장**.

3. **Flush-then-reset 패턴의 장기 점유 (필수)**  
   - 증상: VIA 클린, AUTO_CLEAR 모두 `eeprom_flush_pending()`을 호출한 뒤 즉시 리셋을 예약합니다. 큐가 길어지면 워치독 없이 소프트락에 빠질 수 있습니다.  
   - 부작용 고려: 비동기 콜백으로 옮기면 리셋이 지연될 수 있으므로, “최대 대기시간 + 진행률 로그”를 추가해 사용자가 대기 상태를 인지하도록 해야 합니다.  
   - 판단: 현재 구조도 장기 대기 시 USB 다운으로 이어져 **필수**.

### 4.4 관측 및 테스트 갭
- 큐 길이, `is_erasing` 지속 시간, BootMode 쓰기 지연을 관찰할 수 있는 CLI/로깅이 없습니다. 개선 후 리그레션을 방지하려면 최소한 `eeprom_task()`에 처리 통계를 추가해야 합니다.
- VIA 전체 키맵 덮어쓰기 + USB instability monitor 활성화 조합을 자동화한 스트레스 테스트가 없어 부작용을 빠르게 발견하기 어렵습니다. 이 항목은 **권장**입니다.

## 5. 권장 수정 우선순위
1. **큐 신뢰성 및 처리량 개선**: `qbufferWrite()` 실패 처리, 슬라이스형 `eeprom_update()`, VLA 제거를 한 묶음으로 처리합니다.
2. **BootMode/클린 경로 정합성**: BootMode 저장을 큐/미러와 동기화하고, `eeprom_flush_pending()` 동작을 비차단 구조로 바꿉니다.
3. **플래시 에뮬 Clean-up 비동기화**: 96비트 API 축소와 Clean-up 대기 개선을 동시에 진행해 루프 블로킹을 제거합니다.
4. **하드웨어 성능 최적화/관측치 추가**: I2C 페이지 쓰기, Unlock/Lock 최소화, 큐 메트릭 로그 출력을 순차적으로 적용합니다.

## 6. 테스트 및 회귀 대비 제안
- `cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/sirind/brick60' && cmake --build build -j10`으로 기본 빌드 후, VIA를 통한 대량 EEPROM 쓰기를 최소 3회 반복해 큐 길이/에러 카운터를 확인합니다.
- BootMode HS↔FS 전환 + 즉시 리셋 시나리오를 실행해, 미러가 즉시 갱신되는지 로그(`usbBootModeLabel`)를 확인합니다.
- AUTO_FACTORY_RESET 플래그를 강제로 손상시킨 뒤 전원이 켜질 때까지의 총 지연 시간을 측정해 Clean-up 비동기화 효과를 검증합니다.

## 7. 기존 함수 재활용 검토 결과
- **큐 신뢰성/처리량 개선**: `qbufferWrite()`의 반환값과 `qbufferAvailable()`(src/common/core/qbuffer.h)만 활용하면 큐 가득 참 감지와 다중 항목 플러시가 가능하므로, 새로운 큐 API 없이도 재시도·슬라이스 로직을 구현할 수 있습니다.
- **VLA 제거**: `eeprom_read_block()`(src/ap/modules/qmk/port/platforms/eeprom.c:112-125)을 반복 호출하면 고정 크기 버퍼만으로 블록 비교가 가능하여 추가 헬퍼가 필요 없습니다.
- **BootMode 미러 일치**: 현재도 제공되는 `eeprom_update_dword()`와 `eeprom_write_block()`(src/ap/modules/qmk/port/platforms/eeprom.c:150-196)을 호출하면 별도 BootMode 전용 함수 없이 미러 갱신을 보장할 수 있습니다.
- **Clean-up 비차단 처리**: `is_erasing` 플래그와 `eeprom_is_pending()`/`eeprom_flush_pending()`을 조합해, 기존 인터럽트 콜백(`EE_EndOfCleanup_UserCallback`)만으로도 대기 루프를 제거할 수 있어 신규 인터페이스가 필요하지 않습니다.
- **AUTO_CLEAR/VIA 공용화**: 두 경로 모두 이미 `eeconfig_*`와 `usbBootModeApplyDefaults()`/`usb_monitor_storage_apply_defaults()`를 호출하므로, 이 함수들을 재사용해 호출 순서만 정리하면 추가 기능 없이 중복 제거가 가능합니다.

## 8. 단계별 구현 계획과 검증 절차
### 단계 1: QMK 큐 신뢰성 및 처리량 개선
1. 기존 `eeprom_write_byte()`에 `qbufferWrite()` 반환값 점검·재시도 루프를 추가하고, `qbufferAvailable()`로 큐 여유 공간을 확인합니다.
2. `eeprom_update()`는 한 번 호출 시 `min(qbufferAvailable(), 슬라이스 한도)` 만큼 반복 처리하도록 수정하되, 루프 내에서 `millis()` 기반 경과 시간을 체크해 8 kHz 루프 범위를 벗어나지 않도록 합니다.
3. `eeprom_update_block()`의 VLA를 제거하고, 고정 크기(예: 64B) 버퍼로 `eeprom_read_block()`을 반복해 비교한 뒤 변경된 조각만 `eeprom_write_block()`으로 밀어 넣습니다.

**테스트 절차**  
- `cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/sirind/brick60' && cmake --build build -j10`으로 전체 빌드.  
- VIA에서 전체 레이아웃 덮어쓰기(최소 4 KB) 요청을 3회 반복하면서 UART 로그에 “큐 드롭” 메시지가 없는지 확인하고, `qbufferAvailable()`를 CLI나 임시 로그로 출력해 포화가 해소되는지 확인합니다.  
- 키 입력 지연이 없는지 확인하기 위해, 테스트 중에도 기본 키 입력/USB 응답을 체크합니다.

### 단계 2: BootMode 및 VIA 클린 경로 정합성
1. `usbBootModeWriteRaw()`를 `eeprom_update_dword()` 또는 `eeprom_write_block()`으로 전환하고, 저장 직후 `eeprom_buf`를 직접 갱신합니다.  
2. `eeprom_task()`의 `is_req_clean` 처리와 `eeprom_auto_factory_reset` 초기화 루틴에서 공용 헬퍼(예: `eeprom_apply_factory_defaults()`)를 호출하도록 리팩토링하되, 기존 함수들과 동순서를 유지합니다.
3. `eeprom_flush_pending()` 호출은 “최대 대기시간 + 재시도” 로직으로 바꾸어, 루프가 장기 점유되지 않도록 합니다.

**테스트 절차**  
- BootMode HS↔FS 전환 후 즉시 `usbBootModeLoad()`를 호출해 값이 유지되는지 UART 로그로 확인합니다.  
- VIA EEPROM 초기화 명령을 수행하고, UART 로그에서 BootMode/USB 모니터 기본값이 직후에 기록되는지, 그리고 장시간 정지 없이 소프트 리셋이 트리거되는지 확인합니다.  
- AUTO_FACTORY_RESET가 활성화된 빌드에서 플래그를 비정상 값으로 덮어쓴 뒤 전원을 재투입하여 초기화→리셋까지 걸린 시간을 측정합니다.

### 단계 3: 외부 EEPROM 처리량 향상 (페이지/슬라이스/I2C)
1. `zd24c128` 드라이버에 32바이트 페이지 버퍼를 추가하고, `eeprom_update()`가 큐에서 연속 주소를 감지하면 페이지 단위로 묶어 `i2cWriteA16Bytes()` 한 번만 호출하도록 리팩토링합니다. 비정렬 구간은 앞·뒤를 32바이트 경계까지 잘라낸 뒤 남은 부분만 재사용합니다.
2. `EEPROM_WRITE_SLICE_MAX_COUNT`를 시간 기반 상한으로 전환하고(예: `EEPROM_WRITE_SLICE_MAX_US`), `eeprom_update()`가 `micros()`를 참고해 8 kHz 루프 한 주기(125 µs) 안에서 가능한 많은 페이지를 처리하도록 조정합니다. 기존 CLI 계측(`eeprom queue max/ofl`)을 유지해 슬라이스 확대가 실제 처리량 증가로 이어지는지 검증합니다.
3. 외부 EEPROM이 연결된 I2C 채널을 FastMode Plus(1 MHz)까지 끌어올리거나, 다른 센서와 버스를 분리해 충돌을 줄입니다. `hw/driver/i2c.c`의 클럭 설정을 수정한 뒤, 버스 주인이 바뀐다면 `i2cRead/WriteA16Bytes()` 호출부에 새로운 타임아웃을 도입해 오류 복구를 단순화합니다.
4. 대량 쓰기 중에는 `eeprom_task()` 호출 빈도를 일시적으로 높여 큐를 빠르게 소모하도록 하고, 큐가 비워지면 원래 SOF당 1회 호출 패턴으로 되돌립니다. 이를 위해 `qmkUpdate()`에서 “burst 모드” 상태를 확인해 `eeprom_update()`를 추가 호출합니다.

**테스트 절차**  
- `docs/brick60.layout.json`을 VIA로 업로드하면서 `cli eeprom info`를 반복 실행해 큐가 1500엔트리 이상으로 치솟지 않는지, 페이지 쓰기 도입 후 완료 시간이 줄었는지 측정합니다.  
- 로직 애널라이저(또는 HAL 로그)의 I2C SCL 파형을 확인해 1 MHz 설정이 실제로 적용됐는지, NACK/버스 충돌이 없는지 검증합니다.  
- I2C 버스에 다른 장치가 공유되는 빌드(예: RGB, 센서)가 있다면 동일 시점에 CLI 폴링을 돌려 장애가 없는지 확인합니다.  
- 큐 슬라이스 확대가 USB Instability Monitor에 영향을 주지 않는지, 대량 쓰기 중에도 `usbHidMonitorBackgroundTick()` 로그가 정상 주기로 출력되는지 확인합니다.

### 단계 4: 플래시 에뮬/클린업 경로 최적화
1. `eeprom/emul.c`에서 주석 처리된 `EE_Read/WriteVariable8bits()` API를 복구하거나, 12바이트 캐시를 두고 96비트 엔트리를 한 번에 쓰도록 변경해 Unlock/Lock 사이클을 최소화합니다.
2. `EE_CleanUp_IT()` 이후에는 바쁜 대기를 없애고, `is_erasing`이 true일 동안 `eeprom_update()`가 큐를 잠시 유지하도록 상태 머신을 구성합니다. `EE_EndOfCleanup_UserCallback()`을 활용해 클린업 종료 시점을 알리고, 종료 후 누락된 쓰기를 즉시 재개합니다.
3. 플래시 에뮬 전용 `cli eeprom info` 필드를 추가해 최근 클린업 소요 시간과 대기 중인 항목 수를 노출합니다. 이 값으로 V251112R3 대비 지연이 감소했는지 정량 비교합니다.

**테스트 절차**  
- `cli eeprom write` 등으로 플래시 에뮬 경로를 집중적으로 호출해 Clean-up 횟수와 평균 시간이 단축됐는지 로그로 확인합니다.  
- Clean-up이 진행 중일 때도 `usbProcess()`/`qmkUpdate()` 루프가 끊기지 않는지, UART 타임스탬프를 비교해 프레임 스킵이 없는지 검증합니다.  
- AUTO_FACTORY_RESET 빌드에서 플래그를 손상시킨 뒤 전원을 재투입해, 새로운 Clean-up 상태 머신이 전체 초기화 시간을 단축하는지 기록합니다.

## 9. 최신 진행 현황 (V251112R3)
- **단계 1 완료**: 큐 재시도 루프, 8바이트 슬라이스, VLA 제거, CLI `eeprom info` 계측까지 적용돼 장시간 레이아웃 덮어쓰기에서도 `queue ofl = 0`을 유지하는 것을 실기로 확인했습니다.  
- **단계 2 진행 중**: `eeprom_apply_factory_defaults()` 공용화로 AUTO_CLEAR/VIA 초기화 흐름을 통합했고 BootMode·USB Monitor 기본값도 동일 루틴을 거치지만, BootMode 직접 쓰기 경로와 플러시 모드 결정(차단/비차단 전환)은 아직 정리 중입니다.  
- **단계 3 대기**: 페이지 쓰기·슬라이스 확대·I2C 클럭 상향은 설계만 마친 상태이며, 실제 드라이버 변경과 계측 추가는 미착수입니다.  
- **단계 4 대기**: 플래시 에뮬 8비트 API 복구와 Clean-up 상태 머신은 구현 전이며, 현재는 V251112R3 이전과 동일한 블로킹 동작을 사용합니다.
