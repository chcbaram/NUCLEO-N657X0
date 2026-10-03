# 24. 외부 NOR (XSPI2) — MX25UM51245G

> 보드 부트 플래시를 **OPI DTR 50 MHz** 로 읽고·쓰고·지우고, memory-mapped(XIP)로 **약 100 MB/s** 에 읽는다.
> 되돌릴 수 없는 HSLV 퓨즈는 **태우지 않았다.** 드라이버는 Nucleo BSP 없이 HAL 위에 직접 썼고,
> API 는 다른 프로젝트(weact-h750)의 `qspi.c` 와 같은 모양이다.
> 관련: [03-board-boot-mapping.md](03-board-boot-mapping.md) 2절 (부트 플래시), [01-boot-process.md](01-boot-process.md) (OTP)

---

## 1. 하드웨어

| 항목 | 값 |
|---|---|
| 플래시 | **MX25UM51245GXDI00** (U31) — Macronix 512 Mbit / 64 MB Octal NOR, 1.8 V |
| JEDEC ID | `C2 80 3A` (Macronix / MX25UM / 512 Mbit) |
| 컨트롤러 | XSPI2, I/O 매니저(XSPIM) **Port 2**, **NCS1** |
| 핀 (AF9) | PN0 DQS0, PN1 NCS1, PN6 CLK, PN2~5 IO0~3, PN8~11 IO4~7 |
| 핀 전원 | V<sub>DDIO3</sub> = 1.8 V (`HAL_PWREx_ConfigVddIORange(PWR_VDDIO3, PWR_VDDIO_RANGE_1V8)`) |
| memory-mapped | `0x7000_0000` ~ (`HW_XSPI_ADDR`) |
| 클럭 | IC3 = PLL1 1600 MHz ÷ 32 = **50 MHz** |

> ⚠️ **주소 0 에는 이미 이미지가 있다.** 처음 읽어 보니 `53 54 4D 32` = `"STM2"` (STM32 이미지 헤더 매직)였다.
> 공장 데모로 보인다. 테스트는 **마지막 섹터(`0x03FF_F000`)** 에서 한다.

---

## 2. HSLV 퓨즈 — 태우지 않았다

### 무엇인가

`OTP124 bit15` (`HSLV_VDDIO3`)를 태우면 V<sub>DDIO3</sub> 핀을 1.8 V 고속 모드로 쓸 수 있다.
**되돌릴 수 없다.** ST 코드는 이 퓨즈에 따라 XSPI2 클럭을 정한다.

| OTP124 bit15 | XSPI2 클럭 (ST `Template_FSBL_XIP` MSP) |
|---|---|
| 태움 | PLL1 ÷ 6 = 200 MHz |
| **안 태움** | PLL1 ÷ 24 = **50 MHz** ("High speed IO optimization is disabled, lower XSPI clock speed") |

### 1.8 V 인데 왜 속도가 달라지나

- I/O 버퍼는 **3.3 V 기준**으로 설계돼 있다. 1.8 V 를 넣으면 구동 전류가 줄어 엣지가 느려지고, 토글할 수 있는 최대 주파수가 내려간다.
- HSLV 는 버퍼를 1.8 V 용으로 바꿔 구동력을 되살린다. 대신 그 상태에서 3.3 V 가 들어오면 **핀이 손상된다.**
- 그래서 두 단계로 나눴다. 퓨즈는 "이 도메인은 영원히 1.8 V" 라는 보드 설계자의 선언이다. `PWR_SVMCR3.VDDIO3VRSEL`
  (`HAL_PWREx_ConfigVddIORange(…, 1V8)`)은 퓨즈가 있을 때만 의미가 있다 (HAL 주석: "HSLV_VDDIOx option bit must be set").
  `xspi.c` 가 이 함수를 부르지만, 퓨즈가 없으니 버퍼는 3.3 V 용 그대로라고 본다 (레지스터로 확인하지는 않았다).
- 이 보드는 회로상 V<sub>DDIO3</sub> 가 `VDDA1V8` 에 바로 연결돼 있다 (솔더 브리지 없음, 회로도 4장). 따라서 태워도 손상 위험은 없다.
  남는 문제는 되돌릴 수 없다는 점뿐이다.

### ST 예제는 기본으로 태우지 않는다

FSBL 템플릿에 퓨즈를 태우는 `OTP_Config()` 가 들어 있지만, **FSBL 프로젝트 설정(EWARM / MDK-ARM / CubeIDE)
모두에 `NO_OTP_FUSE` 가 기본 정의돼 있어** 컴파일에서 빠진다. README 는 "최대 속도가 필요하면 한 번만 빼고
돌려라" 라고 안내한다. `HAL_BSEC_OTP_Program` 을 부르는 Nucleo 예제 5 개(FSBL 프로젝트) 모두 그렇다.

### 우리 보드 상태

읽기 전용 `otp read` CLI 로 확인했다 — 공장 기본 상태다.

| OTP | 값 | 뜻 |
|---|---|---|
| OTP124 | `0x00000000` | `HSLV_VDDIO3` 안 태워짐 |
| OTP11 | `0x00000000` | `xspi_3v3` = 0 → 1.8 V 플래시와 맞음 |
| OTP16 / OTP18 | `0x00000000` | 공장 기본 / 보안 부트 안 켜짐 |

BSEC shadow 레지스터(`FVRw`)를 그냥 읽으면 shadow 되지 않는 퓨즈는 0 으로 보여 판단할 수 없다.
`HAL_BSEC_OTP_Read()` 는 **reload 뒤 읽는데, reload 는 `OTPCR` 의 `PROG`/`PPLOCK` 을 0 으로 두고 실행하는 읽기
동작**이다. `otp.c` 에는 퓨즈 쓰기 함수도 명령도 두지 않았다 (`HAL_BSEC_OTP_Program` 이 링크되지 않는 것 확인).

### 판단

**50 MHz 로 간다.** 읽기·쓰기·지우기와 Flash boot 는 다 된다 (BootROM 은 이 설정과 상관없이 플래시를 읽는다).
200 MHz 가 의미 있는 것은 앱을 플래시에서 바로 실행(XIP)할 때뿐이라, 그때 다시 판단한다.

---

## 3. 드라이버 — HAL 위에 직접 썼다

처음에는 ST Nucleo BSP(`stm32n6xx_nucleo_xspi.c`)와 부품 드라이버(`mx25um51245g.c`, 둘 다 BSD-3)를 반입했지만
제대로 동작하지 않았고(7절), **다른 메모리를 쓰게 될 때 Nucleo BSP 에 의존하고 싶지 않다**는 판단으로 걷어냈다.
`src/hw/driver/xspi.c` 하나가 컨트롤러와 플래시 명령을 다 한다. 다른 메모리로 바꿀 때는 **명령 표(`cmd_*`)와
`xspiFlash*` 함수**만 바꾸면 된다.

### 초기화 순서

1. V<sub>DDIO3</sub> 1.8 V
2. **XSPI2 커널 클럭을 50 MHz 로 직접 잡는다** — 안 잡으면 기본 소스로 너무 빠르게 돌 수 있다 (BSP 도 잡지 않았다)
3. XSPIM / XSPI2 / GPION 클럭, XSPI2 리셋, GPIO (AF9)
4. HAL 초기화 + `HAL_XSPIM_Config()` — ST DTR 예제와 같이 Port 2, NCS1, `Req2AckTime = 1`
5. **플래시 리셋을 DTR → STR → SPI 세 모드로 모두 보낸다** — 이전 실행이 플래시를 어떤 모드로 남겼는지 모르기 때문이다
6. SPI 모드에서 JEDEC ID 확인 → CR2 를 써서 **OPI DTR** 로 올리고 ID 를 다시 확인

### 세 가지 모드

모드 전환은 항상 리셋으로 SPI 로 돌아간 뒤 CR2 (`0x0000_0000`)에 쓴다. CLI `xspi mode spi:str:dtr` 로 바꿀 수 있다.

| 모드 | 명령 | CR2 | 컨트롤러 |
|---|---|---|---|
| SPI (1S-1S-1S) | 1 바이트 (`0x9F` …) | `0x00` | — |
| OPI STR (8S-8S-8S) | 2 바이트, 명령 + 반전 (`0x9F60` …) | `0x01` | — |
| **OPI DTR (8D-8D-8D)** | 2 바이트 | `0x02` | MemoryType = Macronix, DHQC 켬, 읽기에 DQS |

### dummy 사이클

| 명령 | dummy | 근거 |
|---|---|---|
| SPI 읽기 (`0x0C` FAST READ 4B) | 8 | |
| OPI 메모리 읽기 (`0xEC13` STR / `0xEE11` DTR) | **20** | 플래시 CR2 `0x300` 의 **기본값(0 = 20 사이클)을 그대로 쓴다.** 200 MHz 까지 유효해서 건드릴 필요가 없다 |
| OPI 레지스터 읽기 (RDID / RDSR / RDCR2) | **4** | ST 의 XSPI_NOR_*_DTR 예제와 같다 |

### OPI DTR 에서 레지스터는 바이트가 두 번씩 온다

DTR 로 ID 를 읽으면 `C2 C2 80 80 3A 3A` 가 온다. 6 바이트를 받아 짝수 번째만 모은다.
1 바이트 레지스터(SR, CR2)도 2 바이트를 받아 앞의 것을 쓴다.

---

## 4. XIP (memory-mapped)

`xspiSetXipMode(true)` 로 들어가고 `false` 로 나온다. **들어가는 길은 이것 하나**다
(`xspiEnableMemoryMappedMode()` 는 `static` — 직접 부르면 아래 과정이 빠져 드라이버 상태와 어긋난다).

| 단계 | 이유 |
|---|---|
| **들어가기 전에 `HAL_XSPI_Abort()`** | 컨트롤러 안의 프리페치 버퍼가 남아 있으면 지우기/쓰기 뒤 첫 읽기가 옛 내용으로 나온다. `qspi.c` (weact-h750) 에서 실기로 겪은 것을 그대로 따랐다 |
| 지금 모드의 읽기/쓰기 명령으로 READ_CFG / WRITE_CFG 설정 후 `HAL_XSPI_MemoryMapped()` | |
| `SCB_CleanInvalidateDCache()` | `0x7000_0000` 은 기본 메모리 맵에서 캐시되는 영역이라 옛 캐시 줄을 버린다 |
| 나올 때 `HAL_XSPI_Abort()` | |

XIP 중에는 `xspiRead()` 가 `memcpy` 로 읽고, 쓰기·지우기는 거부한다.

---

## 5. API 와 CLI

`qspi.c` (weact-h750) 와 같은 모양이다.

| API | 내용 |
|---|---|
| `xspiInit()` / `xspiIsInit()` | |
| `xspiReset()` | 플래시를 리셋하고 지금 모드로 다시 들어간다 |
| `xspiRead/Write(addr, p_data, length)` | 바이트 주소. 쓰기는 256 B 페이지 경계를 알아서 나눈다 |
| `xspiErase(addr, length)` | 덮는 4 KB 섹터를 지운다. 64 KB 로 맞으면 블록 단위 |
| `xspiEraseBlock/Sector/Chip()` | 64 KB / 4 KB / 전체 (최대 460 초) |
| `xspiGetStatus()` | 준비됨(busy 아님)이면 true |
| `xspiGetInfo()` | 크기, 섹터, 페이지, JEDEC ID |
| `xspiSetXipMode()` / `xspiGetXipMode()` | XIP 들어가기/나오기 |
| `xspiGetAddr()` / `xspiGetLength()` | `0x7000_0000` / 64 MB |

| CLI | 내용 |
|---|---|
| `xspi info` | 주소, XIP 상태, JEDEC ID, 모드·클럭, SR/CR2/DC, HAL 상태 |
| `xspi test [addr]` | 섹터 하나를 지우고 쓰고, 간접 읽기와 XIP 로 검증 (기본: 마지막 섹터) |
| `xspi speed-test` | 1 MB 읽기 속도 (XIP 상태에 따라 간접 / memory-mapped) |
| `xspi xip on:off` | |
| `xspi mode spi:str:dtr` | 브링업용 |
| `xspi read/erase/write [addr] ...` | `qspi` 와 같은 형식 |

초기화 로그도 다른 프로젝트 형식을 따른다.

```
[OK] xspiInit()
     JEDEC ID : C2 80 3A
     Macronix : 64 MB, OPI DTR 50 MHz
```

---

## 6. 검증

| 모드 | CR2 | 4 KB 지우기 / 쓰기 / 읽기 | XIP 검증 |
|---|---|---|---|
| SPI | `0x00` | 23 ms / 3.9 ms / 672 µs | ✅ |
| OPI STR | `0x01` | 22 ms / 3.5 ms / 426 µs | ✅ |
| **OPI DTR** | `0x02` | 22 ms / 3.3 ms / 426 µs | ✅ |

| `xspi speed-test` (1 MB) | 속도 |
|---|---|
| 간접 읽기 (OPI DTR) | 7.5 MB/s |
| **XIP (OPI DTR)** | **93 ~ 102 MB/s** — 50 MHz × 8 선 × DTR 이론값 100 MB/s 에 맞는다 (ms 단위 측정이라 10~11 ms 사이에서 흔들린다) |

간접 읽기는 STR 과 DTR 이 같은 속도다. 버스가 아니라 **CPU 가 HAL 로 FIFO 를 폴링해 꺼내는 쪽이 병목**이다.
많이 읽어야 하면 XIP 나 DMA 를 쓴다.

---

## 7. 막혔던 지점

| 증상 | 원인 | 해결 |
|---|---|---|
| ST BSP 로 ID 가 `80 80 3A` | 부품 드라이버의 **설정 템플릿**(`mx25um51245g_conf_template.h`) 값 `DUMMY_CYCLES_REG_OCTAL_DTR = 5`. 플래시는 4 사이클이라 한 클럭(2 바이트) 늦게 읽혀 `C2 C2` 가 빠졌다. BSP 초기화는 1 바이트 CR2 를 확인하는데, DTR 에서는 같은 바이트가 두 번 와서 밀려도 통과했다 | 4 로 바꾸니 `C2 C2 80` 이 읽혔다 |
| ST BSP 로 읽기·지우기가 5 초 뒤 타임아웃 | 템플릿 `DUMMY_CYCLES_READ_OCTAL_DTR = 6` 인데 BSP 의 `EnterDOPIMode()` 는 플래시 CR2 dummy 를 **20** 으로 쓴다. 20 으로 맞춘 뒤에도 첫 명령부터 레지스터 읽기가 실패해 원인을 더 파지 않았다 | BSP 를 걷어내고 직접 작성. 이 저장소에는 보드에 맞춘 설정 파일이 없었다 (sparse checkout) |
| (참고) BSP `XSPI_NOR_MspInit()` | 쓰는 것은 XSPI2 인데 `XSPI1_IRQn` 을 켠다 | 직접 작성하며 해당 없음 (폴링이라 인터럽트를 쓰지 않는다) |

---

## 8. 다음

- **서명 → 플래시 기록 → Flash boot** (`26-flash-boot.md`, 예정) — 주소 0 에 우리 FSBL 을 서명해서 쓰고 BOOT0=0, BOOT1=0 으로 부팅
- XIP 앱을 쓰게 되면 HSLV 퓨즈(200 MHz) 판단
- 간접 읽기를 빠르게 해야 하면 DMA
