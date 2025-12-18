
# nnse_baremetal — Project summary

This document is a focused, single-page summary of the `nnse_baremetal` demo contained under `apps/demos/nnse_baremetal` in this repository. It describes the main functions, key files, build notes, a compact contract (inputs/outputs/error modes), likely edge cases, and a compact ASCII data-flow diagram.

## Purpose

`nnse_baremetal` is a bare-metal demo that runs Ambiq's Neural Network Speech Enhancer (NNSE). The demo shows a minimal integration of the speech enhancement runtime on Ambiq EVB platforms (Apollo family) with platform drivers and a small application harness for real-time audio capture, inference, and output.

## Main functions (what this code does)

- Capture microphone audio (PDM or AUDADC driver code and BSP glue).
- Buffer and manage PCM frames (`PcmBufClass` and helpers).
- Run the NNSE inference path (model wrapper and inference glue in `def_nn3_se.*` and calls into the prebuilt `ns-nnsp.a`).
- Control inference and audio I/O via a controller class (`seCntrlClass.*`).
- Provide simple board support utilities for LEDs and buttons (`am_devices_led.*`, `am_devices_button.*`).
- Build and packaging metadata (Makefile, CMakeLists, BUILDING.md) to create a firmware image that can be flashed to an EVB.

## Key files and purpose

- `apps/demos/nnse_baremetal/README.md` — demo overview and quick commands.
- `apps/demos/nnse_baremetal/BUILDING.md` — build notes, packaging and flashing steps.
- `apps/demos/nnse_baremetal/CMakeLists.txt`, `Makefile`, `module.mk` — build system entry points.
- `apps/demos/nnse_baremetal/src/nnse_baremetal.c` — top-level application (main loop, init).
- `apps/demos/nnse_baremetal/src/seCntrlClass.c/.h` — controller that orchestrates capture -> inference -> output.
- `apps/demos/nnse_baremetal/src/def_nn3_se.c/.h` — neural-net wrapper / NNSE definition and inference helpers.
- `apps/demos/nnse_baremetal/src/PcmBufClass.*` — circular/frame buffer for PCM data (capture and feeding inference).
- `apps/demos/nnse_baremetal/src/ParamsNNCntrl.h` — runtime constants / model parameters.
- `apps/demos/nnse_baremetal/libs/ns-nnsp.a` — prebuilt NN runtime / optimized library (binary archive used by the demo).
- `apps/demos/nnse_baremetal/src/am_devices_*.c/.h` — minimal device glue (LEDs, buttons, PDM/I2S hooks) for the target EVB.
- `apps/demos/nnse_baremetal/GIT_SUBTREE_WORKFLOW.md`, `GITHUB_INSTRUCTIONS.md` — repo extraction/publishing notes.

## Tiny contract (inputs / outputs / success criteria)

- Inputs: microphone samples (PDM or AUDADC), optional configuration flags (output path: DAC / BLE / USB), build-time toolchain and target platform.
- Outputs: processed/denoised audio (to configured output), firmware image (`nnse_baremetal.bin`), optional logs via RTT/serial.
- Error modes: missing toolchain, missing/unsupported microphone configuration, model library mismatch, insufficient memory for model, missing flash/device.

## Likely edge cases

- Microphone format mismatch (PDM vs AUDADC) — the code contains configuration points; ensure correct driver is enabled.
- Model or runtime too large to fit device RAM/flash — linking `ns-nnsp.a` and model data must fit platform.
- Timing/regression with PDM interrupt handling (real-time deadlines) — watch main loop and ISR interactions.
- Missing or miswired peripherals (DAC, codec, or BLE/USB interfaces) — behavior depends on chosen output path.

## Quick build & flash (from repo root)

1. Build for a target platform defined in the top-level Makefile (examples):

   make -j PLATFORM=apollo510_evb EXAMPLE=demos/nnse_baremetal

2. Deploy (flash) with the standard neuralSPOT make targets:

   make deploy PLATFORM=apollo510_evb EXAMPLE=demos/nnse_baremetal

See `BUILDING.md` in the demo folder for details and platform-specific notes.

## Minimal verification steps

- Inspect console/RTT output when firmware boots to confirm initialization messages.
- Feed a live microphone signal and confirm processed audio on the configured output (DAC/BLE/USB as configured).
- If available, run the example with instrumentation (SEGGER RTT) to confirm inference() is invoked periodically.

## ASCII data-flow diagram (nnse_baremetal only)

The following diagram focuses only on components in `apps/demos/nnse_baremetal` and close platform glue.

Microphone (PDM/AUDADC)
   |
   v
 [am_hal_pdm / am_hal_i2s glue]
   |
   v
 [PcmBufClass]  -- buffers and frames --> [seCntrlClass]
                                           |
                                           v
                                 [def_nn3_se -> ns-nnsp.a]
                                 (run inference, produce cleaned frames)
                                           |
                                           v
                                [Audio output path(s)]
                                /        |         \
                    (DAC)  DAC driver   |    (BLE/USB/Opus) optional
                                   [Opus encoder or streamer]
                                           |
                                           v
                                  External sink (speaker / host / browser)

Control & debug paths:

- `nnse_baremetal.c` (main) initializes and coordinates `seCntrlClass` and peripheral drivers.
- `ambiq_nnsp_debug.h` + RTT/printf provide logs back to host for debugging.

## Where to look next

- `apps/demos/nnse_baremetal/src/nnse_baremetal.c` — start here to understand lifecycle and initialization.
- `apps/demos/nnse_baremetal/src/seCntrlClass.c` — follow this for capture -> inference orchestration.
- `apps/demos/nnse_baremetal/src/def_nn3_se.c` — neural wrapper and calls into the binary runtime.
- `apps/demos/nnse_baremetal/BUILDING.md` — platform-specific build and packaging details.

## Requirements coverage

- Requested: "single-page README-style summary file in the repo (PROJECT_SUMMARY.md) but only for the nnse_baremetal code" — Done.

---
Generated: PROJECT_SUMMARY.md for `apps/demos/nnse_baremetal` only.
