# HID 폴링 타이밍 분석 (V250928R3)

## 1. 현상 정리
- `matrix info on` CLI에서 `hid_info.time_max`가 간헐적으로 250us 이상으로 상승한다는 제보를 확인했다.
- 로그를 보면 평균적인 `time_max`는 124~125us(고속 USB의 1 마이크로프레임)이나, 키 입력이 폭증하면 250us, 800us 이상으로 튀는 샘플이 섞인다.
- 동일한 구간에서 `Poll Rate` 표기는 80~120Hz 수준으로, 실제로는 이벤트가 있을 때만 리포트를 전송하고 있음을 의미한다.

## 2. 측정 경로 추적
1. 키 이벤트가 발생하면 `host_keyboard_send()`가 `usbHidSendReport()`를 호출해 키보드 IN 엔드포인트를 전송 큐에 올린다. 성공 시 전송 시간 기준점을 `rate_time_pre`에 기록하고 샘플 요청 플래그를 세팅한다.【F:src/ap/modules/qmk/port/host.c†L83-L104】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1159-L1188】
2. 전송이 바쁜 경우에는 `report_q` 큐에 적재되며, `HAL_TIM_PWM_PulseFinishedCallback()`이 주기적으로 큐를 비우면서 다음 전송을 걸고 동일하게 기준 시간을 갱신한다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1616-L1645】
3. 호스트가 IN 토큰을 보내 전송이 완료되면 `USBD_HID_DataIn()`이 호출되어 상태를 IDLE로 돌리고, 여기에서 `usbHidMeasureRateTime()`을 실행해 `micros() - rate_time_pre` 값을 누적/최대값으로 저장한다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1048-L1072】【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1453-L1483】
4. 각 8k 샘플 윈도우마다 `usbHidMeasurePollRate()`가 누적된 최소/최대 값을 CLI에 노출하는 `usb_hid_rate_info_t` 구조체로 옮긴다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1216-L1238】【F:src/ap/modules/qmk/port/matrix.c†L120-L147】

## 3. 원인 분석
- `rate_time_us`는 “전송을 건 시점”과 “호스트가 해당 전송을 수락한 시점” 사이의 실측 지연이다.
- 큐에 전송이 밀리면 각 전송은 이전 전송이 완료될 때까지 기다리므로, 호스트가 준비된 데이터 없이 IN 토큰을 보내면 한 번 더 마이크로프레임을 소모한다. 이 경우 `rate_time_us`가 정확히 125us 배수로 증가한다.
- 로그에서 관찰한 250us, 835us 값은 각각 2프레임(HS 4kHz), 7~8프레임(HS≈1kHz) 지연에 해당한다. 이는 펌웨어가 늦게 응답했다기보다는 큐가 한 프레임 이상 비어 있어 호스트가 한번 더 IN을 재시도한 상황으로 해석된다.
- DMA 기반 매트릭스 최적화(R5~R2)는 전송 빈도를 높였지만, 전송 파이프라인은 기존과 동일하게 동작하므로 `time_max` 스파이크는 USB 전송 재시도에 기인한 측정값으로 판단된다.

## 4. 개선안 제안 (V250928R3)
- **목표**: `time_max`의 급상승이 실제 USB 폴링 지연인지, 큐 공백으로 인한 재시도인지 구분할 수 있는 추가 진단 정보를 제공한다.
- **아이디어**:
  - `usbHidMeasureRateTime()`에서 예상 폴링 간격(HS 125us/FS 1000us)을 계산하고, 측정값이 이를 초과할 때 초과분(`excess_us`)과 당시 큐 길이를 별도 카운터로 누적한다.
  - CLI(`matrix info` 혹은 `usb hid rate`)에서 `time_max`와 함께 `excess_max`, `queued_max`를 표시해, 순수한 전송 지연과 큐 백로그를 분리한다.
  - 구현 시 ISR 경로에 분기와 1~2회의 비교 연산이 추가되므로, 8kHz 환경에서도 부담이 크지 않다.

## 5. 오리지널 코드 의도와 비교
- 기존 구현은 “호스트가 데이터를 소비하는 데 걸린 시간”을 그대로 기록해 USB 안정성 모니터의 입력으로 활용한다. 값이 커지면 바로 `usbHidMonitorSof()`가 다운그레이드 후보를 판단하도록 설계되어 있다.【F:src/hw/driver/usb/usb_hid/usbd_hid.c†L1216-L1250】
- 제안안은 관측치를 분류해 **원인 분석을 쉽게** 하려는 것이며, 기존의 최대치 기록 자체를 바꾸지는 않는다.

## 6. 부작용 검토 및 결정
- ISR 안에 비교/누적을 추가하면 마이크로초 단위 비용이 늘어나지만, 계산량이 작고 레지스터 접근만 사용하므로 USB 타이밍에는 영향을 주지 않는다.
- 반대로 `time_max` 자체를 보정하거나 필터링하면 USB 안정성 모니터가 실제 지연을 감지하지 못할 위험이 있다. 따라서 **기존 측정값은 유지**하고, 진단용 카운터를 추가하는 방향이 안전하다.
- 결론적으로 현 시점에서는 **코드를 수정하지 않고** 현상을 모니터링하되, V250928R3 아이디어를 바탕으로 별도 진단 카운터를 구현할 준비를 진행한다. 이는 `time_max`가 의미 있는 경고 신호라는 원래 의도를 훼손하지 않으면서도 후속 분석을 쉽게 만든다.
