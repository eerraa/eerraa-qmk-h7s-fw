# Build actual source regions without rewriting their production statements.
import re
from pathlib import Path

from rgb_modes import check_fixture_numbers

def function(source, name):
    m=re.search(rf"(?:static\s+)?(?:void|bool|uint\d+_t)\s+{name}\([^;]*?\)\s*\{{",source)  # V260913R1: uint8_t 반환 함수(채도 복원 규칙)도 추출한다
    assert m, name
    i=m.end(); depth=1
    while depth:
        depth += (source[i]=='{')-(source[i]=='}'); i+=1
    return source[m.start():i]

def generate(root: Path, build: Path):
    via=(root/'src/ap/modules/qmk/quantum/via.c').read_text(encoding='utf-8')
    header=(root/'src/ap/modules/qmk/quantum/via.h').read_text(encoding='utf-8')
    ids=re.search(r'enum via_command_id\s*\{.*?\};',header,re.S).group(0)
    body=function(via,'raw_hid_receive')
    prefix=body[body.index('{')+1:body.index('    uint8_t *command_id')]
    assert 'via_command_kb' not in prefix and body.index('data[3] > 28U') < body.index('via_command_kb(')
    out=build/'test_via_guard.c'
    out.write_text('#include <assert.h>\n#include <stddef.h>\n#include <stdint.h>\n#include <stdio.h>\n'+ids+'''
static unsigned passed_guard;
static void guard(uint8_t *data, uint8_t length) {\n'''+prefix+'''
passed_guard++;
}
int main(void) {
  struct { uint32_t before; uint8_t data[32]; uint32_t after; } p = {.before=0x12345678U,.after=0xABCDEF01U};
  for (unsigned command=0; command<256U; command++) for(unsigned size=0; size<256U; size++) {
    p.data[0]=(uint8_t)command; p.data[3]=(uint8_t)size;
    unsigned before=passed_guard;
    guard(p.data,32U);
    int buffer=(command==id_dynamic_keymap_macro_get_buffer || command==id_dynamic_keymap_macro_set_buffer ||
                command==id_dynamic_keymap_get_buffer || command==id_dynamic_keymap_set_buffer);
    if(buffer && size>28U) assert(p.data[0]==id_unhandled && passed_guard==before);
    else assert(p.data[0]==command && passed_guard==before+1U);
    assert(p.before==0x12345678U && p.after==0xABCDEF01U);
  }
  for(unsigned length=0; length<256U; length++) {
    unsigned before=passed_guard;
    guard(NULL,(uint8_t)length); assert(passed_guard==before);
    p.data[0]=id_get_protocol_version;
    guard(p.data,(uint8_t)length);
    assert(passed_guard==before+(length==32U));
  }
  puts("PASS: actual VIA pre-dispatch guard, all 65536 command/size pairs, 256 frame lengths, NULL/canaries");
  return 0;
}
''',encoding='utf-8')
    usb=(root/'src/hw/driver/usb/usb.c').read_text(encoding='utf-8')
    reset=function(usb,'usbProcessDeferredReset')
    out_reset=build/'test_reset_barrier.c'
    out_reset.write_text('''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define USB_NON_MODE 0U
#define USB_RESET_DETACH_DELAY_MS 100U
static struct { bool pending; uint32_t ready_ms; } usb_reset_request;
static bool dirty, replies, is_init=true;
static uint32_t now, stops, deinits, resets, waits, USBD_Device, is_usb_mode=1U;
static uint32_t millis(void) { return now; }
static bool eeprom_is_pending(void) { return dirty; }
static bool usbHidViaResponsesPending(void) { return replies; }
static void USBD_Stop(uint32_t *d) { (void)d; stops++; }
static void USBD_DeInit(uint32_t *d) { (void)d; deinits++; }
static void delay(uint32_t ms) { assert(ms==100U); waits++; }
static void resetToReset(void) { resets++; }
'''+reset+'''
int main(void) {
 usb_reset_request.pending=true; usb_reset_request.ready_ms=4U; now=UINT32_MAX-3U;
 usbProcessDeferredReset(); assert(stops==0U && usb_reset_request.pending);
 now=4U; dirty=true; replies=true; usbProcessDeferredReset(); assert(stops==0U);
 dirty=false; usbProcessDeferredReset(); assert(stops==0U);
 dirty=true; replies=false; usbProcessDeferredReset(); assert(stops==0U);
 dirty=false; usbProcessDeferredReset();
 assert(stops==1U && deinits==1U && resets==1U && waits==1U && !is_init && is_usb_mode==USB_NON_MODE);
 assert(!usb_reset_request.pending); usbProcessDeferredReset(); assert(stops==1U);
 puts("PASS: actual reset-service function waits for grace, persistence ACK and VIA completion without blocking input");
 return 0;
}
''',encoding='utf-8')
    rgb=(root/'src/ap/modules/qmk/quantum/rgblight/rgblight.c').read_text(encoding='utf-8')
    keyboard=(root/'src/ap/modules/qmk/quantum/keyboard.c').read_text(encoding='utf-8')
    suspend=(root/'src/ap/modules/qmk/port/platforms/suspend.c').read_text(encoding='utf-8')
    matrix_task=function(keyboard,'matrix_task')
    wake_event=function(suspend,'suspend_wakeup_key_event')
    assert 'suspend_wakeup_key_event(row, col, key_pressed);' in matrix_task
    assert matrix_task.index('suspend_wakeup_key_event(row, col, key_pressed);') < matrix_task.index('action_exec(event);')
    assert 'if (pressed)' in wake_event and 'usbHidRequestRemoteWakeFromInput()' in wake_event

    hid=(root/'src/hw/driver/usb/usb_hid/usbd_hid.c').read_text(encoding='utf-8')
    wake_entry=function(hid,'usbHidRequestRemoteWakeFromInput')
    wake=function(hid,'usbHidRemoteWakeSuspended')
    keyboard_send=function(hid,'usbHidSendReport')
    extra_send=function(hid,'usbHidSendReportEXK')
    on_resume=function(hid,'usbHidOnResume')
    accept_sof=function(hid,'usbHidConsumeWakeSof')
    assert 'USBD_STATE_SUSPENDED' in wake_entry and 'usbHidRemoteWakeSuspended()' in wake_entry
    assert 'usbHidLock' not in wake_entry and '__disable_irq' not in wake_entry
    assert 'GINTMSK &= ~USB_OTG_GINTMSK_WUIM' in wake
    assert 'delay(10U)' in wake and 'HAL_PCD_DeActivateRemoteWakeup(pcd)' in wake
    assert 'USB_OTG_GINTSTS_WKUINT' in wake and 'USB_OTG_DSTS_SUSPSTS' in wake
    assert 'USB_OTG_PCGCCTL_GATECLK' not in wake
    assert 'usbHidRequestRemoteWakeFromInput' not in keyboard_send
    assert 'usbHidRequestRemoteWakeFromInput' not in extra_send
    assert 'USB_HID_WAKE_IDLE' in on_resume and 'wake_skip_stale_sof' in on_resume
    assert 'USBD_STATE_SUSPENDED' not in on_resume and 'USB_OTG_DSTS_SUSPSTS' not in on_resume
    assert 'USB_HID_WAKE_IDLE' in accept_sof and 'wake_skip_stale_sof' in accept_sof
    assert 'USBD_STATE_SUSPENDED' not in accept_sof and 'USB_OTG_DSTS_SUSPSTS' not in accept_sof

    irq=(root/'src/bsp/device/stm32h7rsxx_it.c').read_text(encoding='utf-8')
    assert 'usbHidWakeTick' not in irq
    conf=(root/'src/hw/driver/usb/usbd_conf.c').read_text(encoding='utf-8')
    hardware_active=function(conf,'usbPcdHardwareActive')
    suspended_sof=function(conf,'usbHidLogicalSuspendedSof')
    sof_callback=conf[conf.index('* @brief  SOF callback.'):conf.index('* @brief  Reset callback.')]
    reset_callback=conf[conf.index('* @brief  Reset callback.'):conf.index('* @brief  Suspend callback.')]
    suspend_callback=conf[conf.index('* @brief  Suspend callback.'):conf.index('* @brief  Resume callback.')]
    resume_callback=conf[conf.index('* @brief  Resume callback.'):conf.index('* @brief  ISOOUTIncomplete callback.')]
    resume_active=function(conf,'usbPcdResumeIfActive')
    assert 'static volatile bool bus_suspended = false;' in conf
    assert 'USB_OTG_DSTS_SUSPSTS' in hardware_active and '== 0U' in hardware_active
    assert 'if (bus_suspended && usbPcdHardwareActive(hpcd))' in sof_callback
    assert 'pdev->dev_state == USBD_STATE_SUSPENDED' in sof_callback and 'usbHidLogicalSuspendedSof(hpcd)' in sof_callback
    assert 'usbHidConsumeWakeSof()' in suspended_sof
    assert 'usbPcdHardwareActive(hpcd)' in suspended_sof
    assert suspended_sof.index('usbHidConsumeWakeSof()') < suspended_sof.index('USBD_LL_SOF(pdev)')
    assert 'bus_suspended = false;' in reset_callback
    assert reset_callback.index('bus_suspended = false;') < reset_callback.index('USBD_LL_Reset')
    assert 'bus_suspended = true;' in suspend_callback
    assert 'hardware_resumed = usbPcdHardwareActive(hpcd)' in resume_callback
    assert 'bus_suspended = false;' in resume_callback
    assert 'pdev->dev_state == USBD_STATE_SUSPENDED' in resume_callback
    assert resume_callback.index('__HAL_PCD_UNGATE_PHYCLOCK(hpcd)') < resume_callback.index('usbPcdHardwareActive(hpcd)')
    assert 'pcd_resume_pending = true;' in resume_callback
    assert 'USB_OTG_GINTSTS_SOF' in resume_callback and 'pcd_resume_skip_stale_sof' in suspended_sof
    assert 'usbPcdResumeIfActive(hpcd)' in resume_callback
    assert 'usbPcdHardwareActive(hpcd)' in resume_active and 'USBD_STATE_SUSPENDED' in resume_active
    assert resume_active.index('usbHidOnResume()') < resume_active.index('USBD_LL_Resume(pdev)')
    assert resume_active.count('USBD_LL_Resume(pdev)') == 1

    sleep=(root/'src/ap/modules/qmk/port/rgb_sleep.c').read_text(encoding='utf-8')
    sleep_apply=function(sleep,'rgb_sleep_apply_rgb')
    output_gate=function(rgb,'rgblight_set_output_suspend_state')
    render=function(rgb,'rgblight_render_frame')
    task=function(rgb,'rgblight_task')
    assert 'rgblight_set_output_suspend_state(want_dark)' in sleep_apply
    assert 'rgblight_suspend' not in sleep_apply and 'rgblight_disable_noeeprom' not in sleep_apply
    assert 'rgblight_config' not in output_gate and 'rgblight_request_render' in output_gate
    assert 'output_suspended' in render and 'memset(rgblight_frame, 0' in render and 'rgblight_driver.setleds(rgblight_frame, num_leds)' in render
    assert 'if (output_suspended)' in task and 'rgblight_flush_render_queue()' in task
    gate=function(rgb,'rgblight_task_periodic_due')
    assert 'timer_expired32' in gate and 'rgblight_task_slice_armed = false' in gate
    out_rgb=build/'test_rgb_task_gate.c'
    out_rgb.write_text(r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define timer_expired32(current, future) ((uint32_t)((current) - (future)) < UINT32_MAX / 2U)
static uint32_t rgblight_task_next_run;
static bool rgblight_task_slice_armed;
''' + gate + r'''
int main(void) {
  assert(!rgblight_task_periodic_due(false, false, 0U));
  assert(rgblight_task_periodic_due(true, false, 100U));
  assert(!rgblight_task_periodic_due(true, false, 100U));
  assert(rgblight_task_periodic_due(true, false, 101U));
  assert(!rgblight_task_periodic_due(false, false, 0U));
  /* More than a 16-bit half-range later: inactivity disarmed the gate, so resume is immediate. */
  assert(rgblight_task_periodic_due(true, false, 0x00018000U));
  assert(rgblight_task_periodic_due(true, true, 0U));
  assert(rgblight_task_periodic_due(true, false, 0x00018000U));
  /* 32-bit wrap: a +1 ms deadline remains ordered across UINT32_MAX. */
  assert(!rgblight_task_periodic_due(false, false, 0U));
  assert(rgblight_task_periodic_due(true, false, UINT32_MAX));
  assert(rgblight_task_periodic_due(true, false, 0U));
  puts("PASS: actual RGB task gate disarms while inactive and resumes immediately with 32-bit wrap-safe scheduling");
  return 0;
}
''',encoding='utf-8')

    # V260911R2: TD/LT 지연 정책은 RGB 통합 fixture가 실제 wait 포트와 함께 실행해 검사한다.

    qmk=(root/'src/ap/modules/qmk/qmk.c').read_text(encoding='utf-8')
    qmk_update=function(qmk,'qmkUpdate')
    assert 'keyboard_task();' in qmk_update and 'ws2812Task();' in qmk_update
    assert qmk_update.index('keyboard_task();') < qmk_update.index('ws2812Task();')

    ws=(root/'src/hw/driver/ws2812.c').read_text(encoding='utf-8')
    ws_start=function(ws,'ws2812StartTransfer')
    ws_service=function(ws,'ws2812Service')
    ws_refresh=function(ws,'ws2812Refresh')
    assert 'HAL_TIM_PWM_Stop_DMA' not in ws_refresh
    assert 'HAL_DMA_GetError' in ws_service and 'WS2812_QUIESCING' in ws_service
    assert ws_service.index('HAL_DMA_GetError') < ws_service.index('ws2812StopOutput();', ws_service.index('HAL_DMA_GetError'))
    assert 'ws2812_refresh_pending = true' in ws_refresh
    assert 'TIM_CCDMAREQUEST_UPDATE' in function(ws,'ws2812Init')
    assert '__DMB()' in ws_start and 'WS2812_PIPELINE_SLOTS' in ws
    assert ws_start.index('ws2812PinMode(2U)') < ws_start.index('HAL_TIM_PWM_Start_DMA')
    stop = function(ws, 'ws2812StopOutput')
    assert stop.index('ws2812PinIdle()') < stop.index('HAL_TIM_PWM_Stop_DMA')
    assert 'delay(' not in ws_start and 'delay(' not in ws_service
    assert '&led[' not in function(rgb, 'rgblight_indicator_apply_overlay')
    assert 'memcpy(rgblight_frame, led' in render
    for port_path in (root/'src/ap/modules/qmk/keyboards/era').glob('**/port/indicator_port.c'):
        port = port_path.read_text(encoding='utf-8')
        refresh = function(port, 'led_update_ports')
        assert 'rgblight_indicator_request_host_refresh()' in refresh
        assert 'rgblight_indicator_post_host_event' not in refresh
    out_ws=root/'tools/firmware_regression_tests/test_ws2812_transport.c'

    # Kill switch는 호스트가 받는 report에서 판정한다: 6KRO 송신은 사본을 filter에 통과시킨 뒤 중복을 거르고 보낸다.
    sender=function((root/'src/ap/modules/qmk/quantum/action_util.c').read_text(encoding='utf-8'),'send_6kro_report')
    assert sender.index('keyboard_report_filter(&report);') < sender.index('memcmp(&report, &last_report')
    assert 'host_keyboard_send(&report);' in sender and 'host_keyboard_send(keyboard_report)' not in sender
    assert 'kill_switch_process' not in qmk
    # V260913R1: 채도 복원 규칙은 목적지 효과로 판정한다. Solid Color 출발 한정이 되살아나면 실패한다.
    carries=function(rgb,'rgblight_mode_carries_hue')
    transition=function(rgb,'rgblight_mode_transition_sat')
    assert 'RGBLIGHT_MODE_STATIC_LIGHT' not in transition
    assert 'old_base_mode == new_base_mode' in transition and 'rgblight_mode_carries_hue(new_base_mode)' in transition
    helper=function(rgb,'rgblight_sethsv_eeprom_helper')
    assert 'rgblight_effect_pulse_evaluate_output' not in helper
    assert helper.index('rgblight_config.val = val;') < helper.index('rgblight_effect_pulse_on_base_mode_update();')
    assert helper.index('rgblight_config.val = val;') < helper.index('rgblight_effect_pulse_on_hsv_update();')
    assert task.index('rgblight_consume_host_led_queue();') < task.index('rgblight_effect_pulse_evaluate_output();')
    out_sat=build/'test_rgb_mode_transition.c'
    out_sat.write_text(r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define RGBLIGHT_EFFECT_RAINBOW_MOOD
#define RGBLIGHT_EFFECT_RAINBOW_SWIRL
#define RGBLIGHT_EFFECT_STATIC_GRADIENT
#define RGBLIGHT_EFFECT_CHRISTMAS
enum { RGBLIGHT_MODE_STATIC_LIGHT = 1, RGBLIGHT_MODE_BREATHING = 2, RGBLIGHT_MODE_RAINBOW_MOOD = 6, RGBLIGHT_MODE_RAINBOW_SWIRL = 9,
       RGBLIGHT_MODE_SNAKE = 15, RGBLIGHT_MODE_KNIGHT = 21, RGBLIGHT_MODE_CHRISTMAS = 24, RGBLIGHT_MODE_STATIC_GRADIENT = 25,
       RGBLIGHT_MODE_RGB_TEST = 35, RGBLIGHT_MODE_ALTERNATING = 36, RGBLIGHT_MODE_TWINKLE = 37,
       RGBLIGHT_MODE_PULSE_OFF_PRESS = 43, RGBLIGHT_MODE_PULSE_ON_PRESS = 44, RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD = 45,
       RGBLIGHT_MODE_PULSE_ON_PRESS_HOLD = 46 };
/* Board layout with every effect enabled: each variant maps onto its base mode exactly as rgblight_modes.h expands. */
static const uint8_t mode_base_table[47] = {0, 1, 2, 2, 2, 2, 6, 6, 6, 9, 9, 9, 9, 9, 9, 15, 15, 15, 15, 15, 15, 21, 21, 21, 24,
  25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 35, 36, 37, 37, 37, 37, 37, 37, 43, 44, 45, 46};
''' + carries + '\n' + transition + r'''
int main(void) {
  /* white (sat 0) entering a hue-carrying effect restores full saturation, from any different base mode */
  assert(rgblight_mode_transition_sat(RGBLIGHT_MODE_STATIC_LIGHT, RGBLIGHT_MODE_RAINBOW_MOOD, 0) == UINT8_MAX);
  assert(rgblight_mode_transition_sat(RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, RGBLIGHT_MODE_RAINBOW_MOOD, 0) == UINT8_MAX);
  assert(rgblight_mode_transition_sat(RGBLIGHT_MODE_BREATHING + 2, RGBLIGHT_MODE_RAINBOW_SWIRL + 3, 0) == UINT8_MAX);
  assert(rgblight_mode_transition_sat(RGBLIGHT_MODE_PULSE_ON_PRESS, RGBLIGHT_MODE_STATIC_GRADIENT + 4, 0) == UINT8_MAX);
  assert(rgblight_mode_transition_sat(RGBLIGHT_MODE_SNAKE, RGBLIGHT_MODE_CHRISTMAS, 0) == UINT8_MAX);
  assert(rgblight_mode_transition_sat(RGBLIGHT_MODE_RAINBOW_MOOD, RGBLIGHT_MODE_RAINBOW_SWIRL, 0) == UINT8_MAX);
  /* variants inside one effect, and effects that stay visible in white, keep the chosen saturation */
  assert(rgblight_mode_transition_sat(RGBLIGHT_MODE_RAINBOW_MOOD, RGBLIGHT_MODE_RAINBOW_MOOD + 1, 0) == 0);
  assert(rgblight_mode_transition_sat(RGBLIGHT_MODE_STATIC_GRADIENT, RGBLIGHT_MODE_STATIC_GRADIENT + 9, 0) == 0);
  assert(rgblight_mode_transition_sat(RGBLIGHT_MODE_STATIC_LIGHT, RGBLIGHT_MODE_BREATHING, 0) == 0);
  assert(rgblight_mode_transition_sat(RGBLIGHT_MODE_STATIC_LIGHT, RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 0) == 0);
  assert(rgblight_mode_transition_sat(RGBLIGHT_MODE_RAINBOW_MOOD, RGBLIGHT_MODE_STATIC_LIGHT, 0) == 0);
  assert(rgblight_mode_transition_sat(RGBLIGHT_MODE_STATIC_LIGHT, RGBLIGHT_MODE_TWINKLE + 3, 0) == 0);
  assert(rgblight_mode_transition_sat(RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, RGBLIGHT_MODE_PULSE_OFF_PRESS_HOLD, 0) == 0);
  /* a saturated colour is never touched */
  assert(rgblight_mode_transition_sat(RGBLIGHT_MODE_STATIC_LIGHT, RGBLIGHT_MODE_RAINBOW_MOOD, 17) == 17);
  assert(rgblight_mode_transition_sat(RGBLIGHT_MODE_PULSE_OFF_PRESS, RGBLIGHT_MODE_CHRISTMAS, 255) == 255);
  puts("PASS: actual saturation-restore rule keys on the destination effect, not on leaving Solid Color");
  return 0;
}
''',encoding='utf-8')
    check_fixture_numbers(root/'src/ap/modules/qmk', out_sat.read_text(encoding='utf-8'), out_sat.name)
    return out, out_reset, out_rgb, out_ws, out_sat
