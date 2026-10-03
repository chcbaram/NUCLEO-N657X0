#!/usr/bin/env python3
"""UART(ST-LINK VCP) 로 FSBL / 앱 / 데이터를 내려받는다. (macOS / Linux / Windows)

  python3 tools/download.py [bin] [--target boot|fw|data] [--offset N] [--port P] [--baud B]

  대상
    boot : FSBL (서명본 -trusted.bin). 보드가 FSBL2 에 쓰고 검증한 뒤 FSBL1 로 복사한다.
           기본 bin 은 build/stm32n6-fw-trusted.bin, 끝나면 리셋한다 (--no-reset 으로 끈다)
    fw   : 앱. 앞에 TAG 섹터가 있고, 다 쓴 뒤 보드가 TAG 를 기록한다 (커밋)
    data : 데이터 영역 + --offset (4 KB 단위)

  cmd 패킷은 CLI 와 같은 UART 로 간다. 보드는 02 FD 로 시작하는 패킷만 골라낸다.
  115200 으로 붙어서 --baud 로 올리고 (기본 4 Mbps, 0 이면 그대로), 끝나면 115200 으로 되돌린다.
  baram-term 이 포트를 열고 있으면 baram-ctl 로 잠시 놓게 한다 (--no-baram 으로 끈다).

  필요 : pyserial
"""
import argparse
import glob
import os
import shutil
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cmdproto import *   # noqa: E402,F403

PRJ_DIR   = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DEF_BOOT  = os.path.join(PRJ_DIR, "build", "stm32n6-fw-trusted.bin")
BAUD_DEF  = 115200
CHUNK     = 1016                 # cmd 데이터 최대 1024 B - offset 4 B
FSBL_MAGIC = b"STM2"


def find_port():
  try:
    from serial.tools import list_ports
    # ST VID(0x0483) 는 ST 칩을 쓴 다른 장치도 쓴다. ST-LINK 의 PID / 이름으로 고른다
    stlink_pid = {0x374B, 0x374E, 0x374F, 0x3752, 0x3753, 0x3754, 0x3757}
    st = [p.device for p in list_ports.comports()
          if p.vid == 0x0483 and (p.pid in stlink_pid or "STLINK" in (p.product or "").upper())]
    if st:
      return sorted(st)[0]
  except ImportError:
    pass
  ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
  return ports[0] if ports else None


class BaramTerm:
  """baram-term 이 포트를 쥐고 있으면 놓게 하고, 끝나면 다시 열게 한다."""

  def __init__(self, port, enable):
    self.port = port
    self.ctl  = shutil.which("baram-ctl") if enable else None
    self.released = False

  def __enter__(self):
    if self.ctl:
      r = subprocess.run([self.ctl, "--port", self.port, "release"], capture_output=True)
      self.released = (r.returncode == 0)
      if self.released:
        print("baram-term : 포트를 잠시 놓음")
    return self

  def __exit__(self, *exc):
    if self.released:
      subprocess.run([self.ctl, "--port", self.port, "resume"], capture_output=True)
      print("baram-term : 포트 다시 엶")


def request_ok(ch, cmd, data=b"", timeout=3.0, what=""):
  r = ch.request(cmd, data, timeout=timeout)
  if r["err"]:
    sys.exit(f"{what or hex(cmd)} 실패 err={err_str(r['err'])}")
  return r


def change_baud(ch, tr, baud):
  """보드와 같이 보율을 바꾼다. 실패하면 115200 으로 돌아온다 (보드는 3 초 뒤 스스로 돌아온다)."""
  r = ch.request(BOOT_CMD_BAUD, struct.pack("<I", baud))
  if r["err"]:
    print(f"보율 {baud} 거부 err={err_str(r['err'])}, {BAUD_DEF} 로 진행")
    return BAUD_DEF
  tr.set_baud(baud)
  time.sleep(0.05)
  try:
    ch.request(BOOT_CMD_INFO, timeout=1.0)
    return baud
  except TimeoutError:
    print(f"보율 {baud} 에서 응답 없음. {BAUD_DEF} 로 돌아간다")
    tr.set_baud(BAUD_DEF)
    time.sleep(3.5)
    ch.request(BOOT_CMD_INFO, timeout=2.0)
    return BAUD_DEF


def download(ch, target, image, offset, args):
  if target == TARGET_BOOT and image[:4] != FSBL_MAGIC:
    sys.exit("서명 헤더(STM2)가 없다. -trusted.bin 을 줄 것 (빌드가 post-build 로 만든다)")

  if target == TARGET_FW:
    v = parse_version(ch.request(BOOT_CMD_VERSION)["data"])
    if v["img_type"]:
      print(f"현재 FW   : {v['name']} {v['version']}  [{v['img_str']}] {v['fw_size']} B  crc 0x{v['fw_crc']:04X}")
    else:
      print("현재 FW   : 없음")

  begin = struct.pack("<IB", len(image), target)
  if target == TARGET_DATA:
    begin += struct.pack("<I", offset)
  request_ok(ch, BOOT_CMD_FW_BEGIN, begin, what="BEGIN")

  t0 = time.time()
  request_ok(ch, BOOT_CMD_FW_ERASE, timeout=120.0, what="ERASE")
  print(f"  지우기 {time.time()-t0:6.2f}s")

  t0 = time.time()
  for off in range(0, len(image), CHUNK):
    r = ch.request(BOOT_CMD_FW_WRITE, struct.pack("<I", off) + image[off:off+CHUNK], timeout=3.0)
    if r["err"]:
      sys.exit(f"\nWRITE off=0x{off:X} 실패 err={err_str(r['err'])}")
    if (off // CHUNK) % 8 == 0:
      print(f"\r  쓰기   {off*100//len(image):3d}%", end="", flush=True)
  dt = time.time() - t0
  print(f"\r  쓰기   {dt:6.2f}s  ({len(image)/dt/1024:.1f} KB/s)")

  t0 = time.time()
  r = request_ok(ch, BOOT_CMD_FW_END, timeout=60.0, what="END")
  size, crc = struct.unpack("<II", r["data"][:8])
  host_crc = crc16(image[:size])
  ok = (crc == host_crc)
  print(f"  확인   {time.time()-t0:6.2f}s  {size} B  crc 0x{crc:04X} (호스트 0x{host_crc:04X}) {'OK' if ok else 'FAIL'}")
  if not ok:
    sys.exit("CRC 가 다르다")

  if target == TARGET_FW:
    r = ch.request(BOOT_CMD_FW_VERIFY, timeout=30.0)
    img = r["data"][0] if r["data"] else 0
    print(f"  판정   {IMG_TYPE[img] if img < len(IMG_TYPE) else '?'}")


def main():
  ap = argparse.ArgumentParser(description="UART 로 FSBL / 앱 / 데이터 내려받기")
  ap.add_argument("binary", nargs="?", help="보낼 bin (boot 기본: build/stm32n6-fw-trusted.bin)")
  ap.add_argument("--target", choices=["boot", "fw", "data"], default="boot")
  ap.add_argument("--offset", type=lambda s: int(s, 0), default=0, help="data 영역 안 오프셋 (4 KB 단위)")
  ap.add_argument("--port", help="시리얼 포트. 없거나 auto 면 ST-LINK VCP 자동")
  ap.add_argument("--baud", type=int, default=4000000, help="전송 보율 (기본 4 Mbps, 0 이면 115200 그대로)")
  ap.add_argument("--no-reset", action="store_true", help="boot 대상에서 끝난 뒤 리셋하지 않는다")
  ap.add_argument("--no-baram", action="store_true", help="baram-term 포트 놓기/다시 열기를 하지 않는다")
  args = ap.parse_args()

  # 줄 단위로 내보내 진행 표시가 바로 보이게 한다
  sys.stdout.reconfigure(line_buffering=True)

  target = {"boot": TARGET_BOOT, "fw": TARGET_FW, "data": TARGET_DATA}[args.target]
  path = args.binary or (DEF_BOOT if target == TARGET_BOOT else None)
  if not path or not os.path.isfile(path):
    sys.exit(f"bin 이 없다: {path}")
  image = open(path, "rb").read()

  port = args.port if args.port and args.port.lower() != "auto" else find_port()
  if not port:
    sys.exit("시리얼 포트를 찾지 못했다. --port 로 줄 것")

  import serial
  with BaramTerm(port, not args.no_baram):
    ser = serial.Serial(port, BAUD_DEF, timeout=0.2)
    tr  = SerialTransport(ser)
    ch  = CmdChannel(tr)
    try:
      info = parse_info(ch.request(BOOT_CMD_INFO, timeout=2.0)["data"])
      print(f"연결     : {port}  {info['name']} {info['version']}  [{info['mode_str']}]")
      if info["mode"] != DEV_MODE_BOOT:
        sys.exit("앱이다. 부트로더(FSBL)에서만 쓸 수 있다")
      if info["cmd_ver"] == 0:
        sys.exit("확장 INFO 가 없다 (다른 보드의 부트로더?)")

      print(f"보낼 것  : {os.path.basename(path)}  {len(image)/1024:.1f} KB -> {TARGET_STR[target]}"
            + (f" +0x{args.offset:X}" if target == TARGET_DATA else ""))

      baud = BAUD_DEF
      if args.baud and args.baud != BAUD_DEF:
        baud = change_baud(ch, tr, args.baud)
      print(f"보율     : {baud}")

      t0 = time.time()
      download(ch, target, image, args.offset, args)
      print(f"합계     : {time.time()-t0:.2f}s")

      if baud != BAUD_DEF:
        try:
          ch.request(BOOT_CMD_BAUD, struct.pack("<I", BAUD_DEF), timeout=1.0)
        except TimeoutError:
          pass
        tr.set_baud(BAUD_DEF)

      if target == TARGET_BOOT and not args.no_reset:
        try:
          ch.request(BOOT_CMD_RESET, timeout=1.0)
        except TimeoutError:
          pass
        print("리셋     : 새 FSBL 로 부팅")
    finally:
      ser.close()


if __name__ == "__main__":
  main()
