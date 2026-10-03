#ifndef XSPI_H_
#define XSPI_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_XSPI


typedef struct {
  uint32_t FlashSize;          /*!< Size of the flash */
  uint32_t EraseSectorSize;    /*!< Size of sectors for the erase operation */
  uint32_t EraseSectorsNumber; /*!< Number of sectors for the erase operation */
  uint32_t ProgPageSize;       /*!< Size of pages for the program operation */
  uint32_t ProgPagesNumber;    /*!< Number of pages for the program operation */
  uint8_t  device_id[20];
} xspi_info_t;


bool xspiInit(void);
bool xspiIsInit(void);
bool xspiReset(void);
bool xspiRead(uint32_t addr, uint8_t *p_data, uint32_t length);
bool xspiWrite(uint32_t addr, uint8_t *p_data, uint32_t length);
bool xspiErase(uint32_t addr, uint32_t length);
bool xspiEraseBlock(uint32_t block_addr);
bool xspiEraseSector(uint32_t sector_addr);
bool xspiEraseChip(void);
bool xspiGetStatus(void);
bool xspiGetInfo(xspi_info_t* p_info);
bool xspiGetXipMode(void);
bool xspiSetXipMode(bool enable);
uint32_t xspiGetAddr(void);
uint32_t xspiGetLength(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
