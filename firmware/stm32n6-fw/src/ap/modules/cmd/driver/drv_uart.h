#ifndef DRV_UART_H_
#define DRV_UART_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


#ifdef _USE_HW_CMD

bool     drvUartInit(cmd_driver_t *p_driver, uint8_t ch, uint32_t baud);
bool     drvUartSetBaud(cmd_driver_t *p_driver, uint32_t baud);
uint32_t drvUartGetBaud(cmd_driver_t *p_driver);
void     drvUartUpdate(cmd_driver_t *p_driver);

#endif

#ifdef __cplusplus
}
#endif

#endif
