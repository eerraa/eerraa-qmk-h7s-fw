#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hid_tx_queue.h"

typedef struct { bool fail; const hid_tx_packet_t *pointer; uint32_t id; } arm_state_t;
static bool arm(void *ctx, const hid_tx_packet_t *packet)
{
  arm_state_t *s = ctx;
  if (s->fail) return false;
  s->pointer = packet;
  memcpy(&s->id, packet->data, sizeof(s->id));
  return true;
}
static uint32_t random_state = 0x7A116EEDU;
static uint32_t random_next(void) { random_state = random_state * 1664525U + 1013904223U; return random_state; }
int main(void)
{
  hid_tx_queue_t q;
  hid_tx_packet_t slots[5];
  arm_state_t state = {0};
  uint32_t model[5], head = 0U, count = 0U, sequence = 0U, active_id = 0U, accepted = 0U, delivered = 0U;
  bool active = false;
  hidTxInit(&q, slots, 5U);
  assert(!hidTxKick(&q, arm, &state));
  assert(!hidTxComplete(&q));
  for (uint32_t iteration = 0; iteration < 100000U; iteration++) {
    uint32_t action = random_next() % 4U;
    if (action < 2U) {
      hid_tx_packet_t p = {.length = 4U};
      sequence++;
      memcpy(p.data, &sequence, 4U);
      bool ok = hidTxPush(&q, &p);
      assert(ok == (count < 5U));
      if (ok) { model[(head + count) % 5U] = sequence; count++; accepted++; }
    } else if (action == 2U) {
      state.fail = (random_next() % 5U) == 0U;
      bool expected = !active && count != 0U && !state.fail;
      assert(hidTxKick(&q, arm, &state) == expected);
      if (expected) { active_id = model[head]; head = (head + 1U) % 5U; count--; active = true; }
    } else if (active) {
      uint32_t payload;
      memcpy(&payload, state.pointer->data, 4U);
      assert(payload == active_id);  // delayed HAL pointer read must see the original active report
      assert(hidTxComplete(&q));
      active = false;
      delivered++;
    }
    assert(q.count == count && q.busy == active);
    if (active) assert(memcmp(state.pointer->data, &active_id, 4U) == 0);
  }
  assert(accepted == delivered + count + (active ? 1U : 0U));
  if (!active) { state.fail = false; if (count) { assert(hidTxKick(&q, arm, &state)); active_id = model[head]; active = true; } }
  hidTxDiscardPending(&q);
  assert(q.count == 0U);
  assert(q.busy == active);
  if (active) assert(memcmp(q.active.data, &active_id, 4U) == 0);
  hid_tx_packet_t invalid = {.length = 33U};
  assert(!hidTxPush(&q, &invalid));
  assert(!hidTxPush(NULL, &invalid));
  puts("PASS: HID FIFO 100000 deterministic operations, failed-arm retry, immutable active buffer, wrap/full/discard");
  return 0;
}
