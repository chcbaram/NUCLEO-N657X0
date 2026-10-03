#include "bsp.h"
#include "hw_def.h"


/* 외부 로더용 bsp.

   CubeProgrammer 가 RAM 에 올린 함수를 하나씩 부르므로 벡터 테이블도 인터럽트도 없다.
   그래서 SysTick 대신 DWT 사이클 카운터로 HAL_GetTick() 을 만든다.
   캐시와 MPU 는 켜지 않는다. CubeProgrammer 가 디버그 포트로 RAM 에 넣은 데이터를
   CPU 가 캐시 없이 바로 봐야 하기 때문이다.

   Init() 은 여러 번 불릴 수 있어서 매번 처음부터 같은 상태로 만든다. */


static bool bspClockInit(void);

static uint32_t tick_ms     = 0;
static uint32_t tick_cycle  = 0;




bool bspInit(void)
{
  // -mfloat-abi=hard 라 FPU 를 먼저 켠다 (startup 의 SystemInit 이 하던 일)
  SCB->CPACR |= (0xFUL << 20);
  __DSB();
  __ISB();

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT       = 0;
  DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
  tick_ms           = 0;
  tick_cycle        = 0;

  if (bspClockInit() != true)
  {
    return false;
  }

  return true;
}

void delay(uint32_t ms)
{
  HAL_Delay(ms);
}

uint32_t millis(void)
{
  return HAL_GetTick();
}

// 인터럽트가 없으니 SysTick 을 켜지 않는다 (HAL_RCC_ClockConfig 가 부른다)
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
  (void)TickPriority;
  return HAL_OK;
}

// 지난 호출 뒤 흐른 사이클을 ms 로 쌓는다. CYCCNT 는 400 MHz 에서 10 초마다 넘치지만
// 기다리는 루프가 그보다 자주 부르므로 차이만 보면 된다.
uint32_t HAL_GetTick(void)
{
  uint32_t cycle_per_ms = SystemCoreClock / 1000;
  uint32_t cur          = DWT->CYCCNT;
  uint32_t diff         = cur - tick_cycle;

  if (cycle_per_ms == 0)
    cycle_per_ms = 1;

  tick_ms    += diff / cycle_per_ms;
  tick_cycle += (diff / cycle_per_ms) * cycle_per_ms;

  return tick_ms;
}

void HAL_Delay(uint32_t Delay)
{
  uint32_t pre = HAL_GetTick();

  while ((HAL_GetTick() - pre) < Delay)
  {
  }
}

/*
  펌웨어(stm32n6-fw)의 클럭과 PLL1 은 같게 두고 CPU 만 낮춘다.

    HSI 64 MHz -> PLL1 : M=4 -> N=100 -> 1600 MHz
      IC1 /4 -> CPUCLK 400 MHz   (overdrive 없이 VOS 기본값으로 가능)
      IC2 /4 -> SYSCLK 400 MHz   (AHB /2 -> 200 MHz)
      IC6 /5, IC11 /4
    XSPI2 커널 클럭 IC3 = PLL1 / 32 = 50 MHz 는 xspi.c 가 잡는다.
*/
static bool bspClockInit(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};


  if (HAL_PWREx_ConfigSupply(PWR_EXTERNAL_SOURCE_SUPPLY) != HAL_OK)
  {
    return false;
  }

  osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
  osc.HSIState            = RCC_HSI_ON;
  osc.HSIDiv              = RCC_HSI_DIV1;
  osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  osc.PLL1.PLLState       = RCC_PLL_NONE;
  osc.PLL2.PLLState       = RCC_PLL_NONE;
  osc.PLL3.PLLState       = RCC_PLL_NONE;
  osc.PLL4.PLLState       = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK)
  {
    return false;
  }

  // PLL1 을 만지기 전에 CPU/SYS 를 HSI 로 내린다 (Init 을 다시 부를 때)
  HAL_RCC_GetClockConfig(&clk);
  if ((clk.CPUCLKSource == RCC_CPUCLKSOURCE_IC1) ||
      (clk.SYSCLKSource == RCC_SYSCLKSOURCE_IC2_IC6_IC11))
  {
    clk.ClockType    = (RCC_CLOCKTYPE_CPUCLK | RCC_CLOCKTYPE_SYSCLK);
    clk.CPUCLKSource = RCC_CPUCLKSOURCE_HSI;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    if (HAL_RCC_ClockConfig(&clk) != HAL_OK)
    {
      return false;
    }
  }

  osc.OscillatorType     = RCC_OSCILLATORTYPE_NONE;
  osc.PLL1.PLLState      = RCC_PLL_ON;
  osc.PLL1.PLLSource     = RCC_PLLSOURCE_HSI;
  osc.PLL1.PLLM          = 4;
  osc.PLL1.PLLN          = 100;
  osc.PLL1.PLLFractional = 0;
  osc.PLL1.PLLP1         = 1;
  osc.PLL1.PLLP2         = 1;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK)
  {
    return false;
  }

  clk.ClockType      = RCC_CLOCKTYPE_CPUCLK | RCC_CLOCKTYPE_HCLK
                     | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1
                     | RCC_CLOCKTYPE_PCLK2  | RCC_CLOCKTYPE_PCLK4
                     | RCC_CLOCKTYPE_PCLK5;
  clk.CPUCLKSource   = RCC_CPUCLKSOURCE_IC1;
  clk.SYSCLKSource   = RCC_SYSCLKSOURCE_IC2_IC6_IC11;
  clk.AHBCLKDivider  = RCC_HCLK_DIV2;
  clk.APB1CLKDivider = RCC_APB1_DIV1;
  clk.APB2CLKDivider = RCC_APB2_DIV1;
  clk.APB4CLKDivider = RCC_APB4_DIV1;
  clk.APB5CLKDivider = RCC_APB5_DIV1;

  clk.IC1Selection.ClockSelection  = RCC_ICCLKSOURCE_PLL1;
  clk.IC1Selection.ClockDivider    = 4;
  clk.IC2Selection.ClockSelection  = RCC_ICCLKSOURCE_PLL1;
  clk.IC2Selection.ClockDivider    = 4;
  clk.IC6Selection.ClockSelection  = RCC_ICCLKSOURCE_PLL1;
  clk.IC6Selection.ClockDivider    = 5;
  clk.IC11Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  clk.IC11Selection.ClockDivider   = 4;

  if (HAL_RCC_ClockConfig(&clk) != HAL_OK)
  {
    return false;
  }

  return true;
}

void Error_Handler(void)
{
}
