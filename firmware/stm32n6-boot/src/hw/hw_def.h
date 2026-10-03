#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION    "V260826R1"
#define _DEF_BOARD_NAME           "STM32N6-BOOT"

#define _USE_HW_BOOTROM
#define _USE_HW_OTP
#define _USE_HW_FLASH


#define _USE_HW_LED
#define      HW_LED_MAX_CH          3


#define _USE_HW_UART
#define      HW_UART_MAX_CH         1
#define      HW_UART_CH_SWD         _DEF_UART1
#define      HW_UART_CH_CLI         HW_UART_CH_SWD


#define _USE_HW_CLI
#define      HW_CLI_CMD_LIST_MAX    32
#define      HW_CLI_CMD_NAME_MAX    16
#define      HW_CLI_LINE_HIS_MAX    8
#define      HW_CLI_LINE_BUF_MAX    64

#define _USE_HW_LOG
#define      HW_LOG_CH              HW_UART_CH_SWD
#define      HW_LOG_BOOT_BUF_MAX    2048
#define      HW_LOG_LIST_BUF_MAX    4096


#define _USE_HW_RTC
#define      HW_RTC_BOOT_MODE       RTC_BKP_DR3
#define      HW_RTC_RESET_BITS      RTC_BKP_DR4
#define      HW_RTC_RESET_CNT       RTC_BKP_DR5
#define      HW_RTC_BOOT_TRY        RTC_BKP_DR6
#define      HW_RTC_FAULT_CNT       RTC_BKP_DR7

#define _USE_HW_RESET
#define      HW_RESET_BOOT          1
#define      HW_RESET_DBLCLK_MS     300
#define      HW_RESET_DBLCLK_CNT    2

#define _USE_HW_XSPI
#define      HW_XSPI_ADDR           0x70000000
#define      HW_XSPI_SIZE           (64*1024*1024)

#define _USE_HW_CMD
#define      HW_CMD_MAX_DATA_LENGTH 1024
#define      HW_CMD_UART_CH         HW_UART_CH_CLI


#define HW_DEV_MODE                 HW_DEV_MODE_BOOT

#define FLASH_ADDR_BOOT             0x70000000
#define FLASH_ADDR_BOOT2            0x70040000
#define FLASH_SIZE_BOOT             (256*1024)
#define FLASH_ADDR_FIRM             0x70100000
#define FLASH_SIZE_FIRM             (16*1024*1024)
#define FLASH_SIZE_TAG              0x1000
#define FLASH_SIZE_VEC              0x400
#define FLASH_ADDR_FIRM_VEC         (FLASH_ADDR_FIRM + FLASH_SIZE_TAG)
#define FLASH_ADDR_DATA             0x71100000
#define FLASH_SIZE_DATA             (HW_XSPI_ADDR + HW_XSPI_SIZE - FLASH_ADDR_DATA)



//-- CLI
//
#define _USE_CLI_HW_LOG             1
#define _USE_CLI_HW_UART            1
#define _USE_CLI_HW_RTC             1
#define _USE_CLI_HW_RESET           1
#define _USE_CLI_HW_MODULE          1
#define _USE_CLI_HW_BOOTROM         1
#define _USE_CLI_HW_OTP             1
#define _USE_CLI_HW_XSPI            1


#endif
