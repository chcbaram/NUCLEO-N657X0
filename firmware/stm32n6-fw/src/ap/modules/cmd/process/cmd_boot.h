#ifndef CMD_BOOT_H_
#define CMD_BOOT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


#ifdef _USE_HW_CMD

bool cmdBootProcess(cmd_t *p_cmd);

#endif

#ifdef __cplusplus
}
#endif

#endif
