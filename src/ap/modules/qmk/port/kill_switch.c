#include "quantum.h"

#ifdef KILL_SWITCH_ENABLE

#include <string.h>
#include "era_state_sync.h"  // V260823R1: SOCD(kill switch) 값 변경 시 CONFIG revision

#define KILL_DEBUG_LOG            false
#define KILL_SWITCH_MAX_CH        2U


enum
{
  KILL_SWITCH_LR = 0,
  KILL_SWITCH_UD,
};

enum via_qmk_kill_switch_value {
    id_qmk_kill_switch_enable      = 1,
    id_qmk_kill_switch_keycode_0   = 2,
    id_qmk_kill_switch_keycode_1   = 3,
};


typedef union
{
  uint64_t raw;

  struct PACKED
  {
    uint8_t  enable;
    uint8_t  mode;
    uint16_t keycode[2];
  };

} kill_switch_config_t;

_Static_assert(sizeof(kill_switch_config_t) == sizeof(uint64_t), "EECONFIG out of spec.");


static void via_qmk_kill_switch_get_value(uint8_t type, uint8_t *data);
static void via_qmk_kill_switch_set_value(uint8_t type, uint8_t *data);
static void via_qmk_kill_switch_save(uint8_t type);
static bool kill_switch_keycode_can_report(uint16_t keycode);
static bool kill_switch_pair_local_valid(uint8_t type);
static bool kill_switch_pair_can_run(uint8_t type);
static void kill_switch_reset_runtime(void);
static void kill_switch_release_runtime(void);
static void kill_switch_add_report_key(uint16_t keycode);
static void kill_switch_del_report_key(uint16_t keycode);


static bool key_pressed[KILL_SWITCH_MAX_CH][2] = {{false, false}, {false, false}};
static kill_switch_config_t kill_switch_config[KILL_SWITCH_MAX_CH];

EECONFIG_DEBOUNCE_HELPER(kill_switch_lr, EECONFIG_USER_KILL_SWITCH_LR, kill_switch_config[KILL_SWITCH_LR]);
EECONFIG_DEBOUNCE_HELPER(kill_switch_ud, EECONFIG_USER_KILL_SWITCH_UD, kill_switch_config[KILL_SWITCH_UD]);


static bool kill_switch_keycode_can_report(uint16_t keycode)
{
  return IS_BASIC_KEYCODE(keycode) || IS_MODIFIER_KEYCODE(keycode);
}

static bool kill_switch_pair_local_valid(uint8_t type)
{
  if (type >= KILL_SWITCH_MAX_CH)
  {
    return false;
  }

  kill_switch_config_t *cfg = &kill_switch_config[type];
  return cfg->mode == 1U &&
         kill_switch_keycode_can_report(cfg->keycode[0]) &&
         kill_switch_keycode_can_report(cfg->keycode[1]) &&
         cfg->keycode[0] != cfg->keycode[1];
}

static bool kill_switch_pair_can_run(uint8_t type)
{
  if (type >= KILL_SWITCH_MAX_CH || !kill_switch_config[type].enable || !kill_switch_pair_local_valid(type))
  {
    return false;
  }

  // V260909R1: 두 활성 pair가 같은 HID usage를 공유하면 report-level ownership이 모호하다.
  // 양쪽을 inert로 만들어 truncation/이중 suppress보다 안전하게 실패한다.
  for (uint8_t other = 0U; other < KILL_SWITCH_MAX_CH; other++)
  {
    if (other == type || !kill_switch_config[other].enable || !kill_switch_pair_local_valid(other))
    {
      continue;
    }
    for (uint8_t i = 0U; i < 2U; i++)
    {
      for (uint8_t j = 0U; j < 2U; j++)
      {
        if (kill_switch_config[type].keycode[i] == kill_switch_config[other].keycode[j])
        {
          return false;
        }
      }
    }
  }
  return true;
}

static void kill_switch_add_report_key(uint16_t keycode)
{
  if (IS_BASIC_KEYCODE(keycode))
  {
    add_key((uint8_t)keycode);
  }
  else if (IS_MODIFIER_KEYCODE(keycode))
  {
    add_mods(MOD_BIT((uint8_t)keycode));
  }
}

static void kill_switch_del_report_key(uint16_t keycode)
{
  if (IS_BASIC_KEYCODE(keycode))
  {
    del_key((uint8_t)keycode);
  }
  else if (IS_MODIFIER_KEYCODE(keycode))
  {
    del_mods(MOD_BIT((uint8_t)keycode));
  }
}

static void kill_switch_reset_runtime(void)
{
  memset(key_pressed, 0, sizeof(key_pressed));
}

static void kill_switch_release_runtime(void)
{
  bool had_tracked_key = false;

  // V260909R1: live config 변경 전에 기존 SOCD가 숨긴 physical usage를 먼저 복원한다.
  // 이후 새 설정은 fresh epoch로 시작하며 이미 눌린 키를 새 pair에 소급 편입하지 않는다.
  for (uint8_t type = 0U; type < KILL_SWITCH_MAX_CH; type++)
  {
    if (!kill_switch_pair_can_run(type))
    {
      continue;
    }
    for (uint8_t i = 0U; i < 2U; i++)
    {
      if (key_pressed[type][i])
      {
        kill_switch_add_report_key(kill_switch_config[type].keycode[i]);
        had_tracked_key = true;
      }
    }
  }

  kill_switch_reset_runtime();
  if (had_tracked_key)
  {
    send_keyboard_report();
  }
}


void kill_switch_init(void)
{
  bool flush_lr = false;
  bool flush_ud = false;

  eeconfig_init_kill_switch_lr();
  if (kill_switch_config[KILL_SWITCH_LR].mode != 1U)
  {
    kill_switch_config[KILL_SWITCH_LR].raw = 0U;
    kill_switch_config[KILL_SWITCH_LR].mode = 1U;
    flush_lr = true;
  }
  if (kill_switch_config[KILL_SWITCH_LR].enable > 1U)
  {
    kill_switch_config[KILL_SWITCH_LR].enable = 1U;
    flush_lr = true;
  }
  if (flush_lr)
  {
    eeconfig_flush_kill_switch_lr(true);
  }

  eeconfig_init_kill_switch_ud();
  if (kill_switch_config[KILL_SWITCH_UD].mode != 1U)
  {
    kill_switch_config[KILL_SWITCH_UD].raw = 0U;
    kill_switch_config[KILL_SWITCH_UD].mode = 1U;
    flush_ud = true;
  }
  if (kill_switch_config[KILL_SWITCH_UD].enable > 1U)
  {
    kill_switch_config[KILL_SWITCH_UD].enable = 1U;
    flush_ud = true;
  }
  if (flush_ud)
  {
    eeconfig_flush_kill_switch_ud(true);
  }

  kill_switch_reset_runtime();
  logPrintf("[ON] KILL SWITCH\n");
}

bool kill_switch_process(uint16_t keycode, keyrecord_t *record)
{
  if (record == NULL)
  {
    return true;
  }

  for (uint8_t type = 0U; type < KILL_SWITCH_MAX_CH; type++)
  {
    if (!kill_switch_pair_can_run(type))
    {
      continue;
    }

    kill_switch_config_t *cfg = &kill_switch_config[type];
    for (uint8_t i = 0U; i < 2U; i++)
    {
      if (keycode != cfg->keycode[i])
      {
        continue;
      }

      uint8_t other = (uint8_t)(1U - i);
      if (record->event.pressed)
      {
        key_pressed[type][i] = true;
        if (key_pressed[type][other])
        {
          kill_switch_del_report_key(cfg->keycode[other]);
#if KILL_DEBUG_LOG
          logPrintf(" unregister_code(%u)-0x%04X\n", other, cfg->keycode[other]);
#endif
        }
      }
      else
      {
        key_pressed[type][i] = false;
        if (key_pressed[type][other])
        {
          kill_switch_add_report_key(cfg->keycode[other]);
#if KILL_DEBUG_LOG
          logPrintf(" register_code(%u)-0x%04X\n", other, cfg->keycode[other]);
#endif
        }
      }
      break;
    }
  }

  return true;
}

bool kill_switch_is_use(uint16_t keycode)
{
  for (uint8_t type = 0U; type < KILL_SWITCH_MAX_CH; type++)
  {
    if (!kill_switch_pair_can_run(type))
    {
      continue;
    }
    if (keycode == kill_switch_config[type].keycode[0] || keycode == kill_switch_config[type].keycode[1])
    {
      return true;
    }
  }
  return false;
}

void via_qmk_kill_swtich_command(uint8_t type, uint8_t *data, uint8_t length)
{
  if (data == NULL || length < 5U || type >= KILL_SWITCH_MAX_CH)
  {
    if (data != NULL && length > 0U)
    {
      data[0] = id_unhandled;
    }
    return;
  }

  // data = [ command_id, channel_id, value_id, value_data ]
  uint8_t *command_id        = &(data[0]);
  uint8_t *value_id_and_data = &(data[2]);

  switch (*command_id)
  {
    case id_custom_set_value:
      {
        uint8_t before[3] = {value_id_and_data[0], 0, 0};
        uint8_t after[3]  = {value_id_and_data[0], 0, 0};

        via_qmk_kill_switch_get_value(type, before);    // V260823R1: 실제로 값이 바뀐 SET에서만 revision을 올린다
        via_qmk_kill_switch_set_value(type, value_id_and_data);
        via_qmk_kill_switch_get_value(type, after);
        if (memcmp(before, after, sizeof(before)) != 0)
        {
          era_state_sync_bump_config();
        }
        break;
      }
    case id_custom_get_value:
      via_qmk_kill_switch_get_value(type, value_id_and_data);
      break;
    case id_custom_save:
      via_qmk_kill_switch_save(type);
      break;
    default:
      *command_id = id_unhandled;
      break;
  }
}

static void via_qmk_kill_switch_get_value(uint8_t type, uint8_t *data)
{
  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);

  switch (*value_id)
  {
    case id_qmk_kill_switch_enable:
      value_data[0] = kill_switch_config[type].enable;
      break;
    case id_qmk_kill_switch_keycode_0:
      value_data[0] = (uint8_t)(kill_switch_config[type].keycode[0] >> 8);
      value_data[1] = (uint8_t)(kill_switch_config[type].keycode[0] & 0xFFU);
      break;
    case id_qmk_kill_switch_keycode_1:
      value_data[0] = (uint8_t)(kill_switch_config[type].keycode[1] >> 8);
      value_data[1] = (uint8_t)(kill_switch_config[type].keycode[1] & 0xFFU);
      break;
    default:
      break;
  }
}

static void via_qmk_kill_switch_set_value(uint8_t type, uint8_t *data)
{
  uint8_t *value_id   = &(data[0]);
  uint8_t *value_data = &(data[1]);

  switch (*value_id)
  {
    case id_qmk_kill_switch_enable:
      {
        uint8_t next = value_data[0] != 0U ? 1U : 0U;
        if (kill_switch_config[type].enable != next)
        {
          kill_switch_release_runtime();
          kill_switch_config[type].enable = next;
        }
        break;
      }
    case id_qmk_kill_switch_keycode_0:
    case id_qmk_kill_switch_keycode_1:
      {
        uint8_t index = (uint8_t)(*value_id - id_qmk_kill_switch_keycode_0);
        uint16_t next = (uint16_t)(((uint16_t)value_data[0] << 8) | value_data[1]);
        if (kill_switch_config[type].keycode[index] != next)
        {
          kill_switch_release_runtime();
          kill_switch_config[type].keycode[index] = next;
        }
        break;
      }
    default:
      break;
  }
}

static void via_qmk_kill_switch_save(uint8_t type)
{
  if (type == KILL_SWITCH_LR)
  {
    eeconfig_flush_kill_switch_lr(true);
  }
  else if (type == KILL_SWITCH_UD)
  {
    eeconfig_flush_kill_switch_ud(true);
  }
}

#endif
