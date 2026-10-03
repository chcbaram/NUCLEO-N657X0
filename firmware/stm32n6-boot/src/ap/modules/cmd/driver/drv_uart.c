#include "driver/drv_uart.h"
#include "qbuffer.h"

#ifdef _USE_HW_CMD


/* UART(ST-LINK VCP) cmd 채널. CLI 와 같은 포트를 쓴다.

   USB CDC 처럼 호스트가 연 보율로 주인을 가를 수 없다 (UART 는 호스트 보율을 MCU 가 모른다).
   그래서 cli 의 RX 필터로 **cmd 패킷만 골라낸다** (nu54v-dk 와 같은 방식).
     02 FD 로 시작하면 패킷 길이만큼 이 채널의 큐로 가져오고, 나머지 바이트는 cli 로 간다.
     사람이 터미널에서 0x02 (Ctrl-B) 뒤에 0xFD 를 칠 일은 없다.

   보율 올리기 (BOOT_CMD_BAUD) : 응답을 다 보낸 뒤 바꾼다. 새 보율로 일정 시간 주고받는 것이 없으면
   기본 보율로 돌아온다. 호스트가 바꾸지 못했거나 죽었을 때 보드가 엉뚱한 보율에 갇히지 않게 한다. */
#define DRV_UART_BAUD_TIMEOUT 3000          // ms
#define DRV_UART_RX_BUF_SIZE  2048
#define DRV_UART_FRAME_TIMEOUT 100          // ms, cmd.c 의 바이트 간 타임아웃과 같다

#define CMD_STX0              0x02
#define CMD_STX1              0xFD
#define CMD_HEAD_SIZE         9             // STX0 STX1 type cmd(2) err(2) len(2)

enum
{
  FILTER_IDLE = 0,
  FILTER_STX1,
  FILTER_HEAD,
  FILTER_BODY,
};


typedef struct
{
  uint8_t  ch;
  uint32_t baud;                            // 기본 보율. BAUD 명령 뒤 시간이 지나면 여기로 돌아온다
} drv_uart_args_t;


static bool     drvUartRxFilter(uint8_t rx_data);
static bool     drvUartOpen(void *args);
static bool     drvUartClose(void *args);
static uint32_t drvUartAvailable(void *args);
static bool     drvUartFlush(void *args);
static uint8_t  drvUartRead(void *args);
static uint32_t drvUartWrite(void *args, uint8_t *p_data, uint32_t length);
static bool     drvUartSetBaud(void *args, uint32_t baud);
static uint32_t drvUartGetBaud(void *args);

static uint8_t   rx_buf[DRV_UART_RX_BUF_SIZE];
static qbuffer_t rx_q;

static uint8_t   f_state    = FILTER_IDLE;
static uint32_t  f_index    = 0;
static uint32_t  f_remain   = 0;
static uint16_t  f_length   = 0;
static uint32_t  f_pre_time = 0;

static uint32_t  act_time   = 0;            // 마지막으로 주고받은 시각 (보율 복귀 판정)






bool drvUartInit(cmd_driver_t *p_driver, uint8_t ch, uint32_t baud)
{
  drv_uart_args_t *p_args = (drv_uart_args_t *)p_driver->args;


  p_args->ch   = ch;
  p_args->baud = baud;

  p_driver->open      = drvUartOpen;
  p_driver->close     = drvUartClose;
  p_driver->available = drvUartAvailable;
  p_driver->flush     = drvUartFlush;
  p_driver->read      = drvUartRead;
  p_driver->write     = drvUartWrite;
  p_driver->set_baud  = drvUartSetBaud;
  p_driver->get_baud  = drvUartGetBaud;

  // RX 필터는 cli 에 하나뿐이라 UART cmd 채널도 하나다
  qbufferCreate(&rx_q, rx_buf, sizeof(rx_buf));
  cliSetRxFilter(drvUartRxFilter);
  return true;
}

void drvUartUpdate(cmd_driver_t *p_driver)
{
  drv_uart_args_t *p_args = (drv_uart_args_t *)p_driver->args;

  if (uartGetBaud(p_args->ch) != p_args->baud &&
      millis() - act_time >= DRV_UART_BAUD_TIMEOUT)
  {
    uartOpen(p_args->ch, p_args->baud);
    logPrintf("[  ] cmd uart baud -> %d (timeout)\n", (int)p_args->baud);
  }
}

bool drvUartRxFilter(uint8_t rx_data)
{
  bool ret = true;


  if (f_state != FILTER_IDLE && millis() - f_pre_time >= DRV_UART_FRAME_TIMEOUT)
  {
    f_state = FILTER_IDLE;
  }
  f_pre_time = millis();

  switch (f_state)
  {
    case FILTER_IDLE:
      if (rx_data != CMD_STX0)
      {
        return false;
      }
      f_state = FILTER_STX1;
      break;

    case FILTER_STX1:
      if (rx_data != CMD_STX1)
      {
        // 0x02 하나는 버린다 (Ctrl-B). 지금 바이트는 cli 로 보낸다
        f_state = FILTER_IDLE;
        return false;
      }
      qbufferWrite(&rx_q, (uint8_t[]){CMD_STX0}, 1);
      f_index = 2;
      f_state = FILTER_HEAD;
      break;

    case FILTER_HEAD:
      if (f_index == 7) f_length  = rx_data;
      if (f_index == 8) f_length |= (uint16_t)rx_data << 8;
      f_index++;
      if (f_index == CMD_HEAD_SIZE)
      {
        f_remain = (uint32_t)f_length + 1;   // data + checksum
        f_state  = FILTER_BODY;
      }
      break;

    case FILTER_BODY:
      if (--f_remain == 0)
      {
        f_state = FILTER_IDLE;
      }
      break;
  }

  if (f_state != FILTER_STX1)
  {
    qbufferWrite(&rx_q, &rx_data, 1);
  }
  act_time = millis();

  return ret;
}

bool drvUartOpen(void *args)
{
  (void)args;
  return true;
}

bool drvUartClose(void *args)
{
  (void)args;
  return true;
}

uint32_t drvUartAvailable(void *args)
{
  (void)args;
  return qbufferAvailable(&rx_q);
}

bool drvUartFlush(void *args)
{
  (void)args;
  qbufferFlush(&rx_q);
  return true;
}

uint8_t drvUartRead(void *args)
{
  uint8_t data = 0;

  (void)args;
  qbufferRead(&rx_q, &data, 1);
  return data;
}

uint32_t drvUartWrite(void *args, uint8_t *p_data, uint32_t length)
{
  drv_uart_args_t *p_args = (drv_uart_args_t *)args;

  act_time = millis();
  return uartWrite(p_args->ch, p_data, length);
}

bool drvUartSetBaud(void *args, uint32_t baud)
{
  drv_uart_args_t *p_args = (drv_uart_args_t *)args;

  act_time = millis();
  return uartOpen(p_args->ch, baud);
}

uint32_t drvUartGetBaud(void *args)
{
  drv_uart_args_t *p_args = (drv_uart_args_t *)args;

  return uartGetBaud(p_args->ch);
}

#endif
