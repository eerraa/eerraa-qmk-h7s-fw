#pragma once
#include "hw_def.h"
#define __IO volatile
#define __STATIC_INLINE static inline
#define __PACKED __attribute__((packed))
#define USBD_MAX_NUM_INTERFACES 3U
void *USBD_static_malloc(uint32_t);
void USBD_static_free(void *);
#define USBD_malloc USBD_static_malloc
#define USBD_free USBD_static_free
#define USBD_memset memset
#define USBD_memcpy memcpy
