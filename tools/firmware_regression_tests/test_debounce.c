#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "debounce.h"
#include "debounce_runtime.h"
static uint32_t clock_ms;
uint16_t timer_read_fast(void) { return (uint16_t)clock_ms; }
static void profile(unsigned type, uint8_t pre, uint8_t post)
{
  debounce_runtime_config_t c = {.type = (debounce_runtime_type_t)type, .pre_ms = pre, .post_ms = post};
  assert(debounce_runtime_apply_config(&c));
}
static void prepare(unsigned type, matrix_row_t *raw, matrix_row_t *cooked)
{
  debounce_free();
  clock_ms = 0U;
  memset(raw, 0, MATRIX_ROWS * sizeof(*raw));
  memset(cooked, 0, MATRIX_ROWS * sizeof(*cooked));
  profile(type, 5U, 5U);
  debounce_init(MATRIX_ROWS);
}
int main(void)
{
  matrix_row_t raw[MATRIX_ROWS], cooked[MATRIX_ROWS];
  prepare(0U, raw, cooked);
  raw[0] = 1U;
  assert(!debounce(raw, cooked, MATRIX_ROWS, true));
  clock_ms = 4U;
  assert(!debounce(raw, cooked, MATRIX_ROWS, false));
  profile(0U, 5U, 5U);
  clock_ms = 5U;
  assert(debounce(raw, cooked, MATRIX_ROWS, false) && cooked[0] == 1U);
  for (unsigned old = 0; old < 3U; old++) {
    for (unsigned next = 0; next < 3U; next++) {
      prepare(old, raw, cooked);
      raw[0] = 1U;
      for (clock_ms = 0; clock_ms < 20U; clock_ms++) debounce(raw, cooked, MATRIX_ROWS, clock_ms == 0U);
      assert(cooked[0] == 1U);
      raw[0] = 0U;
      debounce(raw, cooked, MATRIX_ROWS, true);
      clock_ms++;
      profile(next, 7U, 7U);
      for (unsigned i = 0; i < 30U; i++, clock_ms++) debounce(raw, cooked, MATRIX_ROWS, false);
      assert(cooked[0] == 0U);  // no new raw edge after configuration switch
      raw[0] = 2U;
      debounce(raw, cooked, MATRIX_ROWS, true);
      profile(old, 3U, 3U);
      for (unsigned i = 0; i < 30U; i++, clock_ms++) debounce(raw, cooked, MATRIX_ROWS, false);
      assert(cooked[0] == 2U);
    }
  }
  prepare(0U, raw, cooked);
  raw[0] = 1U;
  debounce(raw, cooked, MATRIX_ROWS, true);
  debounce_runtime_config_t invalid = {.type = (debounce_runtime_type_t)99, .pre_ms = 1, .post_ms = 1};
  assert(!debounce_runtime_apply_config(&invalid));
  assert(!debounce_runtime_apply_config(NULL));
  clock_ms = 1U;
  assert(!debounce(raw, cooked, MATRIX_ROWS, false) && cooked[0] == 0U);
  clock_ms = 5U;
  assert(debounce(raw, cooked, MATRIX_ROWS, false) && cooked[0] == 1U);
  struct { matrix_row_t row[2]; uint32_t canary; } r = {{0,0}, 0x12345678}, c = {{1,1}, 0x76543210};
  for (unsigned i = 0; i < 10U; i++, clock_ms++) debounce(r.row, c.row, 2U, false);
  assert(c.row[0] == 0 && c.row[1] == 0 && c.canary == 0x76543210 && r.canary == 0x12345678);
  assert(!debounce(NULL, cooked, MATRIX_ROWS, true));
  assert(!debounce(raw, cooked, 0U, true));
  assert(!debounce(raw, cooked, MATRIX_ROWS + 1U, true));
  prepare(0U, raw, cooked);
  clock_ms = 65532U;
  raw[1] = 0x8000U;
  debounce(raw, cooked, MATRIX_ROWS, true);
  clock_ms += 5U;
  assert(debounce(raw, cooked, MATRIX_ROWS, false) && cooked[1] == 0x8000U);
  profile(2U, 255U, 0U);
  assert(debounce_runtime_press_delay() == 127U && debounce_runtime_release_delay() == 1U);
  puts("PASS: debounce same-config deadline, 9 mode transitions, pending press/release, invalid config, row canaries, timer wrap");
  return 0;
}
