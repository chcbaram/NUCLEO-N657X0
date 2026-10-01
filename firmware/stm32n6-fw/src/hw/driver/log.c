#include "log.h"


#ifdef _USE_HW_LOG
#include "uart.h"
#include "cli.h"
#ifdef _USE_HW_RTC
#include "rtc.h"
#endif

#ifdef _USE_HW_RTOS
#define lock()      xSemaphoreTake(mutex_lock, portMAX_DELAY);
#define unLock()    xSemaphoreGive(mutex_lock);
#else
#define lock()
#define unLock()
#endif


typedef struct
{
  uint16_t line_index;
  uint16_t buf_length;
  uint16_t buf_length_max;
  uint16_t buf_index;
  uint8_t *buf;
} log_buf_t;


log_buf_t log_buf_boot;
log_buf_t log_buf_list;

static uint8_t buf_boot[LOG_BOOT_BUF_MAX];
static uint8_t buf_list[LOG_LIST_BUF_MAX];

static bool is_init = false;
static bool is_boot_log = true;
static bool is_enable = true;
static bool is_open = false;

static uint8_t  log_ch = LOG_CH;
static uint32_t log_baud = 115200;

static char print_buf[256];

#ifdef _USE_HW_RTOS
static SemaphoreHandle_t mutex_lock;
#endif



#if CLI_USE(HW_LOG)
static void cliCmd(cli_args_t *args);
#endif





bool logInit(void)
{
#ifdef _USE_HW_RTOS
  mutex_lock = xSemaphoreCreateMutex();
#endif

  log_buf_boot.line_index     = 0;
  log_buf_boot.buf_length     = 0;
  log_buf_boot.buf_length_max = LOG_BOOT_BUF_MAX;
  log_buf_boot.buf_index      = 0;
  log_buf_boot.buf            = buf_boot;


  log_buf_list.line_index     = 0;
  log_buf_list.buf_length     = 0;
  log_buf_list.buf_length_max = LOG_LIST_BUF_MAX;
  log_buf_list.buf_index      = 0;
  log_buf_list.buf            = buf_list;


  is_init = true;

#if CLI_USE(HW_LOG)
  cliAdd("log", cliCmd);
#endif

  return true;
}

void logEnable(void)
{
  is_enable = true;
}

void logDisable(void)
{
  is_enable = false;
}

void logBoot(uint8_t enable)
{
  is_boot_log = enable;
}

bool logOpen(uint8_t ch, uint32_t baud)
{
  log_ch   = ch;
  log_baud = baud;
  is_open  = true;

  is_open = uartOpen(ch, baud);

  return is_open;
}

bool logIsOpen(void)
{
  return is_open;
}

// 링 버퍼 : buf_index = 다음에 쓸 위치, buf_length = 남아 있는 바이트 수 (최대 buf_length_max).
// 가득 차면 가장 오래된 것부터 덮어쓴다. 출력은 logBufDump() 가 오래된 것 → 최신 순으로 한다.
//
// 줄 머리. 로그를 버퍼에 넣는 순간에 만든다.
// rtc 시각이 맞춰져 있으면 그 시각을, 아니면 부팅 후 경과 시간을 넣는다.
//
//   [12:34:56]   시각 설정됨
//   [   12.345]  부팅 후 초.밀리초
//
static int logBufHeader(log_buf_t *p_log, char *p_buf, uint32_t size)
{
#ifdef _USE_HW_RTC
  rtc_time_t time;

  if (rtcGetTime(&time) == true)
  {
    return snprintf(p_buf, size, "[%02d:%02d:%02d]\t",
                    time.hours, time.minutes, time.seconds);
  }
#endif

  {
    uint32_t ms = millis();

    return snprintf(p_buf, size, "[%5u.%03u]\t",
                    (unsigned)(ms / 1000), (unsigned)(ms % 1000));
  }
}

bool logBufPrintf(log_buf_t *p_log, char *p_data, uint32_t length)
{
  char     line[sizeof(print_buf) + 32];
  int      line_len;


  line_len = logBufHeader(p_log, line, sizeof(line));
  if (line_len < 0) line_len = 0;
  line_len += snprintf(&line[line_len], sizeof(line) - line_len, "%.*s", (int)length, p_data);
  if (line_len <= 0)
  {
    return false;
  }
  if (line_len >= (int)sizeof(line))
  {
    line_len = sizeof(line) - 1;
  }
  p_log->line_index++;

  for (int i=0; i<line_len; i++)
  {
    p_log->buf[p_log->buf_index] = line[i];
    p_log->buf_index = (p_log->buf_index + 1) % p_log->buf_length_max;
  }

  if (p_log->buf_length + line_len < p_log->buf_length_max)
    p_log->buf_length += line_len;
  else
    p_log->buf_length = p_log->buf_length_max;

  return true;
}

void logPrintf(const char *fmt, ...)
{

  va_list args;
  int len;

  if (is_init != true)
    return;

  lock();

  va_start(args, fmt);
  len = vsnprintf(print_buf, sizeof(print_buf), fmt, args);
  if (len < 0) len = 0;
  if (len >= (int)sizeof(print_buf)) len = sizeof(print_buf) - 1;

  if (is_open == true && is_enable == true)
  {
    uartWrite(log_ch, (uint8_t *)print_buf, len);
  }

  if (is_boot_log)
  {
    logBufPrintf(&log_buf_boot, print_buf, len);
  }
  logBufPrintf(&log_buf_list, print_buf, len);

  va_end(args);

  unLock();
}

/* printf / puts 출력을 로그 채널로 보낸다.
   syscalls.c 의 _write 는 weak 이고, 그대로 두면 구현 없는 __io_putchar(0 번지)를 불러 HardFault 가 난다. */
int _write(int file, char *ptr, int len)
{
  (void)file;

  if (is_open == true && is_enable == true)
  {
    uartWrite(log_ch, (uint8_t *)ptr, len);
  }

  return len;
}


#if CLI_USE(HW_LOG)
// 오래된 것 → 최신 순으로 출력한다. 한 바퀴 돌아 덮어쓴 경우 앞부분의 잘린 줄은 건너뛴다.
// 출력하는 동안 lock 을 잡아 둔다 (그 사이 다른 스레드의 logPrintf 는 기다린다).
//
static void logBufDump(log_buf_t *p_log)
{
  uint32_t start;
  uint32_t length;


  lock();

  length = p_log->buf_length;
  start  = (p_log->buf_index + p_log->buf_length_max - length) % p_log->buf_length_max;

  if (length == p_log->buf_length_max)
  {
    while (length > 0 && p_log->buf[start] != '\n')
    {
      start = (start + 1) % p_log->buf_length_max;
      length--;
    }
    if (length > 0)
    {
      start = (start + 1) % p_log->buf_length_max;
      length--;
    }
  }

  while (length > 0 && cliKeepLoop())
  {
    uint32_t chunk = p_log->buf_length_max - start;

    if (chunk > length) chunk = length;
    if (chunk > 64)     chunk = 64;

    cliWrite(&p_log->buf[start], chunk);
    start   = (start + chunk) % p_log->buf_length_max;
    length -= chunk;
  }

  unLock();
}

void cliCmd(cli_args_t *args)
{
  bool ret = false;



  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("boot.line_index %d\n", log_buf_boot.line_index);
    cliPrintf("boot.buf_length %d\n", log_buf_boot.buf_length);
    cliPrintf("\n");
    cliPrintf("list.line_index %d\n", log_buf_list.line_index);
    cliPrintf("list.buf_length %d\n", log_buf_list.buf_length);

    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "boot"))
  {
    logBufDump(&log_buf_boot);
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "list"))
  {
    logBufDump(&log_buf_list);
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("log info\n");
    cliPrintf("log boot\n");
    cliPrintf("log list\n");
  }
}
#endif


#endif
