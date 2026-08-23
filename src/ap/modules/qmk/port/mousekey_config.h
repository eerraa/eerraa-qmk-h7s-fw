#pragma once


#include <stdbool.h>
#include <stdint.h>


// V260823R1: VIA MOUSE 페이지.
// 자체 런타임 상태를 두지 않는다. EEPROM 이미지를 하나 들고 있고, init / VIA set 시점에만
// QMK mousekey 엔진의 런타임 변수(mk_*)에 그대로 반영한다.
// 페이지가 노출하는 단위는 사용자가 보는 값(px, ms)이고, 엔진이 저장하는 값(비율, 이벤트 수)으로의
// 환산은 이 파일 안에서만 일어난다.

void mousekey_config_init(void);
void mousekey_config_storage_apply_defaults(void);
void mousekey_config_storage_flush(bool force);
bool mousekey_config_handle_via_command(uint8_t *data, uint8_t length);
