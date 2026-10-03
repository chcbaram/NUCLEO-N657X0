# 21. UART (VCP) + 로그 + CLI

> ST-LINK VCP 로 부팅 배너와 로그를 내보내고, **수신은 circular DMA** 로 받는다.
> 이 문서의 핵심은 **D-캐시가 켜진 상태에서 DMA 와 메모리를 공유하는 법**이다.
> 그 위에 참조 프로젝트의 CLI 와, 시간순으로 출력되는 로그 링 버퍼를 얹었다.
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
| `src/hw/driver/log.c` | **링 버퍼 판**(`nu54v-dk`) 이식 (8절). `_write` 를 여기서 로그 채널로 보낸다 |
| `src/common/hw/include/cli.h`, `src/common/hw/src/cli.c` | 참조 프로젝트와 동일 (7절) |
| `src/hw/hw.c` | 부팅 배너 |
| `src/ap/ap.c` | `cliOpen()` + 메인 루프에서 `cliMain()` |
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

## 7. CLI

참조 프로젝트의 `cli.h` / `cli.c` 를 **그대로** 가져왔다. 의존하는 것은 `uart.h` 와 `delay()` 뿐이다.
`uart.c` / `log.c` 에서 빼 두었던 `uart`, `log` 명령도 참조 것 그대로 되살렸다.

참조 프로젝트는 CLI 를 RTOS 스레드(모듈)로 돌리지만 여기는 RTOS 가 없어서 `ap.c` 에서 직접 부른다.

```c
void apInit(void)
{
  cliOpen(HW_UART_CH_CLI, 115200);
}

void apMain(void)
{
  logBoot(false);          // 여기까지가 부팅 로그
  while (1)
  {
    ...                    // LED
    cliMain();
  }
}
```

| 명령 | 내용 |
|---|---|
| `help` | 명령 목록 |
| `md` | 메모리 덤프 |
| `uart info` / `uart test <ch>` | 채널 정보 / 다른 채널과 주고받기 |
| `log info` / `log boot` / `log list` | 버퍼 상태 / 부팅 로그 / 전체 로그 |

RAM 이 68 KB → 88 KB 로 늘었다. 그중 약 9 KB 는 `cliArgsGetFloat()` 가 쓰는 `strtof` 와
newlib 의 큰 수 연산(`_strtod_l`, `__gethex`, `__multiply` …)이다.

---

## 8. 로그 링 버퍼

참조 프로젝트(`stm32c5-ai`)의 `log.c` 는 버퍼 끝에 닿으면 **0 번지로 점프해 덮어쓰고**,
`log list` 는 0 번지부터 출력한다. 그래서 한 번 넘친 뒤에는 **최신 로그가 맨 위**에 나오고
경계에 반쯤 덮인 줄이 깨진 채 섞인다.

이미 고친 판이 `nu54v-dk` 프로젝트에 있어서 그것을 이식했다 (Zephyr 전용 ISR 처리만 뺐다).

| | 참조 원본 | 이식한 판 |
|---|---|---|
| 쓰기 | 넘치면 0 으로 점프 → 중간에 빈 구간 | **바이트 단위 모듈로** — 빈 구간 없음 |
| 출력 | 0 번지부터 | `logBufDump()` — **오래된 것 → 최신**. 넘친 경우 맨 앞의 잘린 줄은 건너뛴다. boot / list 공용 |
| 줄 머리 | `%04X\t` (줄 번호) | `[   12.345]\t` — 부팅 후 초.밀리초 (RTC 가 있으면 시각). 줄 번호는 뺐다 |
| `logPrintf` | `vsnprintf` 반환값을 그대로 씀 | 버퍼 크기로 자른다 |

마지막 줄은 원본의 **버그**였다. `vsnprintf` 는 잘리기 전 길이를 돌려주므로 255 자를 넘는
로그면 `uartWrite` 가 `print_buf` 밖까지 읽었다.

### 검증

list 버퍼만 잠깐 256 B 로 줄여 부팅 로그(337 B)로 넘치게 만들었다.

```
log boot   (2048 B, 안 넘침)          log list   (256 B, 넘침)
0000 [    0.002]  [ Firmware Begin...  0002 [    0.007]  Booting..Ver
0001 [    0.004]  Booting..Name        0003 [    0.009]  Booting..Clock
...                                    ...
0007 [    0.017]                       0007 [    0.017]
```

넘친 쪽은 `buf_length 256`(가득 참)이고, 반쯤 덮인 `0001` 줄을 건너뛰어 `0002` 부터
시간순으로 나온다.

이 확인은 줄 번호가 있던 판으로 했다. 그 뒤 타임스탬프가 있으니 줄 번호는 빼기로 했다
(`log info` 의 `line_index` 카운터는 남아 있다). 출력 순서는 버퍼 안 위치로 정해지므로
번호가 없어도 같다. 다만 같은 밀리초에 찍힌 줄끼리는 구분할 수 없고, 덮여 사라진 줄을
번호 간격으로 알아챌 수는 없다.

---

## 9. 검증

모두 전원을 재인가한 깨끗한 상태에서 다시 확인했다.

| 항목 | 방법 | 결과 |
|---|---|---|
| 빌드 | `cmake --build build -j20` | 경고 없음 |
| 부팅 배너 | `load.sh` 적재 후 baram-term 으로 수신 | ✅ |
| 짧은 문자열 에코 | `hello N6\r\n` | ✅ |
| **링 버퍼 여러 바퀴** | 바이너리 200 B × 15 = 3000 B (1024 B 버퍼 2.9 바퀴) | ✅ 한 바이트도 안 틀림 |
| 캐시 처리의 필요성 | MPU 를 빼고 같은 테스트 | ❌ 수신 정지 (4절) |
| CLI | baram-term 으로 `help`, `uart info`, `log info` | ✅ |
| 로그 순서 | list 256 B 로 넘치게 한 뒤 `log list` | ✅ 오래된 것 → 최신 (8절) |

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

## 10. 막혔던 지점

| 증상 | 원인 | 해결 |
|---|---|---|
| 시리얼에 아무것도 안 들어옴 | 테스트 스크립트(pyserial)와 baram-term 이 같은 포트를 동시에 열었다. macOS 는 둘 다 열리지만 수신 데이터를 나눠 가져간다 | baram-term 이 열려 있으면 `baram-ctl` 로만 주고받는다. 바이너리 테스트처럼 직접 열어야 하면 `baram-ctl release` → 테스트 → `resume` |
| 배너는 나왔는데 에코가 안 됨 | gdb 로 `apMain` 브레이크포인트에서 세운 채 세션을 끝내서 코어가 에코 루프에 들어가지 못했다 | 동작 확인은 `load.sh`(detach 후 실행)로 한다 |
| **돌고 있는 펌웨어에 SWD 로 붙으면 가끔 보드가 멈춤** (`Target unknown error 32` / `Unable to get core ID`, 그 순간 펌웨어도 멈춤) | **해결 → [27](27-swd-attach.md)**. ST 템플릿 `SystemInit()` 이 SYSCFG 클럭을 끄고 `INITSVTORCR` 를 RAM 으로 바꾼 것. 아래 조사 기록은 당시 것이다 | **`load.sh` 를 cortex-debug 와 같은 launch 방식(`--halt`, `--attach` 없음)으로 바꿨다** — 붙는 순간 리셋하고 BootROM 에서 세우므로 돌고 있는 코어에 붙지 않는다. 이미 멈췄다면 USB 재연결 |
| `gdb` 가 엉뚱한 값을 찍음 | 타깃 연결에 실패했는데 `-batch` 가 ELF 의 초기값을 그대로 출력했다 | 출력에 `could not connect` 가 있으면 값을 믿지 않는다 |

### SWD attach 시 멈춤 — 조사 기록 (당시, 원인은 [27](27-swd-attach.md))

실패한 다섯 번은 모두 **돌고 있는 펌웨어에 디버거가 붙는 순간**이었다. 그 뒤로는 SWD 가
코어(AP1)에 닿지 못하고 펌웨어도 멈춘다. 하나씩 걸러낸 것:

| 가설 | 판정 | 근거 |
|---|---|---|
| 펌웨어가 시간이 지나면 스스로 멈춘다 | 아님 | 적재 후 6.5 분 동안 30 초마다 CLI 응답 정상 |
| 돌고 있는 펌웨어 위에 적재하면 항상 깨진다 | 아님 | 전원 재인가 후 10 번 연속 성공 (다른 이미지로 바꿔 올린 경우 포함) |
| BSEC 가 디버그를 잠근다 | 아님 | FSBL 진입 때 이미 `DBGCR=0xB451B400`, `AP_UNLOCK=0xB4` (BootROM 이 DEV 모드에서 연 값). 다시 쓰는 빌드와 안 쓰는 빌드 모두 5/5 통과 |
| 이전 펌웨어의 캐시·MPU 를 물려받아 옛 코드가 실행된다 | 원인은 아닌 듯 | 물려받는 것은 사실이다 — 붙은 직후 `CCR=0x30201`(I/D 캐시 켜짐), `MPU_CTRL=0x5`. gdbserver `-k --attach` 는 실제로 리셋하지 않고, CMSIS `SCB_EnableI/DCache()` 는 이미 켜져 있으면 무효화하지 않는다. 그래도 위 10 번은 깨지지 않았다 |

**LED 펌웨어 때는 같은 방식으로 수십 번 적재해도 한 번도 깨지지 않았다.** 처음 깨진 것은 UART 를
붙인 뒤이고, 800 MHz 는 그보다 나중이라 후보에서 빠진다. 그래서 UART 단계에서 들어간 것 중 하나로 본다.

| UART 단계에서 들어간 것 | 디버거와 엮일 수 있는 점 |
|---|---|
| GPDMA1 CH0 circular (linked-list) | 멈추지 않고 계속 도는 버스 마스터 |
| 메인 루프의 DMA 폴링 (`uartAvailable` → `CBR1` 읽기) | CPU 가 주변장치 레지스터를 쉬지 않고 읽는다 |
| MPU + `.noncacheable` 구역 | 이전 펌웨어의 설정이 그대로 남는다 |
| GPIO / DMA secure 속성 (`ConfigPinAttributes`, `ConfigChannelAttributes`) | 보안 속성 변경 |

실패한 경우에만 있었던 조건 (다음에 하나씩 재현해 볼 것):

1. **Programmer(Hotplug)** 로 돌고 있는 펌웨어에 붙음 — 성공한 10 번은 전부 `load.sh`(gdbserver)였다
2. gdb 세션이 코어를 **브레이크포인트에서 멈춘 채 강제 종료**된 뒤 다음 연결
3. **오래 돌았거나 UART 트래픽이 많았던 뒤** (10 분 방치, 3000 B 에코 테스트 뒤)

#### 해결 — attach 하지 않고 launch 한다

사용자가 **VSCode launch 로는 항상 동작했다**고 알려 줘서 옵션을 비교했다. cortex-debug 는
`--attach` 없이 `--halt` 로 띄운다. 같은 옵션으로 붙어 보면 PC 가 BootROM(`0x1800_3A1A`)에 있고
캐시(`CCR=0x201`)·MPU(`MPU_CTRL=0x4`)가 꺼져 있다. 즉 **붙기 전에 리셋한다.** `load.sh` 를 이
방식으로 바꿨다 ([11](11-project-skeleton.md) 6절). 돌고 있는 코어에 붙는 동작 자체가 없어지므로
방아쇠를 피한다. 바꾼 뒤 돌고 있는 펌웨어 위 적재 5/5 통과.

방아쇠가 attach 안의 무엇인지(위 후보 중 무엇과 엮이는지)는 밝히지 못했다. 피해 가는 방법을 찾은
것이지 원인을 고친 것은 아니다.

`bsp.c` 의 `bspDebugOpen()`(BSEC 로 디버그 포트 열기)은 이 문제와 무관하지만 **Flash boot 에서
디버거를 붙이려면 필요해서** 남겼다. BootROM 은 Flash boot 에서 디버그를 닫은 채 넘긴다
([ST 커뮤니티](https://community.st.com/t5/stm32-mcus-products/how-to-allow-debugger-to-attach-on-stm32n6-when-booting-from/td-p/828077)).

---

## 11. 크기

| | RAM 사용 |
|---|---|
| LED 단계 | 15,456 B (2.95 %) |
| UART + 로그 | 68,160 B (13.03 %) |
| + CLI, 로그 링 버퍼 | 88,720 B (16.96 %) |

늘어난 53 KB 중 가장 큰 것은 **`HAL_RCCEx_PeriphCLKConfig()` 하나(16.9 KB)** 다.
USART1 커널 클럭 소스 하나 고르려고 불렀는데, N6 의 모든 주변장치를 다루는 함수라
`--gc-sections` 로도 줄지 않는다. `UART_SetConfig` 가 BRR 계산에 쓰는
`HAL_RCCEx_GetPeriphCLKFreq()`(4.5 KB)도 들어온다.
지금은 문제없는 크기라 그대로 두었다. 줄여야 할 때는 클럭 소스 선택만 매크로로 하면
16.9 KB 는 빠진다.

그 밖에 로그 버퍼 6 KB(`buf_list` 4 KB + `buf_boot` 2 KB), RX 버퍼 1 KB 등이다.

---

## 12. 다음

- 수신 오류 처리 — 지금은 NVIC 인터럽트를 켜지 않아 ORE/FE 를 처리하지 않는다.
  DMA 가 바이트마다 읽어 가므로 ORE 는 생기지 않을 것으로 보지만 확인하지 않았다
- **SWD attach 시 멈춤** (10 절) — `load.sh` 를 launch 방식으로 바꿔 피했다. 방아쇠가 무엇인지는
  미확정. 다시 attach 가 필요해지면(돌고 있는 상태를 그대로 보고 싶을 때) 남은 조건을 재현해 찾는다