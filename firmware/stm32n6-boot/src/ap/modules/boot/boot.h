#ifndef BOOT_H_
#define BOOT_H_


#include "ap_def.h"


// 앱(FW) 영역 이미지 판정 단계. weact-h750 과 값이 같다 (RAW 는 쓰지 않는다)
typedef enum
{
  BOOT_IMG_NONE = 0,
  BOOT_IMG_RAW,
  BOOT_IMG_VER,         // firm_ver_t 는 있지만 TAG 가 없거나 맞지 않는다
  BOOT_IMG_TAG,         // TAG 의 CRC 까지 맞다
} BootImgType_t;


BootImgType_t bootVerifyFirm(void);
bool          bootGetTag(firm_tag_t *p_tag);
bool          bootGetVer(firm_ver_t *p_ver);


#endif
