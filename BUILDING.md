# Building the nnse_baremetal Demo

This document provides instructions for building the NeuralSPOT Speech Enhancement Bare Metal Demo using different build systems.

## Prerequisites

Before building, ensure you have the following:

1. **AmbiqSuite SDK** (R5.2.0 or later)
2. **GCC ARM Embedded Toolchain** (9-2020-q2-update or compatible)
3. **CMake** (3.10 or later) - for CMake builds
4. **Make** - for Makefile builds

## Build Options

This project supports multiple build methods:

1. Using the NeuralSPOT build system (recommended)
2. Using the standalone Makefile
3. Using CMake (experimental)

## Method 1: Using NeuralSPOT Build System (Recommended)

The recommended approach is to integrate this demo into the full NeuralSPOT build system:

1. Copy this nnse_baremetal folder to the `apps/demos/` directory of your NeuralSPOT installation
2. Navigate to the NeuralSPOT root directory
3. Build using:
   ```bash
   make app_nnse_baremetal
   ```

## Method 2: Using the Standalone Makefile

This project includes a standalone Makefile for basic building:

```bash
cd c:\Users\apineda\Downloads\neuralSPOT-1.2.0-beta\neuralSPOT-1.2.0-beta\nnse_baremetal
make
```

The output binary will be located in the `build/` directory.

## Method 3: Using CMake (Experimental)

### Setting up the Environment

1. Set environment variables for the toolchain:
   ```bash
   export ARM_GCC_PATH=/path/to/gcc-arm-none-eabi
   export AMBIQ_SDK_PATH=/path/to/ambiqsuite
   ```

2. Create a build directory:
   ```bash
   mkdir build && cd build
   ```

3. Configure with CMake:
   ```bash
   cmake .. -DCMAKE_TOOLCHAIN_FILE=../tools/arm-gcc-toolchain.cmake
   ```

4. Build the project:
   ```bash
   make
   ```

### Note on CMake Configuration

The current CMakeLists.txt is a simplified version that doesn't include all the necessary configurations for cross-compiling to the Ambiq Apollo microcontroller. For a complete build, you would need:

1. A proper toolchain file for ARM GCC
2. Definitions for the target microcontroller (Apollo5)
3. Linker scripts for the target
4. Proper include paths for AmbiqSuite SDK headers
5. Definitions for all compiler flags required by the Ambiq microcontroller

### Enhanced CMake Configuration

For a more complete CMake build, you might want to extend the CMakeLists.txt with:

```cmake
# Target microcontroller
set(TARGET_CHIP "apollo5")

# Include paths
include_directories(${AMBIQ_SDK_PATH}/devices)
include_directories(${AMBIQ_SDK_PATH}/mcu/apollo5/hal)
include_directories(${AMBIQ_SDK_PATH}/utils)
include_directories(${AMBIQ_SDK_PATH}/CMSIS/Include)
include_directories(${AMBIQ_SDK_PATH}/CMSIS/AmbiqMicro/Include)

# Compiler definitions
add_compile_definitions(AM_PART_APOLLO5)
add_compile_definitions(AM_CUSTOM_BSP)
add_compile_definitions(gNUC)

# Linker script
set(LINKER_SCRIPT ${AMBIQ_SDK_PATH}/boards/apollo5_evb/tools/gcc/apollo5p_evb.ld)

# Additional compiler flags for embedded target
target_compile_options(nnse_baremetal PRIVATE 
    -mcpu=cortex-m4
    -mthumb
    -mfloat-abi=hard
    -mfpu=fpv4-sp-d16
    -ffunction-sections
    -fdata-sections
)

# Linker flags
set_target_properties(nnse_baremetal PROPERTIES 
    LINK_FLAGS "-T${LINKER_SCRIPT} -Wl,--gc-sections"
)
```

## Troubleshooting

### Common Issues

1. **Missing Libraries**: If you get linker errors about missing libraries, ensure that:
   - The `libs/ns-nnsp.a` file is present
   - You're using a compatible version of the library for your target

2. **Header File Issues**: If you get errors about missing header files:
   - Ensure you have the AmbiqSuite SDK installed
   - Check that the include paths are correctly set

3. **Toolchain Issues**: If you get errors about the compiler:
   - Ensure you have the ARM GCC toolchain installed
   - Verify that the toolchain is in your PATH

### Building for Different Targets

To build for different Ambiq microcontrollers, you may need to:
1. Modify the compiler flags in the Makefile or CMakeLists.txt
2. Change the linker script to match your target device
3. Update any device-specific definitions

## Flashing the Binary

After building, you can flash the binary to your Apollo5 EVB using:
1. The Segger J-Link tools
2. The Ambiq flash programmer
3. OpenOCD with appropriate configuration

Example with Ambiq tools:
```bash
${AMBIQ_SDK_PATH}/tools/apollo3_scripts/create_cust_image_blob.py \
  --load-address 0x200000 \
  --bin build/nnse_baremetal.bin \
  --output build/nnse_baremetal_wire.bin

${AMBIQ_SDK_PATH}/tools/apollo3_scripts/uart_boot_host.py \
  -b 921600 \
  -f build/nnse_baremetal_wire.bin \
  COMx
```

Replace `COMx` with your actual COM port.