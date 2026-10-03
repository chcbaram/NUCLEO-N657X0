#include "ap.h"
#include "module.h"


void apInit(void)
{
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
