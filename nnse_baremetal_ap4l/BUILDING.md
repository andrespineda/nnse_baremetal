# Building the nnse_baremetal Demo

This document provides instructions for building the PERSEV.ai Speech Enhancement Bare Metal Demo using different build systems.

## Prerequisites

Before building, ensure you have the following:

1. **AmbiqSuite SDK** (For Apollo4L_Blue_EVB: R4.5.0 or later. For Apollo510: R5.2.0 or later)
2. **GCC ARM Embedded Toolchain** (9-2020-q2-update or compatible)
3. **Make** - for Makefile builds

## Build Options

This project supports multiple build methods:

1. Using the NeuralSPOT build system (recommended)
2. Using the standalone Makefile


## Method 1: Using NeuralSPOT Build System (Recommended)

The recommended approach is to integrate this demo into the full NeuralSPOT build system:

1. Copy this nnse_baremetal folder to the `apps/demos/` directory of your NeuralSPOT installation
2. Navigate to the NeuralSPOT root directory
3. Build using:
   ```bash
   make EXAMPLES=demos/nnse PLATFORM=apollo4l_blue_evb AS_VERSION=R4.5.0 -j22
 ```

The output binary will be located in the `build/` directory.


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

3. Build the project:
   ```bash
   make
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

After building, you can flash the binary to your Apollo5 EVB using the Segger J-Link tools

## Debugging

The SEGGER Ozone debugging tool is recommended as it provides a vibrant visualization of all running parameters.