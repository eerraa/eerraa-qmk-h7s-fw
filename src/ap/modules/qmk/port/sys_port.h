#pragma once

#include "quantum.h"



void via_qmk_system(uint8_t *data, uint8_t length);
void via_qmk_system_task(void);  // V260901R1: EEPROM CLEAN 확인 비트가 10초를 넘기면 스스로 풀린다
