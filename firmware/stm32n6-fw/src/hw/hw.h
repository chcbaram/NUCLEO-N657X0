#ifndef HW_H_
#define HW_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#include "led.h"
#include "uart.h"
#include "log.h"
#include "cli.h"
#include "rtc.h"
#include "reset.h"
#include "cmd.h"


bool hwInit(void);


#ifdef __cplusplus
}
#endif

#endif
