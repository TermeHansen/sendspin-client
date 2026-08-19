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

# Add common ARM library paths to help find libatomic
list(APPEND CMAKE_PREFIX_PATH /usr/lib/arm-linux-gnueabihf /usr/lib/gcc/arm-linux-gnueabihf/14)

# Find libatomic and add its directory to the link path
find_library(LIBATOMIC atomic 
    PATHS /usr/lib/arm-linux-gnueabihf /usr/lib/gcc/arm-linux-gnueabihf/14
    NO_DEFAULT_PATH
)

if(LIBATOMIC)
    message(STATUS "Found libatomic at: ${LIBATOMIC}")
    # Get the directory containing libatomic
    get_filename_component(LIBATOMIC_DIR ${LIBATOMIC} DIRECTORY)
    # Add the directory to the linker search path
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${LIBATOMIC_DIR}" CACHE INTERNAL "Linker flags with libatomic path")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -L${LIBATOMIC_DIR}" CACHE INTERNAL "Linker flags with libatomic path")
else()
    # Fallback: try without path restriction
    find_library(LIBATOMIC atomic)
    if(LIBATOMIC)
        message(STATUS "Found libatomic at: ${LIBATOMIC}")
        get_filename_component(LIBATOMIC_DIR ${LIBATOMIC} DIRECTORY)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${LIBATOMIC_DIR}" CACHE INTERNAL "Linker flags with libatomic path")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -L${LIBATOMIC_DIR}" CACHE INTERNAL "Linker flags with libatomic path")
    else()
        message(WARNING "libatomic not found, but -latomic will be added to linker flags")
    endif()
endif()

# Always add -latomic to link against the library
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -latomic" CACHE INTERNAL "Linker flags with atomic library")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -latomic" CACHE INTERNAL "Linker flags with atomic library")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -latomic" CACHE INTERNAL "Linker flags with atomic library")

# Set find root path for cross-compilation (if needed)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Message to confirm ARMv6 configuration
message(STATUS "Configuring for ARMv6 (Raspberry Pi 1) with flags: ${ARMv6_COMPILE_FLAGS}")

