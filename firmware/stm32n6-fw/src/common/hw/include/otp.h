#ifndef OTP_H_
#define OTP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_OTP


bool otpInit(void);
bool otpRead(uint32_t id, uint32_t *p_data);

#endif

#ifdef __cplusplus
}
#endif

#endif
