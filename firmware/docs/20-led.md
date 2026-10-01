# 20. LED

> 첫 기능. 목적은 LED 자체보다 **빌드 → 적재 → 실행 → 검증 루프를 확립**하는 것이다.
> 관련: [11-project-skeleton.md](11-project-skeleton.md), [03-board-boot-mapping.md](03-board-boot-mapping.md)

---

## 1. 하드웨어

MB1940 회로도 Sheet 12 + `Drivers/BSP/STM32N6xx_Nucleo/stm32n6xx_nucleo.h` 로 교차 확인했다.

| 채널 | BSP 이름 | 색 | 부품 | MCU 핀 | 볼 |
|---|---|---|---|---|---|
| `_DEF_LED1` | `LED1` / `LED_BLUE` | BLUE | LD7 | PG8 | T14 |
| `_DEF_LED2` | `LED2` / `LED_RED` | RED | LD5 | **PG10** | T10 |
| `_DEF_LED3` | `LED3` / `LED_GREEN` | GREEN | LD6 | PG0 | R13 |

### Active low

결선은 `V_DDIO — LED — 330Ω — MCU핀` 이다. MCU 가 low 를 내보내야 켜진다.
BSP 소스로도 확인된다.

```c
int32_t BSP_LED_On(Led_TypeDef Led)
{
  HAL_GPIO_WritePin(LED_PORT[Led], LED_PIN[Led], GPIO_PIN_RESET);
  ...
```

### ⚠️ PG10 은 BootROM 과 공유하는 핀이다

`LED2` (LD5, RED) 가 물린 **PG10 은 BootROM 이 blocking failure 시
UART5_TX 로 9600 bps 에러 로그를 내보내는 핀**이다 (UM3234 §3.10, Table 32).

- 부팅이 실패하면 이 LED 가 불규칙하게 깜빡인다 → 고장이 아니라 ROM 로그다
- 애플리케이션에서 LED2 를 쓸 때 같은 배선이라는 점을 기억할 것
- ROM 로그를 실제로 읽으려면 모포 커넥터에서 PG10 을 UART 어댑터로 받아야 한다

실제로 적재 전 GPIOG 를 읽어 보면 ROM 이 남긴 설정이 그대로 보인다.

```
GPIOG MODER  = 0xFFDFFFFF   <- PG10 만 output
GPIOG OTYPER = 0x00000400   <- PG10 open-drain
GPIOG ODR    = 0x00000400   <- high (미어서트)
```

UM3234 의 서술 그대로다 — *"a LED can be connected to the BOOTFAILN pin.
In case of a blocking failure, this LED is switched on and set to low open drain."*

---

## 2. 구현

### `src/hw/hw_def.h`

```c
#define _USE_HW_LED
#define      HW_LED_MAX_CH          3
```

### `src/hw/driver/led.c`

```c
static const led_tbl_t led_tbl[LED_MAX_CH] =
{
  {GPIOG, GPIO_PIN_8,  GPIO_PIN_RESET, GPIO_PIN_SET},   // LD7 BLUE
  {GPIOG, GPIO_PIN_10, GPIO_PIN_RESET, GPIO_PIN_SET},   // LD5 RED
  {GPIOG, GPIO_PIN_0,  GPIO_PIN_RESET, GPIO_PIN_SET},   // LD6 GREEN
};
```

`ledInit()` 는 GPIOG 클럭을 켜고 세 핀을 push-pull output 으로 잡은 뒤 `ledOff()` 를 호출한다.
API 는 참조 프로젝트와 동일하다 — `ledInit / ledOn / ledOff / ledToggle`.

### `src/ap/ap.c`

```c
void apMain(void)
{
  uint32_t pre_time = millis();

  while (1)
  {
    if (millis() - pre_time >= 500)
    {
      pre_time = millis();
      ledToggle(_DEF_LED1);
    }
  }
}
```

---

## 3. 클럭 설정 (`bsp.c`)

> 이후 **800 MHz (overdrive) 로 바꿨다** → [22-cpu-800mhz.md](22-cpu-800mhz.md). 아래는 LED 단계 당시의 600 MHz 설정이다.

LED 만 놓고 보면 필요 없지만, 앞으로의 모든 기능이 여기 얹히므로 처음부터 600 MHz 로 잡았다.
ST 템플릿(`Template_FSBL_LRUN`)의 `SystemClock_Config()` 를 따랐다.

```
HSI 64 MHz
  -> PLL1 : M=4 (16 MHz) -> N=75 -> 1200 MHz
       IC1  /2  -> CPUCLK  600 MHz
       IC2  /3  -> SYSCLK  400 MHz  (AHB /2 -> 200 MHz)
       IC6  /4  -> 300 MHz
       IC11 /3  -> 400 MHz
```

### 전원 공급 설정

```c
HAL_PWREx_ConfigSupply(PWR_EXTERNAL_SOURCE_SUPPLY);
HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);
```

> **정정** — [03-board-boot-mapping.md](03-board-boot-mapping.md#4-전원) 에서 회로도를 보고
> "내부 SMPS / 외부 TPS62088 두 경로가 모두 실장" 이라고 적었는데,
> ST 가 이 보드용으로 제공하는 템플릿은 **`PWR_EXTERNAL_SOURCE_SUPPLY`** 를 쓴다.
> 즉 V_DDCORE 는 외부 레귤레이터가 공급하고 내부 SMPS 는 쓰지 않는 구성이 기본이다.

### PLL 변경 전 HSI 로 내려야 한다

```c
HAL_RCC_GetClockConfig(&clk);
if ((clk.CPUCLKSource == RCC_CPUCLKSOURCE_IC1) || ...)
{
  // CPU/SYS 를 HSI 로 전환
}
```

BootROM 이 이미 PLL1 을 켜 놓고 넘겨줄 수 있기 때문이다
(`OTP11[1] no_cpu_pll` 이 0 이면 ROM 이 cold boot 에서 PLL 을 켠다 —
[01-boot-process.md](01-boot-process.md#관련-otp) 참고).
PLL 을 쓰고 있는 상태에서 그 PLL 을 재설정하면 실패한다.

### 캐시

```c
SCB_EnableICache();
SCB_EnableDCache();
```

ROM 은 secure boot 종료 시 DCACHE 를 clean/invalidate 하고 라이프사이클에 따라
ICACHE 만 켠 채 넘겨준다 (UM3234 Table 4). 최종 구성은 FSBL 책임이다.

---

## 4. 검증

### 빌드

```
Memory region     Used Size  Region Size  %age Used
         RAM:       15456 B       511 KB      2.95%
```

```
$ arm-none-eabi-objdump -h build/stm32n6-fw.elf
  0 .isr_vector   0000034c  34180400  34180400     <- 헤더 뒤 정확한 위치
  1 .text         00002cb8  34180750  34180750
$ arm-none-eabi-readelf -h build/stm32n6-fw.elf | grep Entry
  Entry point address:  0x34181121
```

### 적재 후 상태

`apMain()` 진입 시점에 gdb 로 확인.

```
Breakpoint 1, apMain () at src/ap/ap.c:10

SystemCoreClock = 600000000 Hz
GPIOG MODER  = 0xFFDDFFFD
GPIOG OTYPER = 0x00000000
GPIOG ODR    = 0x00000501
```

| 값 | 해석 |
|---|---|
| `SystemCoreClock = 600 MHz` | PLL1 → IC1 /2 설정 정상 |
| `MODER = 0xFFDDFFFD` | bit[1:0]=01, [17:16]=01, [21:20]=01 → PG0 / PG8 / PG10 전부 output |
| `OTYPER = 0` | push-pull. ROM 이 PG10 에 걸어둔 open-drain 이 덮여 있다 |
| `ODR = 0x501` | bit0/8/10 high = 전부 소등 (active low) — `ledInit()` 의 `ledOff()` 결과 |

### 토글 동작

`ledToggle` 에 브레이크를 걸고 세 번 잡았다.

```
Breakpoint 1, ledToggle (ch=0) at src/hw/driver/led.c:59
toggle#1 ODR=0x501 millis=13500
toggle#2 ODR=0x401 millis=14000
toggle#3 ODR=0x501 millis=14500
```

- 간격이 정확히 **500 ms** → SysTick / `millis()` 정상
- ODR bit8 이 `1 → 0 → 1` 로 토글 → **LD7 (BLUE)** 점멸

---

## 5. 막혔던 지점

| 증상 | 원인 | 해결 |
|---|---|---|
| `error: 'SAU' undeclared`, `'SCB_NS' undeclared` | FSBL 은 secure state. CMSIS 헤더가 해당 심볼을 `__ARM_FEATURE_CMSE==3` 에서만 노출 | 컴파일/링크에 `-mcmse` 추가 |
| `stm32n6xx_ll_dlyb.c` 컴파일 실패 | HAL `Src/*.c` 를 전부 glob → 비활성 모듈 소스까지 잡힘 | HAL 소스를 명시 목록으로 관리 |
| 적재 후 GPIO 를 읽으면 ROM 설정만 보임 | `STM32_Programmer_CLI -c` 가 연결할 때마다 software reset → BootROM 복귀 | 적재·확인 모두 gdb 로 |
| gdbserver 가 `ST-LINK firmware upgrade required` | gdbserver 7.13.0 이 출고 펌웨어 `V3J15M6` 거부 | `STLinkUpgrade.sh` 로 **`V3J17M10`** 까지 업그레이드 |
| gdb 에서 `Cannot access memory at address 0x56021814` | 앞선 gdbserver 세션이 detach 한 뒤 새 서버로 다시 attach 하면 주변장치 영역 읽기가 막힌다. 브레이크포인트는 정상 동작하므로 펌웨어 문제는 아니다 | 확인은 **gdbserver 를 새로 띄우고 그 세션 안에서 `load` 까지 한 번에** 한다. `load.sh` 를 돌린 뒤 별도 세션으로 붙지 말 것 |

---

## 6. 다음

- [ ] UART (VCP = USART1, PE5/PE6) + `logPrintf`
- [ ] 부팅 배너 출력 (클럭/버전/빌드일시)
- [ ] BootROM 트레이스 파서 — `0x3410_37F0` / `0x2410_77F0` 를 덮기 전에 읽어 UART 로 출력
