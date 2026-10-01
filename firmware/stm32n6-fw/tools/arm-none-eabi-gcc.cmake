
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)


# 컴파일러는 Homebrew 것으로 고정한다. (ENV 가 먼저라 명시적 지정은 그대로 우선)
#
# CubeCLT 를 pkg 로 정식 설치하면 postinstall 이 자기 GNU-tools-for-STM32
# (arm gcc 14.3.1) 를 /etc/paths 맨 앞에 끼워넣어 Homebrew 를 가린다.
# 지금은 필요한 것만 ~/ST 에 추출해 쓰므로 그 일이 없지만, 누가 pkg 를 설치해도
# 빌드 결과가 조용히 바뀌지 않도록 힌트를 박아 둔다.
#
set(ARM_POSSIBLE_PATHS
    "C:/work/tools/baram-fw-tools/arm_toolchain/arm_gcc/test"
    ENV ARM_TOOLCHAIN_DIR
)

if(CMAKE_HOST_APPLE)
  list(APPEND ARM_POSSIBLE_PATHS "/opt/homebrew/bin")
endif()

# make 도 같은 이유로 시스템 것을 먼저 찾는다. CubeCLT 안의 make 를 잡으면 캐시에
# /opt/ST/STM32CubeCLT_<버전>/Make/bin/make 가 박히고, CubeCLT 를 갈아끼운 순간
# "build tool execution failed" 로 빌드가 깨진다. (실제로 한 번 겪었다)
#
set(MAKE_POSSIBLE_PATHS
    "c:/MinGW-32/bin"
    ENV MAKE_DIR    
)

# /usr/bin 은 Windows 에서는 넣지 않는다. MSYS2 / Git Bash 환경에서 /usr/bin/make
# (MSYS make) 가 잡히면 생성기와 맞지 않아 더 곤란해진다.
#
if(NOT CMAKE_HOST_WIN32)
  list(INSERT MAKE_POSSIBLE_PATHS 0 "/usr/bin")
endif()

find_program(ARM_TOOLCHAIN_DIR
    NAMES arm-none-eabi-gcc.exe arm-none-eabi-gcc
    HINTS ${ARM_POSSIBLE_PATHS}
    PATH_SUFFIXES bin
    DOC "ARM GCC Toolchain Directory"
)

if(NOT ARM_TOOLCHAIN_DIR)
    message(FATAL_ERROR "ARM Toolchain not found. Please set ARM_TOOLCHAIN_DIR environment variable")
endif()



find_program(CMAKE_MAKE_PROGRAM
  NAMES make
        make.exe
  DOC "Find a suitable make program for building under Windows/MinGW"
  HINTS ${MAKE_POSSIBLE_PATHS}
) 

if(NOT CMAKE_MAKE_PROGRAM)
    message(FATAL_ERROR "Make program not found. Please set MINGW_DIR environment variable")
else()
    message(STATUS "Found Make program: ${CMAKE_MAKE_PROGRAM}")
endif()


# ARM_TOOLCHAIN_DIR에서 실행 파일 이름을 제거하고 경로만 추출  
get_filename_component(TOOLCHAIN_PATH "${ARM_TOOLCHAIN_DIR}" DIRECTORY)
set(TOOLCHAIN_PREFIX "${TOOLCHAIN_PATH}/arm-none-eabi-")



set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if (WIN32)
set(CMAKE_C_COMPILER "${TOOLCHAIN_PREFIX}gcc.exe" CACHE FILEPATH "C Compiler path")
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++.exe" CACHE FILEPATH "C++ Compiler path")
else()
set(CMAKE_C_COMPILER "${TOOLCHAIN_PREFIX}gcc" CACHE FILEPATH "C Compiler path")
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++" CACHE FILEPATH "C++ Compiler path")
endif()

set(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy CACHE INTERNAL "objcopy tool")
set(CMAKE_SIZE_UTIL ${TOOLCHAIN_PREFIX}size CACHE INTERNAL "size tool")

set(CMAKE_C_STANDARD    11)
set(CMAKE_CXX_STANDARD  17)

# Disable compiler checks.
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

set(CMAKE_FIND_ROOT_PATH ${BINUTILS_PATH})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)