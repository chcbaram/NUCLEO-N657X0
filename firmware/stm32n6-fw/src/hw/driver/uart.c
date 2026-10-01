#include "uart.h"
#include "qbuffer.h"
#include "cli.h"

#ifdef _USE_HW_UART


#define UART_RX_BUF_LENGTH        1024



typedef struct
{
  bool is_open;
  bool is_hw_init;
  uint32_t baud;

  qbuffer_t qbuffer;
  UART_HandleTypeDef *p_huart;

  uint32_t rx_cnt;
  uint32_t tx_cnt;

  uart_driver_t *p_driver;   /* NULL 이면 HW UART, 아니면 커스텀 드라이버(cli_net 등) */
} uart_tbl_t;

typedef struct
{
  const char         *p_msg;
  USART_TypeDef      *p_uart;
  UART_HandleTypeDef *p_huart;
  DMA_HandleTypeDef  *p_hdma_rx;
} uart_hw_t;


bool uartInitHw(uint8_t ch);
static uint32_t uartGetRxIndex(uint8_t ch);
#if CLI_USE(HW_UART)
static void cliUart(cli_args_t *args);
#endif


static bool is_init = false;

static uart_tbl_t uart_tbl[UART_MAX_CH];

/* DMA 와 공유하는 메모리는 non-cacheable 구역에 둔다 (bspMpuInit 참고)
     rx_buf : DMA 가 쓰고 CPU 가 읽는다
     node   : CPU(HAL) 가 쓰고 DMA 가 읽는다. 한 바퀴마다 DMA 가 다시 읽는다 */
static uint8_t          uart_rx_buf[UART_MAX_CH][UART_RX_BUF_LENGTH] __NON_CACHEABLE;
static DMA_NodeTypeDef  dma_node_usart1_rx __NON_CACHEABLE;

static DMA_QListTypeDef   dma_queue_usart1_rx;
static DMA_HandleTypeDef  hdma_usart1_rx;
static UART_HandleTypeDef huart1;

const static uart_hw_t uart_hw_tbl[UART_MAX_CH] =
{
  {"USART1 SWD   ", USART1, &huart1, &hdma_usart1_rx},   // ST-LINK VCP
};


bool uartInit(void)
{
  for (int i=0; i<UART_MAX_CH; i++)
  {
    uart_tbl[i].is_open    = false;
    uart_tbl[i].is_hw_init = false;
    uart_tbl[i].baud       = 57600;
    uart_tbl[i].rx_cnt     = 0;
    uart_tbl[i].tx_cnt     = 0;
    uart_tbl[i].p_driver   = NULL;
  }

  is_init = true;

#if CLI_USE(HW_UART)
  cliAdd("uart", cliUart);
#endif
  return true;
}

bool uartDeInit(void)
{
  return true;
}

bool uartIsInit(void)
{
  return is_init;
}

bool uartOpen(uint8_t ch, uint32_t baud)
{
  bool ret = false;
  UART_HandleTypeDef *p_huart;


  if (ch >= UART_MAX_CH) return false;

  if (uart_tbl[ch].p_driver != NULL)
  {
    uart_tbl[ch].baud    = baud;
    uart_tbl[ch].is_open = uart_tbl[ch].p_driver->open(baud);
    return uart_tbl[ch].is_open;
  }

  if (uart_tbl[ch].is_open == true && uart_tbl[ch].baud == baud)
  {
    return true;
  }

  switch(ch)
  {
    case _DEF_UART1:
      p_huart = uart_hw_tbl[ch].p_huart;

      uart_tbl[ch].baud    = baud;
      uart_tbl[ch].p_huart = p_huart;

      qbufferCreate(&uart_tbl[ch].qbuffer, &uart_rx_buf[ch][0], UART_RX_BUF_LENGTH);


      // 보레이트를 바꿔 다시 여는 경우. 수신 DMA 를 멈추고 UART 만 다시 설정한다.
      // DMA 채널/노드 설정은 보레이트와 무관해서 다시 하지 않는다.
      //
      if (uart_tbl[ch].is_open)
      {
        uart_tbl[ch].is_open = false;
        HAL_UART_AbortReceive(p_huart);
        HAL_UART_DeInit(p_huart);
      }

      // UART_SetConfig 가 커널 클럭으로 BRR 을 계산하므로 HAL_UART_Init 보다 먼저 한다.
      //
      if (uart_tbl[ch].is_hw_init == false)
      {
        if (uartInitHw(ch) != true)
        {
          break;
        }
        uart_tbl[ch].is_hw_init = true;
      }

      p_huart->Instance                    = uart_hw_tbl[ch].p_uart;
      p_huart->Init.BaudRate               = baud;
      p_huart->Init.WordLength             = UART_WORDLENGTH_8B;
      p_huart->Init.StopBits               = UART_STOPBITS_1;
      p_huart->Init.Parity                 = UART_PARITY_NONE;
      p_huart->Init.Mode                   = UART_MODE_TX_RX;
      p_huart->Init.HwFlowCtl              = UART_HWCONTROL_NONE;
      p_huart->Init.OverSampling           = UART_OVERSAMPLING_16;
      p_huart->Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
      p_huart->Init.ClockPrescaler         = UART_PRESCALER_DIV1;
      p_huart->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

      if (HAL_UART_Init(p_huart) != HAL_OK)
      {
        break;
      }

      __HAL_LINKDMA(p_huart, hdmarx, *uart_hw_tbl[ch].p_hdma_rx);

      // 수신 위치는 DMA 카운터로 구하므로 NVIC 인터럽트는 켜지 않는다.
      //
      if (HAL_UART_Receive_DMA(p_huart, &uart_rx_buf[ch][0], UART_RX_BUF_LENGTH) != HAL_OK)
      {
        break;
      }

      uart_tbl[ch].qbuffer.in  = uartGetRxIndex(ch);
      uart_tbl[ch].qbuffer.out = uart_tbl[ch].qbuffer.in;
      uart_tbl[ch].is_open     = true;
      ret = true;
      break;
  }

  return ret;
}

bool uartClose(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  if (uart_tbl[ch].p_driver != NULL)
  {
    uart_tbl[ch].p_driver->close();
  }

  uart_tbl[ch].is_open = false;

  return true;
}

bool uartSetDriver(uint8_t ch, uart_driver_t *p_driver)
{
  if (ch >= UART_MAX_CH) return false;

  uart_tbl[ch].p_driver = p_driver;
  return true;
}

bool uartInitHw(uint8_t ch)
{
  if (ch == _DEF_UART1)
  {
    RCC_PeriphCLKInitTypeDef clk_cfg  = {0};
    GPIO_InitTypeDef         gpio_cfg = {0};
    DMA_NodeConfTypeDef      node_cfg = {0};
    DMA_HandleTypeDef       *p_hdma   = uart_hw_tbl[ch].p_hdma_rx;


    // 커널 클럭 : PCLK2 (200 MHz, bspClockInit 참고)
    //
    clk_cfg.PeriphClockSelection = RCC_PERIPHCLK_USART1;
    clk_cfg.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
    if (HAL_RCCEx_PeriphCLKConfig(&clk_cfg) != HAL_OK)
    {
      return false;
    }

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPDMA1_CLK_ENABLE();

    /**
      USART1 GPIO Configuration

      [GPIO Pin] ------> [Signal Name]

        PE5     ------>   USART1_TX
        PE6     ------>   USART1_RX
      **/
    gpio_cfg.Pin       = GPIO_PIN_5 | GPIO_PIN_6;
    gpio_cfg.Mode      = GPIO_MODE_AF_PP;
    gpio_cfg.Pull      = GPIO_NOPULL;
    gpio_cfg.Speed     = GPIO_SPEED_FREQ_LOW;
    gpio_cfg.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOE, &gpio_cfg);

    // FSBL 은 secure 로 돈다. 핀도 secure 로 둬서 secure 인 USART1 과 이어준다.
    //
    HAL_GPIO_ConfigPinAttributes(GPIOE, GPIO_PIN_5 | GPIO_PIN_6, GPIO_PIN_SEC | GPIO_PIN_NPRIV);


    /* RX DMA : GPDMA1 CH0, linked-list circular

       노드 하나가 자기 자신을 가리키는 원형 큐다. 한 바퀴(UART_RX_BUF_LENGTH)가 끝나면
       DMA 가 노드를 다시 읽어 처음부터 이어 받는다. */
    p_hdma->Instance                         = GPDMA1_Channel0;
    p_hdma->InitLinkedList.Priority          = DMA_LOW_PRIORITY_LOW_WEIGHT;
    p_hdma->InitLinkedList.LinkStepMode      = DMA_LSM_FULL_EXECUTION;
    p_hdma->InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT0;
    p_hdma->InitLinkedList.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    p_hdma->InitLinkedList.LinkedListMode    = DMA_LINKEDLIST_CIRCULAR;
    if (HAL_DMAEx_List_Init(p_hdma) != HAL_OK)
    {
      return false;
    }

    node_cfg.NodeType                         = DMA_GPDMA_LINEAR_NODE;
    node_cfg.Init.Request                     = GPDMA1_REQUEST_USART1_RX;
    node_cfg.Init.BlkHWRequest                = DMA_BREQ_SINGLE_BURST;
    node_cfg.Init.Direction                   = DMA_PERIPH_TO_MEMORY;
    node_cfg.Init.SrcInc                      = DMA_SINC_FIXED;
    node_cfg.Init.DestInc                     = DMA_DINC_INCREMENTED;
    node_cfg.Init.SrcDataWidth                = DMA_SRC_DATAWIDTH_BYTE;
    node_cfg.Init.DestDataWidth               = DMA_DEST_DATAWIDTH_BYTE;
    node_cfg.Init.SrcBurstLength              = 1;
    node_cfg.Init.DestBurstLength             = 1;
    node_cfg.Init.TransferAllocatedPort       = DMA_SRC_ALLOCATED_PORT1 | DMA_DEST_ALLOCATED_PORT0;
    node_cfg.Init.TransferEventMode           = DMA_TCEM_BLOCK_TRANSFER;
    node_cfg.Init.Mode                        = DMA_NORMAL;
    node_cfg.TriggerConfig.TriggerPolarity    = DMA_TRIG_POLARITY_MASKED;
    node_cfg.DataHandlingConfig.DataExchange  = DMA_EXCHANGE_NONE;
    node_cfg.DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
    node_cfg.SrcSecure                        = DMA_CHANNEL_SRC_SEC;
    node_cfg.DestSecure                       = DMA_CHANNEL_DEST_SEC;
    if (HAL_DMAEx_List_BuildNode(&node_cfg, &dma_node_usart1_rx) != HAL_OK)
    {
      return false;
    }
    if (HAL_DMAEx_List_InsertNode_Tail(&dma_queue_usart1_rx, &dma_node_usart1_rx) != HAL_OK)
    {
      return false;
    }
    if (HAL_DMAEx_List_SetCircularMode(&dma_queue_usart1_rx) != HAL_OK)
    {
      return false;
    }
    if (HAL_DMAEx_List_LinkQ(p_hdma, &dma_queue_usart1_rx) != HAL_OK)
    {
      return false;
    }

    // 버퍼(0x3418_xxxx)와 USART1 이 모두 secure 주소라 채널도 secure 로 둔다.
    // non-secure 채널은 secure 주소에 접근할 수 없다.
    //
    if (HAL_DMA_ConfigChannelAttributes(p_hdma, DMA_CHANNEL_SEC | DMA_CHANNEL_PRIV |
                                                DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC) != HAL_OK)
    {
      return false;
    }

    return true;
  }

  return false;
}

/* DMA 가 다음에 쓸 위치.
   남은 카운트(BNDT)로 구한다. 한 바퀴 끝에서 노드를 다시 읽기 직전에는 BNDT 가 0 이라
   len 이 나오므로 len 으로 나눈 나머지를 쓴다. */
static uint32_t uartGetRxIndex(uint8_t ch)
{
  uint32_t len = uart_tbl[ch].qbuffer.len;

  return (len - __HAL_DMA_GET_COUNTER(uart_tbl[ch].p_huart->hdmarx)) % len;
}

uint32_t uartAvailable(uint8_t ch)
{
  uint32_t ret = 0;

  if (ch >= UART_MAX_CH) return 0;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->available();
  }

  switch(ch)
  {
    case _DEF_UART1:
      if (uart_tbl[ch].is_open)
      {
        uart_tbl[ch].qbuffer.in = uartGetRxIndex(ch);
        ret = qbufferAvailable(&uart_tbl[ch].qbuffer);
      }
      break;
  }

  return ret;
}

bool uartFlush(uint8_t ch)
{
  uint32_t pre_time;

  if (ch >= UART_MAX_CH) return false;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->flush();
  }

  pre_time = millis();
  while(uartAvailable(ch))
  {
    if (millis()-pre_time >= 10)
    {
      break;
    }
    uartRead(ch);
  }

  return true;
}

uint8_t uartRead(uint8_t ch)
{
  uint8_t ret = 0;

  if (ch >= UART_MAX_CH) return 0;

  if (uart_tbl[ch].p_driver != NULL)
  {
    ret = uart_tbl[ch].p_driver->read();
    uart_tbl[ch].rx_cnt++;
    return ret;
  }

  switch(ch)
  {
    case _DEF_UART1:
      qbufferRead(&uart_tbl[ch].qbuffer, &ret, 1);
      break;
  }
  uart_tbl[ch].rx_cnt++;

  return ret;
}

uint32_t uartWrite(uint8_t ch, uint8_t *p_data, uint32_t length)
{
  uint32_t ret = 0;

  if (ch >= UART_MAX_CH) return 0;

  if (uart_tbl[ch].p_driver != NULL)
  {
    ret = uart_tbl[ch].p_driver->write(p_data, length);
    uart_tbl[ch].tx_cnt += ret;
    return ret;
  }

  switch(ch)
  {
    case _DEF_UART1:
      if (uart_tbl[ch].is_open != true)
      {
        break;
      }
      if (HAL_UART_Transmit(uart_tbl[ch].p_huart, p_data, length, 100) == HAL_OK)
      {
        ret = length;
      }
      break;
  }
  uart_tbl[ch].tx_cnt += ret;

  return ret;
}

uint32_t uartPrintf(uint8_t ch, const char *fmt, ...)
{
  char buf[256];
  va_list args;
  int len;
  uint32_t ret;

  va_start(args, fmt);
  len = vsnprintf(buf, 256, fmt, args);

  ret = uartWrite(ch, (uint8_t *)buf, len);

  va_end(args);


  return ret;
}

uint32_t uartGetBaud(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  return uart_tbl[ch].baud;
}

uint32_t uartGetRxCnt(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  return uart_tbl[ch].rx_cnt;
}

uint32_t uartGetTxCnt(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  return uart_tbl[ch].tx_cnt;
}

#if CLI_USE(HW_UART)
void cliUart(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    for (int i=0; i<UART_MAX_CH; i++)
    {
      cliPrintf("_DEF_UART%d : %s, %d bps\n", i+1, uart_hw_tbl[i].p_msg, uartGetBaud(i));
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "test"))
  {
    uint8_t uart_ch;

    uart_ch = constrain(args->getData(1), 1, UART_MAX_CH) - 1;

    if (uart_ch != cliGetPort())
    {
      uint8_t rx_data;

      while(1)
      {
        if (uartAvailable(uart_ch) > 0)
        {
          rx_data = uartRead(uart_ch);
          cliPrintf("<- _DEF_UART%d RX : 0x%X\n", uart_ch + 1, rx_data);
        }

        if (cliAvailable() > 0)
        {
          rx_data = cliRead();
          if (rx_data == 'q')
          {
            break;
          }
          else
          {
            uartWrite(uart_ch, &rx_data, 1);
            cliPrintf("-> _DEF_UART%d TX : 0x%X\n", uart_ch + 1, rx_data);
          }
        }
      }
    }
    else
    {
      cliPrintf("This is cliPort\n");
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("uart info\n");
    cliPrintf("uart test ch[1~%d]\n", HW_UART_MAX_CH);
  }
}
#endif


#endif
