# 29. FSBL / 앱 분리 — stm32n6-boot + stm32n6-fw

> FSBL(`firmware/stm32n6-boot`)이 부트로더를 겸하고, 앱(`firmware/stm32n6-fw`)을 외부 NOR 에서 꺼내 실행한다.
> 앱 실행 방식은 빌드 옵션 `APP_RUN` 으로 고른다 — **SRAM**(복사해서 실행, 확인함) / **XIP**(NOR 에서 그대로, 아직 미검증).
> FSBL 은 앱 이미지의 `firm_ver_t.firm_addr` 로 방식을 알아서 고르므로 FSBL 쪽 설정은 없다.
> 관련: [28](28-uart-download.md) (UART 다운로드), [25](25-rtc-reset.md) (부트 모드 / 리셋)

---

## 1. 쓰는 법

```bash
# 앱 빌드 (기본 APP_RUN=SRAM)
cd firmware/stm32n6-fw
cmake -S . -B build                 # XIP 로 하려면 -DAPP_RUN=XIP (build 를 지우고)
cmake --build build -j20            # build/stm32n6-fw.bin

# 내려받고 실행 — 앱이 돌고 있으면 툴이 FSBL 로 넘긴 뒤 쓴다 (VSCode 태스크 download-uart)
python3 ../stm32n6-boot/tools/download.py --target fw build/stm32n6-fw.bin
```

```
연결     : /dev/cu.usbmodem1412302  STM32N6-FW V260826R1  [APP]
         앱이 실행 중 → FSBL 로 넘어가 다시 붙는다
연결     : /dev/cu.usbmodem1412302  STM32N6-BOOT V260826R1  [BOOT]
  확인     0.05s  87488 B  crc 0x12AF (호스트 0x12AF) OK
실행     : 앱으로 점프
```

| 프로젝트 | 내용 |
|---|---|
| `stm32n6-boot` | FSBL. BootROM 이 AXISRAM2 에 올려 실행한다. 부팅 판정, UART 다운로드, 앱 점프 |
| `stm32n6-fw` | 앱. HAL / CMSIS 는 `../stm32n6-boot` 것을 함께 쓴다. LED 초록, CLI, cmd(INFO / 부트로더 요청) |
| `stm32n6-ext-loader` | CubeProgrammer 외부 로더. `../stm32n6-boot` 의 xspi 드라이버로 빌드 |

---

## 2. 메모리맵

![앱 실행 방식별 메모리맵](images/app-memory-map.svg)

| `APP_RUN` | 링커 스크립트 | 이미지 링크 주소 (`firm_addr`) | FSBL 이 하는 일 |
|---|---|---|---|
| **SRAM** | `stm32n657xx_sram.ld` | `0x3400_0000` (AXISRAM1) | NOR `0x7010_1000` 에서 `fw_size` 만큼 복사 → 점프 |
| XIP | `stm32n657xx_xip.ld` | `0x7010_1000` (NOR) | XSPI 를 memory-mapped 로 두고 그 자리로 점프 |

- 이미지 시작 = 벡터 테이블(1 KB), `+0x400` = `firm_ver_t` (`.version`). FSBL 이 이 자리에서 `firm_addr` / `firm_size` 를 읽는다
- `firm_size` 는 링커 심볼 `_fw_flash_size` (= `.data` 적재 끝 − 이미지 시작). bin 크기와 같다 (87488 B)
- 앱은 AXISRAM1 1 MB 만 쓴다. AXISRAM2 는 BootROM context / 트레이스와 FSBL 자리라 손대지 않는다
- `.noncacheable` / `.bss` / heap / stack 은 NOLOAD 라 이미지에 들어가지 않는다
- ST 템플릿은 이것을 LRUN 이라 부른다. 이 프로젝트는 뜻이 바로 보이게 **SRAM** 이라 했다

---

## 3. 부팅 흐름

| 상황 | FSBL 판정 | 결과 |
|---|---|---|
| 전원 / 리셋 | 앱 TAG 가 맞다 | `[  ] jump : SRAM 0x34000000, 87488 bytes` → 앱 |
| 앱에서 `reset boot` · cmd `FW_UPDATE` | RTC 백업 레지스터 `MODE_BIT_BOOT` | `[  ] boot : stay in FSBL (boot request)` |
| 리셋 버튼 두 번 (300 ms 안) | `reset_count ≥ 2` | `stay in FSBL (reset double click)` |
| 앱이 없거나 깨짐 | TAG / CRC / `firm_ver_t` | `stay in FSBL (no valid app)` |
| FSBL 에서 cmd `FW_JUMP` | TAG 확인 | 응답 후 앱으로 |

판정은 `apInit()` 의 `bootUp()` 이 **모듈을 열기 전에** 한다. 앱으로 갈 때는 CLI / cmd 를 열지 않는다.

앱 부팅 로그에 실행 방식이 찍힌다.

```
[ App Begin... ]
Booting..Name 		: STM32N6-FW
Booting..Addr 		: 0x34000000
Booting..Run  		: SRAM (AXISRAM1)
```

### 점프 전 정리 (`bootJumpTo()`)

| 할 일 | 이유 |
|---|---|
| `uartClose()` — 원형 수신 DMA 를 멈추고 UART 를 내린다 | 그대로 두면 DMA 가 FSBL 버퍼에 계속 쓰고, 앱이 돌고 있는 DMA 채널을 다시 설정한다. 원래 `uartClose()` 는 플래그만 내렸다 |
| SysTick 끄기, NVIC 전부 끄고 대기 지우기 | 앱이 VTOR 를 옮기기 전에 남은 인터럽트가 뜨면 FSBL 핸들러로 간다 (PRIMASK 는 그대로 — 앱 HAL 이 SysTick 을 쓴다) |
| D 캐시 clean + invalidate, I 캐시 invalidate | 복사한 코드가 D 캐시에만 있을 수 있다 |
| `MSPLIM = 0` | FSBL 스택 하한(0x341FF800)이 남아 있으면 앱 스택(AXISRAM1)에서 바로 STKOF 폴트 ([27](27-swd-attach.md) 과 같은 함정) |
| `VTOR = firm_addr`, `MSP = 벡터[0]`, `PC = 벡터[1]` | |

---

## 4. 앱 프로젝트 (`stm32n6-fw`)

`stm32n6-boot` 를 바탕으로 FSBL 전용(BootROM 트레이스, OTP, xspi / flash, 다운로드 처리)을 뺐다.

| 바뀐 것 | 내용 |
|---|---|
| `CMakeLists.txt` | `APP_RUN` (SRAM / XIP) 로 링커 스크립트 선택, `-DAPP_RUN_SRAM` / `-DAPP_RUN_XIP`. ST 서명 없음 (BootROM 이 읽지 않는다) |
| `hw.c` | `firm_ver_t firm_ver` (`.version`, `firm_addr` / `firm_size` 는 링커 심볼), 배너에 `Run : SRAM / XIP` |
| `hw_def.h` | `STM32N6-FW`, `HW_DEV_MODE_APP`, `HW_RESET_BOOT 0` (리셋 원인은 FSBL 이 RTC 에 남긴 것을 읽는다) |
| `cmd_boot.c` | INFO(mode = APP) / FW_UPDATE·FW_JUMP → `resetToBoot()` / BAUD / RESET. 쓰기·지우기는 거부 |
| `ap.c` | LED 초록 (FSBL 은 파랑) |

`download.py` 는 INFO 가 APP 이면 `FW_UPDATE` 를 보내고 1.5 초 뒤 다시 붙는다 (UART 라 포트가 사라지지 않는다).
fw 대상을 다 쓰면 `FW_JUMP` 로 앱을 실행한다.

---

## 5. 검증 (APP_RUN = SRAM)

| 시험 | 결과 |
|---|---|
| FSBL 업데이트 직후 (앱 영역에 시험용 랜덤 데이터) | `no valid image` → FSBL 에 머문다 |
| `download.py --target fw` | 85 KB 쓰기 → TAG → `FW_JUMP` → 앱 (`Addr 0x34000000`, `Run : SRAM`) |
| 앱 CLI `module info` | cli / cmd 모듈 정상 |
| 앱 `reset reset` | FSBL → `jump : SRAM 0x34000000, 87488 bytes` → 앱 |
| 앱이 돌 때 `download.py --target fw` | APP 감지 → FSBL 로 → 쓰기 → 앱 |
| 앱 `reset boot` | `stay in FSBL (boot request)`. 다시 리셋하면 앱 |

XIP 는 빌드만 확인했다 (코드 87 KB 는 NOR, RAM 21 KB). 남은 일:

- 앱의 `SystemInit()` 이 XSPI2 / XSPIM 을 리셋한다 (ST 템플릿 그대로). XIP 에서는 자기가 실행 중인 버스를 끊는다
- 앱의 클럭 설정이 PLL1 을 바꾼다. XSPI2 커널 클럭(IC3)이 PLL1 이라 실행 중에 흔들린다
- HSLV 퓨즈(200 MHz) 판단 ([24](24-xspi-nor.md) 2 절)
