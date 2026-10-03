#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION    "V261003R1"
#define _DEF_BOARD_NAME           "MX25UM51245G_NUCLEO-N657X0"


#define _USE_HW_LED
#define      HW_LED_MAX_CH          3


#define _USE_HW_XSPI
#define      HW_XSPI_ADDR           0x70000000
#define      HW_XSPI_SIZE           (64*1024*1024)


#endif
