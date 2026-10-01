#include "bsp.h"
#include "hw_def.h"


static void bspMpuInit(void);
static bool bspClockInit(void);


bool bspInit(void)
{
  // DMA 와 공유하는 구역은 캐시를 켜기 전에 non-cacheable 로 지정한다.
  // 캐시를 켠 뒤에 바꾸면 그 사이에 올라온 캐시 라인이 남는다.
  //
  bspMpuInit();

  // BootROM 이 DCACHE 를 clean/invalidate 한 뒤 넘겨주지만,
  // 캐시 자체를 켜 두는 것은 FSBL 의 몫이다.
  //
  SCB_EnableICache();
  SCB_EnableDCache();

  if (HAL_Init() != HAL_OK)
  {
    return false;
  }

  if (bspClockInit() != true)
  {
    return false;
  }

  return true;
}

/*
  .noncacheable 구역을 non-cacheable 로 지정한다.

  CPU 는 D-캐시를 거쳐 메모리를 보지만 DMA 는 SRAM 을 직접 읽고 쓴다.
  둘이 공유하는 메모리는 여기에 둬야 서로 같은 값을 본다.

    UART RX 버퍼     DMA 가 쓰고 CPU 가 읽는다  -> 캐시되면 CPU 가 옛 값을 읽는다
    DMA 노드(LLI)    CPU 가 쓰고 DMA 가 읽는다  -> 캐시되면 DMA 가 빈 노드를 읽는다

  변수에 __NON_CACHEABLE 을 붙이면 링커가 이 구역에 모은다.
  나머지 메모리는 기본 메모리 맵(PRIVDEFENA)을 그대로 쓴다.
*/
static void bspMpuInit(void)
{
  extern uint32_t __snoncacheable;
  extern uint32_t __enoncacheable;

  MPU_Attributes_InitTypeDef attr   = {0};
  MPU_Region_InitTypeDef     region = {0};
  uint32_t                   begin  = (uint32_t)&__snoncacheable;
  uint32_t                   end    = (uint32_t)&__enoncacheable;
  uint32_t                   primask;


  primask = __get_PRIMASK();
  __disable_irq();

  HAL_MPU_Disable();

  if (end > begin)
  {
    attr.Number     = MPU_ATTRIBUTES_NUMBER0;
    attr.Attributes = INNER_OUTER(MPU_NOT_CACHEABLE);
    HAL_MPU_ConfigMemoryAttributes(&attr);

    // RLAR 의 limit 은 "포함 끝" 이라 end - 1 을 준다.
    // (HAL 의 GCC 용 __NON_CACHEABLE_SECTION_END 는 -1 이 빠져 있어 32 바이트 넘친다)
    //
    region.Enable           = MPU_REGION_ENABLE;
    region.Number           = MPU_REGION_NUMBER0;
    region.BaseAddress      = begin;
    region.LimitAddress     = end - 1;
    region.AttributesIndex  = MPU_ATTRIBUTES_NUMBER0;
    region.AccessPermission = MPU_REGION_ALL_RW;
    region.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    region.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    HAL_MPU_ConfigRegion(&region);
  }

  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

  __set_PRIMASK(primask);
}

/*
  NUCLEO-N657X0-Q 클럭 구성

    HSI 64 MHz
      -> PLL1 : M=4 (16 MHz) -> N=75 -> 1200 MHz
           IC1  /2  -> CPUCLK  600 MHz
           IC2  /3  -> SYSCLK  400 MHz  (AHB /2 -> 200 MHz)
           IC6  /4  -> 300 MHz
           IC11 /3  -> 400 MHz

  V_DDCORE 는 보드의 외부 레귤레이터(TPS62088)가 공급하므로
  PWR_EXTERNAL_SOURCE_SUPPLY 를 선택한다. (내부 SMPS 아님)
*/
static bool bspClockInit(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

  if (HAL_PWREx_ConfigSupply(PWR_EXTERNAL_SOURCE_SUPPLY) != HAL_OK)
  {
    return false;
  }

  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    return false;
  }

  // 1) HSI 기동
  //
  osc.OscillatorType        = RCC_OSCILLATORTYPE_HSI;
  osc.HSIState              = RCC_HSI_ON;
  osc.HSIDiv                = RCC_HSI_DIV1;
  osc.HSICalibrationValue   = RCC_HSICALIBRATION_DEFAULT;
  osc.PLL1.PLLState         = RCC_PLL_NONE;
  osc.PLL2.PLLState         = RCC_PLL_NONE;
  osc.PLL3.PLLState         = RCC_PLL_NONE;
  osc.PLL4.PLLState         = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK)
  {
    return false;
  }

  // 2) PLL1 을 만지기 전에 CPU/SYS 를 HSI 로 잠시 내린다
  //
  HAL_RCC_GetClockConfig(&clk);
  if ((clk.CPUCLKSource == RCC_CPUCLKSOURCE_IC1) ||
      (clk.SYSCLKSource == RCC_SYSCLKSOURCE_IC2_IC6_IC11))
  {
    clk.ClockType     = (RCC_CLOCKTYPE_CPUCLK | RCC_CLOCKTYPE_SYSCLK);
    clk.CPUCLKSource  = RCC_CPUCLKSOURCE_HSI;
    clk.SYSCLKSource  = RCC_SYSCLKSOURCE_HSI;
    if (HAL_RCC_ClockConfig(&clk) != HAL_OK)
    {
      return false;
    }
  }

  // 3) PLL1 = 1200 MHz
  //
  osc.OscillatorType        = RCC_OSCILLATORTYPE_NONE;
  osc.PLL1.PLLState         = RCC_PLL_ON;
  osc.PLL1.PLLSource        = RCC_PLLSOURCE_HSI;
  osc.PLL1.PLLM             = 4;
  osc.PLL1.PLLN             = 75;
  osc.PLL1.PLLFractional    = 0;
  osc.PLL1.PLLP1            = 1;
  osc.PLL1.PLLP2            = 1;
  osc.PLL2.PLLState         = RCC_PLL_NONE;
  osc.PLL3.PLLState         = RCC_PLL_NONE;
  osc.PLL4.PLLState         = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK)
  {
    return false;
  }

  // 4) 최종 클럭 트리
  //
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
  clk.IC1Selection.ClockDivider    = 2;
  clk.IC2Selection.ClockSelection  = RCC_ICCLKSOURCE_PLL1;
  clk.IC2Selection.ClockDivider    = 3;
  clk.IC6Selection.ClockSelection  = RCC_ICCLKSOURCE_PLL1;
  clk.IC6Selection.ClockDivider    = 4;
  clk.IC11Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  clk.IC11Selection.ClockDivider   = 3;

  if (HAL_RCC_ClockConfig(&clk) != HAL_OK)
  {
    return false;
  }

  return true;
}

void delay(uint32_t ms)
{
  HAL_Delay(ms);
}

void delayUs(uint32_t delay_us)
{
  uint32_t pre_time = micros();

  while (micros() - pre_time <= delay_us)
  {
    //
  }
}

uint32_t millis(void)
{
  return HAL_GetTick();
}

uint32_t micros(void)
{
  uint32_t       m0  = millis();
  __IO uint32_t  u0  = SysTick->VAL;
  uint32_t       m1  = millis();
  __IO uint32_t  u1  = SysTick->VAL;
  const uint32_t tms = SysTick->LOAD + 1;

  if (m1 != m0)
  {
    return (m1 * 1000 + ((tms - u1) * 1000) / tms);
  }
  else
  {
    return (m0 * 1000 + ((tms - u0) * 1000) / tms);
  }
}

void Error_Handler(void)
{
  if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk)
  {
    __BKPT(0);
  }

  __disable_irq();
  while (1)
  {
  }
}

void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
  Error_Handler();
}
