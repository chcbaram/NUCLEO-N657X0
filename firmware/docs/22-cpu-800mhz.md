# 22. CPU 800 MHz (overdrive)

> CPU 클럭을 600 MHz 에서 **800 MHz** 로 올렸다. 코어 전압을 먼저 올려야 해서 보드의 외부
> 레귤레이터를 GPIO 로 바꾼다. 호스트 시계로 잰 실측값은 **793 MHz**(측정 오차 약 1 %).
> 관련: [20-led.md](20-led.md) 3절 (600 MHz 설정), [03-board-boot-mapping.md](03-board-boot-mapping.md) 4절 (V<sub>DDCORE</sub>)

---

## 1. 왜 예제는 전부 600 MHz 인가

- 칩의 기본 상태가 **VOS low, V<sub>DDCORE</sub> 0.81 V** 이고, 이 전압에서 CPU 상한이 600 MHz 다
- 800 MHz 는 코어 전압을 0.89 V 로 올려야 하는데, 그 방법이 **보드마다 다르다** (외부 레귤레이터를 어느 GPIO 로 바꾸는지)
  - STM32N6570-DK : PF4 (`SMPS_OVD`)
  - NUCLEO-N657X0-Q : **PB12** (`PWR_LP`)
- 그래서 CubeMX 기본값과 예제는 600 MHz 로 두고, overdrive 는 ST 커뮤니티의
  [How to use the STM32N6 in overdrive mode](https://community.st.com/t5/stm32-mcus/how-to-use-the-stm32n6-in-overdrive-mode/ta-p/810926) 에서 따로 안내한다

로컬 CubeN6(sparse checkout)의 Nucleo 예제 중 800 MHz 를 쓰는 것은 없었다.
`PWR_REGULATOR_VOLTAGE_SCALE0` 을 쓰는 예제 7 개, `PLLN = 100` 을 쓰는 예제 3 개도 CPU 는 모두 600 MHz 였다.

---

## 2. 데이터시트 근거

DS14791 Rev 2 (`~/Downloads/DS14791_stm32n657_rev2.pdf`).

### 표 22. General operating conditions (p.136)

| 항목 | VOS low | VOS high (overdrive) |
|---|---|---|
| **F<sub>CPU</sub>** (Cortex-M55) | **0 – 600 MHz** | **0 – 800 MHz** |
| V<sub>DDCORE</sub> (Run) | 0.782 / **0.81** / 0.842 V | 0.858 / **0.89** / 0.921 V |
| NPU | 800 MHz | 1000 MHz |
| AXI CPU 버스 (`Fck_cpu_axi`) | 400 MHz | 같음 |
| AHB (`F_HCLK`) | 200 MHz | 같음 |
| USB/ETH 버스 (`Fck_icn_hsl`) | 400 MHz | 같음 |

**버스 상한은 VOS 와 상관없이 같다.** 그래서 CPU 클럭(IC1)만 올리고 나머지는 600 MHz 때와 같게 둔다.

### 표 46. PLL1 to PLL4 characteristics (p.152)

| 항목 | 범위 |
|---|---|
| PLL 입력 (normal) | 5 – 64 MHz |
| PFD 입력 (normal) | 5 – 50 MHz |
| **VCO 출력** | **800 – 3200 MHz** |

---

## 3. 보드 — V<sub>DDCORE</sub> 를 올리는 핀

V<sub>DDCORE</sub> 는 외부 레귤레이터 TPS62088(U17)이 공급하고, 출력 전압은 **PB12(`PWR_LP`)** 로
피드백을 바꿔 정한다 ([03](03-board-boot-mapping.md) 4절).

| PB12 | V<sub>DDCORE</sub> |
|---|---|
| High | **0.890 V (overdrive)** |
| Low | 0.810 V (nominal) |

CubeN6 BSP 의 `BSP_SMPS_Init(SMPS_VOLTAGE_OVERDRIVE)` 가 같은 핀(`SMPS_GPIO_PORT = GPIOB`,
`SMPS_GPIO_PIN = GPIO_PIN_12`)을 High 로 쓴다.

---

## 4. 구현 (`src/bsp/bsp.c`)

### 순서 — 전압 → VOS → 클럭

ST 글의 순서와 같다. 전압이 오르기 전에 클럭을 올리면 사양 밖에서 도는 순간이 생긴다.

```c
HAL_PWREx_ConfigSupply(PWR_EXTERNAL_SOURCE_SUPPLY);

bspCoreVoltageOverdrive();                                  // PB12 High -> 0.89 V
HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE0); // VOS high

// 이후 HSI -> PLL1 -> IC 분주 (기존 순서 그대로)
```

`bspCoreVoltageOverdrive()` 는 PB12 를 High 로 쓴 뒤 1 ms 기다린다.
**레귤레이터 정착 시간 사양은 확인하지 못했다.** ST BSP 는 기다리지 않는데, 여유로 둔 값이다.

### 클럭 구성

| | 600 MHz (이전) | **800 MHz** |
|---|---|---|
| PLL1 | HSI 64 ÷ M4 × **N75** = 1200 MHz | HSI 64 ÷ M4 × **N100** = **1600 MHz** |
| IC1 → CPU | ÷2 = 600 | ÷2 = **800** |
| IC2 → SYSCLK (AXI) | ÷3 = 400 | ÷**4** = 400 |
| AHB | ÷2 = 200 | ÷2 = 200 |
| APB1/2/4/5 | ÷1 = 200 | ÷1 = 200 |
| IC6 | ÷4 = 300 | ÷**5** = 320 |
| IC11 | ÷3 = 400 | ÷**4** = 400 |
| VOS | `SCALE1` | `SCALE0` |

PFD 16 MHz, VCO 1600 MHz 로 표 46 범위 안이다. IC6 은 1600 MHz 로는 300 MHz 를 정확히 만들 수 없어서
가장 가까운 ÷5(320 MHz)로 했다. NPU 상한(800 MHz)보다 한참 낮다.
UART 커널 클럭(PCLK2 = 200 MHz)은 그대로라 보레이트 설정은 바뀌지 않는다.

---

## 5. 검증 — 실제 클럭 재기

### `delay()` 로는 잴 수 없다

SysTick 은 CPU 클럭으로 돌고, reload 값은 **설정한** 클럭 값으로 계산된다. 그래서 `delay(100)`
동안 사이클을 세면 실제 클럭이 얼마든 항상 "맞게" 나온다. 배너의 `HAL_RCC_GetCpuClockFreq()`
도 레지스터 설정을 읽어 계산한 값일 뿐이다. **독립된 시간 기준이 필요하다.**

### 호스트 시계로 잰다

1. 펌웨어가 `MEAS_START` 를 찍고, DWT 사이클 카운터로 정확히 1.6 × 10<sup>9</sup> 사이클을 돈 뒤 `MEAS_END` 를 찍는다
2. 호스트가 baram-term 으로 두 표시가 들어온 시각의 차이를 잰다 (`MEAS_START` 대기는 적재보다 먼저 걸어 둔다)

| CPU 클럭 | 예상 구간 |
|---|---|
| 600 MHz | 2.67 s |
| 800 MHz | 2.00 s |

**결과: 2.018 s → 793 MHz.** 0.9 % 차이는 호스트 쪽 수신 지연이다. 측정 코드는 확인 후 지웠다.

### 그 밖의 확인

| 항목 | 결과 |
|---|---|
| 부팅 배너 | `Booting..Clock : 800 Mhz` |
| CLI (`uart info`, `log list`) | ✅ |
| UART 115200 | ✅ (PCLK2 그대로) |

온도와 소비 전류는 재지 않았다.

---

## 6. 막혔던 지점

| 증상 | 원인 | 해결 |
|---|---|---|
| 처음 800 MHz 빌드를 올렸을 때 배너도 안 나오고, gdb 로 보면 `main` 전에 HardFault / MemManage / `.rodata` 로 점프 | **800 MHz 코드 문제가 아니었다.** 같은 상태에서 이미 잘 돌던 600 MHz 빌드도 똑같이 죽었다. 돌고 있는 펌웨어 위에 리셋 없이 덮어쓴 것이 원인이다 ([21](21-uart-cli.md) 10절) | 전원 재인가 후 첫 적재로 다시 하니 바로 동작 |
| 데이터시트를 st.com 에서 못 받음 | `curl` 이 HTTP/2 스트림 오류 → HTTP/1.1 은 타임아웃, WebFetch 도 타임아웃 | Digikey 미러에서 받음. N657 A0/Z0/X0 는 동작 조건 표가 같다 |
| Read 도구로 PDF 를 못 엶 | poppler(`pdftoppm`) 없음 | scratchpad 의 venv 에 `pypdf` 를 깔아 텍스트로 뽑음 |

---

## 7. 다음

- **NPU overdrive** — NPU 를 쓸 때 IC6 를 1000 MHz 까지 올릴 수 있다 (표 22)
- 온도·소비 전류 측정 — overdrive 에서 얼마나 늘어나는지
