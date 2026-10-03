#!/usr/bin/env bash
#
# FSBL 을 서명해서 외부 NOR(0x70000000) 에 쓰고 리셋한다.
#
#   부팅 : JP1(BOOT0) = 0, JP2(BOOT1) = 0  -> Flash boot
#          쓰기 자체는 JP2 = 1 (Development boot) 에서도 된다.
#
#   서명 : 키 없이(-nk) 헤더만 붙인다. secure_boot 퓨즈를 태우지 않은 보드 전용이다.
#   쓰기 : 이 저장소의 외부 로더(firmware/stm32n6-ext-loader) 를 쓴다. 없으면 먼저 빌드한다.
#
set -e

if [ -z "${CLT:-}" ]; then
  for cand in \
    "$HOME/ST/STM32CubeCLT" \
    "$(ls -d "$HOME"/ST/STM32CubeCLT_* 2>/dev/null | sort -V | tail -1 || true)" \
    "/opt/ST/STM32CubeCLT" \
    "$(ls -d /opt/ST/STM32CubeCLT_* 2>/dev/null | sort -V | tail -1 || true)"
  do
    if [ -n "$cand" ] && [ -x "$cand/STM32CubeProgrammer/bin/STM32_Programmer_CLI" ]; then
      CLT="$cand"
      break
    fi
  done
fi

if [ -z "${CLT:-}" ] || [ ! -x "$CLT/STM32CubeProgrammer/bin/STM32_Programmer_CLI" ]; then
  echo "CubeCLT 를 찾지 못했다. CLT=<경로> ./tools/flash.sh"
  exit 1
fi
echo "CubeCLT : $CLT"

PRG="$CLT/STM32CubeProgrammer/bin/STM32_Programmer_CLI"
SIGN="$CLT/STM32CubeProgrammer/bin/STM32_SigningTool_CLI"

PRJ_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$PRJ_DIR/build/stm32n6-fw.bin"
OUT="$PRJ_DIR/build/stm32n6-fw-trusted.bin"
LDR_DIR="$PRJ_DIR/../stm32n6-ext-loader"
LDR="$LDR_DIR/build/MX25UM51245G_NUCLEO-N657X0.stldr"

[ -f "$BIN" ] || { echo "bin 이 없다: $BIN  (먼저 빌드할 것)"; exit 1; }

if [ ! -f "$LDR" ]; then
  echo "외부 로더 빌드"
  cmake -S "$LDR_DIR" -B "$LDR_DIR/build" > /dev/null
  cmake --build "$LDR_DIR/build" -j20 > /dev/null
fi

# 서명 도구는 결과를 읽기 전용으로 만든다. 지우고 새로 만든다.
#   -align : CubeProgrammer 2.21 부터 필요. 페이로드를 0x400 에 맞춘다
#
rm -f "$OUT"
"$SIGN" -bin "$BIN" -nk -of 0x80000000 -t fsbl -hv 2.3 -align -s -o "$OUT" \
  | grep -E "Entry point|generated" || { echo "서명 실패"; exit 1; }

LOG=$("$PRG" -c port=SWD ap=1 mode=Hotplug -el "$LDR" -w "$OUT" 0x70000000 -v -hardRst 2>&1 \
  | sed 's/\x1b\[[0-9;]*m//g' || true)
echo "$LOG" | grep -E "Size  |Erasing external|verified|Error" || true

# CubeProgrammer 는 연결에 실패해도 종료 코드가 0 일 때가 있어 결과 문구로 판단한다
echo "$LOG" | grep -q "Download verified successfully" || { echo "기록 실패"; exit 1; }
echo "기록 완료"
