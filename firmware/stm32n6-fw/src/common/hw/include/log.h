#ifndef LOG_H_
#define LOG_H_


#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_LOG

#define LOG_CH            HW_LOG_CH
#define LOG_BOOT_BUF_MAX  HW_LOG_BOOT_BUF_MAX
#define LOG_LIST_BUF_MAX  HW_LOG_LIST_BUF_MAX


bool logInit(void);
void logEnable(void);
void logDisable(void);
bool logOpen(uint8_t ch, uint32_t baud);
bool logIsOpen(void);
void logBoot(uint8_t enable);
void logPrintf(const char *fmt, ...);

#else

// 로그가 없는 빌드(ext-loader)에서도 드라이버를 그대로 쓰게 한다
#define logPrintf(...)

#endif

#ifdef __cplusplus
}
#endif



#endif