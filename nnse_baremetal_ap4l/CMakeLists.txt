cmake_minimum_required(VERSION 3.10)

# Project name and version
project(nnse_baremetal VERSION 1.0)

# Set C standard
set(CMAKE_C_STANDARD 11)

# Check if we're building for embedded target
if(CMAKE_CROSSCOMPILING)
    # Target microcontroller
    set(TARGET_CHIP "apollo5")
    
    # Add compiler definitions for Ambiq microcontrollers
    add_compile_definitions(AM_PART_APOLLO5)
    add_compile_definitions(AM_CUSTOM_BSP)
    
    # Set compiler flags for embedded target
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections")
    
    # Set linker flags
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
endif()

# Include directories
include_directories(src)

# Source files
file(GLOB SOURCES "src/*.c")

# Exclude backup files
list(FILTER SOURCES EXCLUDE REGEX ".*\\.(bak|dnc)$")

# Create executable
add_executable(nnse_baremetal ${SOURCES})

# Link libraries
target_link_libraries(nnse_baremetal 
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/ns-nnsp.a
    m  # Math library
)

# Compiler flags
target_compile_options(nnse_baremetal PRIVATE -Wall -O2)

# Note: For a complete embedded build, you would need:
# 1. A proper toolchain file for ARM GCC
# 2. Definitions for the target microcontroller
# 3. Linker scripts for the target
# 4. Proper include paths for AmbiqSuite SDK headers
# 5. Complete compiler flags required by the Ambiq microcontroller
#
# See BUILDING.md for more detailed instructions.