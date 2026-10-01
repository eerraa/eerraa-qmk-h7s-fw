#ifndef USB_COMPOSITE_FIXTURE_CONF_H
#define USB_COMPOSITE_FIXTURE_CONF_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define USE_USBD_COMPOSITE
/* This override is fixture-only. The shipping configuration limit is one. */
#define USBD_MAX_NUM_CONFIGURATION 2U
#define USBD_USER_REGISTER_CALLBACK 0U
#define USBD_SUPPORT_USER_STRING_DESC 0U
#define USBD_CLASS_USER_STRING_DESC 0U
#define USBD_CLASS_BOS_ENABLED 0U
#define __IO volatile
#define __PACKED __attribute__((packed))
#define __STATIC_INLINE static inline

#endif
