#ifndef QBUFFER_H_
#define QBUFFER_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "def.h"



typedef struct
{
  uint32_t in;
  uint32_t out;
  uint32_t len;
  uint32_t size;

  uint8_t *p_buf;
  uint32_t reserve_index;    // V251010R7: 슬롯 예약 API 지원용 임시 인덱스
  uint32_t reserve_next;     // V251010R7: 커밋 시 갱신할 다음 인덱스
  bool     is_acquired;      // V251010R7: 예약 상태 플래그
} qbuffer_t;


void     qbufferInit(void);
bool     qbufferCreate(qbuffer_t *p_node, uint8_t *p_buf, uint32_t length);
bool     qbufferCreateBySize(qbuffer_t *p_node, uint8_t *p_buf, uint32_t size, uint32_t length);
bool     qbufferWrite(qbuffer_t *p_node, uint8_t *p_data, uint32_t length);
bool     qbufferRead(qbuffer_t *p_node, uint8_t *p_data, uint32_t length);
uint8_t *qbufferPeekWrite(qbuffer_t *p_node);
uint8_t *qbufferPeekRead(qbuffer_t *p_node);
uint32_t qbufferAvailable(qbuffer_t *p_node);
void     qbufferFlush(qbuffer_t *p_node);
bool     qbufferAcquire(qbuffer_t *p_node, uint8_t **p_slot);   // V251010R7: 단일 복사용 슬롯 예약
void     qbufferCommit(qbuffer_t *p_node);                      // V251010R7: 예약 슬롯 커밋
void     qbufferRollback(qbuffer_t *p_node);                    // V251010R7: 예약 슬롯 롤백
bool     qbufferPop(qbuffer_t *p_node);                         // V251010R7: 큐 헤드 1건 소비



#ifdef __cplusplus
}
#endif

#endif