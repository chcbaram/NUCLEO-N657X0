#ifndef BOOTROM_H_
#define BOOTROM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_BOOTROM


bool bootromInit(void);
void bootromPrintTrace(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
