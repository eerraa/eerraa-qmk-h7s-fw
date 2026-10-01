#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "usbd_cdc.h"
#include "usbd_ctlreq.h"

static USBD_HandleTypeDef device;
static USBD_CDC_HandleTypeDef cdc;
static const uint8_t *pending;
static uint32_t pending_length, errors;
static uintptr_t reused_low, reused_high;

USBD_StatusTypeDef USBD_CtlSendData(USBD_HandleTypeDef *pdev, uint8_t *buffer, uint32_t length)
{
  assert(pdev == &device);
  pending = buffer;
  pending_length = length;
  return USBD_OK;
}

USBD_StatusTypeDef USBD_CtlPrepareRx(USBD_HandleTypeDef *pdev, uint8_t *buffer, uint32_t length)
{
  (void)pdev; (void)buffer; (void)length;
  assert(0 && "standard IN requests cannot arm OUT");
  return USBD_FAIL;
}

void USBD_CtlError(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *request)
{
  assert(pdev == &device && request != NULL);
  errors++;
}

static uint8_t USBD_CDC_Setup(USBD_HandleTypeDef *, USBD_SetupReqTypedef *) __attribute__((noinline));
#include "usb_cdc_setup.inc"

static void reuse_stack(void) __attribute__((noinline));
static void reuse_stack(void)
{
  volatile uint8_t scratch[2048];
  reused_low = (uintptr_t)&scratch[0];
  reused_high = (uintptr_t)&scratch[sizeof(scratch) - 1U];
  for (unsigned i = 0U; i < sizeof(scratch); i++) scratch[i] = 0xA5U;
}

static void deferred_request(uint8_t request, uint16_t length)
{
  USBD_SetupReqTypedef setup = {.bmRequest = 0x81U, .bRequest = request, .wLength = length};
  pending = NULL;
  pending_length = 0U;
  assert(USBD_CDC_Setup(&device, &setup) == USBD_OK);
  assert(pending != NULL && pending_length == length);
  reuse_stack();
  /* Reject a pointer into the reused stack before attempting a deferred read. */
  assert(((uintptr_t)pending < reused_low || (uintptr_t)pending > reused_high) &&
         "EP0 response storage must outlive SETUP");
  assert(((uintptr_t)pending & 3U) == 0U);
  for (uint16_t i = 0U; i < length; i++) assert(pending[i] == 0U);
}

int main(void)
{
  device.dev_state = USBD_STATE_CONFIGURED;
  device.pClassDataCmsit[0] = &cdc;
  for (unsigned i = 0U; i < 64U; i++) {
    deferred_request(USB_REQ_GET_STATUS, 2U);
    deferred_request(USB_REQ_GET_INTERFACE, 1U);
  }
  assert(errors == 0U);
  device.dev_state = USBD_STATE_ADDRESSED;
  USBD_SetupReqTypedef setup = {.bmRequest = 0x81U, .bRequest = USB_REQ_GET_STATUS, .wLength = 2U};
  assert(USBD_CDC_Setup(&device, &setup) == USBD_FAIL && errors == 1U);
  setup.bRequest = USB_REQ_GET_INTERFACE;
  assert(USBD_CDC_Setup(&device, &setup) == USBD_FAIL && errors == 2U);
  puts("PASS: CDC GET_STATUS/GET_INTERFACE retain aligned zero responses after SETUP stack reuse; unconfigured requests fail");
  return 0;
}
