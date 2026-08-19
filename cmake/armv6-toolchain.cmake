# CMake toolchain file for ARMv6 (Raspberry Pi 1)
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/armv6-toolchain.cmake ..

# Target architecture
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv6l)

# Compiler flags for ARMv6 (Raspberry Pi 1 CPU: arm1176jzf-s)
# -mcpu=arm1176jzf-s: Optimize for the specific CPU in Raspberry Pi 1
# -march=armv6: Target ARMv6 architecture
# -mfpu=vfp: Use VFP floating point unit
# -mfloat-abi=hard: Use hardware floating point
set(ARMv6_COMPILE_FLAGS "-mcpu=arm1176jzf-s -march=armv6 -mfpu=vfp -mfloat-abi=hard")

set(CMAKE_C_FLAGS "${ARMv6_COMPILE_FLAGS}" CACHE INTERNAL "C compiler flags for ARMv6")
set(CMAKE_CXX_FLAGS "${ARMv6_COMPILE_FLAGS}" CACHE INTERNAL "C++ compiler flags for ARMv6")

# Linker flags
set(CMAKE_EXE_LINKER_FLAGS "${ARMv6_COMPILE_FLAGS}" CACHE INTERNAL "Executable linker flags for ARMv6")
set(CMAKE_SHARED_LINKER_FLAGS "${ARMv6_COMPILE_FLAGS}" CACHE INTERNAL "Shared linker flags for ARMv6")
set(CMAKE_MODULE_LINKER_FLAGS "${ARMv6_COMPILE_FLAGS}" CACHE INTERNAL "Module linker flags for ARMv6")

# Find and link libatomic (required for ARMv6 as it lacks hardware atomic instructions)
find_library(LIBATOMIC atomic)
if(LIBATOMIC)
    message(STATUS "Found libatomic: ${LIBATOMIC}")
    # Add -latomic to linker flags
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -latomic" CACHE INTERNAL "Executable linker flags with atomic")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -latomic" CACHE INTERNAL "Shared linker flags with atomic")
else()
    message(WARNING "libatomic not found - ARMv6 builds may fail without atomic operations")
endif()

# Set find root path for cross-compilation (if needed)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Message to confirm ARMv6 configuration
message(STATUS "Configuring for ARMv6 (Raspberry Pi 1) with flags: ${ARMv6_COMPILE_FLAGS}")

