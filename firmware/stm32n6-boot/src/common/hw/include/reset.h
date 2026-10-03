#ifndef RESET_H_
#define RESET_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"


#ifdef _USE_HW_RESET


#define RESET_BIT_POWER       0
#define RESET_BIT_PIN         1
#define RESET_BIT_WDG         2
#define RESET_BIT_SOFT        3
#define RESET_BIT_ETC         4
#define RESET_BIT_MAX         5


//-- 부트 요청 플래그 (RTC 백업 레지스터 HW_RTC_BOOT_MODE 에 담긴다)
//
//   단순 매직이 아니라 플래그인 이유 : 앱이 부트로더를 부를 때 USB 구성을
//   골라야 하기 때문이다. MSC 는 리셋 더블클릭이거나 앱이 명시적으로 요청한
//   경우에만 열거한다.
//
#define MODE_BIT_BOOT         0     // 부트로더에 머무른다
#define MODE_BIT_MSC          1     // MSC(UF2) 도 함께 열거한다
#define MODE_BIT_MAX          2


bool resetInit(void);
void resetLog(void);
void resetToBoot(bool with_msc);
void resetToReset(void);

uint32_t resetGetBits(void);
void     resetSetBits(uint32_t data);
void     resetSetBootMode(uint32_t data);
uint32_t resetGetBootMode(void);

// 리셋 버튼 더블클릭 감지
uint32_t resetGetCount(void);

// 부팅 확인(confirm) / 폴트 자동 복구용 카운터
uint32_t resetGetBootTry(void);
void     resetSetBootTry(uint32_t cnt);
uint32_t resetGetFaultCount(void);
void     resetIncFaultCount(void);
void     resetSetFaultCount(uint32_t cnt);
void     resetConfirmBoot(void);

#endif


#ifdef __cplusplus
}
#endif

#endif 
