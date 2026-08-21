#include "era_state_sync.h"

#include <stddef.h>


#define ERA_STATE_SYNC_CMD_GET_KEYBOARD_VALUE  0x02U


static uint32_t s_keymap_revision = 1U;
static uint32_t s_macro_revision  = 1U;
static uint32_t s_config_revision = 1U;


static uint32_t era_state_sync_next(uint32_t value)
{
  value++;
  if (value == 0U)
  {
    value = 1U;
  }
  return value;
}

void era_state_sync_bump_keymap(void)
{
  s_keymap_revision = era_state_sync_next(s_keymap_revision);
}

void era_state_sync_bump_macro(void)
{
  s_macro_revision = era_state_sync_next(s_macro_revision);
}

void era_state_sync_bump_config(void)
{
  s_config_revision = era_state_sync_next(s_config_revision);
}

uint32_t era_state_sync_keymap_revision(void)
{
  return s_keymap_revision;
}

uint32_t era_state_sync_macro_revision(void)
{
  return s_macro_revision;
}

uint32_t era_state_sync_config_revision(void)
{
  return s_config_revision;
}

static void era_state_sync_put_be32(uint8_t *out, uint32_t value)
{
  out[0] = (uint8_t)(value >> 24);
  out[1] = (uint8_t)(value >> 16);
  out[2] = (uint8_t)(value >> 8);
  out[3] = (uint8_t)value;
}

bool era_state_sync_via_command(uint8_t *data, uint8_t length)
{
  uint8_t version;
  uint8_t tag_hi;
  uint8_t tag_lo;
  uint8_t i;

  if (data == NULL || length < 6U)
  {
    return false;
  }
  if (data[0] != ERA_STATE_SYNC_CMD_GET_KEYBOARD_VALUE || data[1] != ERA_STATE_SYNC_KEYBOARD_VALUE)
  {
    return false;
  }

  version = data[2];
  tag_hi  = data[4];
  tag_lo  = data[5];

  for (i = 2U; i < 32U && i < length; i++)
  {
    data[i] = 0U;
  }

  data[0] = ERA_STATE_SYNC_CMD_GET_KEYBOARD_VALUE;
  data[1] = ERA_STATE_SYNC_KEYBOARD_VALUE;
  data[2] = ERA_STATE_SYNC_ENVELOPE_VERSION;
  data[4] = tag_hi;
  data[5] = tag_lo;

  if (version != ERA_STATE_SYNC_ENVELOPE_VERSION)
  {
    data[3] = ERA_STATE_SYNC_STATUS_UNSUPPORTED_VERSION;
    return true;  // V260821R1: 응답은 via_hid_task enqueue. raw_hid_send를 부르지 않는다.
  }

  data[3] = ERA_STATE_SYNC_STATUS_OK;
  data[6] = ERA_STATE_SYNC_DOMAIN_MASK_INITIAL;
  era_state_sync_put_be32(&data[8], s_keymap_revision);
  era_state_sync_put_be32(&data[12], s_macro_revision);
  era_state_sync_put_be32(&data[16], s_config_revision);
  return true;
}
