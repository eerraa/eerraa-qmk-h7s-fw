#pragma once
#include "hw_def.h"
void usbBootModeApplyDefaults(void);
#ifdef TEST_USB_TRANSPORT
#include "usbd_core.h"
typedef struct { uint32_t unused; } PCD_HandleTypeDef;
#define __HAL_PCD_UNGATE_PHYCLOCK(h) ((void)(h))
HAL_StatusTypeDef HAL_PCD_ActivateRemoteWakeup(PCD_HandleTypeDef *);
HAL_StatusTypeDef HAL_PCD_DeActivateRemoteWakeup(PCD_HandleTypeDef *);
uint8_t usbBootModeGetHsInterval(void);
#endif
