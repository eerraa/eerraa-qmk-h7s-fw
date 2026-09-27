#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define _USE_HW_WS2812
#define _USE_HW_MICROS
#ifndef HW_WS2812_MAX_CH
#define HW_WS2812_MAX_CH 30
#endif
#define CLI_USE(x) 0
#define HAL_OK 0
#define HAL_ERROR 1
#define HAL_BUSY 2
#define HAL_DMA_STATE_READY 0
#define HAL_DMA_STATE_BUSY 1
#define HAL_DMA_STATE_ABORT 2
#define HAL_DMA_ERROR_NONE 0U
#define TIM_CCx_ENABLE 1U
#define TIM_CHANNEL_1 0U
#define TIM_DMA_ID_CC1 0U
#define GPIO_PIN_12 (1U << 12)
#define GPIO_PIN_RESET 0U
#define TIM_CCDMAREQUEST_UPDATE 8U
#define TIM_EGR_UG 1U
#define TIM_FLAG_UPDATE 1U
#define TIM_FLAG_CC1 2U
#define TIM_DMA_CC1 1U
#define __DMB() ((void)0)
#define MODIFY_REG(reg, mask, value) ((reg) = ((reg) & ~(mask)) | (value))
typedef int HAL_StatusTypeDef;
typedef int HAL_DMA_StateTypeDef;
typedef struct { uint32_t EGR, CNT, CCR1, CR2; } TIM_TypeDef;
typedef struct { uint32_t MODER, ODR; } GPIO_TypeDef;
typedef struct { unsigned Request, BlkHWRequest, Direction, SrcInc, DestInc, SrcDataWidth, DestDataWidth, Priority, SrcBurstLength, DestBurstLength, TransferAllocatedPort, TransferEventMode, Mode; } DMA_InitTypeDef;
typedef struct { void *Instance; DMA_InitTypeDef Init; HAL_DMA_StateTypeDef State; uint32_t ErrorCode; } DMA_HandleTypeDef;
typedef struct { unsigned Prescaler, CounterMode, Period, ClockDivision, RepetitionCounter, AutoReloadPreload; } TIM_Base_InitTypeDef;
typedef struct { TIM_TypeDef *Instance; TIM_Base_InitTypeDef Init; DMA_HandleTypeDef *hdma[1]; } TIM_HandleTypeDef;
typedef struct { unsigned ClockSource; } TIM_ClockConfigTypeDef;
typedef struct { unsigned MasterOutputTrigger, MasterSlaveMode; } TIM_MasterConfigTypeDef;
typedef struct { unsigned OCMode, Pulse, OCPolarity, OCNPolarity, OCFastMode, OCIdleState, OCNIdleState; } TIM_OC_InitTypeDef;
typedef struct { unsigned OffStateRunMode, OffStateIDLEMode, LockLevel, DeadTime, BreakState, BreakPolarity, BreakFilter, AutomaticOutput; } TIM_BreakDeadTimeConfigTypeDef;
typedef struct { unsigned Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
static TIM_TypeDef mock_tim;
static GPIO_TypeDef mock_gpio;
#define TIM15 (&mock_tim)
#define GPIOC (&mock_gpio)
#define GPDMA1_Channel4 ((void *)1)
static uint64_t mock_now_ns, mock_next_update_ns, mock_low_ns = 1000000;
static bool mock_timer_running, mock_frozen, mock_suppress_irq, mock_tc, mock_hold_abort;
static const uint8_t *mock_dma_data;
static uint8_t mock_dma_snapshot[4096];
static uint16_t mock_dma_len, mock_dma_index;
static unsigned mock_duty, mock_starts, mock_stops, mock_fail_starts, mock_mask;
static uint32_t mock_start_latency_us;
static bool mock_channel_enabled, mock_moe_enabled;
static void mock_advance_us(uint32_t delta);
static DMA_HandleTypeDef *mock_dma;
static uint8_t mock_rx[HW_WS2812_MAX_CH * 3];
static unsigned mock_bits, mock_wire_count;
static uint8_t mock_wire[1024][HW_WS2812_MAX_CH * 3];
static uint64_t mock_wire_ns[1024];
static bool mock_reset_latched = true;
static void (*mock_before_mask)(void), (*mock_after_restore)(void);
static uint32_t __get_PRIMASK(void) { return mock_mask; }
static void __disable_irq(void) { if (mock_before_mask) { void (*f)(void)=mock_before_mask; mock_before_mask=NULL; f(); } mock_mask=1; }
static void __set_PRIMASK(uint32_t value) { mock_mask=value; if (!value && mock_after_restore) { void (*f)(void)=mock_after_restore; mock_after_restore=NULL; f(); } }
#define __HAL_RCC_GPDMA1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM15_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_LINKDMA(htim, member, hdma_value) ((htim)->member = &(hdma_value))
static void TIM_CCxChannelCmd(TIM_TypeDef *t, uint32_t c, uint32_t state) { (void)t; (void)c; mock_channel_enabled=(state==TIM_CCx_ENABLE); }
#define __HAL_TIM_MOE_ENABLE(t) ((void)(t), mock_moe_enabled=true)
#define __HAL_TIM_SELECT_CCDMAREQUEST(htim, value) ((htim)->Instance->CR2 = (value))
#define __HAL_TIM_DISABLE_DMA(htim, channel) ((void)(htim), (void)(channel))
#define __HAL_TIM_SET_COMPARE(htim, channel, value) ((htim)->Instance->CCR1 = (value))
#define __HAL_TIM_SET_COUNTER(htim, value) ((htim)->Instance->CNT = (value))
#define __HAL_TIM_CLEAR_FLAG(htim, flags) ((void)(htim), (void)(flags))
static int HAL_TIM_Base_Init(TIM_HandleTypeDef *t) { assert(t->Init.Prescaler==2 && t->Init.Period==129); return HAL_OK; }
#define HAL_TIM_ConfigClockSource(t, p) ((void)(t), (void)(p), HAL_OK)
#define HAL_TIM_PWM_Init(t) ((void)(t), HAL_OK)
#define HAL_TIMEx_MasterConfigSynchronization(t, p) ((void)(t), (void)(p), HAL_OK)
#define HAL_TIM_PWM_ConfigChannel(t, p, c) ((void)(t), (void)(p), (void)(c), HAL_OK)
#define HAL_TIMEx_ConfigBreakDeadTime(t, p) ((void)(t), (void)(p), HAL_OK)
static void HAL_GPIO_Init(GPIO_TypeDef *g, const GPIO_InitTypeDef *c) { (void)c; g->MODER=2U<<24; }
static void HAL_GPIO_WritePin(GPIO_TypeDef *g, uint32_t p, uint32_t v) { assert(v==0); g->ODR &= ~p; }
static int HAL_DMA_Init(DMA_HandleTypeDef *d) { d->State=HAL_DMA_STATE_READY; d->ErrorCode=0; return HAL_OK; }
#define HAL_DMA_ConfigChannelAttributes(d, a) ((void)(d), (void)(a), HAL_OK)
#define HAL_NVIC_SetPriority(i, a, b) ((void)0)
#define HAL_NVIC_EnableIRQ(i) ((void)0)
uint32_t micros(void) { return (uint32_t)(mock_now_ns/1000U); }
static int HAL_DMA_GetState(DMA_HandleTypeDef *d) { return d->State; }
static uint32_t HAL_DMA_GetError(DMA_HandleTypeDef *d) { return d->ErrorCode; }
static void HAL_DMA_IRQHandler(DMA_HandleTypeDef *d) {
  if (d->State==HAL_DMA_STATE_ABORT && !mock_hold_abort) { d->State=HAL_DMA_STATE_READY; mock_tc=false; return; }
  if (mock_tc) { mock_tc=false; d->State=HAL_DMA_STATE_READY; }
}
static int HAL_TIM_PWM_Start_DMA(TIM_HandleTypeDef *t, uint32_t c, const uint32_t *data, uint16_t length) {
  (void)c; mock_starts++;
  if (mock_fail_starts) { --mock_fail_starts; return HAL_ERROR; }
  mock_dma=t->hdma[0]; assert(mock_dma->State==HAL_DMA_STATE_READY);
  assert(length<=sizeof(mock_dma_snapshot));
#ifndef TEST_LEGACY_RESET
  assert(t->Instance->CR2==TIM_CCDMAREQUEST_UPDATE);
  assert(mock_channel_enabled && mock_moe_enabled && t->Instance->CCR1==0);
  assert((mock_gpio.MODER&(3U<<24))==(2U<<24));
#endif
  mock_dma_data=(const uint8_t *)data; mock_dma_len=length; mock_dma_index=0;
  memcpy(mock_dma_snapshot,data,length); mock_tc=false;
  mock_dma->State=HAL_DMA_STATE_BUSY; mock_dma->ErrorCode=0;
  mock_timer_running=true; mock_duty=0; mock_next_update_ns=mock_now_ns+1300;
  if (mock_start_latency_us) { uint32_t delay=mock_start_latency_us; mock_start_latency_us=0; mock_advance_us(delay); }
  return HAL_OK;
}
static int HAL_TIM_PWM_Stop_DMA(TIM_HandleTypeDef *t, uint32_t c) {
  (void)c; mock_stops++; mock_timer_running=false;
  mock_channel_enabled=mock_moe_enabled=false;
  if (t->hdma[0]->State==HAL_DMA_STATE_BUSY) t->hdma[0]->State=HAL_DMA_STATE_ABORT;
  else if (t->hdma[0]->State==HAL_DMA_STATE_READY) t->hdma[0]->ErrorCode=2; // actual Abort_IT: NO_XFER
  return HAL_OK;
}
/* Digital receiver only: first N*24 bits until a valid RESET; no analogue/PWM model. */
static void mock_emit(unsigned duty) {
  if (duty) {
    assert(duty==35 || duty==70);
    if (mock_reset_latched) { mock_bits=0; memset(mock_rx,0,sizeof(mock_rx)); mock_reset_latched=false; }
    if (mock_bits<HW_WS2812_MAX_CH*24U) mock_rx[mock_bits/8U]=(uint8_t)((mock_rx[mock_bits/8U]<<1)|(duty==70));
    mock_bits++; mock_low_ns=1300U-duty*10U;
  } else {
    mock_low_ns+=1300U;
    if (!mock_reset_latched && mock_low_ns>280000U) {
      if (mock_bits>=HW_WS2812_MAX_CH*24U) {
        assert(mock_wire_count<1024);
        memcpy(mock_wire[mock_wire_count],mock_rx,sizeof(mock_rx));
        mock_wire_ns[mock_wire_count++]=mock_now_ns;
      }
      mock_bits=0; mock_reset_latched=true;
    }
  }
}
static void mock_advance_us(uint32_t delta) {
  uint64_t end=mock_now_ns+(uint64_t)delta*1000U;
  while (mock_timer_running && !mock_frozen && mock_next_update_ns<=end) {
    mock_now_ns=mock_next_update_ns; mock_next_update_ns+=1300;
    mock_emit((mock_gpio.MODER&(3U<<24))==(2U<<24) ? mock_duty : 0U); mock_duty=mock_tim.CCR1;
    if (mock_dma_index<mock_dma_len) {
      assert(mock_dma_data[mock_dma_index]==mock_dma_snapshot[mock_dma_index]); // pointer retained by DMA
      mock_tim.CCR1=mock_dma_data[mock_dma_index++];
      if (mock_dma_index==mock_dma_len) {
        mock_tc=true;
        if (!mock_suppress_irq && !mock_mask) HAL_DMA_IRQHandler(mock_dma);
      }
    }
  }
  if (!mock_timer_running) mock_low_ns+=end-mock_now_ns;
  mock_now_ns=end;
}
static void mock_reset(void) {
  memset(&mock_tim,0,sizeof(mock_tim)); memset(&mock_gpio,0,sizeof(mock_gpio));
  mock_now_ns=0; mock_low_ns=1000000; mock_timer_running=mock_frozen=mock_suppress_irq=mock_tc=mock_hold_abort=false;
  mock_starts=mock_stops=mock_fail_starts=mock_mask=mock_bits=mock_wire_count=0;
  mock_reset_latched=true; mock_before_mask=mock_after_restore=NULL;
  mock_start_latency_us=0; mock_channel_enabled=mock_moe_enabled=false;
}
#define TIM_COUNTERMODE_UP 0U
#define TIM_CLOCKDIVISION_DIV1 0U
#define TIM_AUTORELOAD_PRELOAD_ENABLE 0U
#define TIM_CLOCKSOURCE_INTERNAL 0U
#define TIM_TRGO_RESET 0U
#define TIM_MASTERSLAVEMODE_DISABLE 0U
#define TIM_OCMODE_PWM1 0U
#define TIM_OCPOLARITY_HIGH 0U
#define TIM_OCNPOLARITY_HIGH 0U
#define TIM_OCFAST_DISABLE 0U
#define TIM_OCIDLESTATE_RESET 0U
#define TIM_OCNIDLESTATE_RESET 0U
#define TIM_OSSR_DISABLE 0U
#define TIM_OSSI_DISABLE 0U
#define TIM_LOCKLEVEL_OFF 0U
#define TIM_BREAK_DISABLE 0U
#define TIM_BREAKPOLARITY_HIGH 0U
#define TIM_AUTOMATICOUTPUT_DISABLE 0U
#define GPIO_MODE_AF_PP 0U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_VERY_HIGH 0U
#define GPIO_AF2_TIM15 0U
#define GPDMA1_REQUEST_TIM15_CH1 0U
#define DMA_BREQ_SINGLE_BURST 0U
#define DMA_MEMORY_TO_PERIPH 0U
#define DMA_SINC_INCREMENTED 0U
#define DMA_DINC_FIXED 0U
#define DMA_SRC_DATAWIDTH_BYTE 0U
#define DMA_DEST_DATAWIDTH_BYTE 0U
#define DMA_LOW_PRIORITY_LOW_WEIGHT 0U
#define DMA_SRC_ALLOCATED_PORT0 0U
#define DMA_DEST_ALLOCATED_PORT0 0U
#define DMA_TCEM_BLOCK_TRANSFER 0U
#define DMA_NORMAL 0U
#define DMA_CHANNEL_NPRIV 0U
#define GPDMA1_Channel4_IRQn 0U
