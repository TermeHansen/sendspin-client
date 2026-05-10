cmake_minimum_required(VERSION 3.10)

project(sendspin-client)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the sendspin-cpp submodule with client-only configuration
# Disable roles we don't need to minimize build dependencies
set(SENDSPIN_ENABLE_PLAYER ON CACHE BOOL "Enable player role" FORCE)
set(SENDSPIN_ENABLE_CONTROLLER ON CACHE BOOL "Enable controller role" FORCE)
set(SENDSPIN_ENABLE_METADATA ON CACHE BOOL "Enable metadata role" FORCE)
set(SENDSPIN_ENABLE_COLOR OFF CACHE BOOL "Disable color role" FORCE)
set(SENDSPIN_ENABLE_ARTWORK OFF CACHE BOOL "Disable artwork role" FORCE)
set(SENDSPIN_ENABLE_VISUALIZER OFF CACHE BOOL "Disable visualizer role" FORCE)

# Disable building examples when using as submodule
set(BUILD_EXAMPLES OFF CACHE BOOL "Disable building sendspin-cpp examples" FORCE)

add_subdirectory(sendspin-cpp)

# Find PortAudio for audio playback
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PORTAUDIO portaudio-2.0)
endif()

# Create the client executable
add_executable(sendspin-client
    src/main.cpp
    src/common/portaudio_sink.cpp
)

# Link against the sendspin library
if(TARGET sendspin)
    target_link_libraries(sendspin-client PRIVATE sendspin)
else()
    message(FATAL_ERROR "sendspin library target not found")
endif()

# Link PortAudio if available
if(PORTAUDIO_FOUND)
    target_link_libraries(sendspin-client PRIVATE ${PORTAUDIO_LINK_LIBRARIES})
    target_include_directories(sendspin-client PRIVATE ${PORTAUDIO_INCLUDE_DIRS})
    target_compile_definitions(sendspin-client PRIVATE SENDSPIN_HAS_PORTAUDIO=1)
    message(STATUS "PortAudio found - audio playback enabled")
else()
    message(STATUS "PortAudio not found - audio playback disabled")
endif()

# mDNS service advertisement (for zeroconf discovery)
if(NOT APPLE)
    find_library(DNS_SD_LIBRARY dns_sd)
    if(DNS_SD_LIBRARY)
        target_link_libraries(sendspin-client PRIVATE ${DNS_SD_LIBRARY})
    endif()
endif()

# Include directories
target_include_directories(sendspin-client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sendspin-cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)
