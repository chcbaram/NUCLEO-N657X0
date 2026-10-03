#!/usr/bin/env python3
"""
FSBL 을 서명해서 외부 NOR(0x70000000) 에 쓰고 리셋한다. (macOS / Linux / Windows)

  부팅 : JP1(BOOT0) = 0, JP2(BOOT1) = 0  -> Flash boot
         쓰기 자체는 JP2 = 1 (Development boot) 에서도 된다.

  서명 : 빌드가 post-build 로 <이름>-trusted.bin 을 만든다 (CMakeLists.txt).
         --bin 으로 받은 파일이 서명돼 있지 않으면(헤더 "STM2" 없음) 여기서 키 없이(-nk) 서명한다.
  쓰기 : 이 저장소의 외부 로더(firmware/stm32n6-ext-loader) 를 쓴다. 없으면 먼저 빌드한다.

  CubeCLT 경로 : $CLT -> ~/ST/STM32CubeCLT(_*) -> /opt/ST/STM32CubeCLT(_*) -> C:/ST/STM32CubeCLT(_*)
  표준 라이브러리만 쓴다.

  사용 : python3 tools/flash.py [--bin <bin>] [--loader <stldr>] [--addr <주소>] [--no-reset]
         인자가 없으면 이 프로젝트(build/stm32n6-fw-trusted.bin) 와 이 저장소의 외부 로더를 쓴다.
"""
import argparse
import glob
import os
import re
import subprocess
import sys

IS_WIN   = os.name == "nt"
EXE      = ".exe" if IS_WIN else ""
PRJ_DIR  = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DEF_BIN  = os.path.join(PRJ_DIR, "build", "stm32n6-fw-trusted.bin")
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


def parse_args():
  ap = argparse.ArgumentParser(description="FSBL 서명 → 외부 NOR 기록 → 리셋")
  ap.add_argument("--bin",    default=DEF_BIN, help="기록할 bin. 서명돼 있지 않으면 서명한다 (기본: %(default)s)")
  ap.add_argument("--loader", default=DEF_LDR, help="외부 로더 .stldr (기본: 이 저장소의 로더, 없으면 빌드)")
  ap.add_argument("--addr",   default="0x70000000", help="기록 주소 (기본: %(default)s, FSBL1 자리)")
  ap.add_argument("--no-reset", action="store_true", help="기록 뒤 리셋하지 않는다")
  return ap.parse_args()


def main():
  # cmake 출력과 순서가 섞이지 않게 줄 단위로 내보낸다
  sys.stdout.reconfigure(line_buffering=True)

  args = parse_args()
  BIN  = os.path.abspath(args.bin)
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

  if signed:
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
