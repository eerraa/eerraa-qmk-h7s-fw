#include "tapdance.h"


#ifdef TAPDANCE_ENABLE

#include <string.h>
#include "port.h"
#include "quantum.h"
#include "process_keycode/process_tap_dance.h"
#include "wait.h"


#define TAPDANCE_SIGNATURE        (0x4E414454UL)   // "TDAN" // V251124R8: Tap Dance EEPROM 시그니처
#define TAPDANCE_VERSION          (1U)
#define TAPDANCE_TERM_MIN_MS      (100U)
#define TAPDANCE_TERM_MAX_MS      (500U)
#define TAPDANCE_TERM_UNIT_MS     (10U)
#define TAPDANCE_TERM_STEP_MS     (20U)
#define TAPDANCE_TERM_DEFAULT_MS  (200U)
#define TAPDANCE_VALUE_STRIDE     (5U)
#define TAPDANCE_VALUE_MAX_ID     (TAPDANCE_SLOT_COUNT * TAPDANCE_VALUE_STRIDE)
#define TAPDANCE_FIELD_TERM       (4U)

enum
{
  SINGLE_TAP = 1,
  SINGLE_HOLD,
  DOUBLE_TAP,
  DOUBLE_HOLD,
  DOUBLE_SINGLE_TAP,
  MORE_TAPS
};


typedef struct PACKED
{
  uint16_t actions[TAPDANCE_ACTION_COUNT];
  uint16_t term_ms;
} tapdance_slot_storage_t;

typedef struct PACKED
{
  tapdance_slot_storage_t slots[TAPDANCE_SLOT_COUNT];
  uint8_t                 version;
  uint8_t                 reserved[3];
  uint32_t                signature;
} tapdance_storage_t;

typedef struct
{
  uint16_t actions[TAPDANCE_ACTION_COUNT];
  uint16_t term_ms;
} tapdance_slot_state_t;

typedef enum
{
  TAPDANCE_ACTION_NONE = 0,
  TAPDANCE_ACTION_TAP,
  TAPDANCE_ACTION_HOLD,
  TAPDANCE_ACTION_DOUBLE_TAP,
  TAPDANCE_ACTION_TAP_HOLD,
} tapdance_action_type_t;

typedef struct
{
  uint8_t slot_index;
} tapdance_user_data_t;

typedef struct
{
  tapdance_action_type_t active_action;
  uint16_t               active_keycode;
} tapdance_runtime_state_t;

typedef struct
{
  uint16_t on_tap;
  uint16_t on_hold;
  uint16_t on_double_tap;
  uint16_t on_tap_hold;
  uint16_t term_ms;
} tapdance_entry_t;

static void tapdance_on_each_tap(tap_dance_state_t *state, void *user_data);
static void tapdance_on_dance_finished(tap_dance_state_t *state, void *user_data);
static void tapdance_on_reset(tap_dance_state_t *state, void *user_data);

static tapdance_storage_t       tapdance_storage = {0};
static tapdance_slot_state_t    tapdance_state[TAPDANCE_SLOT_COUNT];
static tapdance_runtime_state_t tapdance_runtime[TAPDANCE_SLOT_COUNT];
static uint8_t                  tapdance_dance_state[TAPDANCE_SLOT_COUNT] = {0};
static tapdance_user_data_t     tapdance_user_data[TAPDANCE_SLOT_COUNT] =
{
  { .slot_index = 0 },
  { .slot_index = 1 },
  { .slot_index = 2 },
  { .slot_index = 3 },
  { .slot_index = 4 },
  { .slot_index = 5 },
  { .slot_index = 6 },
  { .slot_index = 7 },
};

tap_dance_action_t tap_dance_actions[TAPDANCE_SLOT_COUNT] =
{
  { .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, NULL}, .user_data = &tapdance_user_data[0] },
  { .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, NULL}, .user_data = &tapdance_user_data[1] },
  { .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, NULL}, .user_data = &tapdance_user_data[2] },
  { .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, NULL}, .user_data = &tapdance_user_data[3] },
  { .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, NULL}, .user_data = &tapdance_user_data[4] },
  { .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, NULL}, .user_data = &tapdance_user_data[5] },
  { .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, NULL}, .user_data = &tapdance_user_data[6] },
  { .fn = {tapdance_on_each_tap, tapdance_on_dance_finished, tapdance_on_reset, NULL}, .user_data = &tapdance_user_data[7] },
};

_Static_assert(sizeof(tapdance_slot_storage_t) == 10, "EECONFIG out of spec.");  // V251124R8: 슬롯 크기 고정
_Static_assert(sizeof(tapdance_storage_t) == 88, "EECONFIG out of spec.");        // V251124R8: USER 데이터 슬롯 크기 고정


EECONFIG_DEBOUNCE_HELPER(tapdance, EECONFIG_USER_TAPDANCE, tapdance_storage);


static uint8_t               tapdance_slot_index(uint8_t value_id);
static uint8_t               tapdance_field_index(uint8_t value_id);
static bool                  tapdance_is_storage_valid(const tapdance_storage_t *storage);
static uint16_t              tapdance_normalize_term(uint16_t term_ms);
static void                  tapdance_apply_defaults_locked(void);
static void                  tapdance_sync_state_from_storage(void);
static bool                  tapdance_keycode_is_valid(uint16_t keycode);
static bool                  tapdance_set_value(uint8_t value_id, uint8_t *value_data, uint8_t length);
static void                  tapdance_get_value(uint8_t value_id, uint8_t *value_data, uint8_t length);
static void                  tapdance_load_entry(uint8_t slot_index, tapdance_entry_t *entry);
static uint8_t               tapdance_step(const tap_dance_state_t *state);


void tapdance_init(void)
{
  memset(tapdance_runtime, 0, sizeof(tapdance_runtime));
  eeconfig_init_tapdance();

  if (tapdance_is_storage_valid(&tapdance_storage) == false)
  {
    tapdance_apply_defaults_locked();                      // V251124R8: 손상 슬롯 기본값 복원
    eeconfig_flush_tapdance(true);
  }

  tapdance_sync_state_from_storage();
}

bool tapdance_handle_via_command(uint8_t *data, uint8_t length)
{
  if (data == NULL || length < 4U)
  {
    return false;
  }

  uint8_t *command_id = &(data[0]);
  uint8_t *value_id   = &(data[2]);
  uint8_t *value_data = &(data[3]);
  bool     handled    = false;

  switch (*command_id)
  {
    case id_custom_set_value:
      handled = tapdance_set_value(*value_id, value_data, length);
      if (handled)
      {
        tapdance_get_value(*value_id, value_data, length);  // V251124R8: VIA echo 유지
      }
      break;

    case id_custom_get_value:
      tapdance_get_value(*value_id, value_data, length);
      handled = true;
      break;

    case id_custom_save:
      tapdance_storage_flush(true);
      handled = true;
      break;

    default:
      handled = false;
      break;
  }

  if (handled == false)
  {
    *command_id = id_unhandled;
  }
  return handled;
}

void tapdance_storage_apply_defaults(void)
{
  tapdance_apply_defaults_locked();                        // V251124R8: USER 초기화 시 기본값 기록
  tapdance_sync_state_from_storage();
}

void tapdance_storage_flush(bool force)
{
  eeconfig_flush_tapdance(force);
}

uint16_t tapdance_get_term_ms(uint16_t keycode)
{
  uint8_t slot_index = QK_TAP_DANCE_GET_INDEX(keycode);
  uint16_t term_ms;

  if (slot_index >= TAPDANCE_SLOT_COUNT)
  {
    return TAPDANCE_TERM_DEFAULT_MS;
  }

  term_ms = tapdance_state[slot_index].term_ms;
  if (term_ms < TAPDANCE_TERM_MIN_MS || term_ms > TAPDANCE_TERM_MAX_MS)
  {
    term_ms = TAPDANCE_TERM_DEFAULT_MS;                       // V251124R8: 비정상 값 방어
  }
  return term_ms;
}

static void tapdance_on_each_tap(tap_dance_state_t *state, void *user_data)
{
  tapdance_user_data_t *user = (tapdance_user_data_t *)user_data;
  tapdance_entry_t      entry = {0};

  if (user == NULL || user->slot_index >= TAPDANCE_SLOT_COUNT)
  {
    return;
  }

  tapdance_load_entry(user->slot_index, &entry);
  if (!tapdance_keycode_is_valid(entry.on_tap))
  {
    return;
  }

  if (state->count == 3U)
  {
    tap_code16(entry.on_tap);
    tap_code16(entry.on_tap);
    tap_code16(entry.on_tap);
  }
  else if (state->count > 3U)
  {
    tap_code16(entry.on_tap);
  }
}

static void tapdance_on_dance_finished(tap_dance_state_t *state, void *user_data)
{
  tapdance_user_data_t *user = (tapdance_user_data_t *)user_data;
  tapdance_entry_t      entry = {0};
  uint8_t               slot_index;
  uint8_t               step;

  if (user == NULL)
  {
    return;
  }

  slot_index = user->slot_index;
  if (slot_index >= TAPDANCE_SLOT_COUNT)
  {
    return;
  }

  tapdance_load_entry(slot_index, &entry);
  step = tapdance_step(state);
  tapdance_dance_state[slot_index] = step;

  tapdance_runtime[slot_index].active_action  = TAPDANCE_ACTION_NONE;
  tapdance_runtime[slot_index].active_keycode = KC_NO;

  switch (step)
  {
    case SINGLE_TAP:
      if (tapdance_keycode_is_valid(entry.on_tap))
      {
        register_code16(entry.on_tap);
        tapdance_runtime[slot_index].active_action  = TAPDANCE_ACTION_TAP;
        tapdance_runtime[slot_index].active_keycode = entry.on_tap;
      }
      break;

    case SINGLE_HOLD:
      if (tapdance_keycode_is_valid(entry.on_hold))
      {
        register_code16(entry.on_hold);
        tapdance_runtime[slot_index].active_action  = TAPDANCE_ACTION_HOLD;
        tapdance_runtime[slot_index].active_keycode = entry.on_hold;
      }
      else if (tapdance_keycode_is_valid(entry.on_tap))
      {
        register_code16(entry.on_tap);
        tapdance_runtime[slot_index].active_action  = TAPDANCE_ACTION_HOLD;
        tapdance_runtime[slot_index].active_keycode = entry.on_tap;
      }
      break;

    case DOUBLE_TAP:
      if (tapdance_keycode_is_valid(entry.on_double_tap))
      {
        register_code16(entry.on_double_tap);
        tapdance_runtime[slot_index].active_action  = TAPDANCE_ACTION_DOUBLE_TAP;
        tapdance_runtime[slot_index].active_keycode = entry.on_double_tap;
      }
      else if (tapdance_keycode_is_valid(entry.on_tap))
      {
        tap_code16(entry.on_tap);
        register_code16(entry.on_tap);                               // V251125R3: Vial 폴백과 동일한 1탭+홀드 유지
        tapdance_runtime[slot_index].active_action  = TAPDANCE_ACTION_DOUBLE_TAP;
        tapdance_runtime[slot_index].active_keycode = entry.on_tap;
      }
      break;

    case DOUBLE_HOLD:
      if (tapdance_keycode_is_valid(entry.on_tap_hold))
      {
        register_code16(entry.on_tap_hold);
        tapdance_runtime[slot_index].active_action  = TAPDANCE_ACTION_TAP_HOLD;
        tapdance_runtime[slot_index].active_keycode = entry.on_tap_hold;
      }
      else
      {
        if (tapdance_keycode_is_valid(entry.on_tap))
        {
          tap_code16(entry.on_tap);
        }

        if (tapdance_keycode_is_valid(entry.on_hold))
        {
          register_code16(entry.on_hold);
          tapdance_runtime[slot_index].active_action  = TAPDANCE_ACTION_TAP_HOLD;
          tapdance_runtime[slot_index].active_keycode = entry.on_hold;
        }
        else if (tapdance_keycode_is_valid(entry.on_tap))
        {
          register_code16(entry.on_tap);
          tapdance_runtime[slot_index].active_action  = TAPDANCE_ACTION_TAP_HOLD;
          tapdance_runtime[slot_index].active_keycode = entry.on_tap;
        }
      }
      break;

    case DOUBLE_SINGLE_TAP:
      if (tapdance_keycode_is_valid(entry.on_tap))
      {
        tap_code16(entry.on_tap);
        register_code16(entry.on_tap);
        tapdance_runtime[slot_index].active_action  = TAPDANCE_ACTION_DOUBLE_TAP;
        tapdance_runtime[slot_index].active_keycode = entry.on_tap;
      }
      break;

    case MORE_TAPS:
    default:
      break;
  }
}

static void tapdance_on_reset(tap_dance_state_t *state, void *user_data)
{
  tapdance_user_data_t *user = (tapdance_user_data_t *)user_data;
  tapdance_entry_t      entry = {0};
  uint8_t               step;

  (void)state;

  if (user == NULL)
  {
    return;
  }

  tapdance_load_entry(user->slot_index, &entry);
  step = tapdance_dance_state[user->slot_index];

  wait_ms(TAP_CODE_DELAY);

  switch (step)
  {
    case SINGLE_TAP:
      if (tapdance_keycode_is_valid(entry.on_tap))
      {
        unregister_code16(entry.on_tap);
      }
      break;

    case SINGLE_HOLD:
      if (tapdance_keycode_is_valid(entry.on_hold))
      {
        unregister_code16(entry.on_hold);
      }
      else if (tapdance_keycode_is_valid(entry.on_tap))
      {
        unregister_code16(entry.on_tap);
      }
      break;

    case DOUBLE_TAP:
      if (tapdance_keycode_is_valid(entry.on_double_tap))
      {
        unregister_code16(entry.on_double_tap);
      }
      else if (tapdance_keycode_is_valid(entry.on_tap))
      {
        unregister_code16(entry.on_tap);
      }
      break;

    case DOUBLE_HOLD:
      if (tapdance_keycode_is_valid(entry.on_tap_hold))
      {
        unregister_code16(entry.on_tap_hold);
      }
      else if (tapdance_keycode_is_valid(entry.on_hold))
      {
        unregister_code16(entry.on_hold);
      }
      else if (tapdance_keycode_is_valid(entry.on_tap))
      {
        unregister_code16(entry.on_tap);
      }
      break;

    case DOUBLE_SINGLE_TAP:
      if (tapdance_keycode_is_valid(entry.on_tap))
      {
        unregister_code16(entry.on_tap);
      }
      break;

    case MORE_TAPS:
    default:
      break;
  }

  tapdance_runtime[user->slot_index].active_action  = TAPDANCE_ACTION_NONE;
  tapdance_runtime[user->slot_index].active_keycode = KC_NO;
  tapdance_dance_state[user->slot_index] = 0;
}

static uint8_t tapdance_slot_index(uint8_t value_id)
{
  if (value_id < 1U || value_id > TAPDANCE_VALUE_MAX_ID)
  {
    return TAPDANCE_SLOT_COUNT;
  }
  return (uint8_t)((value_id - 1U) / TAPDANCE_VALUE_STRIDE);
}

static uint8_t tapdance_field_index(uint8_t value_id)
{
  return (uint8_t)((value_id - 1U) % TAPDANCE_VALUE_STRIDE);
}

static bool tapdance_is_storage_valid(const tapdance_storage_t *storage)
{
  if (storage->signature != TAPDANCE_SIGNATURE)
  {
    return false;
  }
  if (storage->version != TAPDANCE_VERSION)
  {
    return false;
  }

  for (uint8_t i = 0; i < TAPDANCE_SLOT_COUNT; i++)
  {
    if (storage->slots[i].term_ms < TAPDANCE_TERM_MIN_MS || storage->slots[i].term_ms > TAPDANCE_TERM_MAX_MS)
    {
      return false;
    }
  }
  return true;
}

static uint16_t tapdance_normalize_term(uint16_t term_ms)
{
  if (term_ms < TAPDANCE_TERM_MIN_MS)
  {
    term_ms = TAPDANCE_TERM_MIN_MS;
  }
  if (term_ms > TAPDANCE_TERM_MAX_MS)
  {
    term_ms = TAPDANCE_TERM_MAX_MS;
  }

  uint16_t offset     = term_ms - TAPDANCE_TERM_MIN_MS;
  uint16_t step_count = offset / TAPDANCE_TERM_STEP_MS;
  uint16_t normalized = TAPDANCE_TERM_MIN_MS + (step_count * TAPDANCE_TERM_STEP_MS);

  if (normalized > TAPDANCE_TERM_MAX_MS)
  {
    normalized = TAPDANCE_TERM_MAX_MS;
  }

  return normalized;
}

static void tapdance_apply_defaults_locked(void)
{
  for (uint8_t i = 0; i < TAPDANCE_SLOT_COUNT; i++)
  {
    for (uint8_t a = 0; a < TAPDANCE_ACTION_COUNT; a++)
    {
      tapdance_storage.slots[i].actions[a] = KC_NO;
    }
    tapdance_storage.slots[i].term_ms = TAPDANCE_TERM_DEFAULT_MS;
  }

  tapdance_storage.version   = TAPDANCE_VERSION;
  tapdance_storage.signature = TAPDANCE_SIGNATURE;
  eeconfig_flag_tapdance(true);
}

static void tapdance_sync_state_from_storage(void)
{
  bool dirty = false;

  for (uint8_t i = 0; i < TAPDANCE_SLOT_COUNT; i++)
  {
    tapdance_slot_storage_t *slot_storage = &tapdance_storage.slots[i];
    tapdance_slot_state_t   *slot_state   = &tapdance_state[i];

    slot_state->term_ms = tapdance_normalize_term(slot_storage->term_ms);
    for (uint8_t a = 0; a < TAPDANCE_ACTION_COUNT; a++)
    {
      slot_state->actions[a] = slot_storage->actions[a];
    }

    if (slot_storage->term_ms != slot_state->term_ms)
    {
      slot_storage->term_ms = slot_state->term_ms;
      dirty = true;
    }
  }

  if (dirty)
  {
    eeconfig_flag_tapdance(true);                               // V251124R8: 정규화 시 dirty 플래그 기록
  }
}

static bool tapdance_keycode_is_valid(uint16_t keycode)
{
  if (keycode == KC_NO || keycode == KC_TRANSPARENT)
  {
    return false;
  }
  return true;
}

bool tapdance_should_finish_immediate(uint8_t slot_index, uint8_t tap_count)
{
  if (slot_index >= TAPDANCE_SLOT_COUNT)
  {
    return false;
  }

  tapdance_slot_state_t *slot_state = &tapdance_state[slot_index];
  bool has_tap      = tapdance_keycode_is_valid(slot_state->actions[0]);
  bool has_hold     = tapdance_keycode_is_valid(slot_state->actions[1]);
  bool has_double   = tapdance_keycode_is_valid(slot_state->actions[2]);
  bool has_tap_hold = tapdance_keycode_is_valid(slot_state->actions[3]);

  if (tap_count == 1U && has_tap && has_hold && has_double == false && has_tap_hold == false)
  {
    return true;                                                 // V251125R1: tap/hold 단순 조합은 즉시 완료
  }
  if (tap_count == 2U && has_double)
  {
    return true;                                                 // V251125R1: 더블 탭 지정 시 즉시 완료
  }

  return false;
}

static bool tapdance_set_value(uint8_t value_id, uint8_t *value_data, uint8_t length)
{
  uint8_t slot_index;
  uint8_t field_index;

  if (value_data == NULL || length < 4U)
  {
    return false;                                               // V251124R8: VIA 패킷 최소 길이 확인
  }

  slot_index = tapdance_slot_index(value_id);
  field_index = tapdance_field_index(value_id);

  if (slot_index >= TAPDANCE_SLOT_COUNT)
  {
    return false;
  }

  if (field_index < TAPDANCE_ACTION_COUNT)
  {
    tapdance_storage.slots[slot_index].actions[field_index] = ((uint16_t)value_data[0] << 8) | (uint16_t)value_data[1];
  }
  else if (field_index == TAPDANCE_FIELD_TERM)
  {
    uint16_t term_ms = (uint16_t)value_data[0] * TAPDANCE_TERM_UNIT_MS;
    tapdance_storage.slots[slot_index].term_ms = tapdance_normalize_term(term_ms);
  }
  else
  {
    return false;
  }

  tapdance_sync_state_from_storage();
  eeconfig_flag_tapdance(true);
  return true;
}

static void tapdance_get_value(uint8_t value_id, uint8_t *value_data, uint8_t length)
{
  uint8_t slot_index;
  uint8_t field_index;

  if (value_data == NULL || length < 4U)
  {
    return;                                                    // V251124R8: VIA 응답 버퍼 최소 길이 확인
  }

  slot_index = tapdance_slot_index(value_id);
  field_index = tapdance_field_index(value_id);

  if (slot_index >= TAPDANCE_SLOT_COUNT)
  {
    value_data[0] = 0U;
    value_data[1] = 0U;
    return;
  }

  if (field_index < TAPDANCE_ACTION_COUNT)
  {
    uint16_t keycode = tapdance_state[slot_index].actions[field_index];
    value_data[0] = (uint8_t)(keycode >> 8);
    value_data[1] = (uint8_t)(keycode & 0xFF);
  }
  else if (field_index == TAPDANCE_FIELD_TERM)
  {
    uint16_t term_ms = tapdance_state[slot_index].term_ms;
    value_data[0] = (uint8_t)(term_ms / TAPDANCE_TERM_UNIT_MS);
    value_data[1] = 0U;
  }
  else
  {
    value_data[0] = 0U;
    value_data[1] = 0U;
  }
}

static void tapdance_load_entry(uint8_t slot_index, tapdance_entry_t *entry)
{
  if (entry == NULL || slot_index >= TAPDANCE_SLOT_COUNT)
  {
    return;
  }

  entry->on_tap        = tapdance_state[slot_index].actions[0];
  entry->on_hold       = tapdance_state[slot_index].actions[1];
  entry->on_double_tap = tapdance_state[slot_index].actions[2];
  entry->on_tap_hold   = tapdance_state[slot_index].actions[3];
  entry->term_ms       = tapdance_state[slot_index].term_ms;
}

static uint8_t tapdance_step(const tap_dance_state_t *state)
{
  if (state == NULL)
  {
    return SINGLE_TAP;
  }

  if (state->count == 1U)
  {
    if (state->interrupted || !state->pressed)
    {
      return SINGLE_TAP;
    }
    return SINGLE_HOLD;
  }
  else if (state->count == 2U)
  {
    if (state->interrupted)
    {
      return DOUBLE_SINGLE_TAP;
    }
    if (state->pressed)
    {
      return DOUBLE_HOLD;
    }
    return DOUBLE_TAP;
  }

  return MORE_TAPS;
}

#endif
