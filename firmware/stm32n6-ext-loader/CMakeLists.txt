cmake_minimum_required(VERSION 3.13)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools")
include(arm-none-eabi-gcc)


# 프로젝트 이름은 최종 출력 파일 이름으로 사용된다.
#   STM32CubeProgrammer 의 외부 로더(.stldr) 이름이 된다.
#
set(PRJ_NAME MX25UM51245G_NUCLEO-N657X0)


project(${PRJ_NAME}
  LANGUAGES ASM C CXX
)


set(EXECUTABLE ${PRJ_NAME}.stldr)


# 펌웨어 프로젝트와 같은 코드를 쓴다.
#   HAL / CMSIS 와 xspi 드라이버를 복사하지 않고 그대로 가져와서
#   펌웨어에서 고친 것이 로더에도 바로 반영되게 한다.
#
set(FW_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../stm32n6-fw)


file(GLOB_RECURSE SRC_FILES_RECURSE CONFIGURE_DEPENDS
  src/ap/*.c
  src/bsp/*.c
  src/hw/*.c
  )

set(SRC_FILES_FW
  ${FW_DIR}/src/hw/driver/led.c
  ${FW_DIR}/src/hw/driver/xspi.c
  ${FW_DIR}/src/bsp/device/system_stm32n6xx_fsbl.c
  )

set(HAL_DIR ${FW_DIR}/src/lib/ST/STM32N6xx_HAL_Driver/Src)
set(SRC_FILES_ST
  ${HAL_DIR}/stm32n6xx_hal.c
  ${HAL_DIR}/stm32n6xx_hal_cortex.c
  ${HAL_DIR}/stm32n6xx_hal_rcc.c
  ${HAL_DIR}/stm32n6xx_hal_rcc_ex.c
  ${HAL_DIR}/stm32n6xx_hal_gpio.c
  ${HAL_DIR}/stm32n6xx_hal_exti.c
  ${HAL_DIR}/stm32n6xx_hal_dma.c
  ${HAL_DIR}/stm32n6xx_hal_dma_ex.c
  ${HAL_DIR}/stm32n6xx_hal_pwr.c
  ${HAL_DIR}/stm32n6xx_hal_pwr_ex.c
  ${HAL_DIR}/stm32n6xx_hal_xspi.c
  )


add_executable(${EXECUTABLE}
  ${SRC_FILES_RECURSE}
  ${SRC_FILES_FW}
  ${SRC_FILES_ST}
  )


# 로더 쪽(src)을 먼저 둔다. bsp.h / hw_def.h / hal_conf.h 는 로더 것을 쓴다.
#
target_include_directories(${EXECUTABLE} PRIVATE
  src
  src/ap
  src/bsp
  src/bsp/device
  src/hw

  ${FW_DIR}/src/common
  ${FW_DIR}/src/common/core
  ${FW_DIR}/src/common/hw/include
  ${FW_DIR}/src/lib/ST/CMSIS/Include
  ${FW_DIR}/src/lib/ST/CMSIS/Device/ST/STM32N6xx/Include
  ${FW_DIR}/src/lib/ST/STM32N6xx_HAL_Driver/Inc
  )

target_compile_definitions(${EXECUTABLE} PRIVATE
  -DSTM32N657xx
  -DUSE_HAL_DRIVER
  )

target_compile_options(${EXECUTABLE} PRIVATE
  -mcpu=cortex-m55
  -mthumb
  -mfpu=fpv5-d16
  -mfloat-abi=hard
  -mcmse

  -fdata-sections
  -ffunction-sections

  -fstack-usage
  -Wmissing-field-initializers
  -Wuninitialized

  -Wall
  -g3
  -Os
  )

# 로그와 CLI 가 빠지면 펌웨어 드라이버에서 그쪽에만 쓰던 표가 남는다
set_source_files_properties(
  ${SRC_FILES_FW}
  PROPERTIES
    COMPILE_OPTIONS "-Wno-unused-variable;-Wno-unused-const-variable"
  )

set_source_files_properties(
  ${SRC_FILES_ST}
  PROPERTIES
    COMPILE_OPTIONS "-w"
  )

target_link_options(${EXECUTABLE} PRIVATE
  -T${CMAKE_CURRENT_SOURCE_DIR}/src/bsp/ldscript/stm32n6_ext_loader.ld

  -mcpu=cortex-m55
  -mthumb
  -mfpu=fpv5-d16
  -mfloat-abi=hard
  -specs=nano.specs
  -specs=nosys.specs
  -nostartfiles

  -Wl,-Map=${PRJ_NAME}.map,--cref
  -Wl,--gc-sections
  -Wl,--no-warn-rwx-segments
  -Xlinker -print-memory-usage -Xlinker
  )

# 링커 스크립트만 고쳐도 다시 링크되게 한다
set_target_properties(${EXECUTABLE} PROPERTIES
  LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/bsp/ldscript/stm32n6_ext_loader.ld
  )

set(CMAKE_C_FLAGS   "-Wenum-conversion")
set(CMAKE_CXX_FLAGS "-fno-rtti")
