#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define PACKED __attribute__((packed))
#define EECONFIG_USER_KILL_SWITCH_LR 0U
#define EECONFIG_USER_KILL_SWITCH_UD 8U
#define EECONFIG_USER_KKUK 24U

#define id_custom_set_value 1U
#define id_custom_get_value 2U
#define id_custom_save 3U
#define id_unhandled 0xFFU

#define KC_NO 0x00U
#define KC_A 0x04U
#define KC_D 0x07U
#define KC_F 0x09U
#define KC_W 0x1AU
#define KC_EXSEL 0xA4U
#define KC_LEFT_CTRL 0xE0U
#define KC_RIGHT_CTRL 0xE4U
#define IS_BASIC_KEYCODE(code) ((uint16_t)(code) >= KC_A && (uint16_t)(code) <= KC_EXSEL)
#define IS_MODIFIER_KEYCODE(code) ((uint16_t)(code) >= KC_LEFT_CTRL && (uint16_t)(code) <= 0xE7U)
#define MOD_BIT(code) ((uint8_t)(1U << ((uint8_t)(code) & 0x07U)))

typedef struct {
    struct {
        bool pressed;
    } event;
} keyrecord_t;

typedef struct {
    uint8_t mods;
    uint8_t reserved;
    uint8_t keys[20];
} report_keyboard_t;

extern report_keyboard_t *keyboard_report;
uint32_t millis(void);
void clear_keys(void);
void send_keyboard_report(void);
void add_key(uint8_t key);
void del_key(uint8_t key);
void add_mods(uint8_t mods);
void del_mods(uint8_t mods);
void logPrintf(const char *fmt, ...);
void era_state_sync_bump_config(void);
bool kill_switch_is_use(uint16_t keycode);

#define EECONFIG_DEBOUNCE_HELPER(name, offset, config) \
    static inline void eeconfig_init_##name(void) { (void)(offset); (void)sizeof(config); } \
    static inline void eeconfig_flush_##name(bool force) { (void)force; }
