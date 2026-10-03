#include "boot.h"
#include "util_core.h"


/* 앱(FW) 영역 레이아웃 (hw_def.h)

     FLASH_ADDR_FIRM       TAG    4 KB   firm_tag_t. 독립 섹터라 앱을 건드리지 않고 지우고 쓸 수 있다
     FLASH_ADDR_FIRM_VEC   VECTOR 1 KB   앱 벡터 테이블
     + FLASH_SIZE_VEC      VER           firm_ver_t
                           이미지 나머지

   TAG 의 fw_crc 는 FLASH_ADDR_FIRM_VEC 부터 fw_size 바이트의 CRC-16(utilCalcCRC) 이다.
   weact-h750 과 같은 형식이라 툴과 코드를 함께 쓴다. 앱 실행(LRUN / XIP)은 아직 없다. */
#define BOOT_CRC_BUF_SIZE     1024




BootImgType_t bootVerifyFirm(void)
{
  firm_tag_t tag;
  firm_ver_t ver;
  uint8_t    buf[BOOT_CRC_BUF_SIZE];
  uint16_t   crc = 0;
  bool       has_ver;


  has_ver = bootGetVer(&ver);

  if (bootGetTag(&tag) != true)
  {
    return has_ver ? BOOT_IMG_VER : BOOT_IMG_NONE;
  }

  // 이미지가 신고한 크기와 다르면 옛 TAG 다 (weact 의 stale tag 규칙)
  if (has_ver == true && ver.firm_size > 0 && ver.firm_size != tag.fw_size)
  {
    return BOOT_IMG_VER;
  }

  for (uint32_t i = 0; i < tag.fw_size; i += sizeof(buf))
  {
    uint32_t n = tag.fw_size - i;

    if (n > sizeof(buf)) n = sizeof(buf);
    if (flashRead(FLASH_ADDR_FIRM_VEC + i, buf, n) != true)
    {
      return BOOT_IMG_NONE;
    }
    crc = utilCalcCRC(crc, buf, n);
  }

  if (crc != tag.fw_crc)
  {
    return has_ver ? BOOT_IMG_VER : BOOT_IMG_NONE;
  }
  return BOOT_IMG_TAG;
}

bool bootGetTag(firm_tag_t *p_tag)
{
  if (flashRead(FLASH_ADDR_FIRM, (uint8_t *)p_tag, sizeof(firm_tag_t)) != true)
    return false;

  if (p_tag->magic_number != TAG_MAGIC_NUMBER)
    return false;

  if (p_tag->tag_crc != utilCalcCRC(0, (uint8_t *)p_tag, sizeof(firm_tag_t) - 4))
    return false;

  if (p_tag->fw_size == 0 || p_tag->fw_size > FLASH_SIZE_FIRM - FLASH_SIZE_TAG)
    return false;

  return true;
}

bool bootGetVer(firm_ver_t *p_ver)
{
  if (flashRead(FLASH_ADDR_FIRM_VEC + FLASH_SIZE_VEC, (uint8_t *)p_ver, sizeof(firm_ver_t)) != true)
    return false;

  return p_ver->magic_number == VERSION_MAGIC_NUMBER;
}
