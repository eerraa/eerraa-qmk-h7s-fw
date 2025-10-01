# USB 불안정성 모니터 기능 요약 (DEV_MONITOR)

## V250923R1 – 부트 모드 지속화 토대 마련
- `src/ap/modules/qmk/port/port.h`: QMK 사용자 EEPROM 공간에 `EECONFIG_USER_BOOTMODE` 슬롯을 확보하여 USB 폴링 모드 선택값을 영구 저장합니다.【F:src/ap/modules/qmk/port/port.h†L16-L21】
- `src/hw/hw.c`: 부팅 시 QMK EEPROM 이미지를 미리 동기화한 뒤 저장된 부트 모드를 로드해 초기 USB 설정에 반영합니다.【F:src/hw/hw.c†L53-L76】
- `src/hw/driver/usb/usb.h`, `src/hw/driver/usb/usb.c`: `UsbBootMode_t` 열거형과 로드/저장/조회 API, CLI `boot` 명령을 추가하여 폴링 모드 변경을 중앙집중식으로 관리합니다.【F:src/hw/driver/usb/usb.h†L61-L99】【F:src/hw/driver/usb/usb.c†L52-L175】
- `src/hw/driver/usb/usb_hid/usbd_hid.c`: HID 및 VIA/EXK 엔드포인트의 `bInterval`을 현재 부트 모드에 맞춰 동적으로 설정하여 고속/저속 전환 시 리포트 타이밍을 유지합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L470-L603】
- `src/hw/driver/usb/usbd_conf.c`: HS PHY가 Full-Speed 모드로 강제되는 경우에도 1kHz 폴링을 보장하도록 리셋/초기화 경로에서 속도 구성을 조정합니다.【F:src/hw/driver/usb/usbd_conf.c†L200-L220】【F:src/hw/driver/usb/usbd_conf.c†L341-L368】

## V250923R2 – 복합 HID 전송 용량 보강
- `src/hw/driver/usb/usb_cmp/usbd_cmp.c`: Full-Speed에서도 64바이트 패킷과 고속 시 3트랜잭션 버스트를 유지하여 복합 HID 환경에서의 입력 손실을 예방합니다.【F:src/hw/driver/usb/usb_cmp/usbd_cmp.c†L831-L899】

## V250924R1 – 폴링 속도 계측 정합
- `src/hw/driver/usb/usb_hid/usbd_hid.c`: 현재 부트 모드가 Full-Speed인지 여부를 확인해 SOF 샘플 윈도우(1,000 vs 8,000 프레임)를 자동 조정, 각 속도에서 일관된 통계 집계를 가능하게 합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1216-L1239】

## V250924R2 – SOF 간격 감시와 다운그레이드 큐 도입
- `src/hw/driver/usb/usb_hid/usbd_hid.c`: `usbHidMonitorSof()`를 통해 SOF 간격을 마이크로초 단위로 추적하고, 누락된 마이크로프레임 수에 따라 점수를 가산(최대 3점/사건)하여 불안정성을 정량화합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1241-L1422】
- `src/hw/driver/usb/usb_hid/usbd_hid.c`: 현행 부트 모드보다 한 단계 낮은 목표를 계산하는 `usbHidResolveDowngradeTarget()`으로 단계적 폴링 저하 전략을 정의합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1241-L1256】
- `src/hw/driver/usb/usb.c`: 비동기 다운그레이드 큐(`usb_boot_mode_request_t`)를 추가하고, SOF 모니터가 전달한 간격 정보·대상 모드·타임아웃을 저장하여 확인 지연(2초)을 거친 뒤 확정적으로 폴링 모드를 낮춥니다.【F:src/hw/driver/usb/usb.c†L54-L216】
- `src/ap/ap.c`: 메인 루프에 `usbProcess()`를 편성하여 큐 처리와 폴링 모드 전환을 주기적으로 실행합니다.【F:src/ap/ap.c†L20-L38】

## V250924R3 – 워밍업·홀드오프 기반의 안정화 필터링
- `src/hw/driver/usb/usb_hid/usbd_hid.c`: 구성 직후 750ms 홀드오프, 2048(HS)/128(FS) 프레임 워밍업, 재개·실패 지연 타이머를 도입해 초기 연결 진동이나 일시중지에서 복귀 시의 오탐을 줄입니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L477-L509】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1258-L1387】
- `src/hw/driver/usb/usb.c`: `usbProcess()`가 큐 상태를 검사해 ARM/COMMIT 단계별 로그를 출력하고, 타임아웃 시 자동 초기화하여 불필요한 재부팅을 방지합니다.【F:src/hw/driver/usb/usb.c†L218-L266】

## V250924R4 – 속도별 파라미터 캐싱과 임계치 최적화
- `src/hw/hw_def.h`: 펌웨어 버전을 `V250924R4`로 갱신하여 속도별 캐싱 최적화 릴리스를 식별합니다.【F:src/hw/hw_def.h†L1-L15】
- `src/hw/driver/usb/usb_hid/usbd_hid.c`: `usbHidSofMonitorApplySpeedParams()`에서 HS/FS 별 기대 SOF 주기, 정상 범위, 감쇠 주기, 다운그레이드 임계점을 사전 계산해 ISR에서의 분기와 연산 부담을 줄입니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L511-L539】
- `src/hw/driver/usb/usb_hid/usbd_hid.c`: 속도 변경·재개 시 캐시를 갱신하고, 점수 감쇠·확인 지연을 캐시된 파라미터로 수행해 다운그레이드 판단의 일관성을 확보합니다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1258-L1450】
