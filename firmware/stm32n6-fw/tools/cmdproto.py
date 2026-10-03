"""부트로더(FSBL) cmd 패킷 프로토콜 클라이언트. 표준 라이브러리 + pyserial.

weact-h750 의 cmdproto.py 와 같은 패킷 형식이다. 전송계층과 무관하다 (read/write 만 바꾸면 된다).

패킷 (little endian)
  STX0(0x02) STX1(0xFD) type cmd_l cmd_h err_l err_h len_l len_h [data...] checksum
  checksum = (~sum(header+data)) + 1
"""
import struct
import time

STX0, STX1 = 0x02, 0xFD

PKT_TYPE_CMD  = 0x00
PKT_TYPE_RESP = 0x01

BOOT_CMD_INFO      = 0x0000
BOOT_CMD_VERSION   = 0x0001
BOOT_CMD_FW_BEGIN  = 0x0002
BOOT_CMD_FW_ERASE  = 0x0003
BOOT_CMD_FW_WRITE  = 0x0004
BOOT_CMD_FW_READ   = 0x0005
BOOT_CMD_FW_END    = 0x0006
BOOT_CMD_FW_VERIFY = 0x0007
BOOT_CMD_FW_UPDATE = 0x0008
BOOT_CMD_FW_JUMP   = 0x0009
BOOT_CMD_BAUD      = 0x0020
BOOT_CMD_RESET     = 0x0021

TARGET_FW   = 0
TARGET_BOOT = 1
TARGET_DATA = 2
TARGET_STR  = {TARGET_FW: "FW", TARGET_BOOT: "BOOT", TARGET_DATA: "DATA"}

DEV_MODE_BOOT = 0
DEV_MODE_APP  = 1

ERR_STR = {
  0x000A: "WRONG_CMD", 0x000C: "FLASH_ERASE", 0x000D: "WRONG_RANGE", 0x000E: "FLASH_WRITE",
  0x0010: "INVALID_FW", 0x0011: "FW_CRC", 0x0012: "JUMP_TO_FW", 0x0015: "FLASH_READ",
  0x0020: "CMD_MAX_LENGTH", 0x0021: "CMD_CHECKSUM",
}


def err_str(err):
  return f"0x{err:04X} ({ERR_STR.get(err, '?')})"


def build(cmd, data=b"", type_=PKT_TYPE_CMD, err=0):
  head = bytes([STX0, STX1, type_]) + struct.pack("<HHH", cmd, err, len(data))
  body = head + data
  return body + bytes([((~sum(body)) + 1) & 0xFF])


#-- 펌웨어 util_core.c 의 utilCalcCRC 와 같다 (CRC-16, poly 0x8005, MSB first, 초기값 0)
def _crc_table():
  table = []
  for i in range(256):
    crc = i << 8
    for _ in range(8):
      crc = ((crc << 1) ^ 0x8005) if (crc & 0x8000) else (crc << 1)
    table.append(crc & 0xFFFF)
  return table

_CRC_TABLE = _crc_table()


def crc16(data, crc=0):
  for b in data:
    crc = ((crc << 8) ^ _CRC_TABLE[((crc >> 8) ^ b) & 0xFF]) & 0xFFFF
  return crc


class CmdChannel:
  """read(n)/write(b) 만 있으면 되는 전송계층 위의 클라이언트."""

  def __init__(self, transport):
    self.t = transport

  def request(self, cmd, data=b"", timeout=3.0):
    self.t.flush_input()
    self.t.write(build(cmd, data))

    buf = b""
    t0 = time.time()
    while time.time() - t0 < timeout:
      buf += self.t.read(1100)
      # STX 를 찾아 정렬한다. 같은 UART 로 로그가 섞여 들어올 수 있다.
      i = buf.find(bytes([STX0, STX1]))
      if i < 0 or len(buf) - i < 9:
        continue
      _, _, typ, rcmd, err, ln = struct.unpack("<BBBHHH", buf[i:i+9])
      if typ != PKT_TYPE_RESP or rcmd != cmd:
        buf = buf[i+2:]
        continue
      if len(buf) - i < 9 + ln + 1:
        continue
      return {"cmd": rcmd, "err": err, "data": buf[i+9:i+9+ln]}
    raise TimeoutError(f"cmd 0x{cmd:04X} 응답 없음 (받은 {len(buf)}B)")


class SerialTransport:
  def __init__(self, ser):
    self.ser = ser

  def flush_input(self):
    self.ser.reset_input_buffer()

  def write(self, b):
    self.ser.write(b)
    self.ser.flush()

  def read(self, n):
    # 1 바이트만 기다린 뒤 버퍼에 있는 것을 몰아 읽는다 (weact 실측: read(n) 은 매번 타임아웃을 까먹는다)
    first = self.ser.read(1)
    if not first:
      return b""
    n_wait = self.ser.in_waiting
    return first + (self.ser.read(min(n_wait, n)) if n_wait else b"")

  def set_baud(self, baud):
    self.ser.baudrate = baud


#-- 펌웨어 cmd_boot.c 의 boot_info_t 와 바이트 단위로 맞아야 한다 (packed).
#   앞 104 B 는 weact 와 같고, 뒤가 이 보드의 확장이다 (cmd_ver 부터).
INFO_FMT     = "<10I32s32s"
INFO_SZ      = struct.calcsize(INFO_FMT)          # 104
INFO_EXT_FMT = "<5I"
INFO_EXT_SZ  = struct.calcsize(INFO_EXT_FMT)      # 20


def parse_info(d):
  if len(d) < INFO_SZ:
    raise ValueError(f"INFO 응답이 {len(d)} B 다. {INFO_SZ} B 이상이어야 한다")

  f = struct.unpack(INFO_FMT, d[:INFO_SZ])
  out = dict(zip(("magic", "mode", "boot_addr", "boot_size", "firm_addr", "firm_vec_addr",
                  "firm_size", "tag_size", "max_fw_size", "family_id"), f[:10]))
  z = lambda b: b.split(b"\0")[0].decode("ascii", "replace").strip()
  out["name"]     = z(f[10])
  out["version"]  = z(f[11])
  out["mode_str"] = "BOOT" if out["mode"] == DEV_MODE_BOOT else "APP"

  out["cmd_ver"] = 0
  if len(d) >= INFO_SZ + INFO_EXT_SZ:
    e = struct.unpack(INFO_EXT_FMT, d[INFO_SZ:INFO_SZ + INFO_EXT_SZ])
    out.update(zip(("cmd_ver", "boot2_addr", "data_addr", "data_size", "baud"), e))
  return out


VER_FMT  = "<B3sII32s32s"
VER_SZ   = struct.calcsize(VER_FMT)               # 76
IMG_TYPE = ("NONE", "RAW", "VER", "TAG")


def parse_version(d):
  if len(d) < VER_SZ:
    raise ValueError(f"VERSION 응답이 {len(d)} B 다. {VER_SZ} B 이어야 한다")

  img, _rsv, size, crc, name, ver = struct.unpack(VER_FMT, d[:VER_SZ])
  z = lambda s: s.split(b"\0")[0].decode("ascii", "replace").strip()
  return dict(img_type=img, img_str=IMG_TYPE[img] if img < len(IMG_TYPE) else "?",
              fw_size=size, fw_crc=crc, name=z(name), version=z(ver))
