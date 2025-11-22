#include "debounce_runtime.h"
#include "debounce.h"
#include "matrix.h"
#include <stddef.h>
#include <string.h>

#ifndef QMK_DEFAULT_DEBOUNCE_TYPE
#define QMK_DEFAULT_DEBOUNCE_TYPE      sym_defer_pk
#endif
#ifndef QMK_DEFAULT_DEBOUNCE_DELAY
#ifdef DEBOUNCE
#define QMK_DEFAULT_DEBOUNCE_DELAY     DEBOUNCE
#else
#define QMK_DEFAULT_DEBOUNCE_DELAY     5U
#endif
#endif

// V251115R3: 보드 config.h의 디바운스 기본값을 런타임 기본값으로 반영
#define DEBOUNCE_RUNTIME_CFG_sym_defer_pk        { .type = DEBOUNCE_RUNTIME_TYPE_SYM_DEFER_PK,       .pre_ms = (uint8_t)(QMK_DEFAULT_DEBOUNCE_DELAY), .post_ms = (uint8_t)(QMK_DEFAULT_DEBOUNCE_DELAY) }
#define DEBOUNCE_RUNTIME_CFG_sym_eager_pk        { .type = DEBOUNCE_RUNTIME_TYPE_SYM_EAGER_PK,       .pre_ms = 1U,                                       .post_ms = (uint8_t)(QMK_DEFAULT_DEBOUNCE_DELAY) }
#define DEBOUNCE_RUNTIME_CFG_asym_eager_defer_pk { .type = DEBOUNCE_RUNTIME_TYPE_ASYM_EAGER_DEFER_PK, .pre_ms = (uint8_t)(QMK_DEFAULT_DEBOUNCE_DELAY), .post_ms = (uint8_t)(QMK_DEFAULT_DEBOUNCE_DELAY) }
#define DEBOUNCE_RUNTIME_CFG_JOIN(type)          DEBOUNCE_RUNTIME_CFG_##type
#define DEBOUNCE_RUNTIME_CFG(type)               DEBOUNCE_RUNTIME_CFG_JOIN(type)  // V251115R3: 토큰 전달 시 매크로 확장 허용


// V251115R1: VIA 런타임 디바운스 엔진이 각 알고리즘을 동적으로 전환하도록 구현
typedef bool (*debounce_algo_init_t)(uint8_t num_rows);
typedef bool (*debounce_algo_run_t)(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed);
typedef void (*debounce_algo_free_t)(void);


bool debounce_sym_defer_pk_init(uint8_t num_rows);
bool debounce_sym_defer_pk_run(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed);
void debounce_sym_defer_pk_free(void);

bool debounce_sym_eager_pk_init(uint8_t num_rows);
bool debounce_sym_eager_pk_run(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed);
void debounce_sym_eager_pk_free(void);

bool debounce_asym_eager_defer_pk_init(uint8_t num_rows);
bool debounce_asym_eager_defer_pk_run(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed);
void debounce_asym_eager_defer_pk_free(void);


typedef struct
{
  debounce_runtime_type_t type;
  debounce_algo_init_t    init;
  debounce_algo_run_t     run;
  debounce_algo_free_t    free;
  uint8_t                 max_pre_ms;
  uint8_t                 max_post_ms;
} debounce_algo_entry_t;


static const debounce_algo_entry_t k_algorithms[] =
{
  {
    .type        = DEBOUNCE_RUNTIME_TYPE_SYM_DEFER_PK,
    .init        = debounce_sym_defer_pk_init,
    .run         = debounce_sym_defer_pk_run,
    .free        = debounce_sym_defer_pk_free,
    .max_pre_ms  = UINT8_MAX,
    .max_post_ms = UINT8_MAX,
  },
  {
    .type        = DEBOUNCE_RUNTIME_TYPE_SYM_EAGER_PK,
    .init        = debounce_sym_eager_pk_init,
    .run         = debounce_sym_eager_pk_run,
    .free        = debounce_sym_eager_pk_free,
    .max_pre_ms  = UINT8_MAX,
    .max_post_ms = UINT8_MAX,
  },
  {
    .type        = DEBOUNCE_RUNTIME_TYPE_ASYM_EAGER_DEFER_PK,
    .init        = debounce_asym_eager_defer_pk_init,
    .run         = debounce_asym_eager_defer_pk_run,
    .free        = debounce_asym_eager_defer_pk_free,
    .max_pre_ms  = 127,
    .max_post_ms = 127,
  },
};


typedef struct
{
  const debounce_algo_entry_t *algo;
  debounce_runtime_config_t    config;
  uint8_t                      rows;
  bool                         rows_ready;
  bool                         config_ready;
  bool                         pending_reinit;
  debounce_runtime_error_t     last_error;
} debounce_runtime_state_t;

static debounce_runtime_state_t g_runtime = {0};
static const debounce_runtime_config_t k_default_config = DEBOUNCE_RUNTIME_CFG(QMK_DEFAULT_DEBOUNCE_TYPE);


static const debounce_algo_entry_t *debounce_runtime_find_algo(debounce_runtime_type_t type);
static uint8_t                       debounce_runtime_clamp_delay(uint8_t value, uint8_t max_value);
static bool                          debounce_runtime_apply_if_possible(void);
static void                          debounce_runtime_free_active(void);
static bool                          debounce_runtime_passthrough(matrix_row_t raw[],
                                                                  matrix_row_t cooked[],
                                                                  uint8_t      num_rows,
                                                                  bool         changed);


bool debounce_runtime_apply_config(const debounce_runtime_config_t *config)
{
  if (config == NULL)
  {
    return false;
  }

  const debounce_algo_entry_t *algo = debounce_runtime_find_algo(config->type);
  if (algo == NULL)
  {
    g_runtime.last_error = DEBOUNCE_RUNTIME_ERROR_UNSUPPORTED;
    return false;
  }

  debounce_runtime_config_t sanitized = *config;
  sanitized.pre_ms  = debounce_runtime_clamp_delay(config->pre_ms, algo->max_pre_ms);
  sanitized.post_ms = debounce_runtime_clamp_delay(config->post_ms, algo->max_post_ms);

  bool type_changed = (g_runtime.algo != algo);
  g_runtime.algo          = algo;
  g_runtime.config        = sanitized;
  g_runtime.config_ready  = true;
  g_runtime.pending_reinit = true;
  g_runtime.last_error    = DEBOUNCE_RUNTIME_ERROR_NONE;

  if (type_changed)
  {
    debounce_runtime_free_active();
  }

  return debounce_runtime_apply_if_possible();
}

const debounce_runtime_config_t *debounce_runtime_get_config(void)
{
  if (g_runtime.config_ready)
  {
    return &g_runtime.config;
  }
  return &k_default_config;
}

const debounce_runtime_config_t *debounce_runtime_get_default_config(void)
{
  return &k_default_config;                                        // V251115R3: 보드 기본 디바운스 설정 반환
}

debounce_runtime_error_t debounce_runtime_get_last_error(void)
{
  return g_runtime.last_error;
}

bool debounce_runtime_is_ready(void)
{
  return (g_runtime.config_ready == true) &&
         (g_runtime.pending_reinit == false) &&
         (g_runtime.last_error == DEBOUNCE_RUNTIME_ERROR_NONE);
}

uint8_t debounce_runtime_press_delay(void)
{
  return debounce_runtime_get_config()->pre_ms;
}

uint8_t debounce_runtime_release_delay(void)
{
  return debounce_runtime_get_config()->post_ms;
}

void debounce_init(uint8_t num_rows)
{
  g_runtime.rows       = num_rows;
  g_runtime.rows_ready = true;
  debounce_runtime_apply_if_possible();                           // V251115R1: 매트릭스 초기화 시 현재 프로필을 적용
}

bool debounce(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed)
{
  if (g_runtime.algo == NULL || !g_runtime.config_ready)
  {
    return debounce_runtime_passthrough(raw, cooked, num_rows, changed);      // V251115R6: 런타임 준비 실패 시 입력 우회 처리
  }

  if (!debounce_runtime_is_ready())                                             // V251115R4: 재초기화 대기/오류 시 안전 경로로 우회
  {
    debounce_runtime_apply_if_possible();
    if (!debounce_runtime_is_ready())
    {
      return debounce_runtime_passthrough(raw, cooked, num_rows, changed);    // V251115R6: 적용 실패 시 키 입력 손실 방지
    }
  }

  if (num_rows != g_runtime.rows)
  {
    return g_runtime.algo->run(raw, cooked, g_runtime.rows, changed);
  }
  return g_runtime.algo->run(raw, cooked, num_rows, changed);
}

void debounce_free(void)
{
  debounce_runtime_free_active();
  g_runtime.rows_ready    = false;
  g_runtime.pending_reinit = true;
}

static const debounce_algo_entry_t *debounce_runtime_find_algo(debounce_runtime_type_t type)
{
  for (size_t i = 0; i < (sizeof(k_algorithms)/sizeof(k_algorithms[0])); ++i)
  {
    if (k_algorithms[i].type == type)
    {
      return &k_algorithms[i];
    }
  }
  return NULL;
}

static uint8_t debounce_runtime_clamp_delay(uint8_t value, uint8_t max_value)
{
  if (value == 0U)
  {
    value = 1U;
  }
  if (value > max_value)
  {
    value = max_value;
  }
  return value;
}

static bool debounce_runtime_apply_if_possible(void)
{
  if (!g_runtime.config_ready || g_runtime.algo == NULL)
  {
    return false;
  }

  if (!g_runtime.rows_ready)
  {
    g_runtime.pending_reinit = true;
    return true;
  }

  if (g_runtime.pending_reinit == false)
  {
    return true;
  }

  debounce_runtime_free_active();

  if (!g_runtime.algo->init(g_runtime.rows))
  {
    g_runtime.last_error     = DEBOUNCE_RUNTIME_ERROR_ALLOC;
    g_runtime.pending_reinit = true;
    return false;
  }

  g_runtime.pending_reinit = false;
  g_runtime.last_error     = DEBOUNCE_RUNTIME_ERROR_NONE;
  return true;
}

static void debounce_runtime_free_active(void)
{
  if (g_runtime.algo != NULL && g_runtime.algo->free != NULL)
  {
    g_runtime.algo->free();
  }
}

static bool debounce_runtime_passthrough(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed)
{
  bool updated = changed;

  for (uint8_t row = 0; row < num_rows; row++)
  {
    if (cooked[row] != raw[row])
    {
      cooked[row] = raw[row];
      updated     = true;
    }
  }

  return updated;
}
