#pragma once

#include <stdbool.h>


bool eepromAutoFactoryResetCheck(void);           // V251112R3: AUTO_FACTORY_RESET 센티넬 헬퍼
bool eepromScheduleDeferredFactoryReset(void);    // V251112R4: VIA/AUTO 공용 초기화 예약
