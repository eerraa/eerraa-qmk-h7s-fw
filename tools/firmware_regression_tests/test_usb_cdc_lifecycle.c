#include "usb_cdc_lifecycle_hardware.h"
#include "usbd_cdc.h"
#include "usbd_ctlreq.h"
#include "usbd_core.h"
#ifdef USE_USBD_COMPOSITE
#include "usbd_cmp.h"
#endif
#include <stdio.h>

uint32_t test_irqmask, test_ipsr;
USBD_HandleTypeDef USBD_Device;
static PCD_HandleTypeDef pcd;
static bool opened[256], tx_armed[16];
static USBD_StatusTypeDef open_status[256], close_status[256], tx_status, rx_status;
static uint32_t open_calls[256], close_calls[256], tx_calls, rx_calls;
static uint16_t opened_size[256];
static uint8_t *active_tx[16], *active_rx;
static uint32_t active_tx_length[16], active_rx_length, completed_rx_length;
static uint8_t tx_image[16][2048];
static bool close_stops_on_failure[256];
static uint32_t clock_ms, init_calls, deinit_calls, receive_calls, transmit_complete_calls, control_calls;
static uint8_t interface_init_status, interface_deinit_status;
static bool missing_rx_buffer;
static const uint8_t *control_tx;
static uint8_t *control_rx;
static uint32_t control_errors;
static USBD_StatusTypeDef ll_stop_status, ll_deinit_status;
static uint32_t ll_stop_calls, ll_deinit_calls, ep0_open_calls;
static void *last_deinit_interface;

#ifdef USE_USBD_COMPOSITE
#define TEST_CDC_CLASS_ID 1U
uint8_t USBD_CoreGetEPAdd(USBD_HandleTypeDef *d, uint8_t direction, uint8_t type, uint8_t class_id)
{
  assert(d == &USBD_Device && class_id == TEST_CDC_CLASS_ID);
  return type == USBD_EP_TYPE_INTR ? CDC_CMD_EP : direction == USBD_EP_IN ? CDC_IN_EP : CDC_OUT_EP;
}
uint32_t USBD_CMPSIT_GetClassID(USBD_HandleTypeDef *d, USBD_CompositeClassTypeDef type, uint32_t instance)
{
  assert(d == &USBD_Device && type == CLASS_TYPE_CDC && instance == 0U);
  return TEST_CDC_CLASS_ID;
}
#else
#define TEST_CDC_CLASS_ID 0U
#endif

uint32_t millis(void) { return clock_ms; }
uint32_t micros(void) { return clock_ms * 1000U; }
void delay(uint32_t milliseconds) { clock_ms += milliseconds; }

USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *d, uint8_t ep, uint8_t type, uint16_t size)
{
  assert(d == &USBD_Device);
  if (type == USBD_EP_TYPE_CTRL) {
    assert((ep == 0x00U || ep == 0x80U) && size == USB_MAX_EP0_SIZE);
    ep0_open_calls++;
    return USBD_OK;
  }
  assert(type == USBD_EP_TYPE_BULK || type == USBD_EP_TYPE_INTR);
  open_calls[ep]++;
  if (open_status[ep] != USBD_OK) return open_status[ep];
  if (opened[ep]) return USBD_FAIL;
  opened[ep] = true;
  opened_size[ep] = size;
  if (ep & 0x80U) pcd.IN_ep[ep & 15U].maxpacket = size;
  return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *d, uint8_t ep)
{
  assert(d == &USBD_Device);
  close_calls[ep]++;
  if (close_status[ep] != USBD_OK && !close_stops_on_failure[ep]) return close_status[ep];
  opened[ep] = false;
  if (ep & 0x80U) { tx_armed[ep & 15U] = false; active_tx[ep & 15U] = NULL; }
  else active_rx = NULL;
  return close_status[ep];
}
/* These device-level LL adapters expose return/call ordering only. They do not
 * declare retained payloads stopped; a later successful Close owns that boundary. */
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *d)
{ assert(d == &USBD_Device); ll_stop_calls++; return ll_stop_status; }
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *d)
{ assert(d == &USBD_Device); ll_deinit_calls++; return ll_deinit_status; }
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *d, uint8_t ep, uint8_t *data, uint32_t length)
{
  ep |= 0x80U;
  assert(d == &USBD_Device && opened[ep] && length <= sizeof(tx_image[0]));
  assert(!tx_armed[ep & 15U] && (data != NULL || length == 0U));
  tx_calls++;
  if (tx_status != USBD_OK) return tx_status;
  active_tx[ep & 15U] = data;
  active_tx_length[ep & 15U] = length;
  tx_armed[ep & 15U] = true;
  if (length) memcpy(tx_image[ep & 15U], data, length);
  return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *d, uint8_t ep, uint8_t *data, uint32_t length)
{
  assert(d == &USBD_Device && ep == CDC_OUT_EP && data != NULL);
  rx_calls++;
  if (!opened[ep]) return USBD_FAIL;
  if (rx_status != USBD_OK) return rx_status;
  if (active_rx != NULL) return USBD_BUSY;
  active_rx = data;
  active_rx_length = length;
  return USBD_OK;
}
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *d, uint8_t ep)
{ assert(d == &USBD_Device && ep == CDC_OUT_EP); return completed_rx_length; }
USBD_StatusTypeDef USBD_CtlSendData(USBD_HandleTypeDef *d, uint8_t *data, uint32_t length)
{ assert(d == &USBD_Device && length <= 7U); control_tx = data; return USBD_OK; }
USBD_StatusTypeDef USBD_CtlPrepareRx(USBD_HandleTypeDef *d, uint8_t *data, uint32_t length)
{ assert(d == &USBD_Device && length <= 64U); control_rx = data; return USBD_OK; }
void USBD_CtlError(USBD_HandleTypeDef *d, USBD_SetupReqTypedef *req)
{ assert(d == &USBD_Device && req != NULL); control_errors++; }
void *USBD_GetEpDesc(uint8_t *configuration, uint8_t address)
{
  uint16_t total = configuration[2] | ((uint16_t)configuration[3] << 8);
  for (uint16_t off = 0U; off + 2U <= total && configuration[off] >= 2U; off += configuration[off])
    if (configuration[off + 1U] == USB_DESC_TYPE_ENDPOINT && configuration[off + 2U] == address)
      return &configuration[off];
  return NULL;
}

#include "usb_cdc_lifecycle_core.inc"

/* The production interface and qbuffer operate on their actual application
 * buffers. Their queue-admission behavior is not replaced by a model. */
#include "usb_cdc_lifecycle_interface.inc"

static int8_t tracked_init(USBD_HandleTypeDef *d)
{
  init_calls++;
  int8_t result = missing_rx_buffer ? USBD_OK : USBD_CDC_fops.Init(d);
  return interface_init_status == USBD_OK ? result : (int8_t)interface_init_status;
}
static int8_t tracked_deinit(USBD_HandleTypeDef *d)
{
  deinit_calls++;
  last_deinit_interface = d->pUserData[TEST_CDC_CLASS_ID];
  return interface_deinit_status == USBD_OK ? USBD_CDC_fops.DeInit(d) : (int8_t)interface_deinit_status;
}
static int8_t tracked_receive(USBD_HandleTypeDef *d, uint8_t *data, uint32_t *length)
{ receive_calls++; return USBD_CDC_fops.Receive(d, data, length); }
static int8_t tracked_complete(USBD_HandleTypeDef *d, uint8_t *data, uint32_t *length, uint8_t ep)
{ transmit_complete_calls++; return USBD_CDC_fops.TransmitCplt(d, data, length, ep); }
static int8_t tracked_control(USBD_HandleTypeDef *d, uint8_t command, uint8_t *data, uint16_t length)
{ control_calls++; return USBD_CDC_fops.Control(d, command, data, length); }
static USBD_CDC_ItfTypeDef tracked_interface = {
  tracked_init, tracked_deinit, tracked_control, tracked_receive, tracked_complete,
};

static USBD_CDC_HandleTypeDef *handle(void)
{ return USBD_Device.pClassDataCmsit[TEST_CDC_CLASS_ID]; }
static uint8_t set_tx(uint8_t *data, uint32_t length)
{
#ifdef USE_USBD_COMPOSITE
  return USBD_CDC_SetTxBuffer(&USBD_Device, data, length, TEST_CDC_CLASS_ID);
#else
  return USBD_CDC_SetTxBuffer(&USBD_Device, data, length);
#endif
}
static uint8_t transmit(void)
{
#ifdef USE_USBD_COMPOSITE
  return USBD_CDC_TransmitPacket(&USBD_Device, TEST_CDC_CLASS_ID);
#else
  return USBD_CDC_TransmitPacket(&USBD_Device);
#endif
}
static void prepare(USBD_SpeedTypeDef speed)
{
  assert(handle() == NULL);
  cdcIfInit();
  is_rx_full = false; /* Isolate cases only after their controller owners have stopped. */
  USBD_Device.classId = TEST_CDC_CLASS_ID;
  USBD_Device.pClass[TEST_CDC_CLASS_ID] = &USBD_CDC;
#ifdef USE_USBD_COMPOSITE
  USBD_Device.tclasslist[TEST_CDC_CLASS_ID].Active = 1U;
#endif
  USBD_Device.pData = &pcd;
  USBD_Device.dev_speed = speed;
  USBD_Device.dev_state = USBD_STATE_ADDRESSED;
  USBD_Device.dev_config = 1U;
  assert(USBD_CDC_RegisterInterface(&USBD_Device, &tracked_interface) == USBD_OK);
}
static void configure(USBD_SpeedTypeDef speed)
{
  prepare(speed);
  assert(USBD_CDC.Init(&USBD_Device, 1U) == USBD_OK && handle() != NULL);
  USBD_Device.dev_state = USBD_STATE_CONFIGURED;
  uint16_t packet = speed == USBD_SPEED_HIGH ? 512U : 64U;
  assert(opened_size[CDC_IN_EP] == packet && opened_size[CDC_OUT_EP] == packet);
  assert(opened_size[CDC_CMD_EP] == 8U);
  assert(USBD_Device.ep_in[CDC_CMD_EP & 15U].bInterval ==
         (speed == USBD_SPEED_HIGH ? CDC_HS_BINTERVAL : CDC_FS_BINTERVAL));
  assert(active_rx == UserRxBufferFS && active_rx_length == packet);
  USBD_SetupReqTypedef req = {.bmRequest = 0x21U, .bRequest = CDC_SET_CONTROL_LINE_STATE, .wValue = 1U};
  assert(USBD_CDC.Setup(&USBD_Device, &req) == USBD_OK && cdcIfIsConnected());
}
static void stop(void)
{
  assert(USBD_CDC.DeInit(&USBD_Device, 0U) == USBD_OK);
  assert(handle() == NULL && USBD_Device.pClassData == NULL && active_rx == NULL);
  assert(!opened[CDC_IN_EP] && !opened[CDC_OUT_EP] && !opened[CDC_CMD_EP]);
  USBD_Device.dev_state = USBD_STATE_DEFAULT;
}
static uint8_t complete_tx(void)
{
  unsigned ep = CDC_IN_EP & 15U;
  assert(tx_armed[ep]);
  if (active_tx_length[ep]) assert(!memcmp(active_tx[ep], tx_image[ep], active_tx_length[ep]));
  tx_armed[ep] = false;
  active_tx[ep] = NULL;
  return USBD_CDC.DataIn(&USBD_Device, ep);
}
static uint8_t complete_rx(uint8_t value, uint32_t length)
{
  assert(active_rx != NULL && length <= active_rx_length);
  memset(active_rx, value, length);
  active_rx = NULL;
  completed_rx_length = length;
  return USBD_CDC.DataOut(&USBD_Device, CDC_OUT_EP);
}
static void queued_tx(uint8_t value)
{
  uint8_t data[32]; memset(data, value, sizeof(data));
  assert(cdcIfWrite(data, sizeof(data)) == sizeof(data));
}
static void test_normal(USBD_SpeedTypeDef speed)
{
  configure(speed);
  queued_tx(0x46U);
  assert(USBD_CDC.SOF(&USBD_Device) == USBD_OK && tx_armed[3]);
  queued_tx(0x57U);
  uint8_t image[32]; memcpy(image, UserTxBufferFS, sizeof(image));
  assert(USBD_CDC.SOF(&USBD_Device) == USBD_OK && !memcmp(image, UserTxBufferFS, sizeof(image)));
  unsigned completions = transmit_complete_calls;
  assert(USBD_CDC.DataIn(&USBD_Device, CDC_CMD_EP & 15U) == USBD_OK);
  assert(handle()->TxState == 1U && transmit_complete_calls == completions);
  assert(complete_tx() == USBD_OK);
  assert(USBD_CDC.SOF(&USBD_Device) == USBD_OK && active_tx[3][0] == 0x57U);
  assert(complete_tx() == USBD_OK && handle()->TxState == 0U);
  assert(USBD_CDC_SetRxBuffer(&USBD_Device, UserRxBufferFS + 1U) == USBD_BUSY);
  assert(USBD_CDC_ReceivePacket(&USBD_Device) == USBD_BUSY);
  assert(complete_rx(0x68U, 7U) == USBD_OK && cdcIfAvailable() == 7U);
  assert(active_rx == UserRxBufferFS && handle()->RxState == 1U);
  for (unsigned i = 0U; i < 7U; i++) assert(cdcIfRead() == 0x68U);
  uint32_t packet = speed == USBD_SPEED_HIGH ? 512U : 64U;
  memset(UserTxBufferFS, 0x79U, packet);
  assert(set_tx(UserTxBufferFS, packet) == USBD_OK && transmit() == USBD_OK);
  completions = transmit_complete_calls;
  assert(complete_tx() == USBD_OK && tx_armed[3] && active_tx_length[3] == 0U);
  assert(handle()->TxState == 1U && transmit_complete_calls == completions);
  assert(complete_tx() == USBD_OK && handle()->TxState == 0U && transmit_complete_calls == completions + 1U);
  stop();
}
static void check_stopped_owner(USBD_CDC_HandleTypeDef *retained)
{
  uint8_t buffer[64] = {0};
  uint32_t opens[256], closes[256], tx_before = tx_calls, rx_before = rx_calls;
  uint32_t rx_queued = qbufferAvailable(&q_rx), tx_queued = qbufferAvailable(&q_tx);
  uint32_t receive_before = receive_calls, complete_before = transmit_complete_calls, control_before = control_calls;
  memcpy(opens, open_calls, sizeof(opens)); memcpy(closes, close_calls, sizeof(closes));
  uint8_t image[sizeof(UserTxBufferFS)]; memcpy(image, UserTxBufferFS, sizeof(image));
  assert(USBD_CDC.SOF(&USBD_Device) == USBD_FAIL); /* Actual CDC_SoF_ISR must not consume q_tx or overwrite UserTxBufferFS. */
  assert(set_tx(buffer, sizeof(buffer)) == USBD_FAIL);
  assert(transmit() == USBD_FAIL);
  assert(USBD_CDC_SetRxBuffer(&USBD_Device, buffer) == USBD_FAIL);
  assert(USBD_CDC_ReceivePacket(&USBD_Device) == USBD_FAIL);
  assert(CDC_Transmit_FS(&USBD_Device, buffer, sizeof(buffer)) == USBD_BUSY);
  USBD_SetupReqTypedef req = {.bmRequest = 0x21U, .bRequest = CDC_SET_CONTROL_LINE_STATE};
  assert(USBD_CDC.Setup(&USBD_Device, &req) == USBD_FAIL);
  assert(USBD_CDC.EP0_RxReady(&USBD_Device) == USBD_FAIL);
  assert(USBD_CDC.DataIn(&USBD_Device, CDC_IN_EP & 15U) == USBD_FAIL);
  assert(USBD_CDC.DataOut(&USBD_Device, CDC_OUT_EP) == USBD_FAIL);
  assert(USBD_CDC.Init(&USBD_Device, 1U) == USBD_FAIL);
  assert(handle() == retained && USBD_Device.pClassData == retained);
  assert(!memcmp(image, UserTxBufferFS, sizeof(image)));
  assert(qbufferAvailable(&q_rx) == rx_queued && qbufferAvailable(&q_tx) == tx_queued);
  assert(receive_calls == receive_before && transmit_complete_calls == complete_before && control_calls == control_before);
  assert(tx_calls == tx_before && rx_calls == rx_before);
  assert(!memcmp(opens, open_calls, sizeof(opens)) && !memcmp(closes, close_calls, sizeof(closes)));
  void *slots[USBD_MAX_SUPPORTED_CLASS]; unsigned count = 0U;
  while (count < USBD_MAX_SUPPORTED_CLASS && (slots[count] = USBD_static_malloc(1U)) != NULL) count++;
  assert(count == USBD_MAX_SUPPORTED_CLASS - 1U);
  for (unsigned i = 0U; i < count; i++) USBD_static_free(slots[i]);
}
static void test_failed_close(uint8_t ep, bool already_stopped, bool inspect_storage_first)
{
  configure(USBD_SPEED_HIGH);
  queued_tx(0x8AU); assert(USBD_CDC.SOF(&USBD_Device) == USBD_OK);
  queued_tx(0x9BU);
  USBD_CDC_HandleTypeDef *retained = handle();
  uint8_t *payload = active_tx[3], *rx_buffer = active_rx;
  unsigned deinit_before = deinit_calls;
  close_status[ep] = USBD_FAIL; close_stops_on_failure[ep] = already_stopped;
  uint8_t result = USBD_CDC.DeInit(&USBD_Device, 0U);
  if (inspect_storage_first) assert(handle() == retained && USBD_Device.pClassData == retained);
  assert(result == USBD_FAIL && deinit_calls == deinit_before);
  assert(ep & 0x80U ? USBD_Device.ep_in[ep & 15U].is_used : USBD_Device.ep_out[ep].is_used);
  check_stopped_owner(retained);
  if (ep == CDC_IN_EP && !already_stopped) {
    assert(active_tx[3] == payload && !memcmp(payload, tx_image[3], active_tx_length[3]));
    assert(complete_tx() == USBD_FAIL && retained->TxState == UINT32_MAX);
  }
  if (ep == CDC_OUT_EP && !already_stopped) {
    assert(active_rx == rx_buffer && retained->RxBuffer == rx_buffer);
    unsigned receives = receive_calls;
    assert(complete_rx(0xA5U, 32U) == USBD_FAIL && receive_calls == receives && active_rx == NULL);
  }
  close_status[ep] = USBD_OK; close_stops_on_failure[ep] = false;
  stop();
}
static void test_init_failures(void)
{
  const uint8_t endpoints[] = {CDC_IN_EP, CDC_OUT_EP, CDC_CMD_EP};
  for (unsigned speed = 0U; speed < 2U; speed++) {
    for (unsigned i = 0U; i < 3U; i++) {
      prepare(speed ? USBD_SPEED_HIGH : USBD_SPEED_FULL);
      open_status[endpoints[i]] = USBD_FAIL;
      assert(USBD_CDC.Init(&USBD_Device, 1U) == USBD_FAIL);
      assert(handle() == NULL && !opened[CDC_IN_EP] && !opened[CDC_OUT_EP] && !opened[CDC_CMD_EP]);
      open_status[endpoints[i]] = USBD_OK;
    }
    prepare(speed ? USBD_SPEED_HIGH : USBD_SPEED_FULL);
    rx_status = USBD_FAIL;
    assert(USBD_CDC.Init(&USBD_Device, 1U) == USBD_FAIL && handle() == NULL && active_rx == NULL);
    rx_status = USBD_OK;
  }
  prepare(USBD_SPEED_FULL);
  interface_init_status = USBD_FAIL;
  assert(USBD_CDC.Init(&USBD_Device, 1U) == USBD_FAIL && handle() == NULL);
  interface_init_status = USBD_OK;
  prepare(USBD_SPEED_HIGH);
  missing_rx_buffer = true;
  assert(USBD_CDC.Init(&USBD_Device, 1U) == USBD_EMEM && handle() == NULL);
  missing_rx_buffer = false;
}
static void test_partial_init_failure(bool failed_receive)
{
  prepare(USBD_SPEED_HIGH);
  if (failed_receive) rx_status = USBD_FAIL;
  else open_status[CDC_OUT_EP] = USBD_FAIL;
  uint8_t failed_close = failed_receive ? CDC_OUT_EP : CDC_IN_EP;
  close_status[failed_close] = USBD_FAIL;
  assert(USBD_CDC.Init(&USBD_Device, 1U) == USBD_FAIL);
  USBD_CDC_HandleTypeDef *retained = handle();
  assert(retained != NULL);
  check_stopped_owner(retained);
  rx_status = USBD_OK; open_status[CDC_OUT_EP] = USBD_OK; close_status[failed_close] = USBD_OK;
  stop();
}
static void test_initial_receive_failure(void)
{
  for (unsigned speed = 0U; speed < 2U; speed++) {
    prepare(speed ? USBD_SPEED_HIGH : USBD_SPEED_FULL);
    rx_status = USBD_FAIL;
    assert(USBD_CDC.Init(&USBD_Device, 1U) == USBD_FAIL);
    assert(handle() == NULL && active_rx == NULL && !opened[CDC_IN_EP] && !opened[CDC_OUT_EP] && !opened[CDC_CMD_EP]);
    rx_status = USBD_OK;
  }
}
static void test_interface_deinit_failure(void)
{
  configure(USBD_SPEED_FULL);
  interface_deinit_status = USBD_FAIL;
  USBD_CDC_HandleTypeDef *retained = handle();
  assert(USBD_CDC.DeInit(&USBD_Device, 0U) == USBD_FAIL && handle() == retained);
  assert(!opened[CDC_IN_EP] && !opened[CDC_OUT_EP] && !opened[CDC_CMD_EP]);
  check_stopped_owner(retained);
  interface_deinit_status = USBD_OK;
  stop();
}
static void test_packet_failures(USBD_SpeedTypeDef speed)
{
  configure(speed);
  memset(UserTxBufferFS, 0xB6U, 17U);
  assert(set_tx(UserTxBufferFS, 17U) == USBD_OK);
  tx_status = USBD_BUSY;
  assert(transmit() == USBD_BUSY && handle()->TxState == 0U && !tx_armed[3]);
  tx_status = USBD_OK;
  assert(transmit() == USBD_OK && complete_tx() == USBD_OK);
  rx_status = USBD_FAIL;
  /* The real interface currently ignores failed rearm status. Record that
   * boundary rather than claiming queue-loss/recovery guarantees. */
  assert(complete_rx(0xC7U, 7U) == USBD_OK && active_rx == NULL && handle()->RxState == 0U);
  assert(USBD_CDC_ReceivePacket(&USBD_Device) == USBD_FAIL && active_rx == NULL);
  rx_status = USBD_OK;
  assert(USBD_CDC_ReceivePacket(&USBD_Device) == USBD_OK);
  uint32_t packet = speed == USBD_SPEED_HIGH ? 512U : 64U;
  assert(set_tx(UserTxBufferFS, packet) == USBD_OK && transmit() == USBD_OK);
  tx_status = USBD_FAIL;
  unsigned completions = transmit_complete_calls;
  assert(complete_tx() == USBD_FAIL && handle()->TxState == 1U && !tx_armed[3]);
  assert(transmit_complete_calls == completions && set_tx(UserTxBufferFS, 17U) == USBD_BUSY);
  tx_status = USBD_OK;
  stop();
}
static void test_churn(void)
{
  for (unsigned i = 0U; i < 256U; i++) {
    configure(i & 1U ? USBD_SPEED_HIGH : USBD_SPEED_FULL);
    stop();
    uint32_t closes[256]; memcpy(closes, close_calls, sizeof(closes));
    assert(USBD_CDC.DeInit(&USBD_Device, 0U) == USBD_OK && !memcmp(closes, close_calls, sizeof(closes)));
  }
}

static void check_core_released_interface(void *interface, bool reset)
{
  assert(handle() == NULL && USBD_Device.pClassData == NULL);
  assert(!opened[CDC_IN_EP] && !opened[CDC_OUT_EP] && !opened[CDC_CMD_EP]);
  assert(active_rx == NULL && active_tx[CDC_IN_EP & 15U] == NULL);
  assert(last_deinit_interface == interface && !cdcIfIsConnected());
#ifdef USE_USBD_COMPOSITE
  UNUSED(reset);
  assert(USBD_Device.pUserData[TEST_CDC_CLASS_ID] == interface);
#else
  assert(USBD_Device.pUserData[0] == (reset ? interface : NULL));
#endif
}
static void test_core_normal(void)
{
  const USBD_StatusTypeDef statuses[] = {USBD_OK, USBD_BUSY, USBD_FAIL};
  for (unsigned speed = 0U; speed < 2U; speed++) {
    for (unsigned s = 0U; s < sizeof(statuses) / sizeof(statuses[0]); s++) {
      for (unsigned d = 0U; d < sizeof(statuses) / sizeof(statuses[0]); d++) {
        configure(speed ? USBD_SPEED_HIGH : USBD_SPEED_FULL);
        ll_stop_status = statuses[s]; ll_deinit_status = statuses[d];
        unsigned stops = ll_stop_calls, deinits = ll_deinit_calls, interfaces = deinit_calls;
        USBD_StatusTypeDef expected = ll_deinit_status != USBD_OK ? ll_deinit_status : ll_stop_status;
        assert(USBD_DeInit(&USBD_Device) == expected);
        assert(ll_stop_calls == stops + 1U && ll_deinit_calls == deinits + 1U && deinit_calls == interfaces + 1U);
        check_core_released_interface(&tracked_interface, false);
        assert(USBD_Device.dev_state == USBD_STATE_DEFAULT);
      }
    }
  }
  ll_stop_status = ll_deinit_status = USBD_OK;
  puts("PASS: core DeInit releases a successful FS/HS CDC owner/interface even when LL Stop/DeInit fails, and preserves LL error precedence");
}
static void test_core_endpoint_recovery(USBD_SpeedTypeDef speed, uint8_t ep, bool reset, bool owner_ended)
{
  configure(speed);
  queued_tx(0xD8U); assert(USBD_CDC.SOF(&USBD_Device) == USBD_OK);
  queued_tx(0xE9U);
  assert(complete_rx(0xFAU, 7U) == USBD_OK);
  USBD_CDC_HandleTypeDef *retained = handle();
  unsigned interfaces = deinit_calls, stops = ll_stop_calls, deinits = ll_deinit_calls;
  close_status[ep] = USBD_FAIL; close_stops_on_failure[ep] = owner_ended;
  assert(USBD_DeInit(&USBD_Device) == USBD_FAIL);
  assert(handle() == retained && USBD_Device.pUserData[TEST_CDC_CLASS_ID] == &tracked_interface);
  assert(deinit_calls == interfaces && ll_stop_calls == stops + 1U && ll_deinit_calls == deinits + 1U);
  check_stopped_owner(retained);
  assert(USBD_DeInit(&USBD_Device) == USBD_FAIL); /* Explicit failure remains recoverable without losing callbacks. */
  assert(handle() == retained && USBD_Device.pUserData[TEST_CDC_CLASS_ID] == &tracked_interface);
  assert(deinit_calls == interfaces);
  close_status[ep] = USBD_OK; close_stops_on_failure[ep] = false;
  unsigned closes = close_calls[ep];
  assert(USBD_SetClassConfig(&USBD_Device, 1U) == USBD_FAIL);
  assert(close_calls[ep] == closes && handle() == retained); /* Init never retries teardown. */
  unsigned controls = ep0_open_calls;
  assert((reset ? USBD_LL_Reset(&USBD_Device) : USBD_DeInit(&USBD_Device)) == USBD_OK);
  assert(deinit_calls == interfaces + 1U && ep0_open_calls == controls + (reset ? 2U : 0U));
  check_core_released_interface(&tracked_interface, reset);
  if (reset) {
    USBD_Device.dev_state = USBD_STATE_ADDRESSED;
    assert(USBD_SetClassConfig(&USBD_Device, 1U) == USBD_OK && handle() != NULL);
    assert(USBD_DeInit(&USBD_Device) == USBD_OK);
    check_core_released_interface(&tracked_interface, false);
  }
}
static void test_core_recovery(void)
{
  const uint8_t endpoints[] = {CDC_IN_EP, CDC_OUT_EP, CDC_CMD_EP};
  for (unsigned speed = 0U; speed < 2U; speed++) {
    for (unsigned reset = 0U; reset < 2U; reset++) {
      for (unsigned ep = 0U; ep < sizeof(endpoints) / sizeof(endpoints[0]); ep++)
        test_core_endpoint_recovery(speed ? USBD_SPEED_HIGH : USBD_SPEED_FULL, endpoints[ep], reset != 0U, false);
      test_core_endpoint_recovery(speed ? USBD_SPEED_HIGH : USBD_SPEED_FULL, CDC_IN_EP, reset != 0U, true);
    }
  }
  puts("PASS: actual core/CDC/interface keeps failed endpoint owners and callbacks across repeated DeInit; only explicit successful Reset/DeInit releases them without interface re-registration");
}
static void test_core_interface_recovery(void)
{
  for (unsigned reset = 0U; reset < 2U; reset++) {
    configure(USBD_SPEED_FULL);
    interface_deinit_status = USBD_FAIL;
    USBD_CDC_HandleTypeDef *retained = handle();
    unsigned interfaces = deinit_calls;
    assert(USBD_DeInit(&USBD_Device) == USBD_FAIL && handle() == retained);
    assert(USBD_Device.pUserData[TEST_CDC_CLASS_ID] == &tracked_interface && deinit_calls == interfaces + 1U);
    check_stopped_owner(retained);
    uint32_t closes[256]; memcpy(closes, close_calls, sizeof(closes));
    interface_deinit_status = USBD_OK;
    assert((reset ? USBD_LL_Reset(&USBD_Device) : USBD_DeInit(&USBD_Device)) == USBD_OK);
    assert(deinit_calls == interfaces + 2U && !memcmp(closes, close_calls, sizeof(closes)));
    check_core_released_interface(&tracked_interface, reset != 0U);
  }
  puts("PASS: failed interface DeInit retains its callback alias until explicit cleanup succeeds, without repeating already successful endpoint closes");
}
static uint8_t no_owner_deinit(USBD_HandleTypeDef *d, uint8_t config)
{
  UNUSED(config);
  assert(d == &USBD_Device && handle() == NULL);
  return USBD_FAIL; /* Core-only adapter: an error does not always retain a class resource. */
}
static void test_core_empty(void)
{
  static USBD_ClassTypeDef no_owner_class = {.DeInit = no_owner_deinit};
  prepare(USBD_SPEED_FULL);
  USBD_Device.pClass[TEST_CDC_CLASS_ID] = &no_owner_class;
  USBD_Device.pUserData[TEST_CDC_CLASS_ID] = &tracked_interface;
  assert(USBD_DeInit(&USBD_Device) == USBD_FAIL && handle() == NULL);
#ifdef USE_USBD_COMPOSITE
  assert(USBD_Device.pUserData[TEST_CDC_CLASS_ID] == &tracked_interface);
#else
  assert(USBD_Device.pUserData[0] == NULL);
#endif
  USBD_Device.pClass[TEST_CDC_CLASS_ID] = NULL;
  USBD_Device.pUserData[TEST_CDC_CLASS_ID] = &tracked_interface;
  assert(USBD_DeInit(&USBD_Device) == USBD_OK);
#ifdef USE_USBD_COMPOSITE
  assert(USBD_Device.pUserData[TEST_CDC_CLASS_ID] == &tracked_interface);
#else
  assert(USBD_Device.pUserData[0] == NULL);
#endif
  USBD_Device.pClass[TEST_CDC_CLASS_ID] = &USBD_CDC;
  puts("PASS: core keeps existing standalone NULL semantics for absent owners/classes, including class errors without a retained resource");
}
static void test_core_registration(void)
{
  static USBD_CDC_ItfTypeDef replacement_interface;
  replacement_interface = tracked_interface;
  configure(USBD_SPEED_FULL);
  close_status[CDC_IN_EP] = USBD_FAIL;
  assert(USBD_DeInit(&USBD_Device) == USBD_FAIL);
  assert(USBD_CDC_RegisterInterface(&USBD_Device, NULL) == USBD_FAIL);
  assert(USBD_Device.pUserData[TEST_CDC_CLASS_ID] == &tracked_interface);
  assert(USBD_CDC_RegisterInterface(&USBD_Device, &tracked_interface) == USBD_OK);
  close_status[CDC_IN_EP] = USBD_OK;
  assert(USBD_DeInit(&USBD_Device) == USBD_OK);
  check_core_released_interface(&tracked_interface, false);
  assert(USBD_CDC_RegisterInterface(&USBD_Device, &replacement_interface) == USBD_OK);
  assert(USBD_SetClassConfig(&USBD_Device, 1U) == USBD_OK && handle() != NULL);
  assert(USBD_DeInit(&USBD_Device) == USBD_OK);
  check_core_released_interface(&replacement_interface, false);
  puts("PASS: NULL registration cannot erase retained callbacks; same-interface registration and replacement after successful cleanup still work");
}
int main(int argc, char **argv)
{
  assert(argc <= 2);
  const char *selected = argc > 1 ? argv[1] : "all";
  const char *cases[] = {"all", "normal", "close", "close-storage", "init", "rx-init", "partial", "interface", "packet", "churn",
                        "core-normal", "core-recovery", "core-interface", "core-empty", "core-registration"};
  bool known = false;
  for (unsigned i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) known |= !strcmp(selected, cases[i]);
  assert(known);
  bool all = !strcmp(selected, "all");
  if (all || !strcmp(selected, "normal")) { test_normal(USBD_SPEED_FULL); test_normal(USBD_SPEED_HIGH); }
  if (all || !strcmp(selected, "close")) {
    test_failed_close(CDC_IN_EP, false, false); test_failed_close(CDC_OUT_EP, false, false);
    test_failed_close(CDC_CMD_EP, false, false); test_failed_close(CDC_IN_EP, true, false);
  }
  if (all || !strcmp(selected, "close-storage")) test_failed_close(CDC_IN_EP, false, true);
  if (all || !strcmp(selected, "init")) test_init_failures();
  if (all || !strcmp(selected, "rx-init")) test_initial_receive_failure();
  if (all || !strcmp(selected, "partial")) { test_partial_init_failure(false); test_partial_init_failure(true); }
  if (all || !strcmp(selected, "interface")) test_interface_deinit_failure();
  if (all || !strcmp(selected, "packet")) { test_packet_failures(USBD_SPEED_FULL); test_packet_failures(USBD_SPEED_HIGH); }
  if (all || !strcmp(selected, "churn")) test_churn();
  if (all || !strcmp(selected, "core-normal")) test_core_normal();
  if (all || !strcmp(selected, "core-recovery")) test_core_recovery();
  if (all || !strcmp(selected, "core-interface")) test_core_interface_recovery();
  if (all || !strcmp(selected, "core-empty")) test_core_empty();
  if (all || !strcmp(selected, "core-registration")) test_core_registration();
  puts("PASS: production core/CDC/interface/qbuffer: FS/HS bulk+ZLP, explicit arm failures, failed owners/callbacks retained until explicit cleanup, late callbacks and SOF cannot reuse buffers; bounded class churn");
  return 0;
}
