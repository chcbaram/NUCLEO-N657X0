# 27. 돌고 있는 FSBL 에 디버거가 붙으면 보드가 멈추던 문제

> [21](21-uart-cli.md) 10 절에서 보류했고 [26](26-flash-boot.md) 에서 Flash boot 를 막았던 문제다.
> 원인은 ST 템플릿의 `SystemInit()` 두 줄이었다. 고친 뒤 Development boot / Flash boot 모두에서
> CubeProgrammer, gdbserver 로 돌고 있는 FSBL 에 붙고, `flash.py` 로 다시 쓸 수 있다.

---

## 1. 증상

- 돌고 있는 FSBL 에 디버거가 붙는 순간 `Unable to get core ID`, **펌웨어도 멈춘다** (LED, UART CLI 정지)
- NRST(`-hardRst`, `mode=UR`) 로는 안 풀리고 **USB 를 다시 꽂아야** 풀린다
- Development boot 에서는 `load.sh` 를 launch 방식(리셋 → BootROM 에서 세움)으로 바꿔 피해 왔다.
  Flash boot 에서는 리셋하면 BootROM 이 바로 FSBL 을 띄우므로 attach 밖에 없어서 피할 수 없었다

---

## 2. 원인 — `SystemInit()` (ST 템플릿, CubeN6 v1.4.1 에도 그대로)

| 줄 | ST 의도 | 문제 |
|---|---|---|
| `RCC->APB4ENCR2 = RCC_APB4ENCR2_SYSCFGENC;` | 쓰고 난 SYSCFG 클럭을 끈다 | 디버거(CubeProgrammer) 가 연결하면서 SYSCFG 를 읽는다. 클럭이 꺼진 주변장치를 읽으면 버스 트랜잭션이 끝나지 않아 **디버그 포트까지 멈춘다** |
| `SYSCFG->INITSVTORCR = SCB->VTOR;` | Standby 복귀 때 FSBL 로 바로 오게 한다 | 리셋 뒤 첫 벡터가 BootROM(0x18000000) 대신 RAM 의 FSBL 이 된다. 디버거가 건 리셋이 BootROM 을 건너뛴다 |

**두 줄을 뺐다.** 이 프로젝트는 Standby 를 쓰지 않는다 (`src/bsp/device/system_stm32n6xx_fsbl.c`).

> 일반화: **N6 에서는 디버거가 클럭 꺼진 주변장치를 읽으면 보드 전체가 멈추고 전원 재인가로만 풀린다.**
> Errata ES0620 2.2.3(클럭 없는 RISAF 에 쓰면 디버그 연결이 끊기고 복구 안 됨), ST 커뮤니티의 ST 직원 답변
> ("디버거 연결이 끊기는 주원인은 전원·클럭이 꺼진 AXI/AHB 메모리나 주변장치 접근") 과 같은 종류다.
> 그래서 gdb 에는 **실행 중인 코드와 맞는 ELF** 를 줘야 한다. 맞지 않는 ELF 로 스택을 풀면 엉뚱한 주소를 읽다 같은 식으로 멈춘다.

---

## 3. 좁혀 간 방법

실패하면 USB 를 다시 꽂아야 해서, 성공하면 다음으로 넘어가는 시험부터 했다.
코드를 다시 빌드하지 않고 gdb 로 레지스터 쓰기만 흉내 낸 뒤 RAM 의 `b .`(0xE7FE) 로 돌려 놓고 CubeProgrammer 로 붙었다.

| 시험 | 결과 |
|---|---|
| BootROM 상태 (펌웨어 없음) | ✅ |
| gdb 세션만 열고 닫음 | ✅ |
| RAM 무한루프 | ✅ |
| `SystemInit()` 한 번 실행 | ❌ |
| 클럭 설정 없이(64 MHz) / LED 만 / `bspInit()` 없이 | ❌ (모두 `SystemInit()` 포함) |
| 보상 셀만 / RCC 묶음 / PWR 묶음 / SAU 지우기 (SYSCFG 클럭은 켜 둔 채) | ✅ |
| `INITSVTORCR` 만 (SYSCFG 클럭 켰다 **끔**) | ❌ |
| SYSCFG 클럭만 켜 두도록 고친 펌웨어 | ✅ 5/5 (`INITSVTORCR` 수정 전에는 gdb `monitor halt` 에서 멈춤) |
| 두 줄 모두 고친 펌웨어 | ✅ |

한때 의심했지만 아닌 것: BSEC(`DBGCR`/`AP_UNLOCK` 은 Flash boot 에서도 이미 열려 있다), `RCC_MISCENR.DBGEN`, `DBGMCU_CR.DBGCLKEN`,
SWD 핀(PA13/PA14 는 AF0), 캐시·MPU, 800 MHz 클럭, UART DMA.

---

## 4. 돌고 있는 FSBL 위에서 외부 로더 돌리기

붙을 수 있게 되자 다음 문제가 나왔다. CubeProgrammer 는 FSBL 을 세우고 그 자리(0x34180400~) 에 로더를 덮어쓴 뒤
`Init` 을 부르는데, **FSBL 의 코어 설정이 남아 있다**. `Init` 이 HardFault → lockup.

| 남은 설정 | 문제 | 처리 |
|---|---|---|
| `MSPLIM = 0x341FF800` (startup) | 로더 스택 0x34184904 가 하한 아래 → 첫 push 에서 STKOF. 폴트 벡터도 로더로 덮여 lockup | 로더 `Init` 을 naked 로 두고 스택을 쓰기 전에 `cpsid i`, `MSPLIM = 0` |
| 인터럽트 (SysTick 등) | 벡터 테이블이 로더로 덮여 있다 | `flash.py` 가 `-coreReg PRIMASK=1` |
| I/D 캐시, MPU | 캐시에 남은 FSBL 코드를 실행할 수 있다 | `flash.py` 가 `-w32` 로 `MPU_CTRL=0`, `CCR=0x201`, `ICIALLU` |

CubeProgrammer 는 `MSPLIM` 을 쓸 수 없다 (`-coreReg` 목록에 없고, `-w32` 로 DCRSR 을 쓰면 읽어서 비교하다 실패한다).
gdb 에서는 `$msplim` 이 없어서 DCRSR/DCRDR 로 썼다 — REGSEL `0x1C` = MSPLIM_S (읽으면 0x341FF800 이 나와 확인).

---

## 5. 검증

| | Development boot | Flash boot |
|---|---|---|
| CubeProgrammer 연결 (돌고 있는 FSBL) | ✅ 5/5 | ✅ 3/3 |
| gdbserver `--attach` + `monitor halt` | ✅ | ✅ |
| `flash.py` (돌고 있는 FSBL 위에서) | ✅ 2/2 | ✅ — 새 빌드로 부팅 확인 |
| `reset reset` 뒤 다시 연결 | — | ✅ |

> gdbserver 의 `-k` 는 "리셋 상태에서 초기화(NRST)" 다. attach 할 때 붙이지 않는다.
> `load.sh` 는 지금도 launch 방식(리셋 → BootROM 에서 세움)이 깔끔해서 그대로 둔다.
