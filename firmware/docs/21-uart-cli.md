# 21. UART (VCP) + 로그

> ST-LINK VCP 로 부팅 배너와 로그를 내보내고, **수신은 circular DMA** 로 받는다.
> 이 문서의 핵심은 **D-캐시가 켜진 상태에서 DMA 와 메모리를 공유하는 법**이다.
> CLI 는 아직 붙이지 않았다 (7절).
> 관련: [20-led.md](20-led.md), [03-board-boot-mapping.md](03-board-boot-mapping.md)

---

## 1. 하드웨어

| 항목 | 값 |
|---|---|
| 주변장치 | USART1 (secure 주소 `0x5200_1000`) |
| 핀 | PE5 = TX, PE6 = RX, AF7 |
| 연결 | ST-LINK V3EC VCP ([03](03-board-boot-mapping.md) 4절) |
| 커널 클럭 | PCLK2 = 200 MHz → `BRR = 1736` → 115,207 bps |
| 수신 DMA | GPDMA1 CH0, request 107 (`GPDMA1_REQUEST_USART1_RX`) |

호스트 포트 이름은 USB 자리마다 바뀐다. `STM32_Programmer_CLI -l` 로 확인한다.

---

## 2. 구현

참조 프로젝트(`stm32c5-ai`)의 `uart.c` / `log.c` 구조와 인터페이스를 그대로 따랐다.
다만 참조 프로젝트는 신형 HAL2 API(`hal_uart_handle_t`)이고 N6 는 클래식 HAL 이라
본체는 다시 썼다.

| 파일 | 내용 |
|---|---|
| `src/common/hw/include/uart.h`, `log.h` | 참조 프로젝트와 동일 |
| `src/hw/driver/uart.c` | USART1 + GPDMA1 CH0 circular 수신 |
| `src/hw/driver/log.c` | 참조 것에서 CLI 부분만 뺐다. `_write` 를 여기서 로그 채널로 보낸다 |
| `src/hw/hw.c` | 부팅 배너 |
| `src/ap/ap.c` | **임시 에코** — CLI 가 붙으면 없앤다 |
| `src/bsp/bsp.c` | `bspMpuInit()` — 4절 |
| `src/bsp/ldscript/*.ld` | `.noncacheable` 구역 — 4절 |
| `stm32n6xx_hal_conf.h`, `CMakeLists.txt` | `HAL_UART_MODULE_ENABLED`, `hal_uart.c`, `hal_uart_ex.c` 추가 |

---

## 3. DMA 수신

참조 프로젝트와 같은 방식이다. **DMA 가 링 버퍼에 계속 쓰고, CPU 는 DMA 의 남은 카운트로
쓰기 위치를 읽기만 한다.** 인터럽트를 쓰지 않는다.

```c
in = (len - __HAL_DMA_GET_COUNTER(hdmarx)) % len;   // __HAL_DMA_GET_COUNTER = CBR1.BNDT
```

### N6 에서 circular 는 linked-list 다

N6 HAL 의 DMA 모드에는 `DMA_CIRCULAR` 가 없고 `DMA_NORMAL` 뿐이다. circular 는
**노드 하나가 자기 자신을 가리키는 linked-list** 로 만든다. 한 바퀴(1024 B)가 끝나면 DMA 가 메모리에서 노드를 다시 읽어
처음부터 이어 받는다. ST 예제 `UART_ReceptionToIdle_CircularDMA` 의 구성을 따랐다.

```c
HAL_DMAEx_List_Init()            // LinkedListMode = DMA_LINKEDLIST_CIRCULAR
HAL_DMAEx_List_BuildNode()       // 노드 설정
HAL_DMAEx_List_InsertNode_Tail()
HAL_DMAEx_List_SetCircularMode() // 노드가 자기 자신을 가리키게
HAL_DMAEx_List_LinkQ()
```

`HAL_UART_Receive_DMA()` 는 linked-list 모드를 알아본다. 헤드 노드의 카운트(`CBR1`)와
주소(`CSAR`/`CDAR`)를 채운 뒤 `HAL_DMAEx_List_Start_IT()` 로 시작한다
(`stm32n6xx_hal_uart.c` 의 `UART_Start_Receive_DMA`).

### `% len` 이 필요한 이유

한 바퀴 끝에서 노드를 다시 읽기 직전에는 `BNDT` 가 잠깐 0 이 된다. 그 순간 읽으면
`in = len` 이 되는데, 이 값은 버퍼 인덱스로 쓸 수 없다. 참조 프로젝트에는 없던 처리다.

---

## 4. 캐시 — DMA 와 공유하는 메모리

### 문제

`bspInit()` 이 D-캐시를 켠다. **CPU 는 캐시를 거쳐 메모리를 보지만 DMA 는 SRAM 을 직접
읽고 쓴다.** 둘이 공유하는 메모리는 두 군데이고, 방향이 반대다.

| 메모리 | 쓰는 쪽 → 읽는 쪽 | 캐시되면 |
|---|---|---|
| RX 버퍼 | DMA → CPU | DMA 가 SRAM 에 새 바이트를 써도 CPU 는 캐시에 남은 옛 값을 읽는다 |
| **DMA 노드** | **CPU → DMA** | CPU(HAL)가 쓴 노드가 write-back 캐시에 머물러, DMA 가 **빈 노드**를 읽는다 |

RX 버퍼만 챙기기 쉬운데, 노드도 반드시 같이 챙겨야 한다. ST 예제도 노드에
`__NON_CACHEABLE` 을 붙인다.

### 해결 — MPU 로 non-cacheable 구역을 만든다

캐시 관리 호출(`SCB_InvalidateDCache_by_Addr` 등)을 매번 하는 대신,
**공유 메모리를 캐시되지 않는 구역에 모아 둔다.** 세 단계가 맞물려야 한다.

| 단계 | 위치 | 내용 |
|---|---|---|
| ① 변수 표시 | `uart.c` | `__NON_CACHEABLE` (HAL 매크로) |
| ② 한곳에 모으기 | 링커 스크립트 | `.noncacheable` 구역, `__snoncacheable` ~ `__enoncacheable` |
| ③ 캐시 끄기 | `bsp.c` `bspMpuInit()` | MPU region 0, `INNER_OUTER(MPU_NOT_CACHEABLE)` |

```c
static uint8_t         uart_rx_buf[UART_MAX_CH][UART_RX_BUF_LENGTH] __NON_CACHEABLE;
static DMA_NodeTypeDef dma_node_usart1_rx __NON_CACHEABLE;
```

`bspMpuInit()` 은 **캐시를 켜기 전에** 부른다. 켠 뒤에 바꾸면 그 사이에 올라온 캐시 라인이
남는다. 나머지 메모리는 `MPU_PRIVILEGED_DEFAULT` 로 기본 메모리 맵을 그대로 쓴다.

### ⚠️ 원래 링커 스크립트는 ①과 ②가 맞지 않았다

| | 섹션 이름 |
|---|---|
| HAL 의 `__NON_CACHEABLE` (`stm32n6xx_hal_def.h`) | `.noncacheable` |
| 우리 링커 스크립트가 모으던 것 | `noncacheable_buffer` |

이대로 `__NON_CACHEABLE` 을 붙이면 변수가 그 구역에 안 들어가고 일반(캐시되는) 메모리에
놓인다. **컴파일 에러도 경고도 없이 조용히 틀린다.** 게다가 MPU 설정 코드가 아예 없어서
`.noncacheable` 구역은 이름만 있고 실제로는 캐시되고 있었다.
링커 스크립트를 `KEEP(*(.noncacheable))` 로 고쳐 HAL 매크로와 맞췄다.

### 32 바이트 정렬과 `end - 1`

MPU 는 주소를 32 바이트 단위로 자르고(`RBAR`/`RLAR` 의 `& 0xFFFFFFE0`),
Cortex-M55 캐시 라인도 32 바이트다.

- 구역 시작·끝을 `ALIGN(32)` 로 맞췄다. 원래 `ALIGN(8)` 이었는데 그대로 두면 버퍼 일부가
  MPU 보호 밖으로 빠질 수 있다
- `RLAR` 의 limit 은 "포함 끝" 이라 `end - 1` 을 준다. HAL 의 GCC 용
  `__NON_CACHEABLE_SECTION_END` 는 `-1` 이 빠져 있어서 그대로 쓰면 32 바이트 넘친다
  (ARMCC 용 매크로에는 `-1` 이 있다)

빌드 결과:

| 심볼 | 주소 |
|---|---|
| `__snoncacheable` | `0x3418_E400` |
| `uart_rx_buf` (1024 B) | `0x3418_E400` |
| `dma_node_usart1_rx` | `0x3418_E800` |
| `__enoncacheable` | `0x3418_E840` |

### 검증 — MPU 를 빼면 무엇이 깨지나

`bspMpuInit()` 호출만 지우고 같은 테스트를 돌렸다. **에코가 한 바이트도 돌아오지 않았다.**
옛 값을 읽는 수준이 아니라 수신 자체가 멈췄다. gdb 로 `apMain` 에서 세워 보면:

| | 값 | 의미 |
|---|---|---|
| DMA `CSR` | `0x1001` | **USEF(설정 오류) + IDLE** — 시작하자마자 멈췄다 |
| 채널 `CBR1` / `CDAR` / `CLLR` | 전부 `0` | DMA 가 **전부 0 인 노드**를 읽어 갔다 |
| 노드 (CPU 쪽에서 본 값) | `CTR2=0x6B`, `CBR1=0x400`, `CSAR=0x5200_1024`, `CDAR`=버퍼, `CLLR`=자기 자신 | **값은 정확했다** |

HAL 이 노드를 올바르게 썼지만 그 값은 캐시에만 있었고, DMA 는 SRAM 의 0 을 읽었다.
위 표의 두 번째 줄(DMA 노드)이 그대로 재현된 것이다.
(디버거로 읽은 노드 값이 정확했던 것으로 보아, 디버거 읽기는 CPU 와 같은 캐시 경로를
타는 것으로 보인다. 관찰에서 추론한 것이고 문서로 확인하지는 않았다)

---

## 5. Secure 설정

FSBL 은 secure 로 돈다. 버퍼(`0x3418_xxxx`)와 USART1(`0x5200_1000`)이 모두 secure 주소라
DMA 도 secure 여야 한다. non-secure 채널은 secure 주소에 접근할 수 없다.

| 대상 | 설정 |
|---|---|
| DMA 채널 | `HAL_DMA_ConfigChannelAttributes(SEC \| PRIV \| SRC_SEC \| DEST_SEC)` |
| DMA 노드 | `SrcSecure = DMA_CHANNEL_SRC_SEC`, `DestSecure = DMA_CHANNEL_DEST_SEC` |
| PE5 / PE6 | `HAL_GPIO_ConfigPinAttributes(GPIO_PIN_SEC \| GPIO_PIN_NPRIV)` |

linked-list 모드에서는 노드를 다시 읽을 때마다 `CTR1` 이 노드 값으로 바뀐다.
그래서 노드 쪽 `SrcSecure`/`DestSecure` 도 반드시 줘야 한다.

### `CPU_IN_SECURE_STATE` 를 확인할 것

위 두 HAL 함수는 보안 설정 부분이 **`#if defined CPU_IN_SECURE_STATE` 안에** 있다.
이 매크로가 없으면 secure 지정이 **조용히 빠진다.** CMSIS 디바이스 헤더가
`__ARM_FEATURE_CMSE == 3` 일 때 정의하므로 `-mcmse` 가 필요하다
([11](11-project-skeleton.md) 5.1절). 실제 빌드 플래그로 정의되는 것을 확인했다.

### ST 예제와 다른 점

ST 예제는 `SystemIsolation_Config()` 에서 `__HAL_RCC_RIFSC_CLK_ENABLE()` 을 부르지만 넣지 않았다.
두 HAL 함수 모두 각각 DMA·GPIO 의 `SECCFGR`/`PRIVCFGR` 만 쓰고 RIFSC 는 건드리지 않는다.

---

## 6. `printf` 와 `_write`

`syscalls.c` 의 `_write` 는 weak 이고 weak `__io_putchar` 를 부르는데, 그 구현이 없다.
그대로 두면 `printf` 를 처음 부르는 순간 **0 번지로 점프해 HardFault** 가 난다.
`log.c` 에서 `_write` 를 다시 정의해 로그 채널로 보낸다.

---

## 7. 검증

모두 전원을 재인가한 깨끗한 상태에서 다시 확인했다.

| 항목 | 방법 | 결과 |
|---|---|---|
| 빌드 | `cmake --build build -j20` | 경고 없음 |
| 부팅 배너 | `load.sh` 적재 후 baram-term 으로 수신 | ✅ |
| 짧은 문자열 에코 | `hello N6\r\n` | ✅ |
| **링 버퍼 여러 바퀴** | 바이너리 200 B × 15 = 3000 B (1024 B 버퍼 2.9 바퀴) | ✅ 한 바이트도 안 틀림 |
| 캐시 처리의 필요성 | MPU 를 빼고 같은 테스트 | ❌ 수신 정지 (4절) |

부팅 배너:

```
[ Firmware Begin... ]
Booting..Name 		: STM32N6-FW
Booting..Ver  		: V260826R1
Booting..Clock		: 600 Mhz
Booting..Date 		: Oct  2 2026
Booting..Time 		: 00:39:32
Booting..Addr 		: 0x34180400
```

`Clock` 은 참조 프로젝트의 `SYSCLK` 대신 `HAL_RCC_GetCpuClockFreq()` 를 찍는다.
N6 는 CPU 클럭(IC1, 600 MHz)과 SYSCLK(IC2, 400 MHz)이 다르다.
`Addr` 은 벡터 테이블 위치(`SCB->VTOR`)다.

---

## 8. 막혔던 지점

| 증상 | 원인 | 해결 |
|---|---|---|
| 시리얼에 아무것도 안 들어옴 | 테스트 스크립트(pyserial)와 baram-term 이 같은 포트를 동시에 열었다. macOS 는 둘 다 열리지만 수신 데이터를 나눠 가져간다 | baram-term 이 열려 있으면 `baram-ctl` 로만 주고받는다. 바이너리 테스트처럼 직접 열어야 하면 `baram-ctl release` → 테스트 → `resume` |
| 배너는 나왔는데 에코가 안 됨 | gdb 로 `apMain` 브레이크포인트에서 세운 채 세션을 끝내서 코어가 에코 루프에 들어가지 못했다 | 동작 확인은 `load.sh`(detach 후 실행)로 한다 |
| MPU 를 뺀 실험 뒤 SWD 가 안 붙음 (`Unable to get core ID`), 출력도 없음 | 원인은 확인하지 못했다. 정상 빌드로 되돌려도 계속됐고 Programmer 로 NRST 를 걸어도 반복됐다 | **USB 를 뽑았다 꽂아 전원을 재인가**하자 정상 빌드가 첫 실행부터 동작했다 |
| `gdb` 가 엉뚱한 값을 찍음 | 타깃 연결에 실패했는데 `-batch` 가 ELF 의 초기값을 그대로 출력했다 | 출력에 `could not connect` 가 있으면 값을 믿지 않는다 |

---

## 9. 크기

| | RAM 사용 |
|---|---|
| LED 단계 | 15,456 B (2.95 %) |
| UART + 로그 | 68,160 B (13.03 %) |

늘어난 53 KB 중 가장 큰 것은 **`HAL_RCCEx_PeriphCLKConfig()` 하나(16.9 KB)** 다.
USART1 커널 클럭 소스 하나 고르려고 불렀는데, N6 의 모든 주변장치를 다루는 함수라
`--gc-sections` 로도 줄지 않는다. `UART_SetConfig` 가 BRR 계산에 쓰는
`HAL_RCCEx_GetPeriphCLKFreq()`(4.5 KB)도 들어온다.
지금은 문제없는 크기라 그대로 두었다. 줄여야 할 때는 클럭 소스 선택만 매크로로 하면
16.9 KB 는 빠진다.

그 밖에 로그 버퍼 6 KB(`buf_list` 4 KB + `buf_boot` 2 KB), RX 버퍼 1 KB 등이다.

---

## 10. 다음

- **CLI** — 참조 프로젝트의 `cli.c` 이식. `ap.c` 의 임시 에코를 대체하고,
  `log.c` / `uart.c` 의 `cliAdd()` 명령(`log`, `uart`)도 되살린다
- 수신 오류 처리 — 지금은 NVIC 인터럽트를 켜지 않아 ORE/FE 를 처리하지 않는다.
  DMA 가 바이트마다 읽어 가므로 ORE 는 생기지 않을 것으로 보지만 확인하지 않았다
