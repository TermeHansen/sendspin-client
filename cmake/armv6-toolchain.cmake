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

# Linker flags
set(CMAKE_EXE_LINKER_FLAGS "${ARMv6_COMPILE_FLAGS}" CACHE INTERNAL "Executable linker flags for ARMv6")
set(CMAKE_SHARED_LINKER_FLAGS "${ARMv6_COMPILE_FLAGS}" CACHE INTERNAL "Shared linker flags for ARMv6")
set(CMAKE_MODULE_LINKER_FLAGS "${ARMv6_COMPILE_FLAGS}" CACHE INTERNAL "Module linker flags for ARMv6")

# Link libatomic (required for ARMv6 as it lacks hardware atomic instructions)
# For ARMv6, we always need to link -latomic
message(STATUS "ARMv6: Configuring libatomic support")

# Explicitly add both ARM library paths to linker search
# This ensures the linker can find libatomic in either location
set(ARM_LIBS "-L/usr/lib/arm-linux-gnueabihf -L/usr/lib/gcc/arm-linux-gnueabihf/14 -latomic")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ARM_LIBS}" CACHE INTERNAL "Linker flags with ARM libraries")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${ARM_LIBS}" CACHE INTERNAL "Linker flags with ARM libraries")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${ARM_LIBS}" CACHE INTERNAL "Linker flags with ARM libraries")

# Also add to CMake's library search paths for find_library
list(APPEND CMAKE_PREFIX_PATH /usr/lib/arm-linux-gnueabihf /usr/lib/gcc/arm-linux-gnueabihf/14)
list(APPEND CMAKE_LIBRARY_PATH /usr/lib/arm-linux-gnueabihf /usr/lib/gcc/arm-linux-gnueabihf/14)

# Try to find libatomic (for informational purposes)
find_library(LIBATOMIC atomic 
    PATHS /usr/lib/arm-linux-gnueabihf /usr/lib/gcc/arm-linux-gnueabihf/14
    NO_DEFAULT_PATH
)
if(LIBATOMIC)
    message(STATUS "Found libatomic at: ${LIBATOMIC}")
else()
    message(STATUS "libatomic not found via find_library, but linker paths added")
endif()

# Set find root path for cross-compilation (if needed)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Message to confirm ARMv6 configuration
message(STATUS "Configuring for ARMv6 (Raspberry Pi 1) with flags: ${ARMv6_COMPILE_FLAGS}")

