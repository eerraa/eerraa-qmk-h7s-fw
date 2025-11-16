#ifndef HW_CAPS_EEPROM_H_
#define HW_CAPS_EEPROM_H_


// ---------------------------------------------------------------------------
// [Caps Dependencies] V251114R3
//   - 사용처: src/hw/driver/eeprom/*.c, src/ap/modules/qmk/keyboards/*/config.h 자동 초기화 토글
//   - 비고  : AUTO_FACTORY_RESET_ENABLE/AUTO_FACTORY_RESET_COOKIE와 같은 빌드 가드와 짝지어 사용
// ---------------------------------------------------------------------------
#ifndef _USE_HW_EEPROM
#define _USE_HW_EEPROM
#endif

#ifndef EEPROM_CHIP_ZD24C128
#define EEPROM_CHIP_ZD24C128
#endif


#endif
