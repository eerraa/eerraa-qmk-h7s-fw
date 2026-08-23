/*
 * The MIT License (MIT)
 *
 * Copyright (c) Microsoft Corporation
 * Copyright (c) Ha Thach for Adafruit Industries
 * Copyright (c) Henry Gabryjelski
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


#include "uf2.h"
#include "boot/boot.h"
#include "usb.h"                                                 // V260824R1: tud_disconnect() 사용


//--------------------------------------------------------------------+
//
//--------------------------------------------------------------------+
#define BPB_SECTOR_SIZE           (512)
#define UF2_CSW_DRAIN_DELAY_MS    (500)                          // V260824R1: 마지막 CSW 송신 완료 대기
#define UF2_DETACH_DELAY_MS       (100)                          // V260824R1: 호스트가 디태치를 확정할 최소 시간


static uint32_t _flash_size;
static uint16_t _flash_crc = 0;
static uint32_t _flash_len = 0;
static bool is_reset_req = false;                                // V260824R1: 점프 대신 시스템 리셋으로 펌웨어를 시작한다




bool uf2_flash_is_blank(uint32_t addr, uint32_t size)
{
  uint32_t data;

  for ( uint32_t i = 0; i < size; i += sizeof(uint32_t) )
  {
    flashRead(addr, (uint8_t *)&data, sizeof(uint32_t));

    if (data != 0xffffffff )
    {
      return false;
    }
  }
  return true;
}

void uf2_flash_flush(WriteState *state)
{
  firm_tag_t firm_tag;

  firm_tag.magic_number = TAG_MAGIC_NUMBER;
  firm_tag.fw_addr      = FLASH_SIZE_TAG;
  firm_tag.fw_size      = _flash_len;
  firm_tag.fw_crc       = _flash_crc;
  
  logPrintf("\n");
  logPrintf("fw addr : 0x%X\n", firm_tag.fw_addr);
  logPrintf("fw size : %d B\n", firm_tag.fw_size );
  logPrintf("fw crc  : 0x%X\n", firm_tag.fw_crc);

  if (flashWrite(FLASH_ADDR_UPDATE, (uint8_t *)&firm_tag, sizeof(firm_tag_t)) != true)
  {
    logPrintf("uf2_flash_flush() fail\n");
  }   
}

void uf2_flash_write(WriteState *state, uint32_t addr, void const *data, uint32_t len)
{
  uint32_t flash_addr;
  bool ret = true;


  flash_addr = FLASH_ADDR_UPDATE + FLASH_SIZE_TAG + addr;

  if (!uf2_flash_is_blank(flash_addr, len))
  {
    ret = flashErase(flash_addr, len);
    if (!ret)
    {
      logPrintf("[%s] flashErase(0x%X, %d)\n", ret?"OK":"E_", flash_addr, len);
    }
  }
  ret = flashWrite(flash_addr, (uint8_t *)data, len);
  if (!ret)
  {
    logPrintf("[%s] flashWrite 0x%X, %d\n", ret?"OK":"E_", flash_addr, len);
  }

  _flash_crc  = utilCalcCRC(_flash_crc, (uint8_t *)data, len);
  _flash_len += len;
}

// V260824R1: UF2 완료·이젝트 시 점프 대신 시스템 리셋으로 펌웨어를 시작한다.
//            점프 경로는 USB를 전기적으로 분리하지 못해 호스트가 재열거를
//            수행하지 않는 경우가 있었다. 리셋 경로는 전원 재인가와 동일하다.
void uf2RequestFirmStart(bool is_update_req)
{
  uint32_t boot_param;

  if (is_reset_req)
    return;

  if (is_update_req)
  {
    boot_param  = resetGetBootMode();
    boot_param |= (1<<MODE_BIT_UPDATE);
    resetSetBootMode(boot_param);                                // V260824R1: RTC 백업 레지스터에 예약 (리셋 후에도 보존)
    logPrintf("[  ] uf2 update reserved\n");
  }

  is_reset_req = true;
}

void uf2_flash_complete(WriteState *state)
{
  (void)state;

  // V260824R1: 수 초가 걸리는 플래시 소거/복사를 USB 콜백에서 수행하면
  //            tud_task() 가 멈춰 호스트가 타임아웃한다.
  //            실제 복사는 리셋 이후 bootUp() 의 MODE_BIT_UPDATE 경로가 담당한다.
  uf2RequestFirmStart(true);
}

//--------------------------------------------------------------------+
//
//--------------------------------------------------------------------+

static inline bool is_uf2_block(UF2_Block const *bl)
{
  return (bl->magicStart0 == UF2_MAGIC_START0) &&
         (bl->magicStart1 == UF2_MAGIC_START1) &&
         (bl->magicEnd == UF2_MAGIC_END) &&
         (bl->flags & UF2_FLAG_FAMILYID) &&
         !(bl->flags & UF2_FLAG_NOFLASH);
}



void uf2Init(void)
{
  _flash_size = FLASH_SIZE_FIRM;
}

void uf2Update(void)
{
  static uint8_t state = 0;
  static uint32_t pre_time;

  switch(state)
  {
    case 0:
      if (is_reset_req)
      {
        pre_time = millis();
        state = 1;
      }
      break;

    case 1:
      if (millis()-pre_time >= UF2_CSW_DRAIN_DELAY_MS)
      {
        tud_disconnect();                                        // V260824R1: DCTL.SDIS 로 실제 전기적 디태치
        pre_time = millis();
        state = 2;
      }
      break;

    case 2:
      if (millis()-pre_time >= UF2_DETACH_DELAY_MS)
      {
        resetToReset();                                          // V260824R1: 전원 재인가와 동일한 경로로 펌웨어 시작
      }
      break;
  }
}


/*------------------------------------------------------------------*/
/* Write UF2
 *------------------------------------------------------------------*/

/**
 * Write an uf2 block wrapped by 512 sector.
 * @return number of bytes processed, only 3 following values
 *  -1 : if not an uf2 block
 * 512 : write is successful (BPB_SECTOR_SIZE == 512)
 *   0 : is busy with flashing, tinyusb stack will call write_block again with the same parameters later on
 */
int uf2_write_block(uint32_t block_no, uint8_t *data, WriteState *state)
{
  (void)block_no;
  UF2_Block *bl = (void *)data;

  if (!is_uf2_block(bl))
    return -1;

  if (bl->familyID == BOARD_UF2_FAMILY_ID)
  {
    // generic family ID
    uf2_flash_write(state, bl->targetAddr, bl->data, bl->payloadSize);
  }
  else
  {
    // TODO family matches VID/PID
    logPrintf("familyID Not Match\n");
    logPrintf("  0x%X:0x%X\n", BOARD_UF2_FAMILY_ID, bl->familyID);
    return -1;
  }

  //------------- Update written blocks -------------//
  if (bl->numBlocks)
  {
    // Update state num blocks if needed
    if (state->numBlocks != bl->numBlocks)
    {
      if (bl->numBlocks >= MAX_BLOCKS || state->numBlocks)
      {
        state->numBlocks = 0xffffffff;
      }
      else
      {
        state->numBlocks = bl->numBlocks;
      }
    }

    if (bl->blockNo < MAX_BLOCKS)
    {
      uint8_t const  mask = 1 << (bl->blockNo % 8);
      uint32_t const pos  = bl->blockNo / 8;

      // only increase written number with new write (possibly prevent overwriting from OS)
      if (!(state->writtenMask[pos] & mask))
      {
        state->writtenMask[pos] |= mask;
        state->numWritten++;
      }

      // flush last blocks
      // TODO numWritten can be smaller than numBlocks if return early
      if (state->numWritten >= state->numBlocks)
      {
        uf2_flash_flush(state);
      }
    }
  }

  return BPB_SECTOR_SIZE;
}
