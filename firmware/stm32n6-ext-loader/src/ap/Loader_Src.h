#ifndef LOADER_SRC_H_
#define LOADER_SRC_H_

#include "hw.h"


// CubeProgrammer 가 심볼 이름으로 찾아 부르는 함수. 성공 1, 실패 0
int Init(void);
int Write(uint32_t Address, uint32_t Size, uint8_t *buffer);
int SectorErase(uint32_t EraseStartAddress, uint32_t EraseEndAddress);
int MassErase(uint32_t Parallelism);


#endif
