#include "eeprom.h"
#if defined(QMK_KEYMAP_CONFIG_H)
#include "qmk/port/platforms/eeprom.h"                 // V251112R2: QMK EEPROM 큐 상태 출력
#endif


#if defined(_USE_HW_EEPROM) && defined(EEPROM_CHIP_ZD24C128)
#include "i2c.h"
#include "cli.h"



#if CLI_USE(HW_EEPROM)
void cliEeprom(cli_args_t *args);
#endif


#define EEPROM_MAX_SIZE                (16*1024)
#define EEPROM_PAGE_SIZE               32                          // V251112R5: ZD24C128 페이지 크기
#define EEPROM_WRITE_I2C_TIMEOUT_MS    10                          // V251112R5: 페이지 쓰기 I2C 타임아웃
#define EEPROM_WRITE_READY_TIMEOUT_MS  100                         // V251112R5: 페이지 쓰기 완료 확인 제한 시간


static bool is_init = false;
static uint8_t i2c_ch = _DEF_I2C1;
static uint8_t i2c_addr = 0x50;
static uint8_t page_write_buf[EEPROM_PAGE_SIZE];                   // V251112R5: I2C 페이지 버퍼

static bool eepromWaitReady(uint32_t timeout_ms);



bool eepromInit()
{
  bool ret;


  ret = i2cBegin(i2c_ch, 1000);                                    // V251112R5: FastMode Plus 1 MHz


  if (ret == true)
  {
    ret = eepromValid(0x00);
  }

  logPrintf("[%s] eepromInit()\n", ret ? "OK":"NG");
  if (ret == true)
  {
    logPrintf("     chip  : ZD24C128\n");
    logPrintf("     found : 0x%02X\n", i2c_addr);
    logPrintf("     size  : %dKB\n", eepromGetLength()/1024);
  }
  else
  {
    logPrintf("     empty\n");
  }

#if CLI_USE(HW_EEPROM)
  cliAdd("eeprom", cliEeprom);
#endif

  is_init = ret;

  return ret;
}

bool eepromIsInit(void)
{
  return is_init;
}

bool eepromValid(uint32_t addr)
{
  uint8_t data;
  bool ret;

  if (addr >= EEPROM_MAX_SIZE)
  {
    return false;
  }

  ret = i2cReadA16Bytes(i2c_ch, i2c_addr, addr, &data, 1, 100);

  return ret;
}

bool eepromReadByte(uint32_t addr, uint8_t *p_data)
{
  bool ret;

  if (addr >= EEPROM_MAX_SIZE)
  {
    return false;
  }

  ret = i2cReadA16Bytes(i2c_ch, i2c_addr, addr, p_data, 1, 100);

  return ret;
}

bool eepromWritePage(uint32_t addr, uint8_t const *p_data, uint32_t length)
{
  // V251112R5: 큐에서 전달된 연속 구간을 32바이트 페이지로 전송
  bool ret = true;
  uint32_t page_offset;

  if (length == 0)
  {
    return true;
  }
  if (addr >= EEPROM_MAX_SIZE || (addr + length) > EEPROM_MAX_SIZE)
  {
    return false;
  }

  page_offset = addr % EEPROM_PAGE_SIZE;
  if ((page_offset + length) > EEPROM_PAGE_SIZE)
  {
    return false;
  }

  for (uint32_t i = 0; i < length; i++)
  {
    page_write_buf[i] = p_data[i];
  }

  ret = i2cWriteA16Bytes(i2c_ch, i2c_addr, addr, page_write_buf, length, EEPROM_WRITE_I2C_TIMEOUT_MS);
  if (ret != true)
  {
    return false;
  }

  return eepromWaitReady(EEPROM_WRITE_READY_TIMEOUT_MS);
}

bool eepromWriteByte(uint32_t addr, uint8_t data_in)
{
  return eepromWritePage(addr, &data_in, 1);
}

bool eepromRead(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  bool ret = true;
  uint32_t i;


  for (i=0; i<length; i++)
  {
    ret = eepromReadByte(addr + i, &p_data[i]);
    if (ret != true)
    {
      break;
    }
  }

  return ret;
}

bool eepromWrite(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  bool ret = false;

  while (length > 0)
  {
    uint32_t page_space = EEPROM_PAGE_SIZE - (addr % EEPROM_PAGE_SIZE);
    uint32_t chunk      = length < page_space ? length : page_space;

    // V251112R5: 페이지 단위로 잘라내어 쓰기 지연 최소화
    ret = eepromWritePage(addr, p_data, chunk);
    if (ret == false)
    {
      break;
    }

    addr   += chunk;
    p_data += chunk;
    length -= chunk;
  }

  return ret;
}

uint32_t eepromGetLength(void)
{
  return EEPROM_MAX_SIZE;
}

bool eepromIsErasing(void)
{
  return false;                                                     // V251112R8: 외부 EEPROM은 클린업 개념 없음
}

bool eepromFormat(void)
{
  return true;
}

static bool eepromWaitReady(uint32_t timeout_ms)
{
  // V251112R5: FastMode Plus에서 완료 여부를 짧게 폴링
  uint32_t pre_time = millis();

  while (millis()-pre_time < timeout_ms)
  {
    if (i2cIsDeviceReady(i2c_ch, i2c_addr) == true)
    {
      return true;
    }
    delay(1);
  }

  return false;
}




#if CLI_USE(HW_EEPROM)
void cliEeprom(cli_args_t *args)
{
  bool ret = true;
  uint32_t i;
  uint32_t addr;
  uint32_t length;
  uint8_t  data;
  uint32_t pre_time;
  bool eep_ret;


  if (args->argc == 1)
  {
    if(args->isStr(0, "info") == true)
    {
      cliPrintf("eeprom init   : %s\n", eepromIsInit() ? "True":"False");
      cliPrintf("eeprom length : %d bytes\n", eepromGetLength());
#if defined(QMK_KEYMAP_CONFIG_H)
      cliPrintf("eeprom queue cur : %lu entries\n", (unsigned long)eeprom_get_write_pending_count());   // V251112R2: QMK 큐 계측
      cliPrintf("eeprom queue max : %lu entries\n", (unsigned long)eeprom_get_write_pending_max());     // V251112R2: 최고 사용량
      cliPrintf("eeprom queue ofl : %lu events\n", (unsigned long)eeprom_get_write_overflow_count());  // V251112R2: 직접 쓰기 횟수
#endif
      cliPrintf("emul cleanup busy : %d\n", eepromIsErasing());                                        // V251112R8: 외부 EEPROM에서도 계측 필드 제공
      cliPrintf("emul cleanup last : 0ms\n");                                                          // V251112R8: 클린업 미지원 보드 → 고정 0
      cliPrintf("emul cleanup wait : 0 entries\n");                                                    // V251112R8: 외부 EEPROM은 큐 대기로 전환 없음
      cliPrintf("emul cleanup cnt  : 0\n");                                                            // V251112R8: 클린업 횟수 개념 없음
    }
    else if(args->isStr(0, "format") == true)
    {
      if (eepromFormat() == true)
      {
        cliPrintf("format OK\n");
      }
      else
      {
        cliPrintf("format Fail\n");
      }
    }
    else
    {
      ret = false;
    }
  }
  else if (args->argc == 3)
  {
    if(args->isStr(0, "read") == true)
    {
      addr   = (uint32_t)args->getData(1);
      length = (uint32_t)args->getData(2);

      if (length > eepromGetLength())
      {
        cliPrintf( "length error\n");
      }
      for (i=0; i<length; i++)
      {
        if (eepromReadByte(addr+i, &data) == true)
        {
          cliPrintf( "addr : %d\t 0x%02X\n", addr+i, data);          
        }
        else
        {
          cliPrintf("eepromReadByte() Error\n");
          break;
        }
      }
    }
    else if(args->isStr(0, "write") == true)
    {
      addr = (uint32_t)args->getData(1);
      data = (uint8_t )args->getData(2);

      pre_time = millis();
      eep_ret = eepromWriteByte(addr, data);

      cliPrintf( "addr : %d\t 0x%02X %dms\n", addr, data, millis()-pre_time);
      if (eep_ret)
      {
        cliPrintf("OK\n");
      }
      else
      {
        cliPrintf("FAIL\n");
      }
    }
    else
    {
      ret = false;
    }
  }
  else
  {
    ret = false;
  }


  if (ret == false)
  {
    cliPrintf( "eeprom info\n");
    cliPrintf( "eeprom format\n");
    cliPrintf( "eeprom read  [addr] [length]\n");
    cliPrintf( "eeprom write [addr] [data]\n");
  }

}
#endif 


#endif 
