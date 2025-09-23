# NeuralSPOT Speech Enhancement Bare Metal Demo

This demo showcases real-time speech enhancement using neuralSPOT libraries on Ambiq Apollo microcontrollers. It captures audio from a PDM microphone, processes it through a neural network for speech enhancement, and outputs the enhanced audio via I2S to a DAC.

## Standalone Repository

This code is also available in a standalone GitHub repository at: https://github.com/andrespineda/nnse_baremetal
See [GITHUB_WORKFLOW.md](GITHUB_WORKFLOW.md) for details on how to maintain both repositories in sync.

## Overview

The application demonstrates:
- PDM microphone audio capture
- Real-time speech enhancement using neural networks
- I2S audio output to external DAC
- Button control for toggling speech enhancement
- LED indication of processing status
- Performance profiling capabilities

## Hardware Setup

### Required Connections
- PDM Microphone:
  - PDM0_CLK: GPIO 50
  - PDM0_DATA: GPIO 51
- I2S Output (J8 Header):
  - I2S0_CLK: GPIO 5 (J8 Pin 9)
  - I2S0_DATA: GPIO 6 (J8 Pin 10)
  - I2S0_WS: GPIO 13 (J8 Pin 8) (Note: Modified from default GPIO 7)
- User Interface:
  - Button 0: Custom (board-specific)
  - Button 1: Custom (board-specific)
  - LED 0: Custom (board-specific)

**Important Hardware Note**: The I2S0_WS pin has been modified from the default GPIO 7 to GPIO 13 to match the J8 pin header configuration on the Apollo5_EVB. This requires a corresponding change in the board support package files. The I2S DAC should be connected to the J8 header with the pin mapping shown above.

## Software Architecture

### Data Flow
1. PDM peripheral captures audio samples from microphone
2. DMA transfers samples to memory buffers
3. Neural network processes audio for speech enhancement
4. I2S peripheral streams processed audio to DAC
5. Buttons control application behavior
6. LEDs provide visual feedback

### Buffer Management and Synchronization

The application uses a sophisticated ping-pong buffering system with DMA to ensure seamless audio streaming:

#### Ping-Pong Buffering
- The system uses two buffers (ping and pong) that alternate between being filled by PDM and read by I2S
- When one buffer is being filled by PDM, the other is being read by I2S

#### DMA Interrupts
- When PDM finishes filling a buffer, it generates a DMA completion interrupt (AM_HAL_PDM_INT_DCMP)
- When I2S finishes reading a buffer, it generates its own DMA completion interrupt (AM_HAL_I2S_INT_TXDMACPL)

#### Buffer Swapping
- Hardware automatically alternates between the two buffers
- When PDM finishes buffer 1, it starts filling buffer 2
- When I2S finishes reading buffer 1, it starts reading buffer 2
- This creates a seamless flow where one buffer is always being filled while the other is being read

#### Synchronization
- The g_bPDMDataReady flag is set in the PDM interrupt handler when a buffer is filled
- The main loop processes the data when this flag is set
- The buffer swapping happens automatically at the hardware level through DMA

This system ensures that:
- Data is never overwritten before being sent to the DAC
- The CPU is only notified when a complete buffer is ready for processing
- Both PDM and I2S can operate continuously without blocking each other
- There's no need for explicit buffer management - the hardware handles it automatically

## Building and Running

### Build Commands
```bash
# Clean previous builds
make clean

# Build the demo
make -j PLATFORM=apollo510_evb EXAMPLE=demos/nnse_baremetal

# Deploy to target
make deploy PLATFORM=apollo510_evb EXAMPLE=demos/nnse_baremetal
```

### Runtime Controls
- Button 1: Toggle speech enhancement on/off
- LED 0: Indicates when speech enhancement is active (on) or bypassed (off)

## Performance Profiling

The application includes performance monitoring capabilities:
- Audio frame processing counters
- FIFO overflow detection
- Latency measurements
- Periodic status reporting via RTT

## Customization

### Platform Support
This demo is configured for Apollo510 EVB but can be adapted to other Apollo platforms by modifying:
- Pin configurations in am_bsp_pins.h
- Platform-specific settings in makefiles
- Board support package integration

### Audio Parameters
Key audio parameters can be adjusted:
- PDM decimation rate
- Sample rate
- Buffer sizes
- Gain settings

## Repository Structure

```
nnse_baremetal/
├── src/                 # Source code files
├── libs/                # Pre-compiled neural network library
├── README.md           # This file
├── BUILDING.md         # Detailed build instructions
├── HARDWARE_NOTES.md   # Hardware modification details
├── module.mk           # Module definition for NeuralSPOT build system
└── Makefile            # Standalone build file
```

## Building

This project can be built using multiple methods. For detailed instructions, please refer to [BUILDING.md](BUILDING.md).

### Quick Build with Make

To build the project using the standalone Makefile, simply run:

```bash
make
```

The output binary will be located in the `build/` directory.

### Note on CMake

While a CMakeLists.txt file is provided, building with CMake requires additional configuration for the embedded target. Please see [BUILDING.md](BUILDING.md) for more details.

## Dependencies

This project depends on the AmbiqSuite SDK and NeuralSPOT libraries. For a complete build environment, please refer to the original NeuralSPOT repository.

## License

This project inherits the license from the original NeuralSPOT repository.