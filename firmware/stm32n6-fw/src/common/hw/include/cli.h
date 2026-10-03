#ifndef CLI_H_
#define CLI_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#define CLI_USE(module)       ((_USE_CLI_ ## module) && defined(_USE_HW_CLI))

#ifdef _USE_HW_CLI

#define CLI_CMD_LIST_MAX      HW_CLI_CMD_LIST_MAX
#define CLI_CMD_NAME_MAX      HW_CLI_CMD_NAME_MAX

#define CLI_LINE_HIS_MAX      HW_CLI_LINE_HIS_MAX
#define CLI_LINE_BUF_MAX      HW_CLI_LINE_BUF_MAX




typedef struct
{
  uint16_t   argc;
  char     **argv;

  int32_t  (*getData)(uint8_t index);
  float    (*getFloat)(uint8_t index);
  char    *(*getStr)(uint8_t index);
  bool     (*isStr)(uint8_t index, const char *p_str);
} cli_args_t;


bool cliInit(void);
bool cliOpen(uint8_t ch, uint32_t baud);
bool cliIsBusy(void);
bool cliOpenLog(uint8_t ch, uint32_t baud);
bool cliMain(void);

// 수신 바이트를 cli 보다 먼저 보는 필터. true 를 돌려주면 그 바이트는 cli 로 가지 않는다.
//   같은 UART 에 다른 프로토콜(cmd 패킷)을 얹을 때 쓴다 (nu54v-dk 와 같은 방식).
typedef bool (*cli_rx_filter_t)(uint8_t rx_data);
bool cliSetRxFilter(cli_rx_filter_t filter);
void cliPrintf(const char *fmt, ...);
bool cliAdd(const char *cmd_str, void (*p_func)(cli_args_t *));
bool cliKeepLoop(void);
void cliPutch(uint8_t data);
uint8_t  cliGetPort(void);
uint32_t cliAvailable(void);
uint8_t  cliRead(void);
uint32_t cliWrite(uint8_t *p_data, uint32_t length);
bool cliRunStr(const char *fmt, ...);
void cliShowCursor(bool visibility);
void cliMoveUp(uint8_t y);
void cliMoveDown(uint8_t y);
void cliBegin(void);

#endif

#ifdef __cplusplus
}
#endif



#endif