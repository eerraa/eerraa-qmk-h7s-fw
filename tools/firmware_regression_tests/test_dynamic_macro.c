#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR 256U
#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE 32U
#define DYNAMIC_KEYMAP_MACRO_COUNT 16U
#define DYNAMIC_KEYMAP_MACRO_DELAY 10U

static uint8_t storage[DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE];
static uint8_t output[256];
static unsigned reads, output_length, calls;

static uint8_t eeprom_read_byte(const uint8_t *address)
{
    uintptr_t offset = (uintptr_t)address;
    assert(offset >= DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR);
    offset -= DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR;
    /* Reject a bad read before touching memory or allowing a long-running loop. */
    assert(offset < sizeof(storage));
    assert(++reads <= sizeof(storage) * 2U + 1U);
    return storage[offset];
}

static void send_string_with_delay(const char *data, uint8_t delay)
{
    assert(delay == DYNAMIC_KEYMAP_MACRO_DELAY);
    size_t length = strlen(data);
    assert(length < 8U && output_length + length <= sizeof(output));
    memcpy(output + output_length, data, length);
    output_length += length;
    calls++;
}

#include "dynamic_macro.inc"

static void reset_fixture(void)
{
    memset(storage, 0, sizeof(storage));
    reads = output_length = calls = 0U;
}

int main(void)
{
    reset_fixture();
    memset(storage, 'a', sizeof(storage) - 1U);
    /* Macro zero occupies the whole buffer. There is no macro one to read. */
    dynamic_keymap_macro_send(1U);
    assert(calls == 0U && output_length == 0U);
    reads = 0U;
    dynamic_keymap_macro_send(0U);
    assert(output_length == sizeof(storage) - 1U);

    for (unsigned id = 0U; id < DYNAMIC_KEYMAP_MACRO_COUNT; id++) {
        reset_fixture();
        dynamic_keymap_macro_send((uint8_t)id);
        assert(calls == 0U);
    }
    reset_fixture();
    dynamic_keymap_macro_send(DYNAMIC_KEYMAP_MACRO_COUNT);
    assert(reads == 0U);

    reset_fixture();
    const uint8_t macros[] = {'x', 0U, SS_QMK_PREFIX, SS_TAP_CODE, 4U,
                            SS_QMK_PREFIX, SS_DOWN_CODE, 5U,
                            SS_QMK_PREFIX, SS_UP_CODE, 5U,
                            SS_QMK_PREFIX, SS_DELAY_CODE, '1', '0', '|', 0U};
    memcpy(storage, macros, sizeof(macros));
    dynamic_keymap_macro_send(1U);
    assert(calls == 4U && output_length == sizeof(macros) - 3U);
    assert(memcmp(output, macros + 2U, output_length) == 0);

    /* Truncated commands still stop at the sentinel without reading past it. */
    const uint8_t commands[] = {SS_TAP_CODE, SS_DOWN_CODE, SS_UP_CODE, SS_DELAY_CODE};
    for (unsigned command = 0U; command < sizeof(commands); command++) {
        for (unsigned remaining = 1U; remaining <= 6U; remaining++) {
            reset_fixture();
            memset(storage, 'a', sizeof(storage) - 1U);
            unsigned start = sizeof(storage) - 1U - remaining;
            storage[start] = SS_QMK_PREFIX;
            if (remaining > 1U) storage[start + 1U] = commands[command];
            dynamic_keymap_macro_send(0U);
        }
    }

    reset_fixture();
    storage[sizeof(storage) - 1U] = 'a';
    dynamic_keymap_macro_send(0U);
    assert(reads == 1U && calls == 0U);
    puts("PASS: production macro reader: missing macro, final sentinel, empty/valid/truncated commands and interrupted-write guard");
    return 0;
}
