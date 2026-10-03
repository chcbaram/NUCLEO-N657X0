#include "Dev_Inf.h"
#include "hw_def.h"


// 지우기 단위는 64 KB 블록. SectorErase() 가 이 크기로 지운다
//
struct StorageInfo const StorageInfo =
{
  _DEF_BOARD_NAME,                              // CubeProgrammer 목록에 보이는 이름
  NOR_FLASH,
  HW_XSPI_ADDR,                                 // 0x70000000
  HW_XSPI_SIZE,                                 // 64 MB
  0x1000,                                       // Write() 한 번에 받는 크기
  0xFF,
  {
    {HW_XSPI_SIZE / 0x10000, 0x10000},          // 1024 x 64 KB
    {0x00000000, 0x00000000},
  }
};
