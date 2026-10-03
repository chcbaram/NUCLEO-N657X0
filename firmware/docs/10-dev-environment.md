# 10. 개발 환경 점검

> 2026-08-26 최초 실측 / **2026-10-01 호스트 갱신 후 재실측**
> 호스트: macOS 27.0 (Darwin 27.0.0), Apple Silicon
> 대상: NUCLEO-N657X0-Q (MB1940-C02), STM32N657X0H3Q
>
> macOS 를 올리면서 CubeCLT 의 버전 제약(Qt6 / macOS 13+)이 없어졌다.
> 그래서 **CubeCLT 는 최신 버전 하나만 두고, 경로에는 버전을 박지 않는다.** (4절)

---

## 1. 결론 요약

| 항목 | 상태 |
|---|---|
| 컴파일러 | ✅ 추가 설치 불필요 |
| 빌드 도구 | ✅ 추가 설치 불필요 |
| 플래싱 / 서명 도구 | ✅ CubeCLT 1.22.0 에서 **필요한 것만 `~/ST` 에 추출** (pkg 설치 안 함) |
| 외부 플래시 로더 | ✅ 이 보드 전용 로더 존재 |
| 디버거 연결 | ✅ **JP2(BOOT1) = 1 로 해결됨** — Development boot 이어야 BootROM 이 SWD 를 열어준다 (5절) |

컴파일러와 빌드 도구는 **추가 설치가 필요 없다.** CubeCLT 도 pkg 를 설치하지 않고
필요한 파일만 뽑아 쓴다 (3.3 GB → 511 MB, sudo 불필요). 나머지는 **보드 점퍼 하나**뿐이다.

---

## 2. 컴파일러

```
arm-none-eabi-gcc (Arm GNU Toolchain 15.3.Rel1 (Build arm-15.149)) 15.3.1 20260627
/opt/homebrew/bin/arm-none-eabi-gcc
```

Cortex-M55 지원 확인 완료. (최초 기록 당시 14.2 → 현재 15.3, 플래그/매크로는 동일하다)

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
cmake   4.4.3    /opt/homebrew/bin/cmake
make    3.81     /usr/bin/make      <- 시스템 make
```

`cmake` 는 Homebrew, `make` 는 macOS 기본 것을 쓴다. 오래된 3.81 이지만 CMake 가 만드는
Makefile 에는 충분하고 병렬 빌드도 정상이다. `ninja` 는 쓰지 않는다.

### 병렬 빌드는 `-j20` 고정

```bash
cmake --build build -j20
```

호스트가 Apple M5 Max (18 코어) 라서 `-j8` 로는 절반도 안 쓴다. 코어 수를 덮는 값으로
**숫자를 박아 둔다.** 숫자를 주는 쪽을 택한 이유는 아래 두 가지다.

- OS 마다 코어 수를 세는 방법이 달라서(`sysctl -n hw.ncpu` / `nproc` /
  `$env:NUMBER_OF_PROCESSORS`), 자동으로 세려면 `tasks.json` 에 OS 분기가 생긴다.
  **Windows 와 명령을 같게 유지하려면 상수가 낫다.**
- 숫자 없는 `-j` 는 "코어 수만큼" 이 아니라 **"준비된 타깃 전부"** 다 (아래).

| jobs | clean build 시간 (현재 22 파일) |
|---|---|
| `-j1` | 3.89 s |
| `-j4` | 1.26 s |
| `-j8` | 1.04 s |
| **`-j20`** | **0.86 s** |
| `-j` (무제한) | 0.87 s |

#### 숫자 없는 `-j` 를 쓰지 않는 이유

`cmake --build` 는 숫자 없는 `-j` 를 make 에 그대로 넘긴다. make 래퍼로 실제 인자를 확인했다.

```
cmake --build b -j20        ->  make -f Makefile -j20
cmake --build b -j          ->  make -f Makefile -j        <- 숫자 없이 전달
cmake --build b --parallel  ->  make -f Makefile -j
cmake --build b             ->  make -f Makefile
```

GNU Make 는 숫자 없는 `-j` 를 **제한 없음**으로 해석한다. 파일 200 개짜리 합성 프로젝트로
(각 파일이 `stm32n6xx_hal.h` 를 포함, `-O2`) 동시 실행 수와 그 순간의 메모리 합을 재면:

| | 시간 | 최대 동시 컴파일 | 그때 메모리 합 |
|---|---|---|---|
| `-j18` | 1.48 s | 18 개 | 0.9 GB |
| `-j` (무제한) | 1.10 s | **173 개** | **9.0 GB** |

gcc 한 개가 이 프로젝트 파일을 컴파일할 때 쓰는 피크 메모리가 평균 **53 MB** 다
(HAL `rcc.c` 68 MB, `bsp.c` 56 MB). 코어는 18 개뿐이라 173 개를 띄워도 실제로 도는 것은
18 개고 나머지는 메모리만 붙잡고 기다린다. 얻는 건 0.4 초, 치르는 건 8 GB 다.
파일이 1000 개가 되면 45 GB 를 넘겨 64 GB 램도 스왑으로 밀린다.

> 참고 — 이 호스트의 한계: RAM 64 GB, `kern.maxprocperuid` 10666.
> 프로세스 수보다 메모리가 먼저 막힌다.

## 4. ST 툴체인 — pkg 를 설치하지 않고 필요한 것만 쓴다

CubeCLT 1.22.0 pkg 는 설치하면 **3.3 GB** 인데, 이 프로젝트가 쓰는 것은 **225 MB** 뿐이다.
나머지는 이미 깔려 있는 것과 중복이거나 이 보드와 무관하다.
그래서 **pkg 를 설치하지 않고 필요한 것만 `~/ST` 에 추출해서 쓴다.** (sudo 불필요)

### 무엇이 필요한가

| 항목 | 크기 | 용도 |
|---|---|---|
| `STM32CubeProgrammer/` | 200 MB | gdbserver 가 `-cp` 로 참조. `STM32_Programmer_CLI`, `STM32_SigningTool_CLI`, `ExternalLoader/*.stldr` |
| `STLink-gdb-server/` | 3 MB | `ST-LINK_gdbserver` + `libSTLinkUSBDriver` / `libusb` |
| `STMicroelectronics_CMSIS_SVD/STM32N657.svd` | 22 MB | 디버거 레지스터 뷰 |
| `jre/` | 286 MB | `STLinkUpgrade.sh` 전용 (ST-LINK 펌웨어 업그레이드). 이 맥에 java 가 없어서 같이 둔다 |

빼는 것:

| 항목 | 크기 | 이유 |
|---|---|---|
| `GNU-tools-for-STM32` | 1150 MB | Homebrew arm-none-eabi-gcc **15.3.1** 을 쓴다 (번들은 14.3.1) |
| SVD 나머지 245 개 | 1045 MB | N6 아닌 다른 패밀리 |
| `st-arm-clang` | 571 MB | 쓰지 않는다 |
| `CMake` / `Make` / `Ninja` | 37 MB | Homebrew cmake **4.4.3** / `/usr/bin/make` 를 쓴다 |

### ⚠️ pkg 를 설치하면 PATH 맨 앞을 빼앗긴다

중복이라서만 빼는 게 아니다. `postinstall` 이 `/etc/paths` 를 이렇게 다룬다.

```bash
cat /etc/paths >> /etc/clt.tmp     # 기존 내용을 ST 경로 "뒤에" 붙이고
cat /etc/clt.tmp > /etc/paths      # 통째로 덮어쓴다  → ST 경로가 맨 앞
```

ST 경로가 `/usr/bin` 은 물론 `/etc/paths.d/homebrew` 보다도 **앞**에 와서,
설치 직후 기본 PATH 상의 도구가 조용히 바뀐다.

| 명령 | 설치 전 | pkg 설치 후 |
|---|---|---|
| `arm-none-eabi-gcc` | Homebrew **15.3.1** | CubeCLT 번들 **14.3.1** |
| `cmake` | Homebrew **4.4.3** | CubeCLT 번들 **4.3** |
| `make` | `/usr/bin` **3.81** | CubeCLT 번들 **4.4.x** |

**이것이 2026-10-01 에 빌드가 깨졌던 원인이다.** 1.22.0 이 PATH 맨 앞을 차지했고 →
CMake 가 거기 있는 make 를 캐시에 박았고 → CubeCLT 를 지우자 경로가 사라졌다
([3절](#3-빌드-도구)). 추출 방식으로 쓰면 이 문제 자체가 없다.

그래도 누가 pkg 를 설치했을 때 빌드 결과가 바뀌지 않도록,
`tools/arm-none-eabi-gcc.cmake` 의 탐색 힌트에 `/opt/homebrew/bin`(컴파일러) 과
`/usr/bin`(make) 을 넣어 CMake 가 보는 것을 고정해 두었다.
ST 번들 툴체인으로 시험해 보려면 환경변수로 덮으면 된다.

```bash
ARM_TOOLCHAIN_DIR=$STM32CLT/GNU-tools-for-STM32/bin cmake -S . -B build
```

### 추출 방법 (약 4 초)

```bash
PKG=~/Downloads/stm32cubeclt_1.22.0_29188_20260626_1359-Mac-aarch64
TMP=$(mktemp -d)
pkgutil --expand "$PKG/stm32cubeclt_1.22.0_29188_20260626_1359-Mac-aarch64.pkg" "$TMP/pkg"

DST=~/ST/STM32CubeCLT_1.22.0
mkdir -p "$DST" && cd "$DST"
gunzip -c "$TMP/pkg/tmp.pkg/Payload" | cpio -idm \
  './STLink-gdb-server/*' \
  './STM32CubeProgrammer/*' \
  './STMicroelectronics_CMSIS_SVD/STM32N657.svd' \
  './jre/*' \
  './STLinkUpgrade.sh' \
  './STM32CubeCLT_metadata.sh'

ln -sfn "$DST" ~/ST/STM32CubeCLT      # 버전 없는 링크
```

`Payload` 는 gzip 된 cpio 라서 `pkgutil --expand` + `cpio` 로 원하는 경로만 뽑을 수 있다.
바이너리는 재배치 가능해서 `~/ST` 에서도 그대로 실행된다.

### Windows 에서는

Windows 용 CubeCLT 는 일반 설치 프로그램이고 `C:\ST\STM32CubeCLT_<버전>` 에 들어간다.
추출 방식(`pkgutil` / `cpio`)은 macOS pkg 전용이므로 그냥 설치해서 쓴다.
빌드 자체는 양쪽 동일하다 ([11-project-skeleton.md](11-project-skeleton.md#6-빌드--적재)).

| | macOS (이 호스트) | Windows |
|---|---|---|
| CubeCLT | 필요한 것만 `~/ST` 에 추출 | 설치 프로그램으로 설치 |
| 적재 | `./tools/load.sh` | `cortex-debug` 런치 구성 |
| 병렬 빌드 | `-j20` | `-j20` (같다) |

> `.vscode/launch.json` 에는 `osx` 섹션만 있다. Windows 에서 디버깅하려면
> `windows` 섹션에 그 호스트의 `serverpath` / `stm32cubeprogrammer` / `svdFile` 을
> 추가해야 한다. (경로를 확인한 적이 없어서 비워 두었다)

### 버전은 링크로만 참조한다

| 참조하는 곳 | 어떻게 찾는가 |
|---|---|
| `tools/load.sh` | `$CLT` → `~/ST/STM32CubeCLT` → `~/ST/STM32CubeCLT_*` 최신 → `/opt/ST/STM32CubeCLT` → `/opt/ST/STM32CubeCLT_*` 최신 (자동) |
| `.vscode/launch.json` | `${userHome}/ST/STM32CubeCLT/...` |

버전을 올릴 때는 새로 추출하고 링크만 다시 걸면 되고, 고칠 파일은 없다.

```bash
export STM32CLT=~/ST/STM32CubeCLT
$STM32CLT/STM32CubeProgrammer/bin/STM32_Programmer_CLI
$STM32CLT/STM32CubeProgrammer/bin/STM32_SigningTool_CLI
$STM32CLT/STLink-gdb-server/bin/ST-LINK_gdbserver
$STM32CLT/STMicroelectronics_CMSIS_SVD/STM32N657.svd
$STM32CLT/STLinkUpgrade.sh
```

### 실측 (2026-10-02)

| 도구 | 버전 | 비고 |
|---|---|---|
| STM32CubeProgrammer | **2.23.0** | 1.21.0 시절 2.22.0 보다 올라갔다 |
| STM32 Signing Tool | **2.23.0** | `-align` 필요 (아래) |
| ST-LINK_gdbserver | **7.14.0** | 7.13.0 → 7.14.0 |
| 번들 jre | OpenJDK 21.0.10 (Temurin) | `STLinkUpgrade.sh` 용 |

```
$ $STM32CLT/STM32CubeProgrammer/bin/STM32_Programmer_CLI -c port=SWD ap=1 mode=Hotplug
ST-LINK FW  : V3J17M10
Board       : NUCLEO-N657X0-Q
Voltage     : 3.29V
Device ID   : 0x486        Revision ID : Rev B
Device name : ST32N657     Device CPU  : Cortex-M55
```

**gdbserver 7.14.0 은 기존 ST-LINK 펌웨어 `V3J17M10` 을 그대로 받아들였다.**
추가 업그레이드 없이 `./tools/load.sh` 로 SRAM 적재·실행까지 확인했다.

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

CubeProgrammer 2.22.0 이상이면 **`-align` 옵션이 필요**하다. 도구 도움말이 직접 설명한다.

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
ST-LINK FW  : V3J17M10          <- 출고 시 V3J15M6 -> STLinkUpgrade.sh 로 갱신
Board       : NUCLEO-N657X0-Q
Voltage     : 3.29V
```

VCP: `/dev/cu.usbmodem1412302` (2026-10-02 실측. USB 포트가 바뀌면 번호도 바뀐다.
`STM32_Programmer_CLI -l` 로 확인한다. 최초 기록 당시에는 `/dev/cu.usbmodem114102` 였다)

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
export STM32CLT=~/ST/STM32CubeCLT
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

CubeN6 는 이미 로컬에 있다 → [README.md](README.md#로컬-stm32cuben6)

---

## 7. 체크리스트

- [x] arm-none-eabi-gcc 15.3 (Cortex-M55 / MVE 확인)
- [x] cmake 4.4.3 / 시스템 make 3.81
- [x] **CubeCLT 1.22.0 필요한 것만 `~/ST` 에 추출 + `~/ST/STM32CubeCLT` 링크** (225 MB + jre 286 MB)
- [x] Programmer 2.23.0 / SigningTool 2.23.0 / gdbserver 7.14.0 / STM32N657.svd 동작 확인
- [x] ST-LINK 펌웨어 `V3J17M10` (gdbserver 7.13.0 요구사항)
- [x] `MX25UM51245G_STM32N6570-NUCLEO.stldr` 외부 로더
- [x] STM32CubeN6 로컬 sparse checkout
- [x] ST-LINK V3EC 인식, VCP `/dev/cu.usbmodem114102`
- [x] **JP2 (BOOT1) = 1 로 두고 SWD 연결 확인** — Device ID `0x486`, Rev B, Cortex-M55
- [ ] VSCode `cortex-debug` 확장 (참조 프로젝트에서 쓰고 있다면 이미 설치되어 있음)

> **ST-LINK 펌웨어** — gdbserver 7.13.0 은 출고 펌웨어 `V3J15M6` 을 거부했다.
> `STLinkUpgrade.sh` 로 `V3J17M10` 까지 올려서 해결했고, 그 상태가 유지되고 있다.
> 자세한 내용은 [11-project-skeleton.md](11-project-skeleton.md#6-빌드--적재) 참고.

```bash
$STM32CLT/STLinkUpgrade.sh
```
