#include "otp.h"
#include "cli.h"


#ifdef _USE_HW_OTP


/* OTP(BSEC 퓨즈) 읽기 전용.

   쓰기(태우기)는 되돌릴 수 없어서 함수도 CLI 명령도 일부러 두지 않는다.
   HAL_BSEC_OTP_Read() 는 퓨즈를 shadow 레지스터로 reload 한 뒤 읽는다.
   reload 는 OTPCR 의 PROG/PPLOCK 을 0 으로 두고 실행하는 읽기 동작이다.
   shadow 되지 않는 퓨즈는 reload 없이 FVRw 를 읽으면 0 으로 보인다. */
#define OTP_FUSE_MAX      376


static BSEC_HandleTypeDef hbsec;


#if CLI_USE(HW_OTP)
static void cliCmd(cli_args_t *args);
#endif




bool otpInit(void)
{
  __HAL_RCC_BSEC_CLK_ENABLE();

  hbsec.Instance = BSEC;

#if CLI_USE(HW_OTP)
  cliAdd("otp", cliCmd);
#endif
  return true;
}

bool otpRead(uint32_t id, uint32_t *p_data)
{
  if (id >= OTP_FUSE_MAX || p_data == NULL)
  {
    return false;
  }

  return HAL_BSEC_OTP_Read(&hbsec, id, p_data) == HAL_OK;
}


#if CLI_USE(HW_OTP)
void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc >= 2 && args->isStr(0, "read"))
  {
    uint32_t id  = (uint32_t)args->getData(1);
    uint32_t cnt = 1;

    if (args->argc >= 3)
    {
      cnt = (uint32_t)args->getData(2);
    }

    for (uint32_t i=0; i<cnt && id+i<OTP_FUSE_MAX; i++)
    {
      uint32_t data;

      if (otpRead(id + i, &data))
        cliPrintf("OTP%-3u : 0x%08X\n", (unsigned)(id + i), (unsigned)data);
      else
        cliPrintf("OTP%-3u : read fail (err 0x%X)\n", (unsigned)(id + i), (unsigned)hbsec.ErrorCode);
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("otp read id [count]\n");
  }
}
#endif


#endif
