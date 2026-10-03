#include "ap.h"
#include "module.h"
#include "boot/boot.h"


static void bootUp(void);


void apInit(void)
{
  //-- 앱으로 넘어가면 여기서 돌아오지 않는다. 모듈(CLI, cmd)은 FSBL 에 머물 때만 연다
  bootUp();

  //-- 각 모듈의 init() 이 우선순위 순으로 실행된다 (MODULE_DEF 로 자기 등록)
  moduleInit();
}

void apMain(void)
{
  uint32_t pre_time = millis();

  logBoot(false);

  while (1)
  {
    if (millis() - pre_time >=100)
    {
      pre_time = millis();
      ledToggle(_DEF_LED1);
    }

    moduleUpdate();
  }
}

/*
 * 부팅 판정. 아래 중 하나면 FSBL 에 머문다 (부트로더 모드).
 *   - 앱이 부트로더를 요청했다 (resetToBoot → RTC 백업 레지스터의 MODE_BIT_BOOT)
 *   - 리셋 버튼을 두 번 눌렀다 (resetInit 의 더블클릭 판정)
 *   - 실행할 수 있는 앱이 없다 (TAG / CRC)
 * 그 밖에는 앱으로 점프한다.
 */
void bootUp(void)
{
  const char *reason;

  if (resetGetBootMode() & (1<<MODE_BIT_BOOT))
  {
    reason = "boot request";
  }
  else if (resetGetCount() >= HW_RESET_DBLCLK_CNT)
  {
    reason = "reset double click";
  }
  else
  {
    bootJumpFirm();               // 성공하면 돌아오지 않는다
    reason = "no valid app";
  }

  logPrintf("[  ] boot : stay in FSBL (%s)\n", reason);
}
