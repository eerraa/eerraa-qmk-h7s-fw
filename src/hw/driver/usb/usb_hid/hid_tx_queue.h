#pragma once
#include <stdbool.h>
#include <stdint.h>

// V260909R1: 호출자가 IRQ 직렬화를 소유한다. active.data는 완료 전까지 불변이다.
#define HID_TX_PACKET_BYTES 32U
typedef struct {
  uint32_t request_us;
  uint16_t diagnostic_session;
  uint8_t length;
  uint8_t reserved;
  uint8_t data[HID_TX_PACKET_BYTES];
} hid_tx_packet_t;

typedef struct {
  hid_tx_packet_t *slots;
  uint16_t capacity;
  uint16_t head;
  uint16_t count;
  bool busy;
  hid_tx_packet_t active;
} hid_tx_queue_t;

typedef bool (*hid_tx_arm_t)(void *context, const hid_tx_packet_t *packet);
void hidTxInit(hid_tx_queue_t *q, hid_tx_packet_t *slots, uint16_t capacity);
bool hidTxPush(hid_tx_queue_t *q, const hid_tx_packet_t *packet);
bool hidTxKick(hid_tx_queue_t *q, hid_tx_arm_t arm, void *context);
bool hidTxComplete(hid_tx_queue_t *q);
void hidTxDiscardPending(hid_tx_queue_t *q);
