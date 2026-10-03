# 23. BootROM 트레이스 파서

> BootROM 이 부팅하면서 AXISRAM2 에 남긴 바이너리 트레이스를 FSBL 에서 읽어 **부팅 배너에 한 줄 요약**,
> CLI `bootrom trace` 로 **전체 목록**을 출력한다. Flash boot 로 넘어가면 "왜 FSBL 이 안 뜨는지"를
> 알아낼 거의 유일한 수단이 된다.
> 관련: [02-fsbl-loading.md](02-fsbl-loading.md) 9절 (트레이스 포맷과 상태 워드)

---

## 1. 결과

```
Booting..ROM  		: v0x501 DevBoot ClosedUnlocked reset=Sft traces=20 err=0
```

| 항목 | 값 | 뜻 |
|---|---|---|
| `v0x501` | `BOOTCORE_BootRomVer` 의 인자 | BootROM 버전 |
| `DevBoot` | 마지막 `BOOTCORE_BootAction*` | JP2(BOOT1)=1, Development boot |
| `ClosedUnlocked` | 마지막 `BOOTCORE_ChipMode*` | 라이프사이클 `CLOSED_UNLOCKED` |
| `reset=Sft` | 마지막 `BOOTCORE_HwReset*` | software reset — `load.sh` 가 붙으면서 건 리셋. 전원 재인가면 `POR` 이어야 한다 |
| `traces=20 err=0` | | 항목 수, level 이 ERR 인 항목 수 |

```
cli# bootrom trace
S/N  timestamp level code       name / args
S     1736 INFO  0x2A000001
S     1753 INFO  0x00000155 BOOTCORE_BootRomVer 0x00000501
S     1767 INFO  0x00000158 BOOTCORE_BootRomForRtlVer 0x001F0202
S     1792 INFO  0x00002506 BOOTCORE_HwResetSft
...
S     2093 INFO  0x00000025 BOOTCORE_ChipModeClosedUnlocked
S     2810 INFO  0x00000010 BOOTCORE_LogicalResetSystem
S     3180 INFO  0x00000034 BOOTCORE_BootActionDevBoot
...
20 traces
```

`S`/`N` 은 secure / non-secure 버퍼다. 두 버퍼를 타임스탬프 순으로 합쳐 출력한다.
DEV boot 에서는 non-secure 버퍼가 비어 있다.

---

## 2. 트레이스 위치와 포맷

| 버퍼 | 주소 | 크기 |
|---|---|---|
| Secure | `0x3410_37F0` | 2 KB |
| Non-secure | `0x2410_77F0` | 2 KB |

FSBL 이미지(`0x3418_0400` ~)와 스택(`0x341F_F800` ~ `0x3420_0000`)과 겹치지 않는다. 그래서
[02](02-fsbl-loading.md) 9.1절의 "덮기 전에 읽어야 한다"는 지금 구성에서는 해당이 없고, 부팅 뒤
아무 때나 CLI 로 읽어도 된다. 나중에 이 RAM 을 다른 용도로 쓰게 되면 그 전에 읽어야 한다.

한 항목은 32 비트 워드로 이렇게 구성된다.

| 워드 | 내용 |
|---|---|
| `0xFFDDBB00` | START |
| size | **그 뒤 바이트 수** (timestamp + level + code + 인자). `0x0C` 면 인자 없음 |
| timestamp | 정렬에 쓴다. 단위는 확인하지 않았다 |
| level | 0 INFO / 1 WARN / 2 ERR / 3 DEBUG |
| code | 메시지 코드 |
| 인자 | (size − 12) / 4 개 |

secure 코드에서 non-secure 별칭 `0x2410_77F0` 를 읽어도 폴트가 나지 않는 것을 확인했다.

---

## 3. 파서는 직접 작성했다 — 라이선스

ST 가 GitHub 에 공개한 참조 구현([STM32N6-Boot-ROM-Traces](https://github.com/stm32-hotspot/STM32N6-Boot-ROM-Traces)
의 `rom_trace_parser.c`)이 있지만 **가져오지 않았다.**

| | |
|---|---|
| 우리 repo | **MIT**, 공개 |
| ST 참조 repo 의 LICENSE 표 | "STM32 Projects" → **SLA0044** |
| `rom_trace_parser.c` 머리말 | "proprietary … **confidential** … dissemination strictly limited" (옛 문구로 보임) |

SLA0044 는 **오픈소스 라이선스(MIT, BSD, Apache, GPL …)의 적용을 받게 되는 방식으로 재배포하는 것을
금한다** ([SLA0044](https://www.st.com/resource/en/license_agreement/SLA0044.txt)). MIT 인 공개 repo 에
넣기에는 회색지대이고, 머리말의 "기밀" 문구도 걸려서 포맷만 보고 새로 썼다.

ST 파일의 1247 줄 중 약 950 줄은 코드 → 이름 표(230 개)다. 우리는 **다음 단계(Flash boot)에서 볼 만한
55 개만** 이름을 붙였다.

| 분류 | 개수 |
|---|---|
| `BOOTCORE_BootAction*` — 부트 동작 (DEV / 보안 부트 / 부트 없음 …) | 5 |
| `BOOTCORE_ChipMode*` — 라이프사이클 | 8 |
| `BOOTCORE_HwReset*`, `LogicalReset*` — 리셋 원인 | 9 |
| `BOOTCORE_BootRom*` — 버전 | 6 |
| `SECBOOT_*` — 이미지 인증·서명·복호 | 27 |

나머지는 코드로만 찍는다. 우리 보드 트레이스의 절반(`0x2A000001`, `0x2B`, `0xA0`~`0xBD`, `0x16A` …)은
**ST 참조 표에도 없는 코드**라서 표를 다 가져와도 이름이 나오지 않는다.

---

## 4. 구현

| 파일 | 내용 |
|---|---|
| `src/hw/driver/bootrom.c` | 파서, 이름 표, 요약, CLI `bootrom` |
| `src/common/hw/include/bootrom.h` | `bootromInit()`, `bootromPrintTrace()` |
| `src/hw/hw_def.h` | `_USE_HW_BOOTROM`, `_USE_CLI_HW_BOOTROM` |
| `src/hw/hw.c` | 배너 끝에서 `bootromInit()` |

파서는 START 를 찾아 size 를 읽고, size 가 12 미만이거나 4 의 배수가 아니거나 버퍼 끝을 넘으면
그 버퍼의 파싱을 멈춘다. RAM 은 3.8 KB 늘었다 (88.9 → 92.7 KB).

| CLI | 내용 |
|---|---|
| `bootrom info` | 배너와 같은 요약 |
| `bootrom trace` | 전체 목록 (secure + non-secure, 타임스탬프 순) |

---

## 5. 다음

- **Flash boot 에서 확인** — 서명 실패, 이미지 길이, 매직 넘버 같은 `SECBOOT_*` 와 `BootActionNoBoot` 가
  실제로 어떻게 찍히는지 본다. 이 문서의 이름 표는 그때를 위한 것이다
- 상태 워드 (`uint64_t`, [02](02-fsbl-loading.md) 9.2절) — BootROM 이 넘겨주는 boot context 에서 읽어 함께 보여 줄 수 있다
