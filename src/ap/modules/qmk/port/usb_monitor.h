#pragma once

#include <stdbool.h>
#include <stdint.h>

#include QMK_KEYMAP_CONFIG_H

// V251108R1: USB 안정성 모니터 VIA 핸들러/스토리지 인터페이스
#ifdef USB_MONITOR_ENABLE
#include "port.h"

extern usb_monitor_config_t usb_monitor_config;

void usb_monitor_init(void);                                   // V251112R6: USB 모니터 기본값 초기화 진입점
void via_qmk_usb_monitor_command(uint8_t *data, uint8_t length);
void usb_monitor_storage_init(void);
void usb_monitor_storage_set_enable(bool enable);
void usb_monitor_storage_flush(bool force);
bool usb_monitor_storage_is_enabled(void);
void usb_monitor_storage_apply_defaults(void);                 // V251112R5: EEPROM 기본값 적용 헬퍼
#else
static inline void via_qmk_usb_monitor_command(uint8_t *data, uint8_t length)
{
  (void)data;
  (void)length;
}

static inline void usb_monitor_init(void)
{
}

static inline void usb_monitor_storage_init(void)
{
}

static inline void usb_monitor_storage_set_enable(bool enable)
{
  (void)enable;
}

static inline void usb_monitor_storage_flush(bool force)
{
  (void)force;
}

static inline bool usb_monitor_storage_is_enabled(void)
{
  return false;
}

static inline void usb_monitor_storage_apply_defaults(void)
{
}
#endif
