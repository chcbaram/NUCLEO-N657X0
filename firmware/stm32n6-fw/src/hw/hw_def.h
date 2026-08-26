#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION    "V260826R1"
#define _DEF_BOARD_NAME           "STM32N6-FW"


//-- LED
//   NUCLEO-N657X0-Q (MB1940)
//     LED1 : PG8  LD7 BLUE
//     LED2 : PG10 LD5 RED    <- BootROM 이 blocking failure 시 UART5_TX(9600) 로 쓰는 핀
//     LED3 : PG0  LD6 GREEN
//   전부 active low (V_DDIO - LED - 330R - MCU)
//
#define _USE_HW_LED
#define      HW_LED_MAX_CH          3


#endif
