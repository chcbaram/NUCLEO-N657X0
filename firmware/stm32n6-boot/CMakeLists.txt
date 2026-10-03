cmake_minimum_required(VERSION 3.13)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools")
include(arm-none-eabi-gcc)


# 프로젝트 이름은 최종 출력 파일 이름으로 사용된다.
#
set(PRJ_NAME stm32n6-boot)


project(${PRJ_NAME}
  LANGUAGES ASM C CXX
)


set(EXECUTABLE ${PRJ_NAME}.elf)


# 지정한 폴더에 있는 파일만 포함한다.
#
file(GLOB SRC_FILES CONFIGURE_DEPENDS
  src/*.c
  src/*.cpp
  )

# 하위폴더에 있는 파일까지 포함한다.
#
file(GLOB_RECURSE SRC_FILES_RECURSE CONFIGURE_DEPENDS
  src/ap/*.c
  src/ap/*.cpp
  src/bsp/*.c
  src/bsp/*.s
  src/common/*.c
  src/hw/*.c
  )

# ST HAL 은 glob 하지 않고 사용하는 모듈만 명시한다.
#   - stm32n6xx_hal_conf.h 의 HAL_xxx_MODULE_ENABLED 와 짝을 맞춘다
#   - 기능을 추가할 때 여기에 한 줄씩 늘린다
#   (전체 glob 을 하면 비활성 모듈 소스가 같이 잡혀 빌드가 깨진다. 예: ll_dlyb)
#
set(HAL_DIR src/lib/ST/STM32N6xx_HAL_Driver/Src)
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
  ${HAL_DIR}/stm32n6xx_hal_uart.c
  ${HAL_DIR}/stm32n6xx_hal_uart_ex.c
  ${HAL_DIR}/stm32n6xx_hal_bsec.c
  ${HAL_DIR}/stm32n6xx_hal_xspi.c
  ${HAL_DIR}/stm32n6xx_hal_rtc.c
  ${HAL_DIR}/stm32n6xx_hal_rtc_ex.c
  )


add_executable(${EXECUTABLE}
  ${SRC_FILES}
  ${SRC_FILES_RECURSE}
  ${SRC_FILES_ST}
  )


target_include_directories(${EXECUTABLE} PRIVATE
  src
  src/ap
  src/ap/modules
  src/ap/modules/cmd
  src/bsp
  src/bsp/device
  src/common
  src/common/core
  src/common/hw/include
  src/hw
  src/hw/driver
  src/lib

  src/lib/ST/CMSIS/Include
  src/lib/ST/CMSIS/Device/ST/STM32N6xx/Include
  src/lib/ST/STM32N6xx_HAL_Driver/Inc
  )

target_compile_definitions(${EXECUTABLE} PRIVATE
  -DSTM32N657xx
  -DUSE_HAL_DRIVER
  -DUSE_FULL_ASSERT
  )

target_compile_options(${EXECUTABLE} PRIVATE
  -mcpu=cortex-m55
  -mthumb
  -mfpu=fpv5-d16
  -mfloat-abi=hard
  # FSBL 은 secure state 로 실행된다. CMSIS core 헤더가 SAU / SCB_NS 를
  # 노출하려면 __ARM_FEATURE_CMSE == 3 이어야 하고, 그건 -mcmse 로 켜진다.
  -mcmse

  -fdata-sections
  -ffunction-sections

  -fstack-usage
  -Wmissing-field-initializers
  -Wuninitialized

  -Wall
  -g3
  -O0
  )

set_source_files_properties(
  ${SRC_FILES_ST}
  PROPERTIES
    COMPILE_OPTIONS "-O2;-w"
  )

target_link_options(${EXECUTABLE} PRIVATE
  -T${CMAKE_CURRENT_SOURCE_DIR}/src/bsp/ldscript/STM32N657XX_AXISRAM2_fsbl.ld

  -mcpu=cortex-m55
  -mthumb
  -mfpu=fpv5-d16
  -mfloat-abi=hard
  -mcmse
  -specs=nano.specs
  -specs=nosys.specs

  -lc
  -lm
  -fno-rtti

  -Wl,-Map=${PRJ_NAME}.map,--cref
  -Wl,--gc-sections
  -Wl,--no-warn-rwx-segments
  -Xlinker -print-memory-usage -Xlinker
  )

set(CMAKE_C_FLAGS   "-Wenum-conversion")
set(CMAKE_CXX_FLAGS "-fno-rtti")


# .elf -> .bin
#
add_custom_command(TARGET ${EXECUTABLE}
  POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} ARGS -O binary ${EXECUTABLE} ${PRJ_NAME}.bin
  COMMENT "Invoking: Make Bin"
  )


# .bin -> -trusted.bin (Flash boot 용 헤더, 키 없이 서명)
#   CubeCLT 의 STM32_SigningTool_CLI 를 쓴다. 못 찾으면 서명만 건너뛴다.
#   찾는 순서 : $CLT -> ~/ST/STM32CubeCLT(_*) -> /opt/ST -> C:/ST
#   -nk : 키 없음 (secure_boot 퓨즈를 태우지 않은 보드 전용), -align : 페이로드를 0x400 에 맞춘다
#   서명 도구가 결과를 읽기 전용으로 만들어서 먼저 지운다.
#
file(GLOB CLT_CANDIDATES
  "$ENV{HOME}/ST/STM32CubeCLT_*"
  "/opt/ST/STM32CubeCLT_*"
  "C:/ST/STM32CubeCLT_*"
  )
set(CLT_HINTS "$ENV{CLT}" "$ENV{HOME}/ST/STM32CubeCLT" "/opt/ST/STM32CubeCLT" ${CLT_CANDIDATES})
list(TRANSFORM CLT_HINTS APPEND "/STM32CubeProgrammer/bin")
find_program(STM32_SIGNING_TOOL STM32_SigningTool_CLI HINTS ${CLT_HINTS})

if(STM32_SIGNING_TOOL)
  add_custom_command(TARGET ${EXECUTABLE}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -f ${PRJ_NAME}-trusted.bin
    COMMAND ${STM32_SIGNING_TOOL} -bin ${PRJ_NAME}.bin -nk -of 0x80000000 -t fsbl -hv 2.3 -align -s
            -o ${PRJ_NAME}-trusted.bin > ${CMAKE_CURRENT_BINARY_DIR}/sign.log
    COMMENT "Invoking: Sign Bin (${PRJ_NAME}-trusted.bin)"
    )
else()
  message(WARNING "STM32_SigningTool_CLI 를 찾지 못해 -trusted.bin 을 만들지 않는다 (CLT=<CubeCLT 경로>)")
endif()
