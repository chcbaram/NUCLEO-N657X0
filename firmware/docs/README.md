# NUCLEO-N657X0-Q 펌웨어 문서

STM32N657X0H3Q / MB1940-C02 보드 기준 펌웨어 개발 참고 문서.

## 현재 상태 (2026-10-02)

| | |
|---|---|
| 보드 | NUCLEO-N657X0-Q (MB1940-C02), Device ID `0x486` Rev B |
| **부트 점퍼** | **JP2(BOOT1) = 1 → Development boot.** 이 상태여야 SWD 가 붙는다 |
| 펌웨어 | `firmware/stm32n6-fw` — FSBL 골격 + LED + **UART(VCP, DMA 수신) + 로그 + CLI + BootROM 트레이스** 동작 확인 |
| 클럭 | **800 MHz overdrive** (HSI → PLL1 1600 MHz → IC1 /2, V<sub>DDCORE</sub> 0.89 V) → [22](22-cpu-800mhz.md) |
| 빌드 | 88,720 B / 511 KB (16.96%) — arm-none-eabi-gcc 15.3.1 |
| 툴 | CubeCLT 1.22.0 에서 필요한 것만 `~/ST` 에 추출 (Programmer 2.23.0 / gdbserver 7.14.0). 적재·SWD 확인 완료 |

### 바로 다시 시작하기

```bash
cd firmware/stm32n6-fw
cmake -S . -B build && cmake --build build -j20
./tools/load.sh                       # SRAM 적재 후 실행 (LD7 500ms 점멸 + VCP 115200 부팅 배너 + cli#)
```

연결이 안 되면 **JP2(BOOT1)가 1 쪽(pin 2-3)인지** 먼저 확인한다.
`load.sh` 는 VSCode launch 와 같은 방식(붙기 전에 리셋)으로 적재한다. 그래도 `Target unknown error 32` 로
실패하면 **USB 를 뽑았다 꽂은 뒤** 다시 한다 ([21](21-uart-cli.md) 10절).

```bash
export STM32CLT=~/ST/STM32CubeCLT
$STM32CLT/STM32CubeProgrammer/bin/STM32_Programmer_CLI -c port=SWD ap=1 mode=Hotplug
```

> CubeCLT 는 **pkg 를 설치하지 않는다.** 3.3 GB 중 쓰는 것은 225 MB 뿐이고,
> 설치하면 `/etc/paths` 맨 앞을 차지해 Homebrew gcc/cmake 를 가린다.
> 필요한 것만 `~/ST` 에 추출하고 버전 없는 링크로 참조한다 →
> [10-dev-environment.md](10-dev-environment.md#4-st-툴체인--pkg-를-설치하지-않고-필요한-것만-쓴다)
>
> ```bash
> ln -sfn ~/ST/STM32CubeCLT_<버전> ~/ST/STM32CubeCLT
> ```

### 다음 작업

1. 외부 NOR (XSPI2) — 여기서 `HAL_XSPI/BSEC` 재활성화, OTP `VDDIO3_HSLV` 판단 필요
2. 서명 → 플래시 기록 → Flash boot 전환 (BOOT0=0, BOOT1=0)
   - Flash boot 에서는 BootROM 이 디버그 포트를 닫는다. `bspDebugOpen()` 이 이미 다시 연다
3. FSBL / Application 분리 (LRUN 또는 XIP)
4. (보류) SWD attach 시 멈춤의 방아쇠 규명 — launch 방식으로 피했다 → [21](21-uart-cli.md) 10절

## 문서 번호 규칙

| 대역 | 성격 |
|---|---|
| `00~09` | 하드웨어 / 부팅 레퍼런스 — 데이터시트·RM·회로도에서 확정한 사실 |
| `10~19` | 개발 환경 / 프로젝트 구조 |
| `20~` | **기능별 구현 기록** — 기능 하나당 문서 하나 |

기능 문서는 "무엇을 왜 그렇게 했는지 + 막혔던 지점 + 검증 방법"을 남긴다.
코드만 봐서는 알 수 없는 것(N6 특유의 제약, OTP, 부트 규약, 툴 버전 이슈)이 대상이다.

## 목차

### 레퍼런스

| 문서 | 내용 |
|---|---|
| [01-boot-process.md](01-boot-process.md) | 전원 인가 → BootROM 진입, 부트 모드 결정, 라이프사이클과 보안 부트 |
| [02-fsbl-loading.md](02-fsbl-loading.md) | BootROM의 FSBL 복사·인증·실행, 이미지 헤더 v2.3, 서명, ROM 트레이스 |
| [03-board-boot-mapping.md](03-board-boot-mapping.md) | MB1940 보드의 부트 점퍼·플래시·시리얼·전원·리셋·LED 결선 |

### 개발 환경

| 문서 | 내용 | 상태 |
|---|---|---|
| [10-dev-environment.md](10-dev-environment.md) | 툴체인 점검, CubeCLT 설치/경로 규칙, 보드 연결 확인 | ✅ |
| [11-project-skeleton.md](11-project-skeleton.md) | `stm32n6-fw` 디렉터리/CMake 구조, 링커·스타트업, 빌드·적재 방법 | ✅ |

### 구현 기록

| 문서 | 기능 | 상태 |
|---|---|---|
| [20-led.md](20-led.md) | LED 구동 + 빌드/적재/검증 루프 확립 | ✅ |
| [21-uart-cli.md](21-uart-cli.md) | UART(VCP) + 로그 + CLI, **DMA 수신과 D-캐시**, 로그 링 버퍼 | ✅ |
| [22-cpu-800mhz.md](22-cpu-800mhz.md) | CPU 800 MHz (overdrive), 실측 793 MHz | ✅ |
| [23-bootrom-trace.md](23-bootrom-trace.md) | BootROM 트레이스 파서 (직접 작성, 라이선스 이유) | ✅ |
| `24-flash-boot.md` | 서명 → 외부 NOR 기록 → Flash boot 전환 | 예정 |
| `25-app-split.md` | FSBL / Application 분리 (LRUN 또는 XIP) | 예정 |

## 그림

| 그림 | 설명 |
|---|---|
| [boot-sequence.svg](images/boot-sequence.svg) | 전원 인가 → BootROM → FSBL → App 전체 시퀀스 |
| [memory-map.svg](images/memory-map.svg) | 부팅 관련 메모리 맵 |
| [axisram2-layout.svg](images/axisram2-layout.svg) | AXISRAM2 내 ROM 작업영역과 FSBL download buffer |
| [fsbl-copy.svg](images/fsbl-copy.svg) | 외부 NOR → SRAM 복사 구조 |
| [rom-flow-tree.svg](images/rom-flow-tree.svg) | BootROM 내부 실행 순서와 분기 |
| [fsbl-image-layout.svg](images/fsbl-image-layout.svg) | STM32 이미지 헤더 v2.3 필드 |
| [board-boot-config.svg](images/board-boot-config.svg) | MB1940 보드 부트 결선 |

> 그림은 SVG다. 코드블록 ASCII 아트는 한글이 2칸 폭이라 정렬이 깨지므로 다이어그램은 전부 이미지로 둔다.

## 출처

문서 내 모든 주소·비트필드는 아래 1차 자료에서 직접 확인한 값이다. 추정한 부분은 본문에 명시했다.

| 약칭 | 문서 | 비고 |
|---|---|---|
| RM0486 | STM32N6x5/N6x7 Reference Manual, Rev 4 | 4669 p |
| DS14791 | STM32N657X0 Datasheet, Rev 10 | |
| UM3234 | How to proceed with boot ROM on STM32N6 MCUs, Rev 5 | BootROM 동작의 1차 자료 |
| 회로도 | `hardware/mb1940-n657x0q-c02-schematic.pdf` | MB1940-C02, 2024-05-16 |
| CubeN6 | [STM32CubeN6](https://github.com/STMicroelectronics/STM32CubeN6) | 로컬 `~/hdd/git/STM32CubeN6` |

### 로컬 STM32CubeN6

sparse checkout 으로 이 보드에 필요한 것만 받아 두었다.

```bash
cd ~/hdd/git/STM32CubeN6
git sparse-checkout list
#   Drivers/BSP/Components
#   Drivers/BSP/STM32N6xx_Nucleo
#   Drivers/CMSIS
#   Drivers/STM32N6xx_HAL_Driver
#   Middlewares/ST/STM32_ExtMem_Manager
#   Projects/NUCLEO-N657X0-Q
#   Utilities/Common
```

필요한 경로가 더 생기면 `git sparse-checkout add <path>` 후 해당 서브모듈을
`git submodule update --init --depth 1 <path>` 로 받으면 된다.
(HAL, CMSIS device, BSP, 외부 플래시 드라이버 등은 전부 별도 서브모듈이다.)

참고할 만한 시작점:

- `Projects/NUCLEO-N657X0-Q/Templates/Template_FSBL_XIP` — 앱을 외부 플래시에서 XIP
- `Projects/NUCLEO-N657X0-Q/Templates/Template_FSBL_LRUN` — 앱을 내부 RAM에 복사 후 실행
- `Projects/NUCLEO-N657X0-Q/ROT_Provisioning/BootROM` — 키 생성/프로그래밍, secure boot 활성화
- `Drivers/BSP/STM32N6xx_Nucleo/stm32n6xx_nucleo.h` — LED/버튼/XSPI 핀 정의
