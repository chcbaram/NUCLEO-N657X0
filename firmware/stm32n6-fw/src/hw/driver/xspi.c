#include "xspi.h"
#include "log.h"
#include "cli.h"


#ifdef _USE_HW_XSPI


/* 보드 부트 플래시 MX25UM51245G (64 MB Octal NOR, 1.8 V) — XSPI2, XSPIM Port 2, NCS1.
   HAL 위에 직접 쓴다. 다른 메모리로 바꿀 때는 명령 표(cmd_*)와 xspiFlash* 함수만 바꾼다.

   클럭 50 MHz : HSLV 퓨즈(OTP124 bit15)를 태우지 않아서 ST 코드의 "퓨즈 없음" 경로와 같게 둔다.
   메모리 읽기 dummy 는 플래시 기본값 20 (CR2 0x300 = 0, 200 MHz 까지 유효) 을 그대로 쓴다. */
#define XSPI_CLK_DIV          32                  // IC3 = PLL1 1600 MHz / 32 = 50 MHz
#define XSPI_PAGE_SIZE        256
#define XSPI_SECTOR_SIZE      4096
#define XSPI_BLOCK_SIZE       65536
#define XSPI_TIMEOUT          1000                // ms, 명령 하나
#define XSPI_ERASE_TIMEOUT    3000                // ms, 64 KB 블록
#define XSPI_CHIP_TIMEOUT     460000              // ms, 칩 전체 (데이터시트 최대)

#define DUMMY_READ_SPI        8                   // FAST READ 4B
#define DUMMY_READ_OPI        20                  // CR2 0x300 기본값
#define DUMMY_REG_OPI         4                   // RDSR / RDID / RDCR2 (OPI)

#define SR_WIP                0x01
#define SR_WEL                0x02
#define CR2_SOPI              0x01
#define CR2_DOPI              0x02


typedef enum
{
  XSPI_MODE_SPI = 0,                              // 1S-1S-1S
  XSPI_MODE_STR,                                  // 8S-8S-8S
  XSPI_MODE_DTR,                                  // 8D-8D-8D
} xspi_mode_t;

typedef struct
{
  uint8_t  spi;                                   // SPI : 1 바이트
  uint16_t opi;                                   // OPI : 명령 + 반전 (2 바이트)
} xspi_cmd_t;

static const xspi_cmd_t cmd_rdid  = {0x9F, 0x9F60};
static const xspi_cmd_t cmd_rdsr  = {0x05, 0x05FA};
static const xspi_cmd_t cmd_wren  = {0x06, 0x06F9};
static const xspi_cmd_t cmd_rsten = {0x66, 0x6699};
static const xspi_cmd_t cmd_rst   = {0x99, 0x9966};
static const xspi_cmd_t cmd_wrcr2 = {0x72, 0x728D};
static const xspi_cmd_t cmd_rdcr2 = {0x71, 0x718E};
static const xspi_cmd_t cmd_read  = {0x0C, 0xEC13};   // SPI : FAST READ 4B
static const xspi_cmd_t cmd_dtrd  = {0x0C, 0xEE11};   // OPI DTR 읽기
static const xspi_cmd_t cmd_pp    = {0x12, 0x12ED};
static const xspi_cmd_t cmd_se4k  = {0x21, 0x21DE};
static const xspi_cmd_t cmd_be64k = {0xDC, 0xDC23};
static const xspi_cmd_t cmd_ce    = {0x60, 0x609F};

static const char *mode_str[] = {"SPI", "OPI STR", "OPI DTR"};


static XSPI_HandleTypeDef hxspi;
static bool               is_init  = false;
static bool               is_xip   = false;
static xspi_mode_t        mode     = XSPI_MODE_SPI;
static uint8_t            flash_id[3];


static bool xspiInitHw(void);
static bool xspiHalInit(xspi_mode_t new_mode);
static void xspiCmdBuild(XSPI_RegularCmdTypeDef *p_cmd, xspi_mode_t m, const xspi_cmd_t *p_code,
                         bool has_addr, uint32_t addr, uint32_t dummy, uint32_t data_len, bool dqs);
static bool xspiCmd(const xspi_cmd_t *p_code, bool has_addr, uint32_t addr, uint32_t dummy,
                    uint32_t data_len, bool dqs);
static bool xspiFlashReset(void);
static bool xspiFlashReadId(uint8_t *p_id);
static bool xspiFlashReadReg(const xspi_cmd_t *p_code, bool has_addr, uint32_t addr, uint8_t *p_data);
static bool xspiFlashWriteEnable(void);
static bool xspiFlashWaitReady(uint32_t timeout);
static bool xspiFlashSetMode(xspi_mode_t new_mode);
static bool xspiFlashErase(const xspi_cmd_t *p_code, bool has_addr, uint32_t addr, uint32_t timeout);
static bool xspiReadRaw(uint32_t addr, uint8_t *p_data, uint32_t length);
static bool xspiWritePage(uint32_t addr, uint8_t *p_data, uint32_t length);
static bool xspiEnableMemoryMappedMode(void);   // 들어가는 길은 xspiSetXipMode(true) 하나로
#if CLI_USE(HW_XSPI)
static void cliCmd(cli_args_t *args);
#endif




bool xspiInit(void)
{
  bool ret = false;


  // 리셋으로 SPI 에서 ID 를 읽어 확인한 뒤 OPI DTR 로 올린다
  if (xspiInitHw() == true && xspiFlashReset() == true && xspiFlashReadId(flash_id) == true)
  {
    /*
     * JEDEC ID = [제조사][메모리타입][용량]
     *   0xC2 = Macronix, 0x80 = MX25UM (1.8 V Octal), 0x3A = 512 Mbit
     */
    if (flash_id[0] == 0xC2 && flash_id[2] == 0x3A)
    {
      ret = xspiFlashSetMode(XSPI_MODE_DTR);
    }

    logPrintf("[%s] xspiInit()\n", ret ? "OK" : "E_");
    logPrintf("     JEDEC ID : %02X %02X %02X\n", flash_id[0], flash_id[1], flash_id[2]);
    if (ret)
    {
      logPrintf("     Macronix : %d MB, %s %d MHz\n", (int)(HW_XSPI_SIZE / (1024*1024)), mode_str[mode],
                (int)(HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_XSPI2) / 1000000));
    }
    else
    {
      logPrintf("     [E_] unknown flash\n");
    }
  }
  else
  {
    logPrintf("[E_] xspiInit() - GetID fail\n");
  }

  is_init = ret;

#if CLI_USE(HW_XSPI)
  cliAdd("xspi", cliCmd);
#endif
  return ret;
}

bool xspiIsInit(void)
{
  return is_init;
}

/* 플래시를 리셋하고 지금 모드로 다시 들어간다 */
bool xspiReset(void)
{
  if (is_init != true)
  {
    return false;
  }
  xspiSetXipMode(false);
  return xspiFlashSetMode(mode);
}

bool xspiRead(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  bool dtr = (mode == XSPI_MODE_DTR);


  if (is_init != true || length == 0 || addr + length > xspiGetLength())
  {
    return false;
  }
  if (is_xip == true)
  {
    memcpy(p_data, (void *)(xspiGetAddr() + addr), length);
    return true;
  }

  /*
   * OPI DTR 은 2 바이트 단위다. HAL 이 홀수 주소·길이를 거부하므로
   * 앞뒤의 홀수 바이트는 짝수 2 바이트를 읽어 필요한 쪽만 꺼낸다.
   */
  if (dtr && (addr & 1))
  {
    uint8_t pair[2];

    if (xspiReadRaw(addr - 1, pair, 2) != true)
      return false;
    *p_data++ = pair[1];
    addr++;
    length--;
  }
  if (dtr && (length & 1))
  {
    uint8_t pair[2];

    if (xspiReadRaw(addr + length - 1, pair, 2) != true)
      return false;
    p_data[length - 1] = pair[0];
    length--;
  }
  if (length == 0)
  {
    return true;
  }
  return xspiReadRaw(addr, p_data, length);
}

bool xspiWrite(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  if (is_init != true || is_xip == true || addr + length > xspiGetLength())
  {
    return false;
  }

  /*
   * OPI DTR 은 2 바이트 단위다. 앞뒤의 홀수 바이트는 빈 쪽을 0xFF 로 채워 2 바이트로 쓴다.
   * NOR 에 0xFF 를 프로그램하면 그 바이트는 바뀌지 않는다.
   */
  if (mode == XSPI_MODE_DTR && length > 0 && (addr & 1))
  {
    uint8_t pair[2] = {0xFF, p_data[0]};

    if (xspiWritePage(addr - 1, pair, 2) != true)
      return false;
    p_data++;
    addr++;
    length--;
  }
  if (mode == XSPI_MODE_DTR && (length & 1))
  {
    uint8_t pair[2] = {p_data[length - 1], 0xFF};

    if (xspiWritePage(addr + length - 1, pair, 2) != true)
      return false;
    length--;
  }

  while (length > 0)
  {
    uint32_t n = XSPI_PAGE_SIZE - (addr % XSPI_PAGE_SIZE);    // 페이지 경계를 넘지 않게

    if (n > length)
    {
      n = length;
    }
    if (xspiWritePage(addr, p_data, n) != true)
    {
      return false;
    }
    addr   += n;
    p_data += n;
    length -= n;
  }
  return true;
}

/* addr ~ addr+length 를 덮는 4 KB 섹터를 지운다. 64 KB 로 맞으면 블록 단위로 지운다. */
bool xspiErase(uint32_t addr, uint32_t length)
{
  uint32_t cur;
  uint32_t end;


  if (is_init != true || is_xip == true || length == 0 || addr + length > xspiGetLength())
  {
    return false;
  }

  cur = addr - (addr % XSPI_SECTOR_SIZE);
  end = addr + length;
  while (cur < end)
  {
    if ((cur % XSPI_BLOCK_SIZE) == 0 && end - cur >= XSPI_BLOCK_SIZE)
    {
      if (xspiEraseBlock(cur) != true)
      {
        return false;
      }
      cur += XSPI_BLOCK_SIZE;
    }
    else
    {
      if (xspiEraseSector(cur) != true)
      {
        return false;
      }
      cur += XSPI_SECTOR_SIZE;
    }
  }
  return true;
}

bool xspiEraseBlock(uint32_t block_addr)
{
  return xspiFlashErase(&cmd_be64k, true, block_addr, XSPI_ERASE_TIMEOUT);
}

bool xspiEraseSector(uint32_t sector_addr)
{
  return xspiFlashErase(&cmd_se4k, true, sector_addr, XSPI_ERASE_TIMEOUT);
}

/* 칩 전체. 데이터시트 최대 460 초 */
bool xspiEraseChip(void)
{
  return xspiFlashErase(&cmd_ce, false, 0, XSPI_CHIP_TIMEOUT);
}

/* 쓰기/지우기가 끝나 준비된 상태면 true */
bool xspiGetStatus(void)
{
  uint8_t sr;


  if (is_init != true || is_xip == true)
  {
    return false;
  }
  return xspiFlashReadReg(&cmd_rdsr, false, 0, &sr) == true && (sr & SR_WIP) == 0;
}

bool xspiGetInfo(xspi_info_t* p_info)
{
  if (is_init != true)
  {
    return false;
  }

  memset(p_info, 0, sizeof(*p_info));
  p_info->FlashSize          = HW_XSPI_SIZE;
  p_info->EraseSectorSize    = XSPI_SECTOR_SIZE;
  p_info->EraseSectorsNumber = HW_XSPI_SIZE / XSPI_SECTOR_SIZE;
  p_info->ProgPageSize       = XSPI_PAGE_SIZE;
  p_info->ProgPagesNumber    = HW_XSPI_SIZE / XSPI_PAGE_SIZE;
  memcpy(p_info->device_id, flash_id, sizeof(flash_id));
  return true;
}

bool xspiGetXipMode(void)
{
  return is_xip;
}

bool xspiSetXipMode(bool enable)
{
  if (is_init != true)
  {
    return false;
  }

  if (enable)
  {
    if (is_xip == false)
    {
      /*
       * 들어가기 전에도 Abort 한다 (qspi.c 에서 실기로 겪은 것).
       * 컨트롤러 안의 프리페치 버퍼가 남아 있으면 지우기/쓰기 뒤 첫 읽기가 옛 내용으로 나온다.
       * 이 영역은 기본 메모리 맵에서 캐시되므로 D-캐시도 정리해 옛 줄을 버린다.
       */
      if (HAL_XSPI_Abort(&hxspi) != HAL_OK || xspiEnableMemoryMappedMode() != true)
      {
        return false;
      }
      SCB_CleanInvalidateDCache();
      is_xip = true;
    }
  }
  else
  {
    if (is_xip == true)
    {
      // memory-mapped 를 나가는 것은 Abort 로 한다
      if (HAL_XSPI_Abort(&hxspi) != HAL_OK)
      {
        return false;
      }
      is_xip = false;
    }
  }
  return true;
}

uint32_t xspiGetAddr(void)
{
  return HW_XSPI_ADDR;
}

uint32_t xspiGetLength(void)
{
  return HW_XSPI_SIZE;
}

static bool xspiInitHw(void)
{
  RCC_PeriphCLKInitTypeDef clk   = {0};
  GPIO_InitTypeDef         gpio  = {0};
  XSPIM_CfgTypeDef         xspim = {0};


  // VDDIO3 (XSPI2 핀 전원) 1.8 V
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWREx_EnableVddIO3();
  HAL_PWREx_ConfigVddIORange(PWR_VDDIO3, PWR_VDDIO_RANGE_1V8);

  // 커널 클럭 50 MHz. 안 잡으면 기본 소스로 너무 빠르게 돌 수 있다
  clk.PeriphClockSelection                = RCC_PERIPHCLK_XSPI2;
  clk.Xspi2ClockSelection                 = RCC_XSPI2CLKSOURCE_IC3;
  clk.ICSelection[RCC_IC3].ClockSelection = RCC_ICCLKSOURCE_PLL1;
  clk.ICSelection[RCC_IC3].ClockDivider   = XSPI_CLK_DIV;
  if (HAL_RCCEx_PeriphCLKConfig(&clk) != HAL_OK)
  {
    return false;
  }

  __HAL_RCC_XSPIM_CLK_ENABLE();
  __HAL_RCC_XSPI2_CLK_ENABLE();
  __HAL_RCC_XSPI2_FORCE_RESET();
  __HAL_RCC_XSPI2_RELEASE_RESET();
  __HAL_RCC_GPION_CLK_ENABLE();

  /**
    XSPI2 GPIO Configuration (XSPIM Port 2)

      PN0  DQS0    PN1  NCS1    PN6  CLK
      PN2  IO0     PN3  IO1     PN4  IO2     PN5  IO3
      PN8  IO4     PN9  IO5     PN10 IO6     PN11 IO7
  **/
  gpio.Pin       = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 |
                   GPIO_PIN_6 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
  gpio.Mode      = GPIO_MODE_AF_PP;
  gpio.Pull      = GPIO_NOPULL;
  gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF9_XSPIM_P2;
  HAL_GPIO_Init(GPION, &gpio);

  if (xspiHalInit(XSPI_MODE_SPI) != true)
  {
    return false;
  }

  // ST 예제와 같다 : XSPI2 -> Port 2, NCS1
  xspim.nCSOverride = HAL_XSPI_CSSEL_OVR_NCS1;
  xspim.IOPort      = HAL_XSPIM_IOPORT_2;
  xspim.Req2AckTime = 1;
  return HAL_XSPIM_Config(&hxspi, &xspim, XSPI_TIMEOUT) == HAL_OK;
}

/* 모드에 따라 컨트롤러 설정이 다르다 (DTR 은 Macronix 바이트 순서 + DHQC). */
static bool xspiHalInit(xspi_mode_t new_mode)
{
  bool dtr = (new_mode == XSPI_MODE_DTR);


  hxspi.Instance                     = XSPI2;
  hxspi.Init.FifoThresholdByte       = 4;
  hxspi.Init.MemoryMode              = HAL_XSPI_SINGLE_MEM;
  hxspi.Init.MemoryType              = dtr ? HAL_XSPI_MEMTYPE_MACRONIX : HAL_XSPI_MEMTYPE_MICRON;
  hxspi.Init.MemorySize              = HAL_XSPI_SIZE_512MB;
  hxspi.Init.ChipSelectHighTimeCycle = 2;
  hxspi.Init.FreeRunningClock        = HAL_XSPI_FREERUNCLK_DISABLE;
  hxspi.Init.ClockMode               = HAL_XSPI_CLOCK_MODE_0;
  hxspi.Init.WrapSize                = HAL_XSPI_WRAP_NOT_SUPPORTED;
  hxspi.Init.ClockPrescaler          = 0;
  hxspi.Init.SampleShifting          = HAL_XSPI_SAMPLE_SHIFT_NONE;
  hxspi.Init.DelayHoldQuarterCycle   = dtr ? HAL_XSPI_DHQC_ENABLE : HAL_XSPI_DHQC_DISABLE;
  hxspi.Init.ChipSelectBoundary      = 0;
  hxspi.Init.MaxTran                 = 0;
  hxspi.Init.Refresh                 = 0;

  if (HAL_XSPI_DeInit(&hxspi) != HAL_OK || HAL_XSPI_Init(&hxspi) != HAL_OK)
  {
    return false;
  }
  mode = new_mode;
  return true;
}

/* 모드 m 으로 명령 구조체를 채운다. data_len 이 0 이면 데이터 단계가 없다. */
static void xspiCmdBuild(XSPI_RegularCmdTypeDef *p_cmd, xspi_mode_t m, const xspi_cmd_t *p_code,
                         bool has_addr, uint32_t addr, uint32_t dummy, uint32_t data_len, bool dqs)
{
  bool opi = (m != XSPI_MODE_SPI);
  bool dtr = (m == XSPI_MODE_DTR);


  memset(p_cmd, 0, sizeof(*p_cmd));
  p_cmd->OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
  p_cmd->IOSelect           = HAL_XSPI_SELECT_IO_7_0;
  p_cmd->Instruction        = opi ? p_code->opi : p_code->spi;
  p_cmd->InstructionMode    = opi ? HAL_XSPI_INSTRUCTION_8_LINES : HAL_XSPI_INSTRUCTION_1_LINE;
  p_cmd->InstructionWidth   = opi ? HAL_XSPI_INSTRUCTION_16_BITS : HAL_XSPI_INSTRUCTION_8_BITS;
  p_cmd->InstructionDTRMode = dtr ? HAL_XSPI_INSTRUCTION_DTR_ENABLE : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  p_cmd->Address            = addr;
  p_cmd->AddressMode        = has_addr ? (opi ? HAL_XSPI_ADDRESS_8_LINES : HAL_XSPI_ADDRESS_1_LINE)
                                       : HAL_XSPI_ADDRESS_NONE;
  p_cmd->AddressWidth       = HAL_XSPI_ADDRESS_32_BITS;
  p_cmd->AddressDTRMode     = dtr ? HAL_XSPI_ADDRESS_DTR_ENABLE : HAL_XSPI_ADDRESS_DTR_DISABLE;
  p_cmd->AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  p_cmd->DataMode           = data_len == 0 ? HAL_XSPI_DATA_NONE
                                            : (opi ? HAL_XSPI_DATA_8_LINES : HAL_XSPI_DATA_1_LINE);
  p_cmd->DataLength         = data_len;
  p_cmd->DataDTRMode        = dtr ? HAL_XSPI_DATA_DTR_ENABLE : HAL_XSPI_DATA_DTR_DISABLE;
  p_cmd->DummyCycles        = dummy;
  p_cmd->DQSMode            = dqs ? HAL_XSPI_DQS_ENABLE : HAL_XSPI_DQS_DISABLE;
}

static bool xspiCmd(const xspi_cmd_t *p_code, bool has_addr, uint32_t addr, uint32_t dummy,
                    uint32_t data_len, bool dqs)
{
  XSPI_RegularCmdTypeDef cmd;


  xspiCmdBuild(&cmd, mode, p_code, has_addr, addr, dummy, data_len, dqs);
  return HAL_XSPI_Command(&hxspi, &cmd, XSPI_TIMEOUT) == HAL_OK;
}

/* 이전 실행이 플래시를 어떤 모드로 남겼는지 모르므로 DTR -> STR -> SPI 순으로 모두 리셋한다.
   끝나면 플래시와 컨트롤러 모두 SPI 모드다. */
static bool xspiFlashReset(void)
{
  for (int m = XSPI_MODE_DTR; m >= XSPI_MODE_SPI; m--)
  {
    if (xspiHalInit((xspi_mode_t)m) != true ||
        xspiCmd(&cmd_rsten, false, 0, 0, 0, false) != true ||
        xspiCmd(&cmd_rst,   false, 0, 0, 0, false) != true)
    {
      return false;
    }
  }
  HAL_Delay(40);                              // 리셋 복귀 시간 (지우는 중 리셋이면 길다)
  return true;
}

/* OPI DTR 에서는 바이트가 두 번씩 온다 (C2 C2 80 80 3A 3A). 짝수 번째만 모은다. */
static bool xspiFlashReadId(uint8_t *p_id)
{
  uint8_t buf[6];
  bool    opi = (mode != XSPI_MODE_SPI);
  bool    dtr = (mode == XSPI_MODE_DTR);


  if (xspiCmd(&cmd_rdid, opi, 0, opi ? DUMMY_REG_OPI : 0, dtr ? 6 : 3, dtr) != true ||
      HAL_XSPI_Receive(&hxspi, buf, XSPI_TIMEOUT) != HAL_OK)
  {
    return false;
  }
  for (int i=0; i<3; i++)
  {
    p_id[i] = dtr ? buf[i * 2] : buf[i];
  }
  return true;
}

/* 1 바이트 레지스터 읽기 (RDSR, RDCR2). OPI 는 주소가 늘 붙고, DTR 은 2 바이트를 받아 앞의 것을 쓴다. */
static bool xspiFlashReadReg(const xspi_cmd_t *p_code, bool has_addr, uint32_t addr, uint8_t *p_data)
{
  uint8_t buf[2];
  bool    opi = (mode != XSPI_MODE_SPI);
  bool    dtr = (mode == XSPI_MODE_DTR);


  if (xspiCmd(p_code, opi || has_addr, addr, opi ? DUMMY_REG_OPI : 0, dtr ? 2 : 1, dtr) != true ||
      HAL_XSPI_Receive(&hxspi, buf, XSPI_TIMEOUT) != HAL_OK)
  {
    return false;
  }
  *p_data = buf[0];
  return true;
}

static bool xspiFlashWriteEnable(void)
{
  uint8_t sr;


  return xspiCmd(&cmd_wren, false, 0, 0, 0, false) == true &&
         xspiFlashReadReg(&cmd_rdsr, false, 0, &sr) == true &&
         (sr & SR_WEL) != 0;
}

static bool xspiFlashWaitReady(uint32_t timeout)
{
  uint32_t pre = millis();
  uint8_t  sr;


  while (millis() - pre < timeout)
  {
    if (xspiFlashReadReg(&cmd_rdsr, false, 0, &sr) != true)
    {
      return false;
    }
    if ((sr & SR_WIP) == 0)
    {
      return true;
    }
  }
  return false;
}

/* 항상 SPI 에서 출발한다 (리셋으로 SPI 로 돌아온다). CR2 0x0000_0000 에 SOPI/DOPI 를 쓰고,
   컨트롤러도 그 모드로 바꾼 뒤 ID 로 확인한다. */
static bool xspiFlashSetMode(xspi_mode_t new_mode)
{
  uint8_t id[3];
  uint8_t cr2 = (new_mode == XSPI_MODE_DTR) ? CR2_DOPI : CR2_SOPI;


  if (xspiFlashReset() != true)
  {
    return false;
  }

  if (new_mode != XSPI_MODE_SPI)
  {
    if (xspiFlashWriteEnable() != true ||
        xspiCmd(&cmd_wrcr2, true, 0x00000000, 0, 1, false) != true ||
        HAL_XSPI_Transmit(&hxspi, &cr2, XSPI_TIMEOUT) != HAL_OK)
    {
      return false;
    }
    HAL_Delay(1);
    if (xspiHalInit(new_mode) != true)
    {
      return false;
    }
  }

  if (xspiFlashReadId(id) != true || id[0] != 0xC2)
  {
    return false;
  }
  memcpy(flash_id, id, 3);
  return true;
}

static bool xspiFlashErase(const xspi_cmd_t *p_code, bool has_addr, uint32_t addr, uint32_t timeout)
{
  if (is_init != true || is_xip == true)
  {
    return false;
  }
  return xspiFlashWriteEnable() == true &&
         xspiCmd(p_code, has_addr, addr, 0, 0, false) == true &&
         xspiFlashWaitReady(timeout) == true;
}

/* 지금 모드의 읽기/쓰기 명령으로 memory-mapped 설정을 하고 들어간다 */
bool xspiReadRaw(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  bool dtr = (mode == XSPI_MODE_DTR);

  if (xspiCmd(dtr ? &cmd_dtrd : &cmd_read, true, addr,
              mode == XSPI_MODE_SPI ? DUMMY_READ_SPI : DUMMY_READ_OPI, length, dtr) != true)
  {
    return false;
  }
  return HAL_XSPI_Receive(&hxspi, p_data, XSPI_TIMEOUT) == HAL_OK;
}

bool xspiWritePage(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  // 한 페이지 안에서만 부른다 (xspiWrite 가 나눈다)
  return xspiFlashWriteEnable() == true &&
         xspiCmd(&cmd_pp, true, addr, 0, length, false) == true &&
         HAL_XSPI_Transmit(&hxspi, p_data, XSPI_TIMEOUT) == HAL_OK &&
         xspiFlashWaitReady(XSPI_TIMEOUT) == true;
}

static bool xspiEnableMemoryMappedMode(void)
{
  XSPI_RegularCmdTypeDef   cmd = {0};
  XSPI_MemoryMappedTypeDef mm  = {0};
  bool                     dtr = (mode == XSPI_MODE_DTR);


  xspiCmdBuild(&cmd, mode, dtr ? &cmd_dtrd : &cmd_read, true, 0,
               mode == XSPI_MODE_SPI ? DUMMY_READ_SPI : DUMMY_READ_OPI, 1, dtr);
  cmd.OperationType = HAL_XSPI_OPTYPE_READ_CFG;
  if (HAL_XSPI_Command(&hxspi, &cmd, XSPI_TIMEOUT) != HAL_OK)
  {
    return false;
  }

  xspiCmdBuild(&cmd, mode, &cmd_pp, true, 0, 0, 1, false);
  cmd.OperationType = HAL_XSPI_OPTYPE_WRITE_CFG;
  if (HAL_XSPI_Command(&hxspi, &cmd, XSPI_TIMEOUT) != HAL_OK)
  {
    return false;
  }

  mm.TimeOutActivation = HAL_XSPI_TIMEOUT_COUNTER_DISABLE;
  return HAL_XSPI_MemoryMapped(&hxspi, &mm) == HAL_OK;
}


#if CLI_USE(HW_XSPI)
void cliCmd(cli_args_t *args)
{
  bool ret = false;
  uint32_t i;
  uint32_t addr;
  uint32_t length;
  uint8_t  data;
  uint32_t pre_time;
  bool flash_ret;



  if(args->argc == 1 && args->isStr(0, "info"))
  {
    uint8_t sr = 0, cr2 = 0, dc = 0;

    cliPrintf("xspi flash addr  : 0x%X\n", 0);
    cliPrintf("xspi xip   addr  : 0x%X\n", xspiGetAddr());
    cliPrintf("xspi xip   mode  : %s\n", xspiGetXipMode() ? "True":"False");
    cliPrintf("xspi jedec id    : %02X %02X %02X\n", flash_id[0], flash_id[1], flash_id[2]);
    cliPrintf("xspi mode        : %s %d MHz\n", mode_str[mode],
              (int)(HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_XSPI2) / 1000000));
    if (xspiGetXipMode() == false &&
        xspiFlashReadReg(&cmd_rdsr,  false, 0,          &sr)  == true &&
        xspiFlashReadReg(&cmd_rdcr2, true,  0x00000000, &cr2) == true &&
        xspiFlashReadReg(&cmd_rdcr2, true,  0x00000300, &dc)  == true)
    {
      cliPrintf("xspi reg         : SR 0x%02X, CR2 0x%02X, DC 0x%02X\n", sr, cr2, dc);
    }
    cliPrintf("xspi state       : 0x%X, err 0x%X\n", (int)HAL_XSPI_GetState(&hxspi), (int)HAL_XSPI_GetError(&hxspi));
    ret = true;
  }

  // 섹터 하나를 지우고, 패턴을 쓰고, 간접 읽기와 XiP 읽기로 검증한다 (기본 : 마지막 섹터)
  if((args->argc == 1 || args->argc == 2) && args->isStr(0, "test"))
  {
    static uint8_t wbuf[XSPI_SECTOR_SIZE];
    static uint8_t rbuf[XSPI_SECTOR_SIZE];
    uint32_t       exe_time;

    addr = (args->argc == 2) ? (uint32_t)args->getData(1) : xspiGetLength() - XSPI_SECTOR_SIZE;
    addr = addr & ~(XSPI_SECTOR_SIZE - 1);

    for (i=0; i<XSPI_SECTOR_SIZE; i++)
    {
      wbuf[i] = (uint8_t)(i * 7 + (addr >> 12));
    }

    cliPrintf("addr : 0x%X, %s\n", addr, mode_str[mode]);
    pre_time = millis();
    flash_ret = xspiErase(addr, XSPI_SECTOR_SIZE);
    cliPrintf("erase : %s %d ms\n", flash_ret ? "OK" : "FAIL", millis() - pre_time);

    pre_time = millis();
    flash_ret = flash_ret && xspiWrite(addr, wbuf, XSPI_SECTOR_SIZE);
    cliPrintf("write : %s %d ms\n", flash_ret ? "OK" : "FAIL", millis() - pre_time);

    memset(rbuf, 0, sizeof(rbuf));
    pre_time = micros();
    flash_ret = flash_ret && xspiRead(addr, rbuf, XSPI_SECTOR_SIZE);
    exe_time  = micros() - pre_time;
    flash_ret = flash_ret && memcmp(wbuf, rbuf, XSPI_SECTOR_SIZE) == 0;
    cliPrintf("read  : %s %d us\n", flash_ret ? "OK" : "FAIL", exe_time);

    if (flash_ret && xspiSetXipMode(true))
    {
      flash_ret = memcmp(wbuf, (void *)(xspiGetAddr() + addr), XSPI_SECTOR_SIZE) == 0;
      xspiSetXipMode(false);
      cliPrintf("xip   : %s\n", flash_ret ? "OK" : "FAIL");
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "xip"))
  {
    bool xip_enable;

    xip_enable = args->isStr(1, "on") ? true:false;

    if (xspiSetXipMode(xip_enable))
      cliPrintf("xspiSetXipMode() : OK\n");
    else
      cliPrintf("xspiSetXipMode() : Fail\n");

    cliPrintf("xspi xip mode  : %s\n", xspiGetXipMode() ? "True":"False");

    ret = true;
  }

  // 브링업용 : 인터페이스 모드 바꾸기
  if (args->argc == 2 && args->isStr(0, "mode"))
  {
    xspi_mode_t new_mode = args->isStr(1, "dtr") ? XSPI_MODE_DTR :
                           args->isStr(1, "str") ? XSPI_MODE_STR : XSPI_MODE_SPI;

    xspiSetXipMode(false);
    flash_ret = xspiFlashSetMode(new_mode);
    cliPrintf("xspi mode  : %s %s\n", mode_str[mode], flash_ret ? "OK" : "FAIL");
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "read"))
  {
    addr   = (uint32_t)args->getData(1);
    length = (uint32_t)args->getData(2);

    for (i=0; i<length; i++)
    {
      flash_ret = xspiRead(addr+i, &data, 1);

      if (flash_ret == true)
      {
        cliPrintf( "addr : 0x%X\t 0x%02X\n", addr+i, data);
      }
      else
      {
        cliPrintf( "addr : 0x%X\t Fail\n", addr+i);
      }
    }
    ret = true;
  }

  if(args->argc == 3 && args->isStr(0, "erase") == true)
  {
    addr   = (uint32_t)args->getData(1);
    length = (uint32_t)args->getData(2);

    pre_time = millis();
    flash_ret = xspiErase(addr, length);

    cliPrintf( "addr : 0x%X\t len : %d %d ms\n", addr, length, (millis()-pre_time));
    if (flash_ret)
    {
      cliPrintf("OK\n");
    }
    else
    {
      cliPrintf("FAIL\n");
    }
    ret = true;
  }

  if(args->argc == 3 && args->isStr(0, "write") == true)
  {
    uint32_t flash_data;

    addr = (uint32_t)args->getData(1);
    flash_data = (uint32_t )args->getData(2);

    pre_time = millis();
    flash_ret = xspiWrite(addr, (uint8_t *)&flash_data, 4);

    cliPrintf( "addr : 0x%X\t 0x%X %dms\n", addr, flash_data, millis()-pre_time);
    if (flash_ret)
    {
      cliPrintf("OK\n");
    }
    else
    {
      cliPrintf("FAIL\n");
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "speed-test") == true)
  {
    uint32_t buf[512/4];
    uint32_t cnt;
    uint32_t exe_time;
    uint32_t xip_addr;

    xip_addr = xspiGetAddr();
    cnt = 1024*1024 / 512;
    pre_time = millis();
    for (i=0; i<cnt; i++)
    {
      if (xspiGetXipMode())
      {
        memcpy(buf, (void *)(xip_addr + i*512), 512);
      }
      else
      {
        if (xspiRead(i*512, (uint8_t *)buf, 512) == false)
        {
          cliPrintf("xspiRead() Fail:%d\n", i);
          break;
        }
      }
    }
    exe_time = millis()-pre_time;
    if (exe_time > 0)
    {
      cliPrintf("%d KB/sec\n", 1024 * 1000 / exe_time);
    }
    ret = true;
  }


  if (ret == false)
  {
    cliPrintf("xspi info\n");
    cliPrintf("xspi xip on:off\n");
    cliPrintf("xspi test [addr]\n");
    cliPrintf("xspi speed-test\n");
    cliPrintf("xspi mode spi:str:dtr\n");
    cliPrintf("xspi read  [addr] [length]\n");
    cliPrintf("xspi erase [addr] [length]\n");
    cliPrintf("xspi write [addr] [data]\n");
  }
}
#endif


#endif
