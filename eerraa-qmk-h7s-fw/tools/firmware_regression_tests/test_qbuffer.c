#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "qbuffer.h"
int main(void)
{
  qbuffer_t q = {0};
  uint8_t storage[12] = {0x42}, output[16];
  assert(qbufferCreate(&q, storage, 4U));
  memset(output, 0xA5, sizeof(output));
  assert(!qbufferRead(&q, output, 1U));
  assert(output[0] == 0xA5U);  // baseline copied stale memory before checking empty
  assert(qbufferPeekRead(&q) == NULL);
  assert(qbufferCreateBySize(&q, storage, 3U, 4U));
  uint8_t a[3] = {1,2,3}, b[3] = {4,5,6}, c[3] = {7,8,9};
  assert(qbufferWrite(&q, a, 1U) && qbufferWrite(&q, b, 1U) && qbufferWrite(&q, c, 1U));
  assert(qbufferAvailable(&q) == 3U && qbufferPeekWrite(&q) == NULL);
  assert(!qbufferWrite(&q, a, 1U));
  assert(!qbufferRead(&q, output, 4U));
  assert(!memcmp(output, a, 3U) && !memcmp(output+3, b, 3U) && !memcmp(output+6, c, 3U));
  assert(output[9] == 0xA5U && qbufferAvailable(&q) == 0U);
  for (unsigned i=0; i<100000U; i++) {
    a[0]=(uint8_t)i; assert(qbufferWrite(&q,a,1U)); assert(qbufferRead(&q,output,1U));
    assert(!memcmp(a,output,3U));
  }
  memcpy(qbufferPeekWrite(&q), b, 3U); assert(qbufferWrite(&q,NULL,1U));
  assert(!memcmp(qbufferPeekRead(&q),b,3U)); assert(qbufferRead(&q,NULL,1U));
  qbufferFlush(&q); assert(qbufferAvailable(&q)==0U);
  assert(!qbufferCreateBySize(&q,storage,0U,4U));
  assert(!qbufferCreateBySize(&q,storage,3U,UINT32_MAX));
  assert(!qbufferCreate(&q,storage,1U));
  assert(qbufferAvailable(&q)==0U && !qbufferRead(&q,output,1U) && !qbufferWrite(&q,a,1U));
  assert(qbufferPeekRead(&q)==NULL && qbufferPeekWrite(&q)==NULL);
  assert(!qbufferCreate(NULL,storage,4U) && qbufferAvailable(NULL)==0U);
  qbufferFlush(NULL);
  assert(qbufferCreate(&q,NULL,4U) && qbufferWrite(&q,NULL,3U) && qbufferRead(&q,NULL,3U));
  puts("PASS: actual qbuffer empty-output canary, partial-prefix contract, 100000 wraps, full/peek/index-only/invalid geometry");
  return 0;
}
