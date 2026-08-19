# CMake toolchain file for ARMv6 (Raspberry Pi 1)
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/armv6-toolchain.cmake ..

# Target architecture
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Compiler flags for ARMv6
set(CMAKE_C_FLAGS "-marm -march=armv6 -mfpu=vfp -mfloat-abi=hard" CACHE INTERNAL "C compiler flags for ARMv6")
set(CMAKE_CXX_FLAGS "-marm -march=armv6 -mfpu=vfp -mfloat-abi=hard" CACHE INTERNAL "C++ compiler flags for ARMv6")

# Find and link libatomic for 64-bit atomic operations
find_library(LIBATOMIC atomic)
if(LIBATOMIC)
    message(STATUS "Found libatomic: ${LIBATOMIC}")
    set(CMAKE_EXE_LINKER_FLAGS "-latomic" CACHE INTERNAL "Linker flags for ARMv6")
    set(CMAKE_SHARED_LINKER_FLAGS "-latomic" CACHE INTERNAL "Shared linker flags for ARMv6")
    set(CMAKE_MODULE_LINKER_FLAGS "-latomic" CACHE INTERNAL "Module linker flags for ARMv6")
else()
    message(WARNING "libatomic not found - 64-bit atomics may fail on ARMv6")
endif()

# Set find root path for cross-compilation (if needed)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
