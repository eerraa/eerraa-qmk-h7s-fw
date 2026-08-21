#include "platforms/eeprom.h"
#include "timer.h"
#include "wait.h"
#include "action.h"

#include <string.h>

#define HOST_EEPROM_SIZE 4096U

static uint8_t  s_eeprom[HOST_EEPROM_SIZE];
static uint16_t s_timer;

static uintptr_t host_eeprom_index(const void *addr) {
    return (uintptr_t)addr;
}

void eeprom_read_block(void *buf, const void *addr, uint32_t len) {
    uintptr_t off = host_eeprom_index(addr);
    if (off + len > HOST_EEPROM_SIZE) {
        memset(buf, 0, len);
        return;
    }
    memcpy(buf, &s_eeprom[off], len);
}

void eeprom_update_block(const void *buf, void *addr, size_t len) {
    uintptr_t off = host_eeprom_index(addr);
    if (off + len > HOST_EEPROM_SIZE) {
        return;
    }
    memcpy(&s_eeprom[off], buf, len);
}

uint8_t eeprom_read_byte(const uint8_t *addr) {
    uintptr_t off = host_eeprom_index(addr);
    if (off >= HOST_EEPROM_SIZE) {
        return 0;
    }
    return s_eeprom[off];
}

void eeprom_update_byte(uint8_t *addr, uint8_t value) {
    uintptr_t off = host_eeprom_index(addr);
    if (off >= HOST_EEPROM_SIZE) {
        return;
    }
    s_eeprom[off] = value;
}

uint16_t timer_read(void) {
    return s_timer++;
}

uint16_t timer_elapsed(uint16_t last) {
    return (uint16_t)(s_timer - last);
}

void wait_ms(uint16_t ms) {
    (void)ms;
}

action_t action_for_keycode(uint16_t keycode) {
    action_t action;
    (void)keycode;
    memset(&action, 0, sizeof(action));
    return action;
}

void process_action(keyrecord_t *record, action_t action) {
    (void)record;
    (void)action;
}
