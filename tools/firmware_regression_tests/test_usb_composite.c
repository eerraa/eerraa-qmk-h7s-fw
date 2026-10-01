#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include "usbd_core.h"
#include "usbd_ctlreq.h"
#include "usbd_ioreq.h"

_Static_assert(USBD_MAX_NUM_CONFIGURATION == 2U, "configuration-switch coverage is fixture-only");
_Static_assert(USBD_MAX_SUPPORTED_CLASS >= 3U, "three mock class slots are required");

enum { CLASS_COUNT = 3, CONFIG_COUNT = 3, EVENT_CAPACITY = 64 };
typedef enum { EVENT_INIT, EVENT_DEINIT, EVENT_LL_STOP, EVENT_LL_DEINIT, EVENT_EP0_OPEN, EVENT_ACK, EVENT_STALL } event_kind_t;
typedef struct { event_kind_t kind; unsigned id, config; bool init_cleanup; } event_t;
typedef struct { unsigned id, configuration; } class_resource_t;

static USBD_HandleTypeDef stack;
static USBD_DescriptorsTypeDef descriptors;
static uint8_t configuration_descriptor[8];
static USBD_ClassTypeDef classes[CLASS_COUNT];
static class_resource_t resources[CLASS_COUNT];
static USBD_StatusTypeDef init_status[CLASS_COUNT][CONFIG_COUNT];
static USBD_StatusTypeDef deinit_status[CLASS_COUNT][CONFIG_COUNT];
static USBD_StatusTypeDef ll_stop_status, ll_deinit_status;
static unsigned init_calls[CLASS_COUNT][CONFIG_COUNT];
static unsigned deinit_calls[CLASS_COUNT][CONFIG_COUNT];
static unsigned init_cleanup_calls[CLASS_COUNT][CONFIG_COUNT];
static unsigned set_calls, clear_calls, ll_stop_calls, ll_deinit_calls, ep0_open_calls, ack_calls, stall_calls;
static event_t events[EVENT_CAPACITY];
static unsigned event_count;
static bool inside_init_cleanup;

static void record(event_kind_t kind, unsigned id, unsigned cfg)
{
  assert(event_count < EVENT_CAPACITY);
  events[event_count++] = (event_t){kind, id, cfg, inside_init_cleanup};
}

/* Adapter boundary: class callbacks own their storage. Failed Init performs its
 * own one-time partial cleanup; a failed DeInit retains its handle. These rules
 * make core call selection/ordering observable without replacing HID/CDC code.
 * They do not prove any real class or endpoint has physically stopped. */
static uint8_t class_deinit(USBD_HandleTypeDef *pdev, uint8_t cfg)
{
  unsigned id = pdev->classId;
  assert(pdev == &stack && id < CLASS_COUNT && cfg < CONFIG_COUNT);
  deinit_calls[id][cfg]++;
  if (inside_init_cleanup) init_cleanup_calls[id][cfg]++;
  record(EVENT_DEINIT, id, cfg);
  if (deinit_status[id][cfg] == USBD_OK) pdev->pClassDataCmsit[id] = NULL;
  return (uint8_t)deinit_status[id][cfg];
}

static uint8_t class_init(USBD_HandleTypeDef *pdev, uint8_t cfg)
{
  unsigned id = pdev->classId;
  assert(pdev == &stack && id < CLASS_COUNT && cfg < CONFIG_COUNT);
  assert(pdev->pClassDataCmsit[id] == NULL);
  init_calls[id][cfg]++;
  record(EVENT_INIT, id, cfg);
  resources[id] = (class_resource_t){id, cfg};
  pdev->pClassDataCmsit[id] = &resources[id];
  if (init_status[id][cfg] != USBD_OK) {
    inside_init_cleanup = true;
    (void)class_deinit(pdev, cfg);
    inside_init_cleanup = false;
  }
  return (uint8_t)init_status[id][cfg];
}

USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev)
{
  assert(pdev == &stack);
  ll_stop_calls++;
  record(EVENT_LL_STOP, CLASS_COUNT, pdev->dev_config);
  return ll_stop_status;
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev)
{
  assert(pdev == &stack);
  ll_deinit_calls++;
  record(EVENT_LL_DEINIT, CLASS_COUNT, pdev->dev_config);
  return ll_deinit_status;
}

USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep, uint8_t type, uint16_t size)
{
  assert(pdev == &stack && (ep == 0x00U || ep == 0x80U));
  assert(type == USBD_EP_TYPE_CTRL && size == USB_MAX_EP0_SIZE);
  ep0_open_calls++;
  record(EVENT_EP0_OPEN, ep, pdev->dev_config);
  return USBD_OK; /* Endpoint-address adapter only; no control transfer is modeled. */
}

USBD_StatusTypeDef USBD_CtlSendStatus(USBD_HandleTypeDef *pdev)
{
  assert(pdev == &stack);
  ack_calls++;
  record(EVENT_ACK, CLASS_COUNT, pdev->dev_config);
  pdev->ep0_state = USBD_EP0_STATUS_IN;
  return USBD_OK;
}

void USBD_CtlError(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  assert(pdev == &stack && req != NULL);
  stall_calls++;
  record(EVENT_STALL, CLASS_COUNT, req->wValue);
  pdev->ep0_state = USBD_EP0_STALL;
}

/* These two counting adapters preserve the actual production function bodies
 * and expose extra dispatcher calls. EP0 ACK/STALL above record decisions, not
 * electrical transfers. SETUP shape validation precedes USBD_SetConfig and is
 * outside this direct-static-function fixture. */
#define USBD_SetClassConfig composite_production_SetClassConfig
#define USBD_ClrClassConfig composite_production_ClrClassConfig
#include "usb_composite_core.inc"
#undef USBD_SetClassConfig
#undef USBD_ClrClassConfig

USBD_StatusTypeDef USBD_SetClassConfig(USBD_HandleTypeDef *pdev, uint8_t cfg)
{
  set_calls++;
  return composite_production_SetClassConfig(pdev, cfg);
}

USBD_StatusTypeDef USBD_ClrClassConfig(USBD_HandleTypeDef *pdev, uint8_t cfg)
{
  clear_calls++;
  return composite_production_ClrClassConfig(pdev, cfg);
}

#include "usb_composite_ctlreq.inc"

static void reset_trace(void)
{
  memset(init_calls, 0, sizeof(init_calls));
  memset(deinit_calls, 0, sizeof(deinit_calls));
  memset(init_cleanup_calls, 0, sizeof(init_cleanup_calls));
  set_calls = clear_calls = ll_stop_calls = ll_deinit_calls = ep0_open_calls = ack_calls = stall_calls = event_count = 0U;
  inside_init_cleanup = false;
}

static void seed(unsigned active_mask, unsigned null_mask)
{
  memset(&stack, 0, sizeof(stack));
  memset(classes, 0, sizeof(classes));
  memset(resources, 0, sizeof(resources));
  memset(init_status, 0, sizeof(init_status));
  memset(deinit_status, 0, sizeof(deinit_status));
  reset_trace();
  ll_stop_status = ll_deinit_status = USBD_OK;
  stack.dev_state = USBD_STATE_CONFIGURED;
  stack.dev_config = 1U;
  stack.pDesc = &descriptors;
  stack.pConfDesc = configuration_descriptor;
  stack.NumClasses = CLASS_COUNT;
  for (unsigned id = 0U; id < CLASS_COUNT; id++) {
    classes[id].Init = class_init;
    classes[id].DeInit = class_deinit;
    stack.pClass[id] = (null_mask & (1U << id)) ? NULL : &classes[id];
    stack.pUserData[id] = &resources[id];
    stack.tclasslist[id].Active = (active_mask >> id) & 1U;
  }
}

static bool enabled(unsigned id)
{
  return stack.tclasslist[id].Active == 1U && stack.pClass[id] != NULL;
}

static void seed_owned(void)
{
  for (unsigned id = 0U; id < CLASS_COUNT; id++) {
    if (enabled(id)) {
      resources[id] = (class_resource_t){id, 1U};
      stack.pClassDataCmsit[id] = &resources[id];
    }
  }
}

static void check_handle(unsigned id, bool retained)
{
  assert(stack.pClassDataCmsit[id] == (retained ? &resources[id] : NULL));
  assert(stack.pUserData[id] == &resources[id]);
}

static void check_init_failure(unsigned active, unsigned nulls, unsigned failed, unsigned rollback_failures, bool partial_cleanup_fails)
{
  seed(active, nulls);
  assert(enabled(failed));
  init_status[failed][2] = USBD_FAIL;
  for (unsigned id = 0U; id < failed; id++) {
    if (rollback_failures & (1U << id)) deinit_status[id][2] = USBD_BUSY;
  }
  deinit_status[failed][2] = partial_cleanup_fails ? USBD_FAIL : USBD_OK;
  assert(USBD_SetClassConfig(&stack, 2U) == USBD_FAIL);
  assert(set_calls == 1U && clear_calls == 0U);
  for (unsigned id = 0U; id < CLASS_COUNT; id++) {
    unsigned expected_init = enabled(id) && id <= failed ? 1U : 0U;
    assert(init_calls[id][2] == expected_init);
    assert(deinit_calls[id][2] == expected_init);
    assert(init_cleanup_calls[id][2] == (id == failed ? 1U : 0U));
    bool retained = expected_init != 0U && (id == failed ? partial_cleanup_fails : (rollback_failures & (1U << id)) != 0U);
    check_handle(id, retained);
  }
  assert(stack.classId == failed);
  /* The failed class cleans itself before earlier successes are rolled back. */
  unsigned failed_init_event = 0U;
  while (events[failed_init_event].kind != EVENT_INIT || events[failed_init_event].id != failed) failed_init_event++;
  assert(events[failed_init_event + 1U].kind == EVENT_DEINIT && events[failed_init_event + 1U].id == failed);
  assert(events[failed_init_event + 1U].init_cleanup);
  for (unsigned i = failed_init_event + 2U; i < event_count; i++) {
    assert(events[i].kind == EVENT_DEINIT && events[i].id < failed && !events[i].init_cleanup);
  }
}

static void check_init_matrix(void)
{
  for (unsigned active = 1U; active < 8U; active++) {
    for (unsigned failed = 0U; failed < CLASS_COUNT; failed++) {
      if ((active & (1U << failed)) == 0U) continue;
      for (unsigned rollback = 0U; rollback < 8U; rollback++) {
        if ((rollback & ~((1U << failed) - 1U)) != 0U || (rollback & ~active) != 0U) continue;
        check_init_failure(active, 0U, failed, rollback, false);
        check_init_failure(active, 0U, failed, rollback, true);
      }
    }
  }
  check_init_failure(7U, 1U, 2U, 2U, false); /* active slot with NULL class is skipped */
  seed(7U, 0U);
  assert(USBD_SetClassConfig(&stack, 1U) == USBD_OK);
  for (unsigned id = 0U; id < CLASS_COUNT; id++) {
    assert(init_calls[id][1] == 1U && deinit_calls[id][1] == 0U);
    check_handle(id, true);
  }
  puts("PASS: composite Init stops at first failure, rolls back only earlier successes, avoids duplicate failed-class cleanup and preserves failed-cleanup handles");
}

static void configure_deinit_failures(unsigned failure_mask)
{
  for (unsigned id = 0U; id < CLASS_COUNT; id++) {
    if (failure_mask & (1U << id)) deinit_status[id][1] = id == 1U ? USBD_BUSY : USBD_FAIL;
  }
}

static void check_all_teardown_calls(unsigned failure_mask)
{
  for (unsigned id = 0U; id < CLASS_COUNT; id++) {
    assert(deinit_calls[id][1] == 1U && init_calls[id][1] == 0U && init_cleanup_calls[id][1] == 0U);
    check_handle(id, (failure_mask & (1U << id)) != 0U);
  }
}

static void check_clear_matrix(void)
{
  for (unsigned failures = 0U; failures < 8U; failures++) {
    seed(7U, 0U);
    seed_owned();
    configure_deinit_failures(failures);
    assert(USBD_ClrClassConfig(&stack, 1U) == (failures ? USBD_FAIL : USBD_OK));
    check_all_teardown_calls(failures);
    assert(clear_calls == 1U && ll_stop_calls == 0U && ll_deinit_calls == 0U);
  }
  seed(7U, 2U);
  stack.tclasslist[0].Active = 0U;
  seed_owned();
  assert(USBD_ClrClassConfig(&stack, 1U) == USBD_OK);
  assert(deinit_calls[0][1] == 0U && deinit_calls[1][1] == 0U && deinit_calls[2][1] == 1U);
  puts("PASS: composite Clear calls all eligible classes despite errors and preserves each failing class handle");
}

static void check_stop_matrix(void)
{
  const USBD_StatusTypeDef statuses[] = {USBD_OK, USBD_BUSY, USBD_EMEM, USBD_FAIL};
  for (unsigned failures = 0U; failures < 8U; failures++) {
    for (unsigned s = 0U; s < sizeof(statuses) / sizeof(statuses[0]); s++) {
      seed(7U, 0U);
      seed_owned();
      configure_deinit_failures(failures);
      ll_stop_status = statuses[s];
      assert(USBD_Stop(&stack) == (failures ? USBD_FAIL : ll_stop_status));
      check_all_teardown_calls(failures);
      assert(ll_stop_calls == 1U && ll_deinit_calls == 0U && stack.classId == 0U);
      assert(events[0].kind == EVENT_LL_STOP && event_count == CLASS_COUNT + 1U);
      assert(stack.pDesc == &descriptors && stack.pConfDesc == configuration_descriptor);
    }
  }
  puts("PASS: composite Stop propagates class/LL errors and still visits every class after an earlier failure");
}

static void check_deinit_matrix(void)
{
  const USBD_StatusTypeDef statuses[] = {USBD_OK, USBD_BUSY, USBD_EMEM, USBD_FAIL};
  for (unsigned failures = 0U; failures < 8U; failures++) {
    for (unsigned s = 0U; s < sizeof(statuses) / sizeof(statuses[0]); s++) {
      for (unsigned d = 0U; d < sizeof(statuses) / sizeof(statuses[0]); d++) {
        seed(7U, 0U);
        seed_owned();
        configure_deinit_failures(failures);
        ll_stop_status = statuses[s];
        ll_deinit_status = statuses[d];
        USBD_StatusTypeDef expected = ll_deinit_status != USBD_OK ? ll_deinit_status : failures ? USBD_FAIL : ll_stop_status;
        assert(USBD_DeInit(&stack) == expected);
        check_all_teardown_calls(failures);
        assert(ll_stop_calls == 1U && ll_deinit_calls == 1U);
        assert(events[0].kind == EVENT_LL_STOP && events[event_count - 1U].kind == EVENT_LL_DEINIT);
        assert(event_count == CLASS_COUNT + 2U && stack.dev_state == USBD_STATE_DEFAULT);
        assert(stack.pDesc == NULL && stack.pConfDesc == NULL);
      }
    }
  }
  puts("PASS: composite DeInit preserves failure status across class and LL cleanup while executing all remaining calls");
}

static void check_reset_matrix(void)
{
  for (unsigned failures = 0U; failures < 8U; failures++) {
    seed(7U, 0U);
    seed_owned();
    stack.dev_remote_wakeup = 1U;
    stack.dev_test_mode = 1U;
    stack.ep0_state = USBD_EP0_DATA_OUT;
    for (unsigned id = 0U; id < CLASS_COUNT; id++)
      deinit_status[id][0] = failures & (1U << id) ? USBD_FAIL : USBD_OK;
    assert(USBD_LL_Reset(&stack) == (failures ? USBD_FAIL : USBD_OK));
    for (unsigned id = 0U; id < CLASS_COUNT; id++) {
      assert(deinit_calls[id][0] == 1U && deinit_calls[id][1] == 0U);
      check_handle(id, (failures & (1U << id)) != 0U);
    }
    assert(ll_stop_calls == 0U && ll_deinit_calls == 0U && ep0_open_calls == 2U);
    assert(event_count == CLASS_COUNT + 2U);
    assert(events[CLASS_COUNT].kind == EVENT_EP0_OPEN && events[CLASS_COUNT].id == 0x00U);
    assert(events[CLASS_COUNT + 1U].kind == EVENT_EP0_OPEN && events[CLASS_COUNT + 1U].id == 0x80U);
    assert(stack.dev_state == USBD_STATE_DEFAULT && stack.dev_config == 0U);
    assert(stack.dev_remote_wakeup == 0U && stack.dev_test_mode == 0U && stack.ep0_state == USBD_EP0_IDLE);
    assert(stack.ep_in[0].is_used == 1U && stack.ep_out[0].is_used == 1U);
    reset_trace();
    memset(deinit_status, 0, sizeof(deinit_status));
    assert(USBD_LL_Reset(&stack) == USBD_OK);
    for (unsigned id = 0U; id < CLASS_COUNT; id++) {
      assert(deinit_calls[id][0] == 1U);
      check_handle(id, false);
    }
  }
  seed(7U, 2U);
  stack.tclasslist[0].Active = 0U;
  seed_owned();
  assert(USBD_LL_Reset(&stack) == USBD_OK);
  assert(deinit_calls[0][0] == 0U && deinit_calls[1][0] == 0U && deinit_calls[2][0] == 1U);
  for (unsigned id = 0U; id < CLASS_COUNT; id++) check_handle(id, false);
  puts("PASS: composite Reset preserves each interface alias, visits eligible siblings after errors, and releases retained class owners on a later explicit successful Reset");
}

static USBD_SetupReqTypedef request(uint16_t value)
{
  USBD_SetupReqTypedef req = {0};
  req.bRequest = USB_REQ_SET_CONFIGURATION;
  req.wValue = value;
  return req;
}

static void establish_config_one(void)
{
  seed(7U, 0U);
  stack.dev_state = USBD_STATE_ADDRESSED;
  stack.dev_config = 0U;
  USBD_SetupReqTypedef req = request(1U);
  assert(USBD_SetConfig(&stack, &req) == USBD_OK);
  assert(stack.dev_state == USBD_STATE_CONFIGURED && stack.dev_config == 1U && ack_calls == 1U && stall_calls == 0U);
  reset_trace();
}

static void check_config_matrix(void)
{
  for (unsigned failed = 0U; failed < CLASS_COUNT; failed++) {
    for (unsigned rollback_failure = 0U; rollback_failure < 2U; rollback_failure++) {
      establish_config_one();
      init_status[failed][2] = USBD_FAIL;
      if (rollback_failure && failed != 0U) deinit_status[0][2] = USBD_FAIL;
      USBD_SetupReqTypedef req = request(2U);
      assert(USBD_SetConfig(&stack, &req) == USBD_FAIL);
      assert(clear_calls == 1U); /* only old configuration; no extra Clear after failed Init */
      assert(set_calls == 1U && ack_calls == 0U && stall_calls == 1U && stack.ep0_state == USBD_EP0_STALL);
      assert(stack.dev_state == USBD_STATE_ADDRESSED && stack.dev_config == 0U);
      for (unsigned id = 0U; id < CLASS_COUNT; id++) {
        assert(deinit_calls[id][1] == 1U);
        assert(init_calls[id][2] == (id <= failed ? 1U : 0U));
        assert(deinit_calls[id][2] == (id <= failed ? 1U : 0U));
        assert(init_cleanup_calls[id][2] == (id == failed ? 1U : 0U));
        check_handle(id, rollback_failure && failed != 0U && id == 0U);
      }
    }
  }
  for (unsigned failed = 0U; failed < CLASS_COUNT; failed++) {
    establish_config_one();
    deinit_status[failed][1] = USBD_FAIL;
    USBD_SetupReqTypedef req = request(2U);
    assert(USBD_SetConfig(&stack, &req) == USBD_FAIL);
    assert(clear_calls == 1U && set_calls == 0U && stall_calls == 1U && ack_calls == 0U);
    assert(stack.dev_state == USBD_STATE_ADDRESSED && stack.dev_config == 0U);
    for (unsigned id = 0U; id < CLASS_COUNT; id++) {
      assert(deinit_calls[id][1] == 1U && init_calls[id][2] == 0U);
      check_handle(id, id == failed);
    }
  }
  establish_config_one();
  USBD_SetupReqTypedef req = request(2U);
  assert(USBD_SetConfig(&stack, &req) == USBD_OK);
  assert(clear_calls == 1U && set_calls == 1U && ack_calls == 1U && stall_calls == 0U && stack.dev_config == 2U);
  for (unsigned id = 0U; id < CLASS_COUNT; id++) assert(deinit_calls[id][1] == 1U && init_calls[id][2] == 1U);

  establish_config_one();
  req = request(1U);
  assert(USBD_SetConfig(&stack, &req) == USBD_OK);
  assert(clear_calls == 0U && set_calls == 0U && ack_calls == 1U);
  reset_trace();
  req = request(3U);
  assert(USBD_SetConfig(&stack, &req) == USBD_FAIL);
  assert(clear_calls == 0U && set_calls == 0U && ack_calls == 0U && stall_calls == 1U && stack.dev_config == 1U);
  puts("PASS: actual SET_CONFIGURATION with fixture MAX_CONFIG=2 switches 1->2 with one old Clear, one partial cleanup/rollback, STALL without ACK and config=0 on failure");
}

int main(int argc, char **argv)
{
  const char *only = argc > 1 ? argv[1] : "all";
  assert(!strcmp(only, "all") || !strcmp(only, "init") || !strcmp(only, "clear") ||
         !strcmp(only, "stop") || !strcmp(only, "deinit") || !strcmp(only, "reset") || !strcmp(only, "config"));
  if (!strcmp(only, "all") || !strcmp(only, "init")) check_init_matrix();
  if (!strcmp(only, "all") || !strcmp(only, "clear")) check_clear_matrix();
  if (!strcmp(only, "all") || !strcmp(only, "stop")) check_stop_matrix();
  if (!strcmp(only, "all") || !strcmp(only, "deinit")) check_deinit_matrix();
  if (!strcmp(only, "all") || !strcmp(only, "reset")) check_reset_matrix();
  if (!strcmp(only, "all") || !strcmp(only, "config")) check_config_matrix();
  return 0;
}
