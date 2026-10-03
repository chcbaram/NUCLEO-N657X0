#include "hw.h"


bool hwInit(void)
{
  if (bspInit() != true)
  {
    return false;
  }

  ledInit();

  return xspiInit();
}
