#include "debounce_profile.h"


#include <string.h>
#include "eeprom.h"
#include "eeconfig.h"
#include "log.h"
#include "qmk/port/port.h"
#include "via.h"


#define DEBOUNCE_PROFILE_SIGNATURE     (0x434E4244UL)     // "DBNC"
#define DEBOUNCE_PROFILE_VERSION       (1U)
#define DEBOUNCE_PROFILE_MIN_DELAY_MS  (1U)
#define DEBOUNCE_PROFILE_MAX_DELAY_MS  (30U)
#define DEBOUNCE_PROFILE_DEFAULT_DELAY (5U)
#define DEBOUNCE_PROFILE_DEFAULT_TYPE  (DEBOUNCE_RUNTIME_TYPE_SYM_DEFER_PK)


typedef struct __attribute__((packed))
{
  uint8_t  type;
  uint8_t  pre_ms;
  uint8_t  post_ms;
  uint8_t  version;
  uint32_t signature;
} debounce_profile_storage_t;

static debounce_profile_storage_t debounce_profile_storage = {0};


EECONFIG_DEBOUNCE_HELPER(debounce_profile, EECONFIG_USER_DEBOUNCE, debounce_profile_storage);


typedef struct
{
  debounce_profile_values_t values;
  bool                      initialized;
  bool                      applied;
  bool                      requires_reboot;
} debounce_profile_state_t;

static debounce_profile_state_t debounce_profile_state = {0};


static void     debounce_profile_apply_defaults_locked(void);
static void     debounce_profile_sync_from_storage(void);
static bool     debounce_profile_is_storage_valid(const debounce_profile_storage_t *storage);
static uint8_t  debounce_profile_clamp_delay(uint8_t delay);
static void     debounce_profile_store_current(void);
static bool     debounce_profile_set_value_internal(uint8_t id, uint8_t value);
static void     debounce_profile_get_value_internal(uint8_t id, uint8_t *value_data);
static void     debounce_profile_log_change(const char *source);
static bool     debounce_profile_values_changed(const debounce_profile_values_t *before,
                                                const debounce_profile_values_t *after);


void debounce_profile_init(void)
{
  if (debounce_profile_state.initialized)
  {
    return;
  }

  eeconfig_init_debounce_profile();

  if (!debounce_profile_is_storage_valid(&debounce_profile_storage))
  {
    debounce_profile_apply_defaults_locked();                     // V251115R1: 손상 데이터 복구 시 기본값을 기록
    eeconfig_flush_debounce_profile(true);
  }

  debounce_profile_sync_from_storage();
  debounce_profile_state.applied         = false;
  debounce_profile_state.requires_reboot = false;
  debounce_profile_state.initialized     = true;
}

void debounce_profile_apply_current(void)
{
  if (!debounce_profile_state.initialized)
  {
    debounce_profile_init();
  }

  debounce_runtime_config_t config =
  {
    .type    = debounce_profile_state.values.type,
    .pre_ms  = debounce_profile_state.values.pre_ms,
    .post_ms = debounce_profile_state.values.post_ms,
  };

  if (!debounce_runtime_apply_config(&config))
  {
    debounce_profile_state.applied         = false;
    debounce_profile_state.requires_reboot = true;
    logPrintf("[!] Debounce profile apply failed (type %d)\n", config.type);  // V251115R1: 런타임 재초기화 실패 로그
    return;
  }

  debounce_profile_state.applied         = debounce_runtime_is_ready();
  debounce_profile_state.requires_reboot = (debounce_runtime_get_last_error() != DEBOUNCE_RUNTIME_ERROR_NONE);
}

const debounce_profile_values_t *debounce_profile_current(void)
{
  return &debounce_profile_state.values;
}

debounce_profile_status_t debounce_profile_get_status(void)
{
  if (debounce_profile_state.requires_reboot)
  {
    return DEBOUNCE_PROFILE_STATUS_ERROR;
  }

  if (!debounce_profile_state.applied)
  {
    return DEBOUNCE_PROFILE_STATUS_PENDING;
  }

  return DEBOUNCE_PROFILE_STATUS_READY;
}

bool debounce_profile_set_mode(uint8_t mode)
{
  if (mode >= DEBOUNCE_RUNTIME_TYPE_COUNT)
  {
    return false;
  }

  debounce_profile_values_t previous = debounce_profile_state.values;
  debounce_profile_state.values.type = (debounce_runtime_type_t)mode;
  if (mode == DEBOUNCE_RUNTIME_TYPE_SYM_DEFER_PK)
  {
    uint8_t common_delay = debounce_profile_clamp_delay(debounce_profile_state.values.pre_ms);
    debounce_profile_state.values.pre_ms  = common_delay;
    debounce_profile_state.values.post_ms = common_delay;
  }
  else if (mode == DEBOUNCE_RUNTIME_TYPE_SYM_EAGER_PK)
  {
    debounce_profile_state.values.post_ms = debounce_profile_clamp_delay(debounce_profile_state.values.post_ms);
  }
  else
  {
    debounce_profile_state.values.pre_ms  = debounce_profile_clamp_delay(debounce_profile_state.values.pre_ms);
    debounce_profile_state.values.post_ms = debounce_profile_clamp_delay(debounce_profile_state.values.post_ms);
  }

  bool changed = debounce_profile_values_changed(&previous, &debounce_profile_state.values);
  debounce_profile_state.applied         = false;
  debounce_profile_state.requires_reboot = false;
  debounce_profile_store_current();
  debounce_profile_apply_current();
  if (changed)
  {
    debounce_profile_log_change("mode");                    // V251115R2: VIA 디바운스 타입 변경 로그
  }
  return true;
}

bool debounce_profile_set_single_delay(uint8_t delay_ms)
{
  if (debounce_profile_state.values.type != DEBOUNCE_RUNTIME_TYPE_SYM_DEFER_PK)
  {
    return false;
  }

  debounce_profile_values_t previous = debounce_profile_state.values;
  uint8_t clamped = debounce_profile_clamp_delay(delay_ms);
  debounce_profile_state.values.pre_ms  = clamped;
  debounce_profile_state.values.post_ms = clamped;
  bool changed = debounce_profile_values_changed(&previous, &debounce_profile_state.values);
  debounce_profile_state.applied        = false;
  debounce_profile_store_current();
  debounce_profile_apply_current();
  if (changed)
  {
    debounce_profile_log_change("single delay");            // V251115R2: VIA 디바운스 지연 변경 로그
  }
  return true;
}

bool debounce_profile_set_press_delay(uint8_t delay_ms)
{
  if (debounce_profile_state.values.type != DEBOUNCE_RUNTIME_TYPE_ASYM_EAGER_DEFER_PK)
  {
    return false;
  }

  debounce_profile_values_t previous = debounce_profile_state.values;
  debounce_profile_state.values.pre_ms = debounce_profile_clamp_delay(delay_ms);
  bool changed = debounce_profile_values_changed(&previous, &debounce_profile_state.values);
  debounce_profile_state.applied       = false;
  debounce_profile_store_current();
  debounce_profile_apply_current();
  if (changed)
  {
    debounce_profile_log_change("press delay");             // V251115R2: VIA 디바운스 지연 변경 로그
  }
  return true;
}

bool debounce_profile_set_release_delay(uint8_t delay_ms)
{
  if (debounce_profile_state.values.type == DEBOUNCE_RUNTIME_TYPE_SYM_DEFER_PK)
  {
    return false;
  }

  debounce_profile_values_t previous = debounce_profile_state.values;
  debounce_profile_state.values.post_ms = debounce_profile_clamp_delay(delay_ms);
  bool changed = debounce_profile_values_changed(&previous, &debounce_profile_state.values);
  debounce_profile_state.applied        = false;
  debounce_profile_store_current();
  debounce_profile_apply_current();
  if (changed)
  {
    debounce_profile_log_change("release delay");           // V251115R2: VIA 디바운스 지연 변경 로그
  }
  return true;
}

void debounce_profile_save(bool force)
{
  eeconfig_flush_debounce_profile(force);
}

void debounce_profile_restore_defaults(void)
{
  debounce_profile_apply_defaults_locked();
  debounce_profile_sync_from_storage();
  debounce_profile_state.applied         = false;
  debounce_profile_state.requires_reboot = false;
  debounce_profile_apply_current();
  eeconfig_flush_debounce_profile(true);
}

void debounce_profile_storage_apply_defaults(void)
{
  debounce_profile_apply_defaults_locked();                      // V251115R1: EEPROM 공장 초기화 경로에서 기본값만 기록
}

bool debounce_profile_handle_via_command(uint8_t *data, uint8_t length)
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
      handled = debounce_profile_set_value_internal(*value_id, value_data[0]);
      if (handled)
      {
        debounce_profile_get_value_internal(*value_id, value_data);
      }
      break;

    case id_custom_get_value:
      debounce_profile_get_value_internal(*value_id, value_data);
      handled = true;
      break;

    case id_custom_save:
      debounce_profile_save(true);
      handled = true;
      break;

    default:
      handled = false;
      break;
  }

  if (!handled)
  {
    *command_id = id_unhandled;
  }
  return handled;
}

static void debounce_profile_apply_defaults_locked(void)
{
  debounce_profile_storage.type      = (uint8_t)DEBOUNCE_PROFILE_DEFAULT_TYPE;
  debounce_profile_storage.pre_ms    = DEBOUNCE_PROFILE_DEFAULT_DELAY;
  debounce_profile_storage.post_ms   = DEBOUNCE_PROFILE_DEFAULT_DELAY;
  debounce_profile_storage.version   = DEBOUNCE_PROFILE_VERSION;
  debounce_profile_storage.signature = DEBOUNCE_PROFILE_SIGNATURE;
  eeconfig_flag_debounce_profile(true);
}

static void debounce_profile_sync_from_storage(void)
{
  debounce_profile_values_t values;
  values.type    = (debounce_runtime_type_t)debounce_profile_storage.type;
  values.pre_ms  = debounce_profile_clamp_delay(debounce_profile_storage.pre_ms);
  values.post_ms = debounce_profile_clamp_delay(debounce_profile_storage.post_ms);

  if (values.type >= DEBOUNCE_RUNTIME_TYPE_COUNT)
  {
    values.type = DEBOUNCE_PROFILE_DEFAULT_TYPE;
  }
  if (values.type == DEBOUNCE_RUNTIME_TYPE_SYM_DEFER_PK)
  {
    values.post_ms = values.pre_ms;
  }

  debounce_profile_state.values = values;
}

static bool debounce_profile_is_storage_valid(const debounce_profile_storage_t *storage)
{
  if (storage->signature != DEBOUNCE_PROFILE_SIGNATURE)
  {
    return false;
  }
  if (storage->version != DEBOUNCE_PROFILE_VERSION)
  {
    return false;
  }
  if (storage->type >= DEBOUNCE_RUNTIME_TYPE_COUNT)
  {
    return false;
  }
  return true;
}

static uint8_t debounce_profile_clamp_delay(uint8_t delay)
{
  if (delay < DEBOUNCE_PROFILE_MIN_DELAY_MS)
  {
    delay = DEBOUNCE_PROFILE_MIN_DELAY_MS;
  }
  if (delay > DEBOUNCE_PROFILE_MAX_DELAY_MS)
  {
    delay = DEBOUNCE_PROFILE_MAX_DELAY_MS;
  }
  return delay;
}

static void debounce_profile_store_current(void)
{
  debounce_profile_storage.type      = (uint8_t)debounce_profile_state.values.type;
  debounce_profile_storage.pre_ms    = debounce_profile_state.values.pre_ms;
  debounce_profile_storage.post_ms   = debounce_profile_state.values.post_ms;
  debounce_profile_storage.version   = DEBOUNCE_PROFILE_VERSION;
  debounce_profile_storage.signature = DEBOUNCE_PROFILE_SIGNATURE;
  eeconfig_flag_debounce_profile(true);
}

static bool debounce_profile_set_value_internal(uint8_t id, uint8_t value)
{
  switch (id)
  {
    case id_qmk_debounce_mode:
      return debounce_profile_set_mode(value);

    case id_qmk_debounce_time_single:
      return debounce_profile_set_single_delay(value);

    case id_qmk_debounce_time_pre:
      return debounce_profile_set_press_delay(value);

    case id_qmk_debounce_time_post:
      return debounce_profile_set_release_delay(value);

    case id_qmk_debounce_status:
      return true;

    default:
      break;
  }

  return false;
}

static void debounce_profile_get_value_internal(uint8_t id, uint8_t *value_data)
{
  switch (id)
  {
    case id_qmk_debounce_mode:
      value_data[0] = (uint8_t)debounce_profile_state.values.type;
      break;

    case id_qmk_debounce_time_single:
      value_data[0] = debounce_profile_state.values.pre_ms;
      break;

    case id_qmk_debounce_time_pre:
      value_data[0] = debounce_profile_state.values.pre_ms;
      break;

    case id_qmk_debounce_time_post:
      value_data[0] = debounce_profile_state.values.post_ms;
      break;

    case id_qmk_debounce_status:
      value_data[0] = (uint8_t)debounce_profile_get_status();
      break;

    default:
      value_data[0] = 0U;
      break;
  }
}

static bool debounce_profile_values_changed(const debounce_profile_values_t *before,
                                            const debounce_profile_values_t *after)
{
  if (before == NULL || after == NULL)
  {
    return false;
  }

  if (before->type != after->type)
  {
    return true;
  }
  if (before->pre_ms != after->pre_ms)
  {
    return true;
  }
  if (before->post_ms != after->post_ms)
  {
    return true;
  }
  return false;
}

static void debounce_profile_log_change(const char *source)
{
#if LOG_LEVEL_VERBOSE
  const char *log_source = (source != NULL) ? source : "unknown";
  logPrintf("[  ] DEBOUNCE change (%s): type %d, pre %d ms, post %d ms\n",
            log_source,
            debounce_profile_state.values.type,
            debounce_profile_state.values.pre_ms,
            debounce_profile_state.values.post_ms);         // V251115R2: VIA 디바운스 런타임 설정 변경 로그
#else
  (void)source;
#endif
}
