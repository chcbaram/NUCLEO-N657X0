#!/usr/bin/env bash
#
# FSBL 을 AXISRAM2(0x34180400) 에 적재하고 실행시킨다.
#
#   전제 : JP2(BOOT1) = 1  -> Development boot
#          BootROM 이 디버그를 열어준 상태여야 SWD 가 붙는다.
#
#   주의 : STM32_Programmer_CLI 는 -c(연결) 할 때마다 software reset 을 건다.
#          그러면 BootROM 으로 되돌아가면서 적재해 둔 이미지가 실행을 멈춘다.
#          그래서 "적재 후 상태 확인" 은 반드시 gdb 로 해야 한다.
#
set -e

# CubeCLT 경로. 버전을 박아두지 않는다.
#
#   이 맥에는 pkg 를 정식 설치하지 않고, 필요한 것만 추출해서
#   ~/ST/STM32CubeCLT_<버전> 에 두고 ~/ST/STM32CubeCLT 링크로 참조한다.
#   (gcc / cmake / make / ninja 는 이미 있는 것을 쓰므로 뺐다)
#
#   탐색 순서 : $CLT -> ~/ST 링크 -> ~/ST 최신 -> /opt/ST 링크 -> /opt/ST 최신
#
#   gdbserver 는 구형 ST-LINK 펌웨어를 거부한다.
#   거부당하면 "$CLT/STLinkUpgrade.sh" 로 올린다. (번들 jre 필요)
#
if [ -z "${CLT:-}" ]; then
  for cand in \
    "$HOME/ST/STM32CubeCLT" \
    "$(ls -d "$HOME"/ST/STM32CubeCLT_* 2>/dev/null | sort -V | tail -1 || true)" \
    "/opt/ST/STM32CubeCLT" \
    "$(ls -d /opt/ST/STM32CubeCLT_* 2>/dev/null | sort -V | tail -1 || true)"
  do
    if [ -n "$cand" ] && [ -x "$cand/STLink-gdb-server/bin/ST-LINK_gdbserver" ]; then
      CLT="$cand"
      break
    fi
  done
fi

if [ -z "${CLT:-}" ] || [ ! -x "$CLT/STLink-gdb-server/bin/ST-LINK_gdbserver" ]; then
  echo "CubeCLT 를 찾지 못했다. 아래 중 하나를 해 둘 것:"
  echo "  ~/ST/STM32CubeCLT  링크를 만든다 (10-dev-environment.md 4절)"
  echo "  CLT=<경로> ./tools/load.sh"
  exit 1
fi
echo "CubeCLT : $CLT"

PRJ_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ELF="$PRJ_DIR/build/stm32n6-fw.elf"
GDB_PORT=${GDB_PORT:-61234}

[ -f "$ELF" ] || { echo "elf 가 없다: $ELF  (먼저 빌드할 것)"; exit 1; }

# VSCode 의 cortex-debug launch 와 같은 옵션으로 띄운다 (-d = --swd, --halt, -m 1).
#
#   리셋하고 BootROM 에서 세운 뒤 적재하므로 앞 펌웨어의 상태가 남지 않는다.
#   (돌고 있는 펌웨어에 붙으면 보드가 멈추던 문제는 SystemInit() 에서 고쳤다 -> docs/27-swd-attach.md)
#
"$CLT/STLink-gdb-server/bin/ST-LINK_gdbserver" \
  -p "$GDB_PORT" -l 1 -d --halt -m 1 \
  -cp "$CLT/STM32CubeProgrammer/bin" > /tmp/stm32n6-gdbserver.log 2>&1 &
SRV_PID=$!
trap 'kill $SRV_PID 2>/dev/null || true' EXIT

for _ in $(seq 1 40); do
  grep -q "Waiting for debugger connection" /tmp/stm32n6-gdbserver.log && break
  perl -e 'select(undef,undef,undef,0.25)'
done

arm-none-eabi-gdb -q -batch \
  -ex "set confirm off" -ex "set pagination off" \
  -ex "target extended-remote localhost:$GDB_PORT" \
  -ex "load $ELF" \
  -ex "set \$sp = *(unsigned int*)0x34180400" \
  -ex "set \$pc = *(unsigned int*)0x34180404" \
  -ex "detach" \
  "$ELF"

echo "적재 완료 - 실행 중"
