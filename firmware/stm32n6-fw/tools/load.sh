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

#   gdbserver 7.13.0 은 ST-LINK 펌웨어 V3J17M10 이상을 요구한다.
#   업그레이드: /opt/ST/STM32CubeCLT_1.21.0/STLinkUpgrade.sh
#
CLT=${CLT:-/opt/ST/STM32CubeCLT_1.21.0}

PRJ_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ELF="$PRJ_DIR/build/stm32n6-fw.elf"
GDB_PORT=${GDB_PORT:-61234}

[ -f "$ELF" ] || { echo "elf 가 없다: $ELF  (먼저 빌드할 것)"; exit 1; }

"$CLT/STLink-gdb-server/bin/ST-LINK_gdbserver" \
  -p "$GDB_PORT" -l 1 -m 1 -k -e -d --attach \
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
