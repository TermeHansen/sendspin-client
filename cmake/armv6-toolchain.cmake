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
# For ARMv6, we need to link libatomic at the END of the linker command
# Library order matters: atomic library must come after object files that use it
message(STATUS "ARMv6: Configuring libatomic support")

# Use the full path to libatomic.so.1 that we know works
set(ATOMIC_LIB_PATH "/usr/lib/arm-linux-gnueabihf/libatomic.so.1")

# Check if the library exists at this path
if(EXISTS "${ATOMIC_LIB_PATH}")
    message(STATUS "Using explicit libatomic: ${ATOMIC_LIB_PATH}")
    # For CMake, we add the library to CMAKE_EXE_LINKER_FLAGS
    # CMake will place it at the appropriate position in the link command
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ATOMIC_LIB_PATH}" CACHE INTERNAL "Linker flags with explicit atomic library")
else()
    # Fallback: try to find it
    message(WARNING "${ATOMIC_LIB_PATH} not found, trying to find libatomic")
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
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ATOMIC_LIBRARY}" CACHE INTERNAL "Linker flags with found atomic library")
    else()
        message(WARNING "libatomic not found, linking may fail")
    endif()
endif()

# Set find root path for cross-compilation (if needed)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Message to confirm ARMv6 configuration
message(STATUS "Configuring for ARMv6 (Raspberry Pi 1) with flags: ${ARMv6_COMPILE_FLAGS}")

