/* The production HID class supplies the observation; no register timing is modeled. */
static void test_polling_label(void)
{
  assert(strcmp(usbHidGetPollingLabel(), "Unavailable") == 0);
  const char *labels[] = {"8000 Hz (HS)", "4000 Hz (HS)", "2000 Hz (HS)"};
  for (unsigned interval = 1U; interval <= 3U; interval++) {
    hs_interval = interval;
    configure(true);
    assert(strcmp(usbHidGetPollingLabel(), labels[interval - 1U]) == 0);
    /* A changed saved/pending setting and Other-Speed query cannot rewrite the active endpoint. */
    hs_interval = interval % 3U + 1U;
    uint16_t size;
    USBD_HID.GetOtherSpeedConfigDescriptor(&size);
    assert(strcmp(usbHidGetPollingLabel(), labels[interval - 1U]) == 0);
    USBD_Device.dev_old_state = USBD_STATE_CONFIGURED;
    USBD_Device.dev_state = USBD_STATE_SUSPENDED;
    assert(strcmp(usbHidGetPollingLabel(), labels[interval - 1U]) == 0);
    USBD_Device.dev_state = USBD_STATE_CONFIGURED;
    USBD_Device.ep_in[HID_EPIN_ADDR & 15U].bInterval = 4U;
    assert(strcmp(usbHidGetPollingLabel(), "Unavailable") == 0);
    USBD_Device.ep_in[HID_EPIN_ADDR & 15U].bInterval = interval;
    USBD_Device.ep_in[HID_EPIN_ADDR & 15U].is_used = 0U;
    assert(strcmp(usbHidGetPollingLabel(), "Unavailable") == 0);
    USBD_Device.ep_in[HID_EPIN_ADDR & 15U].is_used = 1U;
    uint8_t response[HID_VIA_EP_SIZE];
    uint32_t generation;
    via_packet(0x08U, sizeof(response));
    assert(usbHidReadViaRequest(response, &generation));
    assert(strcmp(usbHidGetPollingLabel(), labels[interval - 1U]) == 0);
    test_usb_reset_pending = true;
    assert(strcmp(usbHidGetPollingLabel(), "Unavailable") == 0);
    usbHidOnBusResetBegin();
    test_usb_reset_pending = false;
    assert(strcmp(usbHidGetPollingLabel(), "Unavailable") == 0);
    assert(!usbHidEnqueueViaResponse(response, sizeof(response), generation));
    stop();
  }
  hs_interval = 1U;
  USBD_Device.dev_speed = USBD_SPEED_FULL;
  USBD_Device.dev_state = USBD_STATE_ADDRESSED;
  assert(USBD_HID.Init(&USBD_Device, 0U) == USBD_OK);
  assert(strcmp(usbHidGetPollingLabel(), "Unavailable") == 0);
  USBD_Device.dev_state = USBD_STATE_CONFIGURED;
  assert(strcmp(usbHidGetPollingLabel(), "1000 Hz (FS)") == 0);
  USBD_Device.dev_speed = USBD_SPEED_LOW;
  assert(strcmp(usbHidGetPollingLabel(), "Unavailable") == 0);
  stop();
  const uint8_t endpoints[] = {HID_EPIN_ADDR, HID_EXK_EP_IN, HID_VIA_EP_IN, HID_VIA_EP_OUT};
  for (unsigned i = 0; i < sizeof(endpoints); i++) {
    open_fail = endpoints[i];
    USBD_Device.dev_state = USBD_STATE_CONFIGURED;
    assert(USBD_HID.Init(&USBD_Device, 0U) == USBD_FAIL);
    assert(strcmp(usbHidGetPollingLabel(), "Unavailable") == 0);
    open_fail = 0U;
    stop();
  }
  receive_fail = true;
  USBD_Device.dev_state = USBD_STATE_CONFIGURED;
  assert(USBD_HID.Init(&USBD_Device, 0U) == USBD_FAIL);
  assert(strcmp(usbHidGetPollingLabel(), "Unavailable") == 0);
  receive_fail = false;
  stop();
  puts("PASS: polling label: active endpoint vs pending/Other-Speed, FS/HS, Suspend, reset and failed init");
}
