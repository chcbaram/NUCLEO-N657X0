#!/usr/bin/env python3
"""
FSBL 이나 앱을 ST-LINK(외부 로더) 로 외부 NOR 에 쓰고 리셋한다. (macOS / Linux / Windows)

  대상 (--target)
    boot : FSBL. 서명본(-trusted.bin)을 0x70000000 (FSBL1) 에 쓴다
    fw   : 앱. TAG 섹터(4 KB)를 PC 에서 계산해 이미지 앞에 붙이고 0x70100000 에 쓴다.
           UART 다운로드에서는 FSBL 이 TAG 를 쓰지만, ST-LINK 로 쓸 때는 이 스크립트가 같은 형식으로 만든다
           (TAG 가 없으면 FSBL 이 앱을 실행하지 않는다)

  부팅 : JP1(BOOT0) = 0, JP2(BOOT1) = 0  -> Flash boot
         쓰기 자체는 JP2 = 1 (Development boot) 에서도 된다.

  서명 : 빌드가 post-build 로 <이름>-trusted.bin 을 만든다 (CMakeLists.txt).
         --bin 으로 받은 파일이 서명돼 있지 않으면(헤더 "STM2" 없음) 여기서 키 없이(-nk) 서명한다.
  쓰기 : 이 저장소의 외부 로더(firmware/stm32n6-ext-loader) 를 쓴다. 없으면 먼저 빌드한다.

  CubeCLT 경로 : $CLT -> ~/ST/STM32CubeCLT(_*) -> /opt/ST/STM32CubeCLT(_*) -> C:/ST/STM32CubeCLT(_*)
  표준 라이브러리만 쓴다.

  사용 : python3 tools/flash.py [--target boot|fw] [--bin <bin>] [--loader <stldr>] [--addr <주소>] [--no-reset]
         boot 기본: build/stm32n6-boot-trusted.bin, fw 기본: ../stm32n6-fw/build/stm32n6-fw.bin
"""
import argparse
import glob
import os
import re
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cmdproto import crc16   # noqa: E402  (펌웨어 utilCalcCRC 와 같은 CRC-16)

IS_WIN   = os.name == "nt"
EXE      = ".exe" if IS_WIN else ""
PRJ_DIR  = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DEF_BIN  = os.path.join(PRJ_DIR, "build", "stm32n6-boot-trusted.bin")
DEF_FW   = os.path.abspath(os.path.join(PRJ_DIR, "..", "stm32n6-fw", "build", "stm32n6-fw.bin"))
ADDR     = {"boot": "0x70000000", "fw": "0x70100000"}

TAG_SIZE      = 0x1000              # FLASH_SIZE_TAG
TAG_MAGIC     = 0x54414720          # "TAG "
VER_MAGIC     = 0x56455220          # "VER "
VER_OFFSET    = 0x400               # 이미지 안 firm_ver_t 위치
VER_SIZE_OFF  = VER_OFFSET + 4 + 32 + 32 + 4   # firm_ver_t.firm_size
LDR_DIR  = os.path.abspath(os.path.join(PRJ_DIR, "..", "stm32n6-ext-loader"))
DEF_LDR  = os.path.join(LDR_DIR, "build", "MX25UM51245G_NUCLEO-N657X0.stldr")
ANSI     = re.compile(r"\x1b\[[0-9;]*m")


def find_clt():
  def tool(d):
    return os.path.join(d, "STM32CubeProgrammer", "bin", "STM32_Programmer_CLI" + EXE)

  def version_key(path):
    return [int(x) for x in re.findall(r"\d+", os.path.basename(path))]

  cands = []
  if os.environ.get("CLT"):
    cands.append(os.environ["CLT"])
  for base in [os.path.expanduser("~/ST"), "/opt/ST", "C:/ST"]:
    cands.append(os.path.join(base, "STM32CubeCLT"))
    cands += sorted(glob.glob(os.path.join(base, "STM32CubeCLT_*")), key=version_key, reverse=True)

  for c in cands:
    if os.path.isfile(tool(c)):
      return c
  return None


def run(args):
  # CubeProgrammer 출력에는 색 코드가 섞여 있다
  p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
  return p.returncode, ANSI.sub("", p.stdout.decode(errors="replace"))


def make_tagged(bin_path):
  """앱 bin 앞에 TAG 섹터를 붙인 <이름>-tag.bin 을 만든다. FSBL 의 cmdBootEndFw() 와 같은 형식이다.

  firm_tag_t { magic "TAG ", fw_addr = TAG 크기, fw_size, fw_crc (CRC-16), tag_crc (앞 16 바이트의 CRC-16) }
  fw_size 는 이미지의 firm_ver_t.firm_size 를 우선한다 (bin 끝 패딩이 있어도 stale tag 가 되지 않게).
  """
  image = open(bin_path, "rb").read()
  size  = len(image)
  if len(image) >= VER_SIZE_OFF + 4:
    magic, = struct.unpack_from("<I", image, VER_OFFSET)
    firm_size, = struct.unpack_from("<I", image, VER_SIZE_OFF)
    if magic == VER_MAGIC and 0 < firm_size <= len(image):
      size = firm_size
    else:
      print("경고: firm_ver_t 가 없다. bin 크기로 TAG 를 만든다")

  head = struct.pack("<4I", TAG_MAGIC, TAG_SIZE, size, crc16(image[:size]))
  tag  = head + struct.pack("<I", crc16(head))
  out  = os.path.splitext(bin_path)[0] + "-tag.bin"
  with open(out, "wb") as f:
    f.write(tag + b"\xFF" * (TAG_SIZE - len(tag)) + image)
  print(f"TAG  : {size} B  crc 0x{crc16(image[:size]):04X}  → {os.path.basename(out)}")
  return out


def parse_args():
  ap = argparse.ArgumentParser(description="ST-LINK(외부 로더) 로 FSBL / 앱 기록 → 리셋")
  ap.add_argument("--target", choices=["boot", "fw"], default="boot", help="boot = FSBL, fw = 앱 (기본: boot)")
  ap.add_argument("--bin",    help="기록할 bin (boot: build/stm32n6-boot-trusted.bin, fw: ../stm32n6-fw/build/stm32n6-fw.bin)")
  ap.add_argument("--loader", default=DEF_LDR, help="외부 로더 .stldr (기본: 이 저장소의 로더, 없으면 빌드)")
  ap.add_argument("--addr",   help="기록 주소 (boot: 0x70000000 FSBL1, fw: 0x70100000 TAG 섹터)")
  ap.add_argument("--no-reset", action="store_true", help="기록 뒤 리셋하지 않는다")
  return ap.parse_args()


def main():
  # cmake 출력과 순서가 섞이지 않게 줄 단위로 내보낸다
  sys.stdout.reconfigure(line_buffering=True)

  args = parse_args()
  BIN  = os.path.abspath(args.bin or (DEF_FW if args.target == "fw" else DEF_BIN))
  args.addr = args.addr or ADDR[args.target]
  LDR  = os.path.abspath(args.loader)

  clt = find_clt()
  if clt is None:
    print("CubeCLT 를 찾지 못했다. CLT=<경로> 로 지정할 것")
    return 1
  print("CubeCLT :", clt)

  prg  = os.path.join(clt, "STM32CubeProgrammer", "bin", "STM32_Programmer_CLI" + EXE)
  sign = os.path.join(clt, "STM32CubeProgrammer", "bin", "STM32_SigningTool_CLI" + EXE)

  if not os.path.isfile(BIN):
    print("bin 이 없다:", BIN, " (먼저 빌드할 것)")
    return 1

  if not os.path.isfile(LDR):
    if LDR != DEF_LDR:
      print("외부 로더가 없다:", LDR)
      return 1
    print("외부 로더 빌드")
    build = os.path.join(LDR_DIR, "build")
    # 펌웨어 빌드 태스크와 같은 명령 (생성기는 지정하지 않는다)
    if subprocess.run(["cmake", "-S", LDR_DIR, "-B", build]).returncode != 0 or \
       subprocess.run(["cmake", "--build", build, "-j20"]).returncode != 0:
      print("외부 로더 빌드 실패")
      return 1

  # 서명돼 있으면 그대로 쓰고, 아니면 키 없이 서명한다 (<이름>-trusted.bin)
  #   서명 도구는 결과를 읽기 전용으로 만든다. 지우고 새로 만든다.
  #   -align : CubeProgrammer 2.21 부터 필요. 페이로드를 0x400 에 맞춘다
  #
  with open(BIN, "rb") as f:
    signed = f.read(4) == b"STM2"

  if args.target == "fw":
    OUT = make_tagged(BIN)      # 앱은 서명하지 않는다 (BootROM 이 읽지 않는다)
  elif signed:
    OUT = BIN
  else:
    OUT = os.path.splitext(BIN)[0] + "-trusted.bin"
    if os.path.exists(OUT):
      os.chmod(OUT, 0o644)
      os.remove(OUT)
    _, log = run([sign, "-bin", BIN, "-nk", "-of", "0x80000000", "-t", "fsbl", "-hv", "2.3",
                  "-align", "-s", "-o", OUT])
    for line in log.splitlines():
      if "Entry point" in line:
        print(line.strip())
    if not os.path.isfile(OUT):
      print(log)
      print("서명 실패")
      return 1

  # 돌고 있는 FSBL 에 붙는다 (Flash boot 에서는 리셋하면 BootROM 이 바로 FSBL 을 띄우므로 이 방법뿐이다).
  #   로더를 RAM 에 넣기 전에 코어를 세우고 FSBL 이 남긴 설정을 정리한다 (docs/27-swd-attach.md 4절).
  #     PRIMASK    = 1      인터럽트 막기 (벡터 테이블이 로더로 덮인다)
  #     0xE000ED94 MPU_CTRL = 0
  #     0xE000ED14 CCR      = 0x201 (리셋값, IC/DC 끔)
  #     0xE000EF50 ICIALLU  = 0     (I 캐시 무효화)
  #   MSPLIM 은 CubeProgrammer 로 쓸 수 없어서 로더 Init 이 지운다.
  #
  print("기록 :", os.path.basename(OUT), "->", args.addr)
  _, log = run([prg, "-c", "port=SWD", "ap=1", "mode=Hotplug", "-halt",
                "-coreReg", "PRIMASK=1",
                "-w32", "0xE000ED94", "0x0",
                "-w32", "0xE000ED14", "0x00000201",
                "-w32", "0xE000EF50", "0x0",
                "-el", LDR, "-w", OUT, args.addr, "-v"]
               + ([] if args.no_reset else ["-hardRst"]))
  for line in log.splitlines():
    if re.search(r"Size  |Erasing external|verified|Error", line):
      print(line.strip())

  # CubeProgrammer 는 실패해도 종료 코드가 0 일 때가 있어 결과 문구로 판단한다
  if "Download verified successfully" not in log:
    print("기록 실패")
    return 1

  print("기록 완료")
  return 0


if __name__ == "__main__":
  sys.exit(main())
