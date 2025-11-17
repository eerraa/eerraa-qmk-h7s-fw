#pragma once


#include <stdbool.h>
#include <stdint.h>
#include "quantum/debounce_runtime.h"


typedef struct
{
  debounce_runtime_type_t type;
  uint8_t                 pre_ms;
  uint8_t                 post_ms;
} debounce_profile_values_t;  // V251115R1: VIA 런타임 디바운스 값 캐시

typedef enum
{
  DEBOUNCE_PROFILE_STATUS_READY   = 0,
  DEBOUNCE_PROFILE_STATUS_PENDING = 1,
  DEBOUNCE_PROFILE_STATUS_ERROR   = 2,
} debounce_profile_status_t;   // V251115R1: VIA 표시용 상태 코드


void                        debounce_profile_init(void);
void                        debounce_profile_apply_current(void);
const debounce_profile_values_t *
                             debounce_profile_current(void);
debounce_profile_status_t   debounce_profile_get_status(void);
bool                        debounce_profile_set_mode(uint8_t mode);
bool                        debounce_profile_set_single_delay(uint8_t delay_ms);
bool                        debounce_profile_set_press_delay(uint8_t delay_ms);
bool                        debounce_profile_set_release_delay(uint8_t delay_ms);
void                        debounce_profile_save(bool force);
void                        debounce_profile_restore_defaults(void);
void                        debounce_profile_storage_apply_defaults(void);
bool                        debounce_profile_handle_via_command(uint8_t *data, uint8_t length);
