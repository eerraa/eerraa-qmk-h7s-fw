/* EEPROM image and unrelated VIA commands are adapters; keymap mutations,
 * VIA dispatch, matrix, tapping, quantum, layers and reports are production. */
#define DYNAMIC_KEYMAP_LAYER_COUNT 4
#define DYNAMIC_KEYMAP_EEPROM_ADDR 64
#define VIA_PROTOCOL_VERSION 12
#define VIA_FIRMWARE_VERSION 0
static uint8_t fixture_keymap_image[64 + 4 * MATRIX_ROWS * MATRIX_COLS * 2];
static uint16_t fixture_reset_keycode;
static unsigned fixture_via_replies;
static uint8_t eeprom_read_byte(const void *p) { assert((uintptr_t)p < sizeof(fixture_keymap_image)); return fixture_keymap_image[(uintptr_t)p]; }
static void eeprom_update_byte(void *p, uint8_t value) { assert((uintptr_t)p < sizeof(fixture_keymap_image)); fixture_keymap_image[(uintptr_t)p] = value; }
static uint16_t keycode_at_keymap_location_raw(uint8_t layer, uint8_t row, uint8_t col) { (void)layer; (void)row; (void)col; return fixture_reset_keycode; }
static bool via_command_kb(uint8_t *data, uint8_t length) { (void)data; (void)length; return false; }
static uint32_t via_get_layout_options(void) { return 0; }
static void via_set_layout_options(uint32_t value) { (void)value; }
static void via_set_device_indication(uint8_t value) { (void)value; }
static void via_custom_value_command(uint8_t *data, uint8_t length) { (void)data; (void)length; }
static void raw_hid_send(uint8_t *data, uint8_t length) { (void)data; assert(length == 32); ++fixture_via_replies; }
static uint8_t dynamic_keymap_macro_get_count(void) { return 0; }
static uint16_t dynamic_keymap_macro_get_buffer_size(void) { return 0; }
static void dynamic_keymap_macro_get_buffer(uint16_t offset, uint16_t size, uint8_t *data) { (void)offset; (void)size; (void)data; }
static bool dynamic_keymap_macro_set_buffer_checked(uint16_t offset, uint16_t size, uint8_t *data) { (void)offset; (void)size; (void)data; return true; }
static bool dynamic_keymap_macro_reset_checked(void) { return true; }
static bool era_state_sync_via_command(uint8_t *data, uint8_t length) { (void)data; (void)length; return false; }
