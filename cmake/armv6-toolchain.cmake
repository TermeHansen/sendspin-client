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

# Find libatomic.so.1 explicitly in common ARM paths
find_library(ATOMIC_LIBRARY 
    NAMES libatomic.so.1 atomic
    HINTS
        /usr/lib/arm-linux-gnueabihf
        /usr/lib/gcc/arm-linux-gnueabihf/14
        /usr/lib
        /lib
)

if(ATOMIC_LIBRARY)
    message(STATUS "Found libatomic: ${ATOMIC_LIBRARY}")
    # Get the directory containing the library
    get_filename_component(ATOMIC_DIR ${ATOMIC_LIBRARY} DIRECTORY)
    # Add the directory to linker search paths
    set(ARM_LIBS "-L${ATOMIC_DIR} -latomic")
else()
    # Fallback: use standard ARM paths
    message(WARNING "libatomic.so.1 not found in standard paths, using default ARM paths")
    set(ARM_LIBS "-L/usr/lib/arm-linux-gnueabihf -L/usr/lib/gcc/arm-linux-gnueabihf/14 -latomic")
endif()

# Apply linker flags
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ARM_LIBS}" CACHE INTERNAL "Linker flags with ARM libraries")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${ARM_LIBS}" CACHE INTERNAL "Linker flags with ARM libraries")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${ARM_LIBS}" CACHE INTERNAL "Linker flags with ARM libraries")

# Set find root path for cross-compilation (if needed)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Message to confirm ARMv6 configuration
message(STATUS "Configuring for ARMv6 (Raspberry Pi 1) with flags: ${ARMv6_COMPILE_FLAGS}")

