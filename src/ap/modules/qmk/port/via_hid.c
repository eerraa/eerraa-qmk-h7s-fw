#include "via_hid.h"
#include "raw_hid.h"
#include <string.h>
#include "log.h"
#include "qbuffer.h"
#include "usb_hid.h"


#define USE_VIA_HID_PRINT   0
#define VIA_HID_REPORT_SIZE 32U
#define VIA_HID_RX_QUEUE_DEPTH 16U                                   // V251108R8: VIA 명령 큐를 16개로 제한

typedef struct
{
  uint8_t len;
  uint8_t buf[VIA_HID_REPORT_SIZE];
} via_hid_packet_t;

static qbuffer_t            via_hid_rx_q;
static via_hid_packet_t     via_hid_rx_buf[VIA_HID_RX_QUEUE_DEPTH];
static volatile uint32_t    via_hid_rx_drop_cnt = 0;


#if USE_VIA_HID_PRINT == 1
static const char *command_id_str[] =
{
  [id_get_protocol_version]                 = "id_get_protocol_version",
  [id_get_keyboard_value]                   = "id_get_keyboard_value",
  [id_set_keyboard_value]                   = "id_set_keyboard_value",
  [id_dynamic_keymap_get_keycode]           = "id_dynamic_keymap_get_keycode",
  [id_dynamic_keymap_set_keycode]           = "id_dynamic_keymap_set_keycode",
  [id_dynamic_keymap_reset]                 = "id_dynamic_keymap_reset",
  [id_custom_set_value]                     = "id_custom_set_value",
  [id_custom_get_value]                     = "id_custom_get_value",
  [id_custom_save]                          = "id_custom_save",
  [id_eeprom_reset]                         = "id_eeprom_reset",
  [id_bootloader_jump]                      = "id_bootloader_jump",
  [id_dynamic_keymap_macro_get_count]       = "id_dynamic_keymap_macro_get_count",
  [id_dynamic_keymap_macro_get_buffer_size] = "id_dynamic_keymap_macro_get_buffer_size",
  [id_dynamic_keymap_macro_get_buffer]      = "id_dynamic_keymap_macro_get_buffer",
  [id_dynamic_keymap_macro_set_buffer]      = "id_dynamic_keymap_macro_set_buffer",
  [id_dynamic_keymap_macro_reset]           = "id_dynamic_keymap_macro_reset",
  [id_dynamic_keymap_get_layer_count]       = "id_dynamic_keymap_get_layer_count",
  [id_dynamic_keymap_get_buffer]            = "id_dynamic_keymap_get_buffer",
  [id_dynamic_keymap_set_buffer]            = "id_dynamic_keymap_set_buffer",
  [id_dynamic_keymap_get_encoder]           = "id_dynamic_keymap_get_encoder",
  [id_dynamic_keymap_set_encoder]           = "id_dynamic_keymap_set_encoder",
  [id_unhandled]                            = "id_unhandled",
};

static void via_hid_print(uint8_t *data, uint8_t length, bool is_resp);
#endif


static void via_hid_receive(uint8_t *data, uint8_t length);


void via_hid_init(void)
{
  qbufferCreateBySize(&via_hid_rx_q, (uint8_t *)via_hid_rx_buf, sizeof(via_hid_packet_t), VIA_HID_RX_QUEUE_DEPTH);
  usbHidSetViaReceiveFunc(via_hid_receive);                          // V251108R8: RX 큐 초기화 후 USB ISR 등록
}

void raw_hid_send(uint8_t *data, uint8_t length)
{
  
}

void via_hid_receive(uint8_t *data, uint8_t length)
{
  #if USE_VIA_HID_PRINT == 1
  via_hid_print(data, length, true);
  #endif

  if (data == NULL || length == 0U)
  {
    return;
  }

  via_hid_packet_t packet;
  packet.len = length > VIA_HID_REPORT_SIZE ? VIA_HID_REPORT_SIZE : length;
  memset(packet.buf, 0, sizeof(packet.buf));
  memcpy(packet.buf, data, packet.len);

  if (qbufferWrite(&via_hid_rx_q, (uint8_t *)&packet, 1) != true)
  {
    via_hid_rx_drop_cnt++;                                          // V251108R8: ISR에서 로그 대신 카운터만 증가
  }
}

void via_hid_task(void)
{
  via_hid_packet_t packet;

  if (via_hid_rx_drop_cnt > 0U)
  {
    uint32_t dropped = via_hid_rx_drop_cnt;
    via_hid_rx_drop_cnt = 0U;
    logPrintf("[!] VIA RX queue overflow : %lu\n", dropped);        // V251108R8: 메인 루프에서만 로그 출력
  }

  while (qbufferAvailable(&via_hid_rx_q) > 0U)
  {
    if (qbufferRead(&via_hid_rx_q, (uint8_t *)&packet, 1) != true)
    {
      break;
    }

    raw_hid_receive(packet.buf, packet.len);                        // V251108R8: VIA 명령을 메인 루프에서 처리

    if (usbHidEnqueueViaResponse(packet.buf, packet.len) != true)
    {
      logPrintf("[!] VIA TX enqueue failed\n");                     // V251108R8: 호스트 응답 큐 적재 실패 감시
      break;
    }
  }
}

#if USE_VIA_HID_PRINT == 1
void via_hid_print(uint8_t *data, uint8_t length, bool is_resp)
{
  uint8_t *command_id   = &(data[0]);
  uint8_t *command_data = &(data[1]);  
  uint8_t data_len = length - 1;


  logPrintf("[%s] id : 0x%02X, 0x%02X, len %d,  %s",
            is_resp == true ? "OK" : "  ",
            *command_id,
            command_data[0],
            length,
            command_id_str[*command_id]);

  if (is_resp == true)
  {
    logPrintf("\n");
    return;
  }

  for (int i=0; i<data_len; i++)
  {
    if (i%8 == 0)
      logPrintf("\n     ");
    logPrintf("0x%02X ", command_data[i]);
  }
  logPrintf("\n");  
}
#endif
