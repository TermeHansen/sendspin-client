# CMake toolchain file for ARMv6 (Raspberry Pi 1)
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/armv6-toolchain.cmake ..

# Target architecture
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv6l)

# Compiler flags for ARMv6 (Raspberry Pi 1 and other ARMv6 processors)
# -march=armv6: Target ARMv6 architecture
# -marm: Use ARM instruction set (not Thumb) - required for ARMv6 with hard-float
# -mfpu=vfp: Use VFP floating point unit
# -mfloat-abi=hard: Use hardware floating point ABI
set(ARMv6_COMPILE_FLAGS "-march=armv6 -marm -mfpu=vfp -mfloat-abi=hard")

set(CMAKE_C_FLAGS "${ARMv6_COMPILE_FLAGS}" CACHE INTERNAL "C compiler flags for ARMv6")
set(CMAKE_CXX_FLAGS "${ARMv6_COMPILE_FLAGS}" CACHE INTERNAL "C++ compiler flags for ARMv6")

# Set find root path for cross-compilation (if needed)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Message to confirm ARMv6 configuration
message(STATUS "Configuring for ARMv6 (Raspberry Pi 1) with flags: ${ARMv6_COMPILE_FLAGS}")

