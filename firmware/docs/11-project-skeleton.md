# 11. stm32n6-boot 프로젝트 골격

> `firmware/stm32n6-boot` — NUCLEO-N657X0-Q 용 FSBL 프로젝트
> 참조: `NUCLEO-C5A3ZG/firmware/stm32c5-ai` 의 레이어 구조를 그대로 따른다.

---

## 1. 전제 — 이 프로젝트는 처음부터 FSBL 이다

STM32N6 에는 내장 유저 플래시가 없다. BootROM 은 이미지를 AXISRAM2 에 적재해 실행하는 것
말고는 코드를 돌릴 방법이 없다. 따라서 **"FSBL 없는 LED 블링크" 라는 단계는 존재하지 않는다.**
처음 만드는 프로그램이 곧 FSBL 이고, 거기에 기능을 붙여 나가는 형태가 된다.

자세한 배경은 [02-fsbl-loading.md](02-fsbl-loading.md) 참고.

---

## 2. 디렉터리 구조

```
firmware/stm32n6-boot/
├── CMakeLists.txt
├── .clang-format                 참조 프로젝트에서 그대로
├── .gitignore
├── .vscode/
│   ├── tasks.json                build / load 태스크
│   ├── launch.json               cortex-debug (CubeCLT, 버전 없는 경로)
│   └── c_cpp_properties.json
├── tools/
│   ├── arm-none-eabi-gcc.cmake   툴체인 정의 (참조 프로젝트에서 그대로)
│   └── load.sh                   빌드 산출물을 SRAM 에 적재 + 실행
└── src/
    ├── main.c / main.h
    ├── ap/                       애플리케이션 로직
    │   ├── ap.c / ap.h / ap_def.h
    ├── bsp/                      보드/칩 초기화
    │   ├── bsp.c / bsp.h         클럭, 캐시, delay, Error_Handler
    │   ├── device/               ST 제공 파일 (hal_conf, it, system, syscalls)
    │   ├── ldscript/             STM32N657XX_AXISRAM2_fsbl.ld
    │   └── startup/              startup_stm32n657xx_fsbl.s
    ├── common/                   칩 비의존 공통 코드
    │   ├── def.h / err_code.h / evt_code.h
    │   ├── core/                 qbuffer, util_core
    │   └── hw/include/           드라이버 공개 헤더 (led.h ...)
    ├── hw/                       드라이버 계층
    │   ├── hw.c / hw.h / hw_def.h
    │   └── driver/               led.c ...
    └── lib/ST/                   벤더 소스 (vendored)
        ├── CMSIS/Include
        ├── CMSIS/Device/ST/STM32N6xx/Include
        └── STM32N6xx_HAL_Driver/{Inc,Src}
```

호출 흐름은 참조 프로젝트와 동일하다.

```
main() -> bspInit()   칩 레벨 초기화 (클럭/캐시)
       -> hwInit()    드라이버 초기화 (led ...)
       -> apInit()
       -> apMain()    무한 루프
```

`hw_def.h` 의 `_USE_HW_xxx` / `HW_xxx_MAX_CH` 매크로로 드라이버를 켜고 끄는 방식도 그대로다.

---

## 3. 반입한 벤더 소스

`~/hdd/git/STM32CubeN6` 에서 필요한 것만 복사했다. (총 31 MB)

| 대상 | 원본 |
|---|---|
| `src/lib/ST/CMSIS/Include` | `Drivers/CMSIS/Core/Include` |
| `src/lib/ST/CMSIS/Device/ST/STM32N6xx/Include` | 동명 경로 (Templates 제외) |
| `src/lib/ST/STM32N6xx_HAL_Driver/{Inc,Src}` | 동명 경로 |
| `src/bsp/startup/startup_stm32n657xx_fsbl.s` | `CMSIS/.../Source/Templates/gcc/` |
| `src/bsp/device/system_stm32n6xx_fsbl.c` | `CMSIS/.../Source/Templates/` |
| `src/bsp/device/stm32n6xx_hal_conf.h` | `Templates/Template_FSBL_LRUN/FSBL/Inc/` |
| `src/bsp/device/stm32n6xx_it.{c,h}` | 〃 |
| `src/bsp/device/{syscalls,sysmem}.c` | `Template_FSBL_LRUN/STM32CubeIDE/Boot/Src/` |
| `src/bsp/ldscript/STM32N657XX_AXISRAM2_fsbl.ld` | `Template_FSBL_LRUN/STM32CubeIDE/Boot/` |

`_fsbl` 접미사가 붙은 startup / system 파일을 써야 한다.
일반 버전은 하드웨어 리셋으로 진입하는 것을 전제로 하고 있어서 FSBL 에는 맞지 않는다.

---

## 4. 링커 스크립트

ST 제공본을 그대로 쓴다.

```
MEMORY
{
  RAM (xrw) : ORIGIN = 0x34180400,   LENGTH = 511K
}
```

`0x34180000` 이 헤더(0x400), `0x34180400` 부터가 코드다. 빌드 결과 확인:

```
$ arm-none-eabi-objdump -h build/stm32n6-boot.elf
  0 .isr_vector   0000034c  34180400  34180400
  1 .text         00002cb8  34180750  34180750

$ arm-none-eabi-objdump -s -j .isr_vector build/stm32n6-boot.elf
 34180400 00002034 21111834 ...
          MSP=0x34200000  Reset=0x34181121
```

MSP 가 `0x34200000` (AXISRAM2 끝) 인 것은 링커의 `_estack = ORIGIN(RAM) + LENGTH(RAM)` 결과다.

### Reset_Handler 가 MSP 를 직접 세팅한다

`startup_stm32n657xx_fsbl.s` 는 진입 직후 스스로 SP 를 설정한다.

```asm
Reset_Handler:
  ldr   r0, =_sstack
  msr   MSPLIM, r0
  ldr   r0, =_estack
  mov   sp, r0
  bl    SystemInit
  ...
```

하드웨어 리셋이 아니라 ROM 이 점프해 들어오는 구조이기 때문이다.
(하드웨어 리셋이라면 코어가 벡터[0] 에서 MSP 를 자동으로 읽는다.)

---

## 5. CMake 구성에서 N6 특이사항 3가지

### 5.1 `-mcmse` 가 필요하다

FSBL 은 secure state 로 실행된다. `system_stm32n6xx_fsbl.c` 가 `SAU`, `SCB_NS` 를 참조하는데,
CMSIS core 헤더는 이 심볼들을 `__ARM_FEATURE_CMSE == 3` 일 때만 노출한다.
그 매크로는 `-mcmse` 로 켜진다.

```
error: 'SAU' undeclared
error: 'SCB_NS' undeclared
```

컴파일·링크 옵션 양쪽에 `-mcmse` 를 넣어야 한다. STM32CubeIDE 가 생성하는 N6 FSBL 프로젝트도 같다.

### 5.2 HAL 소스를 glob 하면 안 된다

`Src/*.c` 를 통째로 glob 하면 비활성 모듈의 소스까지 잡혀 빌드가 깨진다.

```
stm32n6xx_ll_dlyb.c:139: error: request for member 'Units' in something not a structure or union
```

`DLYB` 는 SDMMC/XSPI 모듈이 켜져 있을 때만 타입이 정의된다.
그래서 **`hal_conf.h` 의 `HAL_xxx_MODULE_ENABLED` 와 짝을 맞춰 소스를 명시 목록으로 관리**한다.
기능을 추가할 때 두 곳을 같이 늘리면 된다.

현재 활성 모듈 (LED 단계):

```
GPIO  EXTI  DMA  RCC  PWR  CORTEX
```

`BSEC`, `XSPI` 는 템플릿에서 켜져 있던 것을 껐다. 외부 플래시를 붙일 때 다시 켠다.

### 5.3 컴파일 플래그

```cmake
-mcpu=cortex-m55 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -mcmse
-DSTM32N657xx -DUSE_HAL_DRIVER -DUSE_FULL_ASSERT
```

C5(M33) 프로젝트에서 바뀐 부분은 [10-dev-environment.md](10-dev-environment.md#2-컴파일러) 참고.
`-mcpu=cortex-m55` 는 Helium(MVE) 을 기본으로 켠다.

---

## 6. 빌드 / 적재

```bash
cd firmware/stm32n6-boot

# 빌드
cmake -S . -B build && cmake --build build -j20

# SRAM 적재 + 실행
./tools/load.sh
```

VSCode 에서는 `build-build`(기본 빌드) / `load-sram` 태스크, `Debug FSBL (SRAM)` 런치 구성을 쓴다.

### ⚠️ 적재는 gdb 로 한다

`STM32_Programmer_CLI` 로도 적재 자체는 되지만,

```bash
STM32_Programmer_CLI -c port=SWD ap=1 mode=Hotplug -w build/stm32n6-boot.elf -s 0x34180400
```

**`-c` 로 연결할 때마다 software reset 이 걸린다.** 그러면 BootROM 으로 되돌아가서
적재해 둔 이미지의 실행이 멈춘다. 실제로 적재 직후 GPIO 레지스터를 읽어 확인하려 했더니
BootROM 이 남긴 값(PG10 open-drain)만 보였다.

문제는 **적재한 뒤에** 리셋되는 것이다. 그래서 `tools/load.sh` 는 **ST-LINK gdbserver +
arm-none-eabi-gdb** 로 붙어 `load` 한 뒤, 벡터테이블에서 SP/PC 를 직접 세팅하고 리셋 없이 놓는다.

#### gdbserver 는 launch 방식으로 띄운다 (`--attach` 쓰지 않음)

```bash
ST-LINK_gdbserver -p 61234 -l 1 -d --halt -m 1 -cp <CubeProgrammer/bin>
```

VSCode cortex-debug 의 launch 와 같은 옵션이다. 붙는 순간 **리셋하고 BootROM 에서 세운다.**
그래서 이전 펌웨어의 상태를 하나도 물려받지 않고, **적재보다 먼저** 리셋되니 위 문제와도 무관하다.

| 붙은 직후 | 예전 `-k --attach` | **지금 launch (`--halt`)** |
|---|---|---|
| PC | 돌던 펌웨어 안 (`uartAvailable`) | **BootROM (`0x1800_3A1A`)** |
| `CCR` (캐시) | `0x30201` — I/D 캐시 켜짐 | `0x201` — 꺼짐 |
| `MPU_CTRL` | `0x5` — 이전 설정 살아 있음 | `0x4` — 꺼짐 |

예전 `-k --attach` 는 이름과 달리 리셋하지 않고 돌고 있는 코어에 그대로 붙었는데, UART 를 붙인
뒤로 **가끔 그 순간 보드가 멈춰 전원 재인가가 필요했다** ([21](21-uart-cli.md) 10절).
VSCode launch 로는 한 번도 그런 일이 없었다는 관찰에서 옵션 차이를 찾았다.

```gdb
load
set $sp = *(unsigned int*)0x34180400
set $pc = *(unsigned int*)0x34180404
detach
```

### CubeCLT 경로에는 버전을 박지 않는다

처음에는 여러 버전을 섞어 두고 `1.21.0` 으로 고정했다 (1.22.0 이 Qt6 때문에 macOS 12 에서
실행되지 않아서였다). **호스트를 macOS 27 로 올려 그 제약이 없어졌으므로 최신 하나만 쓴다.**

게다가 pkg 는 3.3 GB 인데 쓰는 것은 225 MB 뿐이다 (gcc / cmake / make / ninja 가 전부
이미 있는 것과 중복). 그래서 **macOS 에서는 pkg 를 설치하지 않고 필요한 것만
`~/ST` 에 추출해서 버전 없는 링크로 참조한다.** 추출 방법은
[10-dev-environment.md](10-dev-environment.md#4-st-툴체인--pkg-를-설치하지-않고-필요한-것만-쓴다) 참고.

```bash
ln -sfn ~/ST/STM32CubeCLT_<버전> ~/ST/STM32CubeCLT      # sudo 불필요
```

| 참조하는 곳 | 어떻게 찾는가 |
|---|---|
| `tools/load.sh` | `$CLT` → `~/ST` 링크 → `~/ST` 최신 → `/opt/ST` 링크 → `/opt/ST` 최신 (자동) |
| `.vscode/launch.json` | `${userHome}/ST/STM32CubeCLT/...` (JSON 이라 글롭 불가 → **링크 필요**) |

Windows 는 설치 프로그램으로 `C:\ST\STM32CubeCLT_<버전>` 에 설치해서 쓴다.

#### ⚠️ make 도 같은 함정이 있다

CubeCLT 는 자기 `Make/bin/make` 를 PATH 에 올린다. CMake 가 그걸 잡으면
`CMakeCache.txt` 에 `/opt/ST/STM32CubeCLT_<버전>/Make/bin/make` 가 박히고,
CubeCLT 를 갈아끼운 순간 빌드가 깨진다.

```
CMake Error: Generator: build tool execution failed,
command was: /opt/ST/STM32CubeCLT_1.22.0/Make/bin/make -f Makefile -j8
```

재구성해도 캐시된 값이 그대로 쓰이므로 안 풀린다. `rm -rf build` 가 필요하다.
재발을 막으려고 `tools/arm-none-eabi-gcc.cmake` 의 탐색 힌트에 `/usr/bin` 을 먼저 넣어
시스템 make 를 잡게 했다.

#### ST-LINK 펌웨어 요구사항

gdbserver 는 구형 ST-LINK 펌웨어를 거부한다.

```
Error in initializing ST-LINK device.
Reason: ST-LINK firmware upgrade required.
```

보드 출고 펌웨어가 `V3J15M6` 이었고, 아래로 **`V3J17M10`** 까지 올려서 해결했다.

```bash
$STM32CLT/STLinkUpgrade.sh      # 번들 jre 필요
#   Firmware version detected: V3J15M6
#   Upgrade is successful.
#   Version read: V3.J17.M10.B0.S0.P0
```

업그레이드 후 `-c port=SWD ap=1 mode=Hotplug` 연결, gdb 적재, LED 토글까지 재확인했다.
최신 gdbserver 가 `V3J17M10` 마저 거부하면 같은 스크립트로 한 번 더 올린다.

---

## 7. 의도적으로 넣지 않은 것

### OTP 퓨즈 프로그래밍

ST 템플릿의 `main.c` 에는 `OTP_Config()` 가 있고, `OTP124 bit15 (VDDIO3_HSLV)` 를 blow 한다.
외부 플래시 전송속도를 위한 설정이다.

**이건 되돌릴 수 없으므로 지금 단계에서는 넣지 않았다.**
외부 플래시(XSPI2)를 실제로 붙이는 단계에서 별도로 판단한다.

### RTOS / 로그 / CLI

참조 프로젝트에는 FreeRTOS, lwIP, USB, CLI 가 들어 있지만 여기서는 전부 제외했다.
UART 를 붙이는 단계에서 로그/CLI 부터 순서대로 올린다.

---

## 8. 현재 빌드 크기

arm-none-eabi-gcc 15.3.1 / 2026-10-01 재빌드 기준.

```
Memory region     Used Size  Region Size  %age Used
         RAM:       15456 B       511 KB      2.95%

   text    data     bss     dec
  12812      20    2596   15428
```

> gcc 14.2 로 빌드했을 때는 `text 12820 / dec 15436` 이었다. 8 B 차이는 컴파일러
> 버전 차이일 뿐이고 링커 리전 사용량은 같다.
