# eerraa-qmk-h7s-fw

## 프로젝트 개요
- 이 펌웨어는 STM32H7S 계열 MCU를 대상으로 하며, 온보드 USB High-speed PHY만을 사용하여 호스트와 통신합니다.
- USB High-speed 모드에서 8,000Hz 인터럽트 기반 입력 보고를 목표로 하며, 이를 위해 타이밍과 버스 대역폭을 최우선으로 설계되었습니다.
- QMK 논리를 이식해 고속 USB 환경에 최적화된 커스텀 키보드용 펌웨어를 제공합니다.

## Codex를 위한 빠른 분석 절차
1. **버전 및 빌드 파라미터 확인**: `src/hw/hw_def.h` 의 `_DEF_FIRMWARE_VERSION` 매크로와 하드웨어 한정 매크로를 먼저 확인합니다.
2. **엔트리 포인트 추적**: `src/main.c` → `src/ap/ap.c` 순으로 초기화 흐름을 따라가며, 필요한 모듈 초기화 코드를 찾습니다.
3. **QMK 계층 파악**: `src/ap/modules/qmk` 하위의 `port`, `keyboards`, `quantum` 디렉터리를 살펴 QMK 포팅 계층과 키보드별 설정을 확인합니다.
4. **수정 시 체크리스트**:
   - 새로운 코드나 로직을 추가/변경하면 `['VYYMMDDRn'] 설명` 형식의 주석을 해당 코드 주변에 남깁니다.
   - 같은 날짜에 여러 차례 수정하는 경우 `R1`, `R2` 형태로 리비전을 올립니다.
   - 펌웨어 버전은 수정 날짜 기준으로 `src/hw/hw_def.h` 의 `_DEF_FIRMWARE_VERSION` 매크로를 `VYYMMDDRn` 형식으로 갱신합니다.

## 빌드 및 펌웨어 생성
> UF2 변환까지 CMake 빌드 타겟에 포함되어 있으므로 별도 변환 절차가 필요 없습니다.

```bash
cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/sirind/brick60'
cmake --build build -j10
```

## 디렉터리 및 파일 역할
### 루트 디렉터리
| 경로 | 설명 |
| --- | --- |
| `CMakeLists.txt` | 전체 프로젝트의 CMake 설정 및 서브 디렉터리 포함 규칙을 정의합니다. |
| `LICENSE` | 프로젝트 라이선스 정보입니다. |
| `README.md` | 프로젝트 개요와 작업 가이드를 제공합니다. |
| `prj/` | IDE 설정 등 개발 환경 관련 프로젝트 파일을 보관합니다. |
| `src/` | 펌웨어 소스 코드와 라이브러리 전반이 위치합니다. |
| `tools/` | 추가 툴체인 스크립트와 UF2 변환 도구가 들어 있습니다. |

### `prj/`
| 경로 | 설명 |
| --- | --- |
| `prj/vscode/baram-45k.code-workspace` | Baram 45K 보드를 위한 VS Code 워크스페이스 설정입니다. |
| `prj/vscode/baram-60mx-6.25u.code-workspace` | Baram 60MX-6.25U 보드를 위한 VS Code 워크스페이스 설정입니다. |

### `src/`
| 경로 | 설명 |
| --- | --- |
| `src/main.c` | `main()` 엔트리 포인트로, 시스템 초기화와 앱 실행을 담당합니다. |
| `src/main.h` | 메인 모듈에서 사용하는 전역 선언을 제공합니다. |
| `src/ap/` | 애플리케이션 레이어와 QMK 포팅 코드를 포함합니다. |
| `src/bsp/` | STM32H7S 보드 지원 패키지(BSP)와 스타트업 코드를 제공합니다. |
| `src/common/` | 공통 정의, 에러 코드, 기본 하드웨어 래퍼 등을 포함합니다. |
| `src/hw/` | 하드웨어 추상화 계층과 펌웨어 버전 정의를 담고 있습니다. |
| `src/lib/` | 외부 라이브러리(ST HAL, 유틸리티 등)를 모아둔 디렉터리입니다. |

#### `src/ap/`
| 경로 | 설명 |
| --- | --- |
| `src/ap/ap.c` | 애플리케이션 메인 루프와 QMK 태스크 스케줄링을 구현합니다. |
| `src/ap/ap.h` | 애플리케이션 인터페이스 선언을 제공합니다. |
| `src/ap/ap_def.h` | 애플리케이션 전역 상수와 구조체 정의를 담고 있습니다. |
| `src/ap/modules/` | 기능 모듈 묶음으로, 특히 QMK 포팅 레이어가 위치합니다. |

##### `src/ap/modules/qmk/`
| 경로 | 설명 |
| --- | --- |
| `src/ap/modules/qmk/CMakeLists.txt` | QMK 모듈 빌드 타겟 구성을 정의합니다. |
| `src/ap/modules/qmk/keyboards/` | 키보드별 레이아웃, 포팅 코드, JSON 메타데이터가 들어 있습니다. |
| `src/ap/modules/qmk/port/` | STM32H7S 환경에 맞춘 QMK 하드웨어 추상화 포팅 계층입니다. |
| `src/ap/modules/qmk/quantum/` | QMK 핵심 로직과 공용 모듈 집합입니다. |
| `src/ap/modules/qmk/qmk.c` | 펌웨어와 QMK 모듈을 연결하는 초기화 루틴을 제공합니다. |
| `src/ap/modules/qmk/qmk.h` | QMK 모듈의 외부 인터페이스 선언입니다. |

###### `keyboards/` 세부 구성
각 키보드 디렉터리는 공통적으로 다음 파일을 포함합니다.
| 경로 | 설명 |
| --- | --- |
| `config.cmake` | 해당 키보드용 CMake 빌드 옵션을 정의합니다. |
| `config.h` | USB VID/PID, 매트릭스 크기 등 키보드 설정을 정의합니다. |
| `info.json` | QMK JSON 메타데이터로, 키맵 에디터 연동에 사용됩니다. |
| `json/*.json` | VIA/Vial 등과 호환되는 확장 JSON 파일입니다. |
| `keymap.c` | 기본 키맵과 레이어 로직을 정의합니다. |
| `port/*.c, *.h` | LED, VIA, 확장 기능 등 키보드별 포트 코드입니다. |

주요 하위 보드:
- `baram/45k` 및 `baram/60MX-6.25U`: Baram 시리즈를 위한 설정과 포팅 코드.
- `era/sirind/brick60` 및 `era/sirind/brick60h`: Era Sirind 시리즈를 위한 레이아웃과 포팅 코드.

###### `port/` 세부 구성
| 경로 | 설명 |
| --- | --- |
| `kill_switch.*` | 키보드 안전 스위치 처리 로직. |
| `kkuk.*` | 사용자 정의 입력 처리 모듈. |
| `matrix.c` / `matrix.h` | 키 매트릭스 스캔 및 디바운스 로직. |
| `override.c` / `override.h` | QMK 기본 동작을 STM32H7S에 맞게 재정의합니다. |
| `platforms/*.c, *.h` | 타이머, GPIO, EEPROM 등 플랫폼 의존 기능. |
| `protocol/*.c, *.h` | 호스트 통신 프로토콜과 리포트 포맷. |
| `sys_port.*` | 시스템 초기화, 인터럽트 설정 등 핵심 포팅 코드. |
| `ver_port.*` | 버전 정보 제공 및 USB 문자열 처리. |
| `via_hid.*` | VIA 호환 HID 채널 구현. |
| `version.h` | QMK 버전 문자열 정의. |

###### `quantum/` 핵심 구성 요약
QMK의 표준 디렉터리로, 주요 기능은 다음과 같이 분류됩니다.
- **입력 처리**: `action*`, `keycode*`, `keymap_*`, `process_keycode` 등 키 동작 및 레이어 제어.
- **장치 기능**: `mousekey*`, `joystick*`, `painter`, `pointing_device*`, `digitizer*` 등이 포인팅 디바이스 및 기타 주변 기능을 담당합니다.
- **효과 및 피드백**: `rgb_matrix`, `rgblight`, `led*`, `haptic*`, `audio`, `backlight` 등 LED/진동/오디오 피드백 모듈.
- **상태 관리**: `eeconfig*`, `dynamic_keymap*`, `wear_leveling`, `sync_timer*` 등 설정 저장 및 동기화.
- **통신/확장**: `via*`, `virtser`, `raw_hid`, `usb` 관련 코드는 고속 USB 경로와 연계됩니다.
- **유틸리티**: `util.h`, `wait.*`, `timer.*`, `bootmagic`, `command*` 등 다양한 헬퍼.

#### `src/bsp/`
| 경로 | 설명 |
| --- | --- |
| `src/bsp/bsp.c` | 보드 초기화 루틴과 하드웨어 핸들러 등록을 제공합니다. |
| `src/bsp/bsp.h` | BSP 인터페이스 선언입니다. |
| `src/bsp/device/` | 클럭, 핀맵 등 장치별 저수준 설정 파일을 보관합니다. |
| `src/bsp/ldscript/` | 링커 스크립트와 메모리 배치를 정의합니다. |
| `src/bsp/startup/` | 스타트업 어셈블리 및 초기화 코드를 포함합니다. |

#### `src/common/`
| 경로 | 설명 |
| --- | --- |
| `src/common/core/` | 코어 시스템 유틸리티와 공용 함수 모음입니다. |
| `src/common/def.h` | 프로젝트 공통 매크로 정의입니다. |
| `src/common/err_code.h` | 에러 코드 테이블을 제공합니다. |
| `src/common/hw/` | 하드웨어 공통 유틸리티 모듈이 위치합니다. |

#### `src/hw/`
| 경로 | 설명 |
| --- | --- |
| `src/hw/hw.c` | 하드웨어 계층 초기화 및 모듈 등록을 수행합니다. |
| `src/hw/hw.h` | 하드웨어 인터페이스 선언입니다. |
| `src/hw/hw_def.h` | 보드별 하드웨어 정의와 `_DEF_FIRMWARE_VERSION` 매크로를 제공합니다. |
| `src/hw/driver/` | USB, GPIO, I2C 등 실제 하드웨어 드라이버 구현이 모여 있습니다. |

#### `src/lib/`
| 경로 | 설명 |
| --- | --- |
| `src/lib/ST/` | STMicroelectronics에서 제공하는 HAL 및 CMSIS 라이브러리. |
| `src/lib/lib8tion/` | QMK에서 사용하는 lib8tion 유틸리티 라이브러리. |

### `tools/`
| 경로 | 설명 |
| --- | --- |
| `tools/W25Q16JV_BARAM-QMK-H7S.stldr` | Flash Programmer용 ST-LINK 로더 설정. |
| `tools/arm-none-eabi-gcc.cmake` | ARM GCC 툴체인 경로와 옵션을 정의하는 CMake 툴체인 파일. |
| `tools/uf2/uf2conv.py` | 빌드 산출물을 UF2 포맷으로 변환하는 스크립트. |
| `tools/uf2/uf2families.json` | UF2 변환 시 사용되는 보드 식별자 목록. |

## 추가 참고 사항
- USB High-speed PHY만을 사용하므로, Full-speed 또는 외부 PHY 관련 코드는 비활성화되어 있습니다.
- 8,000Hz 인터럽트 스케줄이 유지되지 않으면 입력 레이턴시가 증가하므로, 타이머 및 DMA 관련 변경 시 `src/ap/modules/qmk/port/sys_port.*`와 `src/hw/driver/`를 우선 검토하세요.
- QMK 업스트림 업데이트를 병합할 때는 `quantum/` 디렉터리를 우선 비교하고, 플랫폼 종속 코드는 `port/` 계층에서 조정합니다.
