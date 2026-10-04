/* Exercise the production session snapshot across real class lifecycle calls. */
static void test_session_snapshot(void)
{
  usb_hid_session_t first = usbHidGetSession();
  assert(!first.valid);
  configure(true);
  first = usbHidGetSession();
  assert(first.valid && !first.suspended);
  test_irqmask = 1U;
  assert(usbHidGetSession().generation == first.generation);
  assert(test_irqmask == 1U);
  test_irqmask = 0U;
  USBD_Device.dev_old_state = USBD_STATE_CONFIGURED;
  USBD_Device.dev_state = USBD_STATE_SUSPENDED;
  usb_hid_session_t sleeping = usbHidGetSession();
  assert(sleeping.valid && sleeping.suspended && sleeping.generation == first.generation);
  USBD_Device.dev_state = USBD_STATE_CONFIGURED;
  assert(usbHidGetSession().valid && !usbHidGetSession().suspended);
  uint8_t keys[HID_KEYBOARD_REPORT_SIZE] = {0};
  uint8_t consumer[3] = {4U, 0xE9U, 0U};
  keys[2] = 4U;
  assert(usbHidSendReportForGeneration(keys, sizeof(keys), first.generation));
  assert(usbHidSendReportEXKForGeneration(consumer, sizeof(consumer), first.generation));
  drain();
  test_usb_reset_pending = true;
  assert(!usbHidGetSession().valid);
  usbHidOnBusResetBegin();
  test_usb_reset_pending = false;
  assert(!usbHidGetSession().valid);
  assert(usbHidGetSession().generation != first.generation);
  stop();
  configure(true);
  assert(usbHidGetSession().valid && usbHidGetSession().generation != first.generation);
  // Reconfiguration must not replay the cached synthetic keyboard or usage.
  assert(delivered_count >= 4U);
  for (unsigned i = delivered_count - 4U; i < delivered_count; i++) {
    unsigned start = delivered[i].ep == 1U ? 0U : 1U;
    for (unsigned j = start; j < delivered[i].length; j++) assert(delivered[i].data[j] == 0U);
  }
  unsigned count = delivered_count;
  assert(!usbHidSendReportForGeneration(keys, sizeof(keys), first.generation));
  assert(!usbHidSendReportEXKForGeneration(consumer, sizeof(consumer), first.generation));
  drain();
  assert(delivered_count == count);
  usbHidDelayKeyboardReportForGeneration(60000U, first.generation);
  // A retired interval cannot hold fresh ordinary traffic at the same clock.
  // A fresh ordinary state is still reconciled across a later connection.
  assert(usbHidSendReport(keys, sizeof(keys)));
  assert(usbHidSendReportEXK(consumer, sizeof(consumer)));
  drain();
  assert(delivered_count == count + 2U);
  stop();
  configure(true);
  bool key_seen = false, usage_seen = false;
  for (unsigned i = delivered_count - 4U; i < delivered_count; i++) {
    if (delivered[i].ep == 1U && delivered[i].data[2] == 4U) key_seen = true;
    if (delivered[i].ep == 5U && delivered[i].data[0] == 4U && delivered[i].data[1] == 0xE9U) usage_seen = true;
  }
  assert(key_seen && usage_seen);
  memset(keys, 0, sizeof(keys)); consumer[1] = 0U;
  assert(usbHidSendReport(keys, sizeof(keys)));
  assert(usbHidSendReportEXK(consumer, sizeof(consumer)));
  drain();
  stop();
  USBD_Device.dev_old_state = USBD_STATE_ADDRESSED;
  USBD_Device.dev_state = USBD_STATE_SUSPENDED;
  assert(!usbHidGetSession().valid);
  USBD_Device.dev_state = USBD_STATE_DEFAULT;
  open_fail = HID_EPIN_ADDR;
  assert(USBD_HID.Init(&USBD_Device, 0U) == USBD_FAIL);
  assert(!usbHidGetSession().valid);
  open_fail = 0U;
  stop();
  assert(test_irqmask == 0U);
  puts("PASS: coherent HID session: configured, same-generation suspend, raw reset, reconfigure, failed init, IRQ mask");
}
