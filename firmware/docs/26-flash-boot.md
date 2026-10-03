# 26. 서명 → 외부 NOR 기록 → Flash boot

> FSBL 을 키 없이 서명해서 외부 NOR(0x70000000) 에 쓰고, BOOT0=0 / BOOT1=0 으로 부팅했다.
> 쓰기에는 이 저장소에 새로 만든 **외부 로더**(`firmware/stm32n6-ext-loader`)를 쓴다.
> Flash boot 에서 디버거가 붙지 않던 문제는 [27](27-swd-attach.md) 에서 풀었다.
> 관련: [02-fsbl-loading.md](02-fsbl-loading.md) (헤더, 서명), [24-xspi-nor.md](24-xspi-nor.md) (xspi 드라이버)

---

## 1. 빌드 → 기록 → 디버그

```bash
cd firmware/stm32n6-fw
cmake --build build -j20  # build/stm32n6-fw.bin 과 서명본 build/stm32n6-fw-trusted.bin (post-build)
python3 tools/flash.py    # 외부 로더로 기록·검증 → 리셋 (VSCode 태스크 flash-ext)
                          # Windows 는 python tools/flash.py. 표준 라이브러리만 쓴다
```

| 단계 | 어디서 | 명령 |
|---|---|---|
| 서명 | **빌드 post-build** (`CMakeLists.txt`) | `STM32_SigningTool_CLI -bin stm32n6-fw.bin -nk -of 0x80000000 -t fsbl -hv 2.3 -align -s -o stm32n6-fw-trusted.bin` |
| 기록 | `tools/flash.py` | `STM32_Programmer_CLI -c port=SWD ap=1 mode=Hotplug -halt -coreReg PRIMASK=1 -w32 … -el <로더>.stldr -w stm32n6-fw-trusted.bin 0x70000000 -v -hardRst` |
| 디버그 | VSCode **Flash + Attach FSBL** | `flash-ext` 태스크 → 다시 뜬 FSBL 에 `--attach` 로 붙는다 (쓰기 없음). 쓰지 않고 붙기만 하려면 **Attach FSBL** |

- `-nk` 키 없음. secure_boot 퓨즈를 태우지 않은 보드에서만 뜬다 ([01](01-boot-process.md))
- `-align` 페이로드를 0x400 에 맞춘다. 링크 주소 `0x34180400` = 다운로드 버퍼 `0x34180000` + 헤더 0x400
- 서명 도구는 결과를 읽기 전용으로 만든다. 빌드가 먼저 지우고 새로 만든다
- CMake 가 서명 도구를 못 찾으면(`$CLT`, `~/ST`, `/opt/ST`, `C:/ST`) 경고만 내고 서명을 건너뛴다
- CubeProgrammer 는 연결에 실패해도 종료 코드가 0 일 때가 있다. 스크립트는 `Download verified successfully` 문구로 성공을 판단한다
- Development boot / Flash boot 모두에서 **돌고 있는 FSBL 위에** 쓴다. 로더를 올리기 전에 코어를 세우고 PRIMASK, MPU, 캐시를 정리한다 ([27](27-swd-attach.md) 4절)
- 기존 **Debug FSBL (SRAM)** 구성은 플래시에 쓰지 않는다. 리셋 → BootROM 에서 세우고 ELF 를 AXISRAM2 에 올려 실행한다 (JP2=1 용)

### `flash.py` 인자 — 다른 프로젝트 이름으로 쓸 때

| 인자 | 기본 | |
|---|---|---|
| `--bin` | `build/stm32n6-fw-trusted.bin` | 헤더(`STM2`)가 없으면 키 없이 서명해서 `<이름>-trusted.bin` 을 만든 뒤 쓴다 |
| `--loader` | `../stm32n6-ext-loader/build/MX25UM51245G_NUCLEO-N657X0.stldr` | 기본 로더가 없으면 빌드한다 |
| `--addr` | `0x70000000` | FSBL1 자리 (FSBL2 는 `0x70040000`) |
| `--no-reset` | — | 기록 뒤 리셋하지 않는다 |

| 결과 | |
|---|---|
| 99 KB 기록 | 64 KB 블록 2 개 지우기 + 쓰기 약 1 초, 검증 0.4 초 |
| Flash boot (JP1=0, JP2=0) | `bootrom info` → `SecureBootProcess ClosedUnlocked err=0` |
| `reset reset` | Flash boot 로 다시 뜬다 ([25](25-rtc-reset.md) 에서 남긴 확인) |

> 주소 0 에 있던 공장 이미지("STM2")는 백업하지 않고 덮었다.

---

## 2. 외부 로더 — `firmware/stm32n6-ext-loader`

다른 프로젝트(stm32h7r-ext-loader)의 구조를 따랐다. 결과물은 `MX25UM51245G_NUCLEO-N657X0.stldr`.

| 파일 | 내용 |
|---|---|
| `src/ap/Dev_Inf.c` | `StorageInfo` — NOR, 0x70000000, 64 MB, 페이지 4 KB, 64 KB × 1024 |
| `src/ap/Loader_Src.c` | `Init` / `Write` / `SectorErase` / `MassErase` (직접 작성. ST 예제는 SLA0044 라 가져오지 않았다) |
| `src/bsp/bsp.c` | 클럭(CPU 400 MHz, overdrive 없음), DWT 로 만든 `HAL_GetTick` |
| `src/bsp/ldscript/stm32n6_ext_loader.ld` | Loader / SgInfo 두 세그먼트 |

**펌웨어 코드를 그대로 쓴다.** HAL / CMSIS, `xspi.c`, `led.c` 는 복사하지 않고 `../stm32n6-fw` 에서 빌드한다.
펌웨어 드라이버를 고치면 로더에도 바로 들어간다. 로그·CLI 가 없는 빌드를 위해 `log.h` 에 빈 `logPrintf` 를 두었다.

### CubeProgrammer 와의 약속

| 항목 | 내용 |
|---|---|
| 함수 | 이름으로 찾는다. 성공 1, 실패 0 |
| Read / Verify | **두지 않았다.** CubeProgrammer 가 0x70000000 을 디버그 포트로 직접 읽는다. 그래서 평소에는 XIP 상태로 두고 쓰기·지우기 동안만 빠져나온다 |
| startup | 돌지 않는다. `Init()` 이 `.bss` 를 지우고 FPU 를 켠다 |
| 인터럽트 | 없다. `HAL_InitTick` 은 아무것도 안 하고, `HAL_GetTick` 은 DWT 사이클 카운터로 만든다 |
| 캐시 / MPU | 켜지 않는다. 디버그 포트로 넣은 버퍼를 CPU 가 바로 봐야 한다 |
| `Init()` 재호출 | 매번 처음 상태로 만든다. PLL1 을 바꾸기 전에 CPU 를 HSI 로 내린다 |

### 막혔던 지점

| 증상 | 원인 | 해결 |
|---|---|---|
| `Init function fail`, xPSR 0x...03 (HardFault) | CubeProgrammer 는 **0x34180400 에 복귀용 BKPT(4 바이트)** 를 넣고, ELF 주소와 상관없이 그 뒤 0x34180404 부터 로더를 쓴다. 0x34180400 에 링크하면 코드가 4 바이트 밀려 절대 주소가 모두 어긋난다 | 링크 주소 **0x34180404** (ST 의 N6 로더도 같다). `-vb 3` 로그에서 `w ap 1 @0x34180400 : 4 bytes, Data 0x0000BE00` 로 확인 |
| 링커 스크립트만 고쳤는데 결과가 그대로 | CMake 가 링커 스크립트를 의존성으로 보지 않는다 | `LINK_DEPENDS` 추가 |
| `StorageInfo` 가 `.rodata` 로 빨려 들어감 | 먼저 맞는 규칙이 가져간다. `EXCLUDE_FILE` 은 바로 뒤 패턴 하나에만 걸린다 | `*(EXCLUDE_FILE(*Dev_Inf.c.obj) .rodata EXCLUDE_FILE(*Dev_Inf.c.obj) .rodata*)` |
| `no address assigned to the veneers output section .gnu.sgstubs` | `-mcmse` 빌드 | `.gnu.sgstubs` 출력 섹션을 둔다 |

---

## 3. 리셋 뒤 플래시 모드

플래시가 OPI DTR 인 채로 MCU 만 리셋돼도 다시 초기화된다. 두 겹이다.

1. `xspiFlashReset()` 이 DTR → STR → SPI 세 모드로 리셋 명령을 보낸다 ([24](24-xspi-nor.md))
2. 회로상 플래시 RESET#(A4) 이 D4 를 거쳐 NRST 에 물려 있다. N6 는 소프트웨어 / 워치독 리셋도 NRST 로 내보낸다(`PINRSTF` 가 늘 같이 뜬다).
   BootROM 은 SPI 로만 플래시를 읽으므로 이 하드웨어 리셋이 있어야 Flash boot 가 된다

---

## 4. 디버그 포트 (BSEC)

ST 커뮤니티 글은 Flash boot 에서 FSBL 이 `BSEC_AP_UNLOCK = 0xB4`, `BSEC_DBGCR = 0xB451B400` 을 써야 디버거가 붙는다고 한다.
`bspDebugOpen()` 이 같은 값을 쓴다. 그런데 이 보드(ClosedUnlocked)는 **Flash boot 에서도 FSBL 진입 때 이미 그 값**이었다.

| | Development boot | Flash boot |
|---|---|---|
| `DBGCR` / `AP_UNLOCK` / `HDPL` | `0xB451B400` / `0xB4` / `0x51` | 같다 |
| `RCC_MISCENR.DBGEN` | 0 | 0 |
| PA13 / PA14 | AF0 (SWD) | — |
| `DAUTHCTRL` / `DAUTHSTATUS` | `0` / `0x00FF00FF` | — |

`bootrom info` 가 BSEC 값을 함께 보여 준다.

---

## 5. Flash boot 에서 디버거가 붙지 않던 문제 — 해결

처음에는 Flash boot 에서 디버거가 붙는 순간 보드가 멈췄다 (`Unable to get core ID`, USB 재연결 필요).
BSEC 은 열려 있었고, 원인은 ST 템플릿 `SystemInit()` 의 SYSCFG 클럭 끄기와 `INITSVTORCR` 변경이었다.
조사 과정과 수정은 [27-swd-attach.md](27-swd-attach.md).
