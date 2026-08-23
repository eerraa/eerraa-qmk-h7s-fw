#include "mousekey_config.h"

#ifdef MOUSEKEY_ENABLE

#include <string.h>

#include "port.h"
#include "quantum.h"
#include "mousekey.h"
#include "era_state_sync.h"  // V260823R1: MOUSE 값 변경 시 CONFIG revision


#if !defined(ERA_MOUSEKEY_RUNTIME_DELTA)
#  error "V260823R1: MOUSE 페이지는 이벤트당 스텝 크기를 런타임으로 쓴다. CMakeLists에서 ERA_MOUSEKEY_RUNTIME_DELTA를 정의할 것."
#endif


#define MOUSEKEY_CFG_SIGNATURE        (0x4B53554DUL)   // "MUSK"
#define MOUSEKEY_CFG_VERSION          (1U)

// 리포트 디스크립터 상한. move_unit()/wheel_unit()이 어차피 잘라내지만, 여기서도 잘라야
// VIA로 되돌려주는 값이 엔진이 실제로 쓰는 값과 같아진다.
#define MOUSEKEY_CFG_UNIT_MAX         (MOUSEKEY_MOVE_MAX)
#define MOUSEKEY_CFG_WHEEL_UNIT_MAX   (MOUSEKEY_WHEEL_MAX)

// 0은 느린 설정이 아니라 매 패스마다 리포트를 내보내는 값이다 (엔진 판정이 elapsed > interval).
#define MOUSEKEY_CFG_INTERVAL_MIN_MS  (1U)

// 가속 시간의 표시 단위. 50ms를 쓰면 페이지가 제시하는 모든 시간이 페이지가 제시하는 모든
// 갱신 주기에서 정확히 왕복한다.
#define MOUSEKEY_CFG_RAMP_UNIT_MS     (50U)

// 기본값은 업스트림이 아니라 참조 QMK(era_mousekey.c)의 5120x2160 실측값을 따른다.
#define MOUSEKEY_CFG_DEFAULT_INTERVAL_MS  (10U)    // 초당 100 이벤트
#define MOUSEKEY_CFG_DEFAULT_START_PX     (4U)     // 초당 400 카운트
#define MOUSEKEY_CFG_DEFAULT_TOP_PX       (16U)    // 초당 1600 카운트
#define MOUSEKEY_CFG_DEFAULT_RAMP_MS      (1000U)


enum
{
  MOUSEKEY_CFG_WHEEL_ACCEL_OFF = 0,
  MOUSEKEY_CFG_WHEEL_ACCEL_MILD,
  MOUSEKEY_CFG_WHEEL_ACCEL_STRONG,
  MOUSEKEY_CFG_WHEEL_ACCEL_COUNT,
};


typedef struct
{
  uint8_t  delay;
  uint8_t  interval;
  uint8_t  max_speed;
  uint8_t  time_to_max;
  uint8_t  wheel_delay;
  uint8_t  wheel_interval;
  uint8_t  wheel_max_speed;
  uint8_t  wheel_time_to_max;
  uint8_t  move_delta;
  uint8_t  wheel_delta;
  uint8_t  version;
  uint8_t  reserved;
  uint32_t signature;
} mousekey_config_storage_t;

_Static_assert(sizeof(mousekey_config_storage_t) == 16, "EECONFIG out of spec.");  // V260823R1: 슬롯 크기 고정


// 드롭다운 하나가 휠 가속의 두 값을 함께 옮긴다. 램프 길이는 도달 속도 없이는 뜻이 없어서
// 따로 물으면 사용자에게 같은 얘기를 두 번 묻는 꼴이 된다.
static const struct
{
  uint8_t max_speed;
  uint8_t time_to_max;
} mousekey_wheel_accel[MOUSEKEY_CFG_WHEEL_ACCEL_COUNT] =
{
  [MOUSEKEY_CFG_WHEEL_ACCEL_OFF]    = {1, 0},
  [MOUSEKEY_CFG_WHEEL_ACCEL_MILD]   = {4, MOUSEKEY_WHEEL_TIME_TO_MAX},
  [MOUSEKEY_CFG_WHEEL_ACCEL_STRONG] = {MOUSEKEY_WHEEL_MAX_SPEED, MOUSEKEY_WHEEL_TIME_TO_MAX},
};


static mousekey_config_storage_t mousekey_config_storage = {0};


EECONFIG_DEBOUNCE_HELPER(mousekey_cfg, EECONFIG_USER_MOUSEKEY, mousekey_config_storage);


static uint8_t  mousekey_config_clamp(uint8_t value, uint8_t min, uint8_t max);
static bool     mousekey_config_is_storage_valid(const mousekey_config_storage_t *storage);
static bool     mousekey_config_normalize(mousekey_config_storage_t *storage);
static void     mousekey_config_apply_defaults_locked(void);
static void     mousekey_config_apply_runtime(void);
static void     mousekey_config_commit(bool changed);
static uint8_t  mousekey_config_effective_max_speed(void);
static uint8_t  mousekey_config_speed_ratio(uint8_t target_unit, uint8_t step);
static uint16_t mousekey_config_ramp_ms(void);
static void     mousekey_config_hold_ramp_ms(uint16_t ramp_ms);
static bool     mousekey_config_set_value(uint8_t id, uint8_t *value_data, uint8_t length);
static void     mousekey_config_get_value(uint8_t id, uint8_t *value_data, uint8_t length);


void mousekey_config_init(void)
{
  eeconfig_init_mousekey_cfg();

  if (mousekey_config_is_storage_valid(&mousekey_config_storage) == false)
  {
    mousekey_config_apply_defaults_locked();                // V260823R1: 손상 슬롯 복원
    eeconfig_flush_mousekey_cfg(true);
  }
  else if (mousekey_config_normalize(&mousekey_config_storage))
  {
    eeconfig_flag_mousekey_cfg(true);                       // V260823R1: 범위 밖 필드만 보정하고 dirty 기록
  }

  mousekey_config_apply_runtime();
}

void mousekey_config_storage_apply_defaults(void)
{
  mousekey_config_apply_defaults_locked();                  // V260823R1: EEPROM 초기화 시 기본값 적용
  mousekey_config_apply_runtime();
}

void mousekey_config_storage_flush(bool force)
{
  eeconfig_flush_mousekey_cfg(force);
}

bool mousekey_config_handle_via_command(uint8_t *data, uint8_t length)
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
      handled = mousekey_config_set_value(*value_id, value_data, length);
      if (handled)
      {
        mousekey_config_get_value(*value_id, value_data, length);  // V260823R1: 클램프된 실제 값을 되돌려준다
      }
      break;

    case id_custom_get_value:
      mousekey_config_get_value(*value_id, value_data, length);
      handled = true;
      break;

    case id_custom_save:
      mousekey_config_storage_flush(true);
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

static uint8_t mousekey_config_clamp(uint8_t value, uint8_t min, uint8_t max)
{
  if (value < min)
  {
    return min;
  }
  if (value > max)
  {
    return max;
  }
  return value;
}

// 식별만 본다. 범위는 normalize가 담당한다. 열 개의 독립된 손잡이라 조합이 잘못될 여지가 없고,
// 그래서 한 필드가 범위를 벗어나도 나머지 아홉을 버릴 이유가 없다.
static bool mousekey_config_is_storage_valid(const mousekey_config_storage_t *storage)
{
  if (storage->signature != MOUSEKEY_CFG_SIGNATURE)
  {
    return false;
  }
  if (storage->version != MOUSEKEY_CFG_VERSION)
  {
    return false;
  }
  return true;
}

static bool mousekey_config_normalize(mousekey_config_storage_t *storage)
{
  mousekey_config_storage_t previous = *storage;

  storage->interval        = mousekey_config_clamp(storage->interval, MOUSEKEY_CFG_INTERVAL_MIN_MS, UINT8_MAX);
  storage->wheel_interval  = mousekey_config_clamp(storage->wheel_interval, MOUSEKEY_CFG_INTERVAL_MIN_MS, UINT8_MAX);
  storage->max_speed       = mousekey_config_clamp(storage->max_speed, 1U, UINT8_MAX);
  storage->wheel_max_speed = mousekey_config_clamp(storage->wheel_max_speed, 1U, UINT8_MAX);
  storage->move_delta      = mousekey_config_clamp(storage->move_delta, 1U, MOUSEKEY_CFG_UNIT_MAX);
  storage->wheel_delta     = mousekey_config_clamp(storage->wheel_delta, 1U, MOUSEKEY_CFG_WHEEL_UNIT_MAX);
  storage->version         = MOUSEKEY_CFG_VERSION;
  storage->reserved        = 0U;
  storage->signature       = MOUSEKEY_CFG_SIGNATURE;

  return memcmp(&previous, storage, sizeof(*storage)) != 0;
}

static void mousekey_config_apply_defaults_locked(void)
{
  memset(&mousekey_config_storage, 0, sizeof(mousekey_config_storage));

  mousekey_config_storage.delay             = MOUSEKEY_DELAY / 10U;
  mousekey_config_storage.interval          = MOUSEKEY_CFG_DEFAULT_INTERVAL_MS;
  mousekey_config_storage.max_speed         = MOUSEKEY_CFG_DEFAULT_TOP_PX / MOUSEKEY_CFG_DEFAULT_START_PX;
  mousekey_config_storage.time_to_max       = MOUSEKEY_CFG_DEFAULT_RAMP_MS / MOUSEKEY_CFG_DEFAULT_INTERVAL_MS;
  mousekey_config_storage.wheel_delay       = MOUSEKEY_WHEEL_DELAY / 10U;
  mousekey_config_storage.wheel_interval    = MOUSEKEY_WHEEL_INTERVAL;
  mousekey_config_storage.wheel_max_speed   = MOUSEKEY_WHEEL_MAX_SPEED;
  mousekey_config_storage.wheel_time_to_max = MOUSEKEY_WHEEL_TIME_TO_MAX;
  mousekey_config_storage.move_delta        = MOUSEKEY_CFG_DEFAULT_START_PX;
  mousekey_config_storage.wheel_delta       = MOUSEKEY_WHEEL_DELTA;
  mousekey_config_storage.version           = MOUSEKEY_CFG_VERSION;
  mousekey_config_storage.signature         = MOUSEKEY_CFG_SIGNATURE;

  eeconfig_flag_mousekey_cfg(true);
}

// 가속 off는 "가장 빠른 속도로 고정"이 아니라 "첫 스텝 속도로 고정"이다.
// mk_time_to_max가 0이면 엔진의 repeat >= time_to_max 판정이 첫 반복부터 참이라 mk_max_speed가
// 매 이벤트의 스텝 크기가 된다. 저장된 비율은 건드리지 않으므로 램프를 다시 고르면 원래 값이 돌아온다.
static void mousekey_config_apply_runtime(void)
{
  mk_delay             = mousekey_config_storage.delay;
  mk_interval          = mousekey_config_storage.interval;
  mk_max_speed         = (mousekey_config_storage.time_to_max == 0U) ? 1U : mousekey_config_storage.max_speed;
  mk_time_to_max       = mousekey_config_storage.time_to_max;
  mk_wheel_delay       = mousekey_config_storage.wheel_delay;
  mk_wheel_interval    = mousekey_config_storage.wheel_interval;
  mk_wheel_max_speed   = mousekey_config_storage.wheel_max_speed;
  mk_wheel_time_to_max = mousekey_config_storage.wheel_time_to_max;
  mk_move_delta        = mousekey_config_storage.move_delta;
  mk_wheel_delta       = mousekey_config_storage.wheel_delta;
}

static void mousekey_config_commit(bool changed)
{
  mousekey_config_apply_runtime();
  if (changed)
  {
    eeconfig_flag_mousekey_cfg(true);
    era_state_sync_bump_config();  // V260823R1: GET이 새 값을 돌려준 뒤에만 CONFIG revision
  }
}

static uint8_t mousekey_config_effective_max_speed(void)
{
  uint16_t unit = (uint16_t)mousekey_config_storage.move_delta * (uint16_t)mousekey_config_storage.max_speed;

  return unit > MOUSEKEY_CFG_UNIT_MAX ? (uint8_t)MOUSEKEY_CFG_UNIT_MAX : (uint8_t)unit;
}

// 내림이 아니라 반올림이다. 스텝 8에서 상한 127을 내림하면 비율 15가 되어 120으로 되읽히지만,
// 반올림하면 16이 되고 엔진의 자체 클램프가 정확히 127로 되돌려준다.
static uint8_t mousekey_config_speed_ratio(uint8_t target_unit, uint8_t step)
{
  uint16_t ratio;

  if (step == 0U)
  {
    return 1U;
  }

  ratio = ((uint16_t)target_unit + (uint16_t)(step / 2U)) / (uint16_t)step;
  if (ratio < 1U)
  {
    return 1U;
  }
  return ratio > UINT8_MAX ? (uint8_t)UINT8_MAX : (uint8_t)ratio;
}

// 램프는 페이지에서는 시간이고 엔진에서는 이벤트 수다. 저장 대신 파생값으로 두어야
// 갱신 주기를 바꿔도 사용자가 고른 시간이 그대로 유지된다.
static uint16_t mousekey_config_ramp_ms(void)
{
  return (uint16_t)mousekey_config_storage.time_to_max * (uint16_t)mousekey_config_storage.interval;
}

static void mousekey_config_hold_ramp_ms(uint16_t ramp_ms)
{
  uint16_t interval;
  uint32_t events;

  if (ramp_ms == 0U)
  {
    mousekey_config_storage.time_to_max = 0U;                // off: 고정 속도 모드
    return;
  }

  interval = mousekey_config_storage.interval;              // normalize가 1 이상을 보장한다
  if (interval == 0U)
  {
    interval = 1U;
  }

  events = ((uint32_t)ramp_ms + (interval / 2U)) / interval;
  if (events < 1U)
  {
    events = 1U;
  }
  mousekey_config_storage.time_to_max = events > UINT8_MAX ? (uint8_t)UINT8_MAX : (uint8_t)events;
}

static bool mousekey_config_set_value(uint8_t id, uint8_t *value_data, uint8_t length)
{
  mousekey_config_storage_t previous;
  uint16_t                  ramp_ms;
  uint8_t                   target;
  uint8_t                   level;
  bool                      changed;

  if (value_data == NULL || length < 4U)
  {
    return false;
  }

  previous = mousekey_config_storage;

  switch (id)
  {
    case id_qmk_mousekey_cursor_min_speed:
      // 첫 스텝만 바꾸고 최고 속도는 사용자가 둔 자리에 남긴다. 비율은 두 속도 사이의 값이라
      // 그냥 두면 첫 스텝을 바꿀 때 최고 속도가 끌려간다.
      target = mousekey_config_effective_max_speed();
      mousekey_config_storage.move_delta = mousekey_config_clamp(value_data[0], 1U, MOUSEKEY_CFG_UNIT_MAX);
      mousekey_config_storage.max_speed  = mousekey_config_speed_ratio(target, mousekey_config_storage.move_delta);
      break;

    case id_qmk_mousekey_cursor_max_speed:
      target = mousekey_config_clamp(value_data[0], mousekey_config_storage.move_delta, MOUSEKEY_CFG_UNIT_MAX);
      mousekey_config_storage.max_speed = mousekey_config_speed_ratio(target, mousekey_config_storage.move_delta);
      break;

    case id_qmk_mousekey_cursor_acceleration:
      mousekey_config_hold_ramp_ms((uint16_t)value_data[0] * (uint16_t)MOUSEKEY_CFG_RAMP_UNIT_MS);
      break;

    case id_qmk_mousekey_cursor_interval:
      // 주기를 옮기기 전에 램프를 읽고 뒤에 다시 세운다. 그래야 이 손잡이는 움직임을 얼마나
      // 잘게 쪼갤지만 바꾸는 손잡이가 된다.
      ramp_ms = mousekey_config_ramp_ms();
      mousekey_config_storage.interval = mousekey_config_clamp(value_data[0], MOUSEKEY_CFG_INTERVAL_MIN_MS, UINT8_MAX);
      mousekey_config_hold_ramp_ms(ramp_ms);
      break;

    case id_qmk_mousekey_wheel_interval:
      mousekey_config_storage.wheel_interval = mousekey_config_clamp(value_data[0], MOUSEKEY_CFG_INTERVAL_MIN_MS, UINT8_MAX);
      break;

    case id_qmk_mousekey_wheel_acceleration:
      level = value_data[0];
      if (level >= MOUSEKEY_CFG_WHEEL_ACCEL_COUNT)
      {
        level = MOUSEKEY_CFG_WHEEL_ACCEL_STRONG;
      }
      mousekey_config_storage.wheel_max_speed   = mousekey_wheel_accel[level].max_speed;
      mousekey_config_storage.wheel_time_to_max = mousekey_wheel_accel[level].time_to_max;
      break;

    default:
      return false;
  }

  changed = (memcmp(&previous, &mousekey_config_storage, sizeof(previous)) != 0);
  mousekey_config_commit(changed);
  return true;
}

static void mousekey_config_get_value(uint8_t id, uint8_t *value_data, uint8_t length)
{
  uint32_t units;

  if (value_data == NULL || length < 4U)
  {
    return;
  }

  switch (id)
  {
    case id_qmk_mousekey_cursor_min_speed:
      value_data[0] = mousekey_config_storage.move_delta;
      break;

    case id_qmk_mousekey_cursor_max_speed:
      value_data[0] = mousekey_config_effective_max_speed();
      break;

    case id_qmk_mousekey_cursor_acceleration:
      if (mousekey_config_storage.time_to_max == 0U)
      {
        value_data[0] = 0U;
        break;
      }
      units = ((uint32_t)mousekey_config_ramp_ms() + (MOUSEKEY_CFG_RAMP_UNIT_MS / 2U)) / MOUSEKEY_CFG_RAMP_UNIT_MS;
      value_data[0] = units > UINT8_MAX ? (uint8_t)UINT8_MAX : (uint8_t)units;
      break;

    case id_qmk_mousekey_cursor_interval:
      value_data[0] = mousekey_config_storage.interval;
      break;

    case id_qmk_mousekey_wheel_interval:
      value_data[0] = mousekey_config_storage.wheel_interval;
      break;

    case id_qmk_mousekey_wheel_acceleration:
      // 저장된 속도에서 파생한다. 별도 필드로 두면 페이지가 고르지 않은 값이 들어왔을 때
      // 아무도 고르지 않은 단계로 되읽힌다.
      if (mousekey_config_storage.wheel_max_speed <= mousekey_wheel_accel[MOUSEKEY_CFG_WHEEL_ACCEL_OFF].max_speed)
      {
        value_data[0] = MOUSEKEY_CFG_WHEEL_ACCEL_OFF;
      }
      else if (mousekey_config_storage.wheel_max_speed <= mousekey_wheel_accel[MOUSEKEY_CFG_WHEEL_ACCEL_MILD].max_speed)
      {
        value_data[0] = MOUSEKEY_CFG_WHEEL_ACCEL_MILD;
      }
      else
      {
        value_data[0] = MOUSEKEY_CFG_WHEEL_ACCEL_STRONG;
      }
      break;

    default:
      value_data[0] = 0U;
      break;
  }
}

#endif
