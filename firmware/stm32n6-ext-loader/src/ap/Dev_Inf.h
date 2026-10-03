#ifndef DEV_INF_H_
#define DEV_INF_H_


// STM32CubeProgrammer 가 읽는 메모리 정보. 필드 배치는 CubeProgrammer 와 약속된 것이라 바꾸지 않는다.

#define MCU_FLASH       1
#define NAND_FLASH      2
#define NOR_FLASH       3
#define SRAM            4
#define PSRAM           5
#define PC_CARD         6
#define SPI_FLASH       7
#define I2C_FLASH       8
#define SDRAM           9
#define I2C_EEPROM      10

#define SECTOR_NUM      10


struct DeviceSectors
{
  unsigned long SectorNum;
  unsigned long SectorSize;
};

struct StorageInfo
{
  char                 DeviceName[100];
  unsigned short       DeviceType;
  unsigned long        DeviceStartAddress;
  unsigned long        DeviceSize;
  unsigned long        PageSize;
  unsigned char        EraseValue;
  struct DeviceSectors sectors[SECTOR_NUM];
};


#endif
