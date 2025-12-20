# nnse_baremetal — Data Flow Diagram

This file contains a Mermaid diagram showing the runtime data flow from the PDM buffers through the Speech Enhancement (SE) processing pipeline and finally out to the DAC via the I2S stream.

```mermaid
flowchart LR
  %% Acquisition
  subgraph ACQ[PDM Acquisition]
    PDM_MIC[PDM Microphone(s)] -->|PDM bitstream| PDM_DMA[DMA / PDM IRQ]
    PDM_DMA -->|writes| PDM_RING[PDM Ring Buffers]
  end

  %% PDM -> PCM conversion
  PDM_RING -->|decimate + FIR| PCM_CONV[PDM -> PCM Conversion]
  PCM_CONV -->|PCM frames| PREPROC[Preprocessing: gain, DC removal, framing]

  %% Feature / buffer handoff
  PREPROC --> FEAT[Feature extraction / framing / windowing]
  FEAT --> NN[Neural SE: TFLM inference]

  %% Postprocess -> I2S
  NN --> POST[Post-process: smoothing, gain, clipping]
  POST --> I2S_RING[I2S TX Buffer / Ring]
  I2S_RING -->|DMA| I2S_DMA[I2S DMA Engine]
  I2S_DMA -->|I2S frames| DAC[External DAC (I2S peripheral)]

  %% Control & ISRs
  PDM_ISR[PDM ISR / callback] -->|notify| PDM_DMA
  I2S_ISR[I2S ISR / underflow handler] -->|refill| I2S_RING

  %% Timing / rates
  classDef meta fill:#fff7e6,stroke:#aa7a00
  PCM_CONV:::meta
  NN:::meta
  I2S_DMA:::meta

  %% Notes
  subgraph NOTES[ ]
    direction TB
    note1([Sample rates: PDM (1.2-3.0 MHz raw) -> PCM (16/32 kHz) -> NN frame rate ~16-32 ms])
    note2([Buffers: double/triple buffering or ring buffers used to decouple IRQs and inference latency])
  end

  style PDM_RING fill:#f0f8ff,stroke:#1f77b4
  style NN fill:#e6f7ff,stroke:#007acc
  style DAC fill:#eef7e6,stroke:#2ca02c
```

Quick notes:
- The diagram maps the main runtime hand-offs used by nnse_baremetal: PDM acquisition -> conversion to PCM -> preprocessing -> NN inference -> postprocessing -> I2S/DAC output.
- ISRs and DMA decouple the real-time audio capture and playback from potentially blocking NN inference.
