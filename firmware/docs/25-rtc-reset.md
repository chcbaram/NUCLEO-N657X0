# 25. RTC 와 리셋 (rtc.c / reset.c)

> 다른 프로젝트(weact-h750)의 `rtc.c` / `reset.c` 를 N6 에 맞춰 옮겼다.
> RTC 백업 레지스터는 리셋을 넘어 값을 남기는 통로다. Flash boot 와 FSBL / App 분리에서
> "부트로더에 머물러라" 같은 요청을 넘기는 데 쓴다.
> 관련: [01-boot-process.md](01-boot-process.md) 1절 (리셋 소스 판별)

---

## 1. 보드 조건

| 항목 | 내용 |
|---|---|
| RTC 클럭 | 32.768 kHz LSE (보드에 크리스털 있음) |
| VBAT | 배터리 없이 `SB13` 을 거쳐 V<sub>DDIO</sub> 에 연결 |
| 결과 | **리셋에는 시각과 백업 레지스터가 남고, USB 를 뽑으면 지워진다** |

전원이 끊겼다 들어오면 값이 쓰레기일 수 있다. `reset.c` 는 카운터마다 매직(`RESET_CNT_MAGIC`, reset.c 내부)을 같이 써서 유효성을 본다.

---

## 2. rtc.c — N6 에서 달라진 것

| 항목 | 내용 |
|---|---|
| 백업 레지스터 | RTC 가 아니라 **TAMP** 블록(`TAMP_BKPxR`, 32 개). HAL API(`HAL_RTCEx_BKUPWrite/Read`)는 같다 |
| 클럭 | `HAL_PWR_EnableBkUpAccess()` → LSE 가 꺼져 있을 때만 켠다 → RTCSEL = LSE → `RTCAPB` / `RTC` 클럭 |
| RTCSEL 변경 | N6 HAL 은 소스가 바뀌어도 백업 도메인 리셋을 하지 않는다(`#if 0 TO DO`). 이미 LSE 면 아무 일도 없다 |
| Init 필드 | H7 에 없던 `OutPutPullUp`, `BinMode`, `BinMixBcdU` 를 채운다 |
| 보안 | secure FSBL 에서 RTC / TAMP 를 따로 설정하지 않아도 읽고 쓸 수 있었다 |

CLI 는 `rtc info`, `rtc get info`, `rtc set time|date`, 그리고 브링업용 `rtc reg [index] [data]` 를 더했다.

---

## 3. reset.c — 리셋 원인

BootROM 은 `RCC_HWRSR` 만 지우고 **`RCC_RSR` 은 남겨 둔다** (UM3234 3.2.3). `resetInit()` 이 `RCC_RSR` 을 읽고 지운다.

| 플래그 | reset_bits |
|---|---|
| `PORRSTF` / `BORRSTF` | `RESET_BIT_POWER` |
| `PINRSTF` | `RESET_BIT_PIN` |
| `IWDGRSTF` / `WWDGRSTF` | `RESET_BIT_WDG` |
| `SFTRSTF` | `RESET_BIT_SOFT` |
| `LCKRSTF` / `LPWRRSTF` | `RESET_BIT_ETC` (N6 에서 추가) |

N6 는 어떤 리셋이든 `PINRSTF` 가 함께 뜬다. 그래서 리셋 버튼 더블클릭 판정은 weact 와 같이
**POWER → SOFT/WDG → PIN 순서**로 본다. 나머지(부트 모드, boot try, fault 카운트)는 weact 와 같고,
`HW_RESET_BOOT 1` 이라 이 FSBL 이 판정 주체다.

| 백업 레지스터 | 용도 |
|---|---|
| `BKP3` | 부트 모드 (`MODE_BIT_BOOT`, `MODE_BIT_MSC`) |
| `BKP4` | 리셋 원인 (부트로더 → 앱 전달) |
| `BKP5` | 리셋 버튼 클릭 카운트 |
| `BKP6` / `BKP7` | boot try / fault 카운트 |

---

## 4. 검증

| 시험 | 결과 |
|---|---|
| USB 연결 후 첫 적재 | `POWER + PIN + SOFT` — 전원 인가 뒤 한 번도 지우지 않아 쌓여 있던 것 + `load.sh` 의 리셋 |
| 다시 적재 | `PIN + SOFT` — `load.sh` 는 software reset |
| `rtc set date/time` → 다시 적재 | 시각이 이어서 흐른다 |
| `rtc reg 10 0x12345678` → 다시 적재 | 값이 남는다 |
| `reset reset` | 재부팅된다. 단 Development boot(JP2=1)에서는 BootROM 이 FSBL 을 로드하지 않고 멈추므로 **출력이 없는 것이 정상**이다. `load.sh` 로 다시 올린다 |

`reset reset` 뒤 다시 FSBL 이 뜨는 것은 Flash boot 가 되어야 확인할 수 있다 ([README](README.md) 다음 작업).
