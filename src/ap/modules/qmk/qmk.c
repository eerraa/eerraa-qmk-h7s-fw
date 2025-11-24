#include "qmk.h"
#include "qmk/port/port.h"
#include "qmk/port/platforms/eeprom.h"            // V251112R5: EEPROM 버스트 모드 제어
#include "qmk/port/debounce_profile.h"


static void cliQmk(cli_args_t *args);
static void idle_task(void);

static bool is_suspended = false;




bool qmkInit(void)
{
  eeprom_init();
  via_hid_init();
  debounce_profile_init();                         // V251115R1: VIA 디바운스 프로필 초기 로드
#ifdef G_TERM_ENABLE
  tapping_term_init();                             // V251123R4: VIA TAPPING 설정 초기 로드
#endif
#ifdef TAPDANCE_ENABLE
  tapdance_init();                                 // V251124R8: VIA TAPDANCE 설정 초기 로드
#endif

  keyboard_setup();
  keyboard_init();

  
  is_suspended = usbIsSuspended();

  logPrintf("[  ] qmkInit()\n");
  logPrintf("     MATRIX_ROWS : %d\n", MATRIX_ROWS);
  logPrintf("     MATRIX_COLS : %d\n", MATRIX_COLS);
  const debounce_profile_values_t *profile = debounce_profile_current();
  logPrintf("     DEBOUNCE    : mode %d, pre %d ms, post %d ms\n",
            profile->type,
            profile->pre_ms,
            profile->post_ms);                    // V251115R1: VIA 런타임 디바운스 상태 로그

  cliAdd("qmk", cliQmk);
  return true;
}

void qmkUpdate(void)
{
  via_hid_task();                                                // V251108R8: VIA 명령을 메인 루프에서 처리해 USB ISR 부하 감소
  keyboard_task();
  eeprom_task();
  uint8_t burst_calls = eeprom_get_burst_extra_calls();          // V251112R5: 큐 적체 시 추가 페이지 플러시
  while (burst_calls-- > 0 && eeprom_is_pending())
  {
    eeprom_update();                                             // V251112R5: 버스트 모드 동안 즉시 추가 처리
  }
  idle_task();
}

void keyboard_post_init_user(void)
{
#ifdef KILL_SWITCH_ENABLE
  kill_switch_init();
#endif
#ifdef KKUK_ENABLE
  kkuk_init();
#endif
}

bool process_record_user(uint16_t keycode, keyrecord_t *record)
{
#ifdef KILL_SWITCH_ENABLE
  kill_switch_process(keycode, record);
#endif
#ifdef KKUK_ENABLE
  kkuk_process(keycode, record);
#endif
  return true;
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record)
{
#ifdef TAPDANCE_ENABLE
  if (IS_KB_KEYCODE(keycode))
  {
    uint8_t idx = (uint8_t)(keycode - QK_KB_0);
    if (idx < TAPDANCE_SLOT_COUNT)
    {
      keycode = (QK_TAP_DANCE | idx);                            // V251124R8: VIA customKeycodes TDn → TD(n) 매핑
    }
  }
#endif
  return process_record_user(keycode, record);
}

void idle_task(void)
{
  bool is_suspended_cur;

  is_suspended_cur = usbIsSuspended();
  if (is_suspended_cur != is_suspended)
  {
    if (is_suspended_cur)
    {
      suspend_power_down();
    }
    else
    {
      suspend_wakeup_init();
    }

    is_suspended = is_suspended_cur;
  }

#ifdef KKUK_ENABLE
  kkuk_idle();
#endif
}

void cliQmk(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 2 && args->isStr(0, "clear") && args->isStr(1, "eeprom"))
  {
    eeconfig_init();
    cliPrintf("Clearing EEPROM\n");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("qmk info\n");
    cliPrintf("qmk clear eeprom\n");
  }
}
