# 10. 개발 환경 점검

> 2026-08-26 실측. 호스트: macOS 12.7.6 (Darwin 21.6.0), Apple Silicon
> 대상: NUCLEO-N657X0-Q (MB1940-C02), STM32N657X0H3Q

---

## 1. 결론 요약

| 항목 | 상태 |
|---|---|
| 컴파일러 | ✅ 추가 설치 불필요 |
| 빌드 도구 | ✅ 추가 설치 불필요 |
| 플래싱 / 서명 도구 | ⚠️ **CubeCLT 1.21.0 을 써야 함** (1.22.0 은 이 macOS 에서 실행 불가) |
| 외부 플래시 로더 | ✅ 이 보드 전용 로더 존재 |
| 디버거 연결 | ❌ **현재 실패** — 보드가 Development boot 모드가 아님 |

**추가로 설치해야 할 것은 없다.** 다만 버전 선택 두 가지와 보드 점퍼 하나를 정리해야 한다.

---

## 2. 컴파일러

```
arm-none-eabi-gcc (Arm GNU Toolchain 14.2.Rel1 (Build arm-14.52)) 14.2.1 20241119
/opt/homebrew/bin/arm-none-eabi-gcc
```

Cortex-M55 지원 확인 완료.

```bash
arm-none-eabi-gcc -mcpu=cortex-m55 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16 -O2 -c test.c
# → 컴파일 OK
```

미리 정의되는 매크로:

| 매크로 | 값 | 의미 |
|---|---|---|
| `__ARM_ARCH_8M_MAIN__` | 1 | Armv8-M Mainline |
| `__ARM_FP` | 14 | half / single / **double** 정밀도 FPU |
| `__ARM_FEATURE_MVE` | **3** | **Helium (MVE) 정수 + 부동소수 기본 활성** |
| `__ARM_FEATURE_DSP` | 1 | DSP 확장 |

> `-mcpu=cortex-m55` 는 MVE 를 기본으로 켠다. 끄려면 `-mcpu=cortex-m55+nomve`.
> STM32CubeIDE 가 생성하는 N6 프로젝트도 동일하게 `-mfloat-abi=hard -mfpu=fpv5-d16` 를 쓴다.

### 참조 프로젝트(stm32c5-ai) 대비 바뀌어야 할 플래그

| | STM32C5 (Cortex-M33) | STM32N6 (Cortex-M55) |
|---|---|---|
| `-mcpu` | `cortex-m33` | `cortex-m55` |
| `-mfpu` (컴파일) | `fpv4-sp-d16` | `fpv5-d16` |
| `-mfpu` (링크) | `fpv5-sp-d16` | `fpv5-d16` |
| `-mfloat-abi` | `hard` | `hard` |
| define | `-DSTM32C5A3xx` | `-DSTM32N657xx` |
| 링커 스크립트 | `..._FLASH.ld` | **AXISRAM2 용 (`ORIGIN = 0x34180400`)** |

> 참조 프로젝트는 컴파일과 링크의 `-mfpu` 가 서로 달랐다(`fpv4-sp-d16` / `fpv5-sp-d16`).
> N6 로 옮길 때는 양쪽을 `fpv5-d16` 로 통일한다.

---

## 3. 빌드 도구

```
cmake   4.4.2
make    GNU Make 4.4.1_st_20260330-0700
ninja   1.13.2
```

전부 사용 가능. 참조 프로젝트의 `tools/arm-none-eabi-gcc.cmake` 를 그대로 재사용할 수 있다.

---

## 4. ST 툴체인 — 버전 선택이 중요하다

설치되어 있는 CubeCLT:

| CubeCLT | CubeProgrammer | 이 macOS 에서 실행 | N6 SVD | N6 외부로더 | SigningTool |
|---|---|---|---|---|---|
| 1.16.0 | 2.17.0 | ✅ | ❌ | ❌ | ❌ |
| 1.18.0 | 2.19.0 | ✅ | ✅ | ✅ | ✅ |
| 1.21.0 | **2.22.0** | ✅ | ✅ | ✅ | ✅ |
| 1.22.0 | — | ❌ | ✅ | ✅ | ✅ |

### ⚠️ CubeCLT 1.22.0 은 쓸 수 없다

```
$ /opt/ST/STM32CubeCLT_1.22.0/STM32CubeProgrammer/bin/STM32_Programmer_CLI --version
Sorry, "STM32_Programmer_CLI" cannot be run on this version of macOS.
Qt requires macOS 13.0.0 or later, you have macOS 12.7.6.
```

Qt 6 가 macOS 13 이상을 요구한다. 호스트를 올리기 전까지는 **1.21.0 고정**.

### 사용할 경로

```bash
export STM32CLT=/opt/ST/STM32CubeCLT_1.21.0
$STM32CLT/STM32CubeProgrammer/bin/STM32_Programmer_CLI     # v2.22.0
$STM32CLT/STM32CubeProgrammer/bin/STM32_SigningTool_CLI    # v2.22.0
$STM32CLT/STLink-gdb-server/bin/ST-LINK_gdbserver
$STM32CLT/STMicroelectronics_CMSIS_SVD/STM32N657.svd
```

### N6 외부 플래시 로더

`$STM32CLT/STM32CubeProgrammer/bin/ExternalLoader/` 에 있다.

| 로더 | 용도 |
|---|---|
| **`MX25UM51245G_STM32N6570-NUCLEO.stldr`** | **이 보드의 부트 플래시 (U31)** |
| `MX66UW1G45G_STM32N6570-DK.stldr` | DK 보드용 |
| `OTP_FUSES_STM32N6xx.stldr` | OTP 퓨즈 읽기/프로그래밍 |
| `SDMMC_STM32N6570-DK.stldr` | DK 보드 SD |

보드 이름이 정확히 붙어 있는 로더가 이미 들어 있으므로 별도로 만들 필요가 없다.

### 서명 도구의 `-align`

CubeProgrammer 2.22.0 이므로 **`-align` 옵션이 필요**하다. 도구 도움말이 직접 설명한다.

```
--align -align : Align the payload to the 0x400 offset by adding padding bytes
                 at the beginning of the payload.
```

[02-fsbl-loading.md](02-fsbl-loading.md) 에서 정리한 `0x400` 헤더 규약이 도구 도움말로도 확인된다.

---

## 5. 하드웨어 연결 상태

### 인식되는 것

```
ST-LINK SN  : 0038002E3434511734313937
ST-LINK FW  : V3J15M6
Board       : NUCLEO-N657X0-Q
Voltage     : 3.29V
```

VCP: `/dev/cu.usbmodem114102` (ST-LINK 와 같은 USB Location ID `0x00114100`)

> [03-board-boot-mapping.md](03-board-boot-mapping.md) 참고 —
> 이 VCP 는 **USART1 (PE5/PE6)** 이고, BootROM 의 serial boot 인터페이스와 같은 핀이다.

### ❌ 디버그 포트가 열리지 않는다

```
$ STM32_Programmer_CLI -c port=SWD ap=1 mode=Hotplug
Error: Unable to get core ID
Error: No STM32 target found! ...

$ STM32_Programmer_CLI -c port=SWD ap=1 mode=UR
Error: Cannot connect to access port 1! ...
```

ST-LINK 는 보드를 알아보고 전압도 정상인데 코어에 붙지 못한다.
`ap=0` 으로도 동일하다.

**원인은 부트 모드다.** STM32N6 는 BootROM 이 디버그를 열어 줘야 SWD 가 붙는데,
그건 **Development boot (BOOT1 = 1)** 에서만 일어난다.
현재 보드는 Flash boot 또는 Serial boot 위치에 있고, 유효한 FSBL 도 없는 상태로 보인다.

**조치: JP2 (BOOT1) 를 `1` 쪽(V<sub>DDIO</sub> 쪽, pin 2–3)으로 옮기고 리셋한다.**
JP1 (BOOT0) 은 이 모드에서 무관하다.

```bash
# 점퍼 변경 후 재확인
export STM32CLT=/opt/ST/STM32CubeCLT_1.21.0
$STM32CLT/STM32CubeProgrammer/bin/STM32_Programmer_CLI -c port=SWD ap=1 mode=Hotplug
```

코어 ID 가 뜨면 개발 환경 준비 완료다.

---

## 6. 앞으로 필요할 수 있는 것 (지금은 아님)

| 항목 | 언제 필요한가 |
|---|---|
| CMSIS-DSP | 신호처리/필터를 MVE 로 가속할 때 |
| ST Edge AI Core / X-CUBE-AI | NPU(Neural-ART) 를 쓸 때 |
| CubeN6 Middlewares (USBX, ThreadX, FileX, lwIP) | 해당 기능을 붙일 때 — `git sparse-checkout add` 로 추가 |
| macOS 13+ | CubeCLT 1.22.0 이후 버전을 쓰고 싶을 때 |

CubeN6 는 이미 로컬에 있다 → [README.md](README.md#로컬-stm32cuben6)

---

## 7. 체크리스트

- [x] arm-none-eabi-gcc 14.2 (Cortex-M55 / MVE 확인)
- [x] cmake / make / ninja
- [x] CubeCLT 1.21.0 (Programmer 2.22.0, SigningTool 2.22.0, gdbserver, STM32N657.svd)
- [x] `MX25UM51245G_STM32N6570-NUCLEO.stldr` 외부 로더
- [x] STM32CubeN6 로컬 sparse checkout
- [x] ST-LINK V3EC 인식, VCP `/dev/cu.usbmodem114102`
- [x] **JP2 (BOOT1) = 1 로 두고 SWD 연결 확인** — Device ID `0x486`, Rev B, Cortex-M55
- [ ] VSCode `cortex-debug` 확장 (참조 프로젝트에서 쓰고 있다면 이미 설치되어 있음)

> **추가 발견** — `ST-LINK_gdbserver` 는 CubeCLT **1.18.0 (7.10.0)** 을 써야 한다.
> 1.21.0 의 7.13.0 은 보드의 ST-LINK 펌웨어 `V3J15M6` 을 거부하고 업그레이드를 요구한다.
> Programmer / SigningTool / SVD 는 1.21.0, gdbserver 만 1.18.0 으로 섞어 쓴다.
> 자세한 내용은 [11-project-skeleton.md](11-project-skeleton.md#6-빌드--적재) 참고.
