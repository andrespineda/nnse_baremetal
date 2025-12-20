//*****************************************************************************
//
// nnse_baremetal.c - NeuralSPOT Speech Enhancement (SE) Bare Metal Demo
//
// Purpose: This application demonstrates real-time speech enhancement using
//          neuralSPOT libraries on Ambiq Apollo microcontrollers. It captures
//          audio via PDM microphone, processes it with a neural network-based
//          enhancement algorithm, and outputs the enhanced audio via I2S.
//
// Current Release: 0.0.1-beta
// Author: Andres Pineda - apineda2005@gmail.com
// Date: 2025-10-08
//
//*****************************************************************************
//
// Module Diagram (also available for live preview in the markdown file below):
// See: `apps/demos/nnse_baremetal/src/nnse_baremetal_diagram.md`
// ```mermaid
// graph TD
//     A[Main Application] --> B[PDM Module]
//     A --> C[I2S Module]
//     A --> D[Speech Enhancement Module]
//     A --> E[Button Module]
//     A --> F[LED Module]
//     A --> G[Timer Module]
//     A --> H[Performance Profiler]
//     D --> I[Neural Network Controller]
//     I --> J[Feature Module]
//     I --> K[NN Speech Module]
//     I --> L[Neural Nets Module]
// ```
//
//*****************************************************************************
//
// ToDo:
// - [ ] Add configurable enhancement parameters via BLE
// - [ ] Fine-tune gain control post-speech enhancement
// - [ ] Remove debug code
// - [ ] Verify that eMMC, PSRAM, and MSPI are disabled
// - [ ]
// - [ ]
//
//*****************************************************************************

#include "am_mcu_apollo.h"
#include "am_bsp.h"
#include "am_util.h"
#include "am_bsp_pins.h"

#include <stdint.h>
#include <string.h> // Add this for memcpy
#include <math.h>   // Add this for sin function

// Define M_PI if not available in math.h
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

#include "ns_perf_profile.h"
#include "ns_peripherals_power.h"

// NeuralSPOT includes for peripheral (buttons, LEDs) functionality
#include "ns_core.h"
#include "ns_peripherals_button.h"
#include "ns_ambiqsuite_harness.h"
#include "ns_timer.h"
#include "am_devices_led.h"

// NeuralSPOT includes for speech enhancement
#include "seCntrlClass.h"
#include "feature_module.h"
#include "def_nn3_se.h"
#include "nn_speech.h"
#include "ParamsNNCntrl.h"
#include "neural_nets.h"
#include "am_hal_i2s.h"
#include "am_hal_pdm.h"
#include "ambiq_nnsp_debug.h"
#include "ns_peripherals_psram.h"
// #include "ns_peripherals_emmc.h"

// Constants for PDM and I2S configuration

#define CLOCK_SOURCE (PLL)
#define HFRC (0)
#define PLL (1)
#define HF2ADJ (2)
#define PDM_MODULE (0)
#define I2S_MODULE (0)
#define FIFO_THRESHOLD_CNT 16

#define NUM_OF_PCM_SAMPLES 160 // 160 works, do not change
#define SIZE_OF_PCM_SAMPLES (NUM_OF_PCM_SAMPLES * sizeof(uint32_t))

#define NUM_OF_I2S_SAMPLES 160 // 160 works, do not change
#define SIZE_OF_I2S_SAMPLES (NUM_OF_I2S_SAMPLES * sizeof(uint32_t))

#define DATA_VERIFY 0
#define PDM_ISR_TEST_PAD 2
#define I2S_ISR_TEST_PAD 3
#define USE_DMA 1

#define FIFO_THRESHOLD_CNT 16
#define DMA_BYTES PDM_FFT_BYTES
#define DATA_VERIFY 0
#define PDM_ISR_TEST_PAD 2
#define I2S_ISR_TEST_PAD 3

// Sine wave parameters
#define SINE_WAVE_FREQ 1000         // 1000 Hz sine wave
#define SAMPLE_RATE 16000           // 16 kHz sample rate
#define AMPLITUDE 0x7FFFFF          // Maximum amplitude for 24-bit
#define M_PI 3.14159265358979323846 // Define M_PI if not available

// Timer constants for 5-second periodic message
#define TIMER_PERIOD_US 5000000 // 5 seconds in microseconds

//*****************************************************************************
//
// Global variables.
//
//*****************************************************************************
uint32_t FIFO_OVF_Count = 0;
volatile int PDM_Data_Ready = 0;
volatile int I2S_Data_Ready = 0;
uint32_t PDM_Status = 0;
uint32_t I2S_Status = 0;

// Timing variables for PDM buffer processing measurement
volatile uint32_t pdm_start_time = 0;
volatile uint32_t pdm_elapsed_time_us = 0; // Elapsed time in microseconds

// Timing variables for I2S buffer processing measurement
volatile uint32_t i2s_start_time = 0;
volatile uint32_t i2s_elapsed_time_us = 0; // Elapsed time in microseconds

// Sine wave generation variables
static float g_fPhase = 0.0f;
static float g_fPhaseIncrement = 0.0f;

static uint32_t main_loop_count = 0;

//
// PDM and I2S handlers.
//
void *PDM_Handle;
void *I2S_Handle;

//
// PDM and I2S interrupt numbers.
//
static const IRQn_Type pdm_interrupts[] = {PDM0_IRQn};

static const IRQn_Type i2s_interrupts[] = {I2S0_IRQn};

//*****************************************************************************
// Dual buffers used by PDM and I2S to input, process, and output audio data
// Used as the ping-pong buffer of PDM DMA.
// Aligned to 32 bytes to meet data cache requirements.
//
//  KEEP THESE AT uint32_t  DAMMIT!!!!
AM_SHARED_RW int32_t PDM_Buffer[2 * NUM_OF_PCM_SAMPLES] __attribute__((aligned(32)));
AM_SHARED_RW int32_t I2S_Buffer[2 * NUM_OF_I2S_SAMPLES] __attribute__((aligned(32)));

int16_t PDM_se_input_buffer[NUM_OF_PCM_SAMPLES] __attribute__((aligned(32)));
int16_t PDM_se_output_buffer[NUM_OF_PCM_SAMPLES] __attribute__((aligned(32)));
int32_t PDM_Enhanced_Buffer[NUM_OF_PCM_SAMPLES] __attribute__((aligned(32)));
//*****************************************************************************
//
// PDM configuration information.
//
//  1.536 MHz PDM CLK OUT:
//      PDM_CLK_OUT = ePDMClkSpeed / (eClkDivider + 1) / (ePDMAClkOutDivder + 1)
//  16 kHz 16bit Mono Sampling:
//      DecimationRate = 48
//      SAMPLEING_FREQ = PDM_CLK_OUT / (ui32DecimationRate * 2)
//
//*****************************************************************************
    // Use the following for AP510_EVB
        am_hal_pdm_config_t PDM_Config = {
            #if (CLOCK_SOURCE == HFRC)
                .eClkDivider = AM_HAL_PDM_MCLKDIV_1,
                .ePDMAClkOutDivder = AM_HAL_PDM_PDMA_CLKO_DIV7,
                .ePDMClkSpeed = AM_HAL_PDM_CLK_HFRC_24MHZ,
            #elif (CLOCK_SOURCE == PLL)
                .eClkDivider = AM_HAL_PDM_MCLKDIV_1,
                .ePDMAClkOutDivder = AM_HAL_PDM_PDMA_CLKO_DIV1,
                .ePDMClkSpeed = AM_HAL_PDM_CLK_PLL,
            #elif (CLOCK_SOURCE == HF2ADJ)
                .eClkDivider = AM_HAL_PDM_MCLKDIV_1,
                .ePDMAClkOutDivder = AM_HAL_PDM_PDMA_CLKO_DIV7,
                // Use the same clock configuration as the working example
                .ePDMClkSpeed = AM_HAL_PDM_CLK_HFRC2ADJ_24_576MHZ,
            #endif
                .ui32DecimationRate = 48,
                // Use the same gain settings as the working example
                
                // AM_HAL_PDM_GAIN_P285DB = 0x1B,
                // AM_HAL_PDM_GAIN_P270DB = 0x1A,
                // AM_HAL_PDM_GAIN_P255DB = 0x19,
                // AM_HAL_PDM_GAIN_P240DB = 0x18,
                // AM_HAL_PDM_GAIN_P225DB = 0x17,
                // AM_HAL_PDM_GAIN_P210DB = 0x16,
                // AM_HAL_PDM_GAIN_P195DB = 0x15,
                // AM_HAL_PDM_GAIN_P180DB = 0x14,
                // AM_HAL_PDM_GAIN_P165DB = 0x13,
                // AM_HAL_PDM_GAIN_P150DB = 0x12,
                // AM_HAL_PDM_GAIN_P135DB = 0x11,
                // AM_HAL_PDM_GAIN_P120DB = 0x10,
                // AM_HAL_PDM_GAIN_P105DB = 0x0F,

                
                .eLeftGain = AM_HAL_PDM_GAIN_0DB,
                .eRightGain = AM_HAL_PDM_GAIN_0DB,
                .eStepSize = AM_HAL_PDM_GAIN_STEP_0_13DB,
                .bHighPassEnable = AM_HAL_PDM_HIGH_PASS_ENABLE,
                .ui32HighPassCutoff = 0x3,
                .bDataPacking = 1,
                // Use mono configuration like the working example
                .ePCMChannels = AM_HAL_PDM_CHANNEL_LEFT,
                .bPDMSampleDelay = AM_HAL_PDM_CLKOUT_PHSDLY_NONE,
                .ui32GainChangeDelay = AM_HAL_PDM_CLKOUT_DELAY_NONE,
                .bSoftMute = 0,
                .bLRSwap = 0,
            }; 
// Use the following for apollo4l_blue_evb
/* am_hal_pdm_config_t PDM_Config =
{
    //
    // Example setting:
    //  1.5MHz PDM CLK OUT:
    //      AM_HAL_PDM_CLK_HFRC2ADJ_24_576MHZ, AM_HAL_PDM_MCLKDIV_1, AM_HAL_PDM_PDMA_CLKO_DIV7
    //  16.00KHz 24bit Sampling:
    //      DecimationRate = 48
    //
    .ePDMClkSpeed = AM_HAL_PDM_CLK_HFRC2ADJ_24_576MHZ,
    .eClkDivider = AM_HAL_PDM_MCLKDIV_1,
    .ePDMAClkOutDivder = AM_HAL_PDM_PDMA_CLKO_DIV7,
    .ui32DecimationRate = 48,

    .eLeftGain = AM_HAL_PDM_GAIN_P105DB,
    .eRightGain = AM_HAL_PDM_GAIN_P105DB,
    .eStepSize = AM_HAL_PDM_GAIN_STEP_0_13DB,

    .bHighPassEnable = AM_HAL_PDM_HIGH_PASS_ENABLE,
    .ui32HighPassCutoff = 0x3,
    .bDataPacking = 1,
    .ePCMChannels = AM_HAL_PDM_CHANNEL_STEREO,

    .bPDMSampleDelay = AM_HAL_PDM_CLKOUT_PHSDLY_NONE,
    .ui32GainChangeDelay = AM_HAL_PDM_CLKOUT_DELAY_NONE,

    .bSoftMute = 0,
    .bLRSwap = 0,
}; */
//*****************************************************************************
//
// I2S configurations:
//  - 1 channel (Mono)
//  - 16 kHz sample rate
//  - Standard I2S format
//  - 16 bits word width
//  - 16 bits bit-depth
//
// SCLK = 16000 * 1 * 16 = 256 kHz
//
//*****************************************************************************
     // Use the following for AP510_EBD
            static am_hal_i2s_io_signal_t I2S_IO_Config = {
                    .sFsyncPulseCfg =
                        {
                            .eFsyncPulseType = AM_HAL_I2S_FSYNC_PULSE_ONE_SUBFRAME,
                        },
                    .eFyncCpol = AM_HAL_I2S_IO_FSYNC_CPOL_LOW,
                    .eTxCpol = AM_HAL_I2S_IO_TX_CPOL_FALLING,
                    .eRxCpol = AM_HAL_I2S_IO_RX_CPOL_RISING,
                };

                static am_hal_i2s_data_format_t I2S_Data_Config = {
                    .ePhase = AM_HAL_I2S_DATA_PHASE_SINGLE, // changed to Single, like PDM_to_I2S
                    .eChannelLenPhase1 = AM_HAL_I2S_FRAME_WDLEN_32BITS,
                    .eChannelLenPhase2 = AM_HAL_I2S_FRAME_WDLEN_32BITS,
                    .ui32ChannelNumbersPhase1 = 2, // Stereo output
                    .ui32ChannelNumbersPhase2 = 0,
                    .eDataDelay = 0x1,
                    // Change back to 16-bit sample length
                    .eSampleLenPhase1 = AM_HAL_I2S_SAMPLE_LENGTH_16BITS,
                    .eSampleLenPhase2 = AM_HAL_I2S_SAMPLE_LENGTH_16BITS,
                    .eDataJust = AM_HAL_I2S_DATA_JUSTIFIED_LEFT,
                };

                static am_hal_i2s_config_t I2S_Config = {
                    .eMode = AM_HAL_I2S_IO_MODE_MASTER,
                    .eXfer = AM_HAL_I2S_XFER_TX,
                #if (CLOCK_SOURCE == HFRC)
                    .eClock = eAM_HAL_I2S_CLKSEL_HFRC_3MHz,
                    .eDiv3 = 1,
                #elif (CLOCK_SOURCE == PLL)
                    .eClock = eAM_HAL_I2S_CLKSEL_PLL_FOUT3,
                    .eDiv3 = 0,
                #elif (CLOCK_SOURCE == HF2ADJ)
                    .eClock = eAM_HAL_I2S_CLKSEL_HFRC2_APPROX_4MHz,
                    .eDiv3 = 1,
                #endif
                    .eASRC = 0,
                    .eData = &I2S_Data_Config,
                    .eIO = &I2S_IO_Config,
                }; 
// Use the following for Apollo4l_ble_EVD
/* static am_hal_i2s_io_signal_t I2S_IO_Config =
{
  .eFyncCpol = AM_HAL_I2S_IO_FSYNC_CPOL_HIGH,

  .eTxCpol = AM_HAL_I2S_IO_TX_CPOL_FALLING,
  .eRxCpol = AM_HAL_I2S_IO_RX_CPOL_RISING,
};

static am_hal_i2s_data_format_t I2S_Data_Config =
{
  .ePhase = AM_HAL_I2S_DATA_PHASE_SINGLE,
  .ui32ChannelNumbersPhase1 = 2,
  .ui32ChannelNumbersPhase2 = 0,
  .eDataDelay = 0x1,
  .eDataJust = AM_HAL_I2S_DATA_JUSTIFIED_LEFT,

  .eChannelLenPhase1 = AM_HAL_I2S_FRAME_WDLEN_32BITS, //32bits
  .eChannelLenPhase2 = AM_HAL_I2S_FRAME_WDLEN_32BITS, //32bits
  .eSampleLenPhase1 = AM_HAL_I2S_SAMPLE_LENGTH_24BITS,
  .eSampleLenPhase2 = AM_HAL_I2S_SAMPLE_LENGTH_24BITS
};

//*****************************************************************************
//
// I2S configuration information.
//
//*****************************************************************************
static am_hal_i2s_config_t I2S_Config =
{
  .eClock               = eAM_HAL_I2S_CLKSEL_HFRC2_3MHz, //eAM_HAL_I2S_CLKSEL_HFRC_6MHz,
  .eDiv3                = 1,
#if DATA_VERIFY
  .eASRC                = 0,
#else
  .eASRC                = 0,
#endif
  .eMode                = AM_HAL_I2S_IO_MODE_MASTER,
  .eXfer                = AM_HAL_I2S_XFER_TX,
  .eData                = &I2S_Data_Config,
  .eIO                  = &I2S_Data_Config
}; */
//
am_hal_pdm_transfer_t Transfer_PDM = {
    .ui32TargetAddr = (uint32_t)&PDM_Buffer[0],
    // .ui32TargetAddrReverse = (uint32_t)(&PDM_Buffer[NUM_OF_PCM_SAMPLES]),
    .ui32TargetAddrReverse = 0xFFFFFFFF,
    .ui32TotalCount = SIZE_OF_PCM_SAMPLES,

};
am_hal_i2s_transfer_t Transfer_I2S = {
    .ui32TxTargetAddr = (uint32_t)&I2S_Buffer[0],
    // .ui32TxTargetAddrReverse = (uint32_t)(&I2S_Buffer[NUM_OF_I2S_SAMPLES]),
    .ui32TxTargetAddrReverse = 0xFFFFFFFF,
    .ui32TxTotalCount = SIZE_OF_I2S_SAMPLES};

//*****************************************************************************
//
// Additional variables for Speech enhancement.
//
//*************************************************************************

uint32_t audioFrameCount = 0; // Track number of audio frames processed

// Button state variables
volatile uint32_t button0_press_count = 0;
volatile uint32_t button1_press_count = 0;
int volatile Button0Pressed = 0;
int volatile Button1Pressed = 0;
bool enableSE = false; // Speech enhancement toggle - default to false

// Timer variables for periodic status message
uint32_t TimerCount = 0;
ns_timer_config_t TimerConfig;

// Speech Enhancement Control Structure
seCntrlClass cntrl_inst;

// Audio buffer for speech enhancement

int16_t g_left_audioFrame[SAMPLES_FRM_NNCNTRL_CLASS] __attribute__((aligned(16)));
int16_t seOutput[NUM_OF_PCM_SAMPLES];
__attribute__((aligned(16))); // For left channel SE output

// Use this to generate a sinwave for debugging instead
// of using the microphone
int16_t static sinWave[SAMPLES_FRM_NNCNTRL_CLASS];

uint32_t seLatency = 0;
uint32_t seStart, seEnd;
uint32_t seLatencyCapturePeriod = 10; // measure every 100 frames (1s)
uint32_t currentSESample = 0;
uint32_t currentOpusSample = 0;
uint32_t latency_us = 0; // Add declaration for latency measurement
ns_perf_counters_t pp;

#if USE_DMA
// #define BUFFER_SIZE_BYTES               PDM_FFT_BYTES
//! RX size = TX size * output sample rate /internal sample rate
//! Notice .eClock from I2S_Config and I2SConfilave.
// #define BUFFER_SIZE_ASRC_RX_BYTES       PDM_FFT_BYTES
#endif

//*****************************************************************************
//
// PDM initialization.
//
//*****************************************************************************
void pdm_init(void) {
    // Initialize sine wave parameters
    g_fPhaseIncrement = 2.0f * M_PI * SINE_WAVE_FREQ / SAMPLE_RATE;

    am_bsp_pdm_pins_enable(PDM_MODULE);
    am_hal_pdm_initialize(PDM_MODULE, &PDM_Handle);
    am_hal_pdm_power_control(PDM_Handle, AM_HAL_PDM_POWER_ON, false);
    am_hal_pdm_configure(PDM_Handle, &PDM_Config);

    am_hal_pdm_fifo_threshold_setup(PDM_Handle, FIFO_THRESHOLD_CNT);

    am_hal_pdm_interrupt_enable(
        PDM_Handle,
        (AM_HAL_PDM_INT_DERR | AM_HAL_PDM_INT_DCMP | AM_HAL_PDM_INT_UNDFL | AM_HAL_PDM_INT_OVF));

    NVIC_SetPriority(pdm_interrupts[PDM_MODULE], AM_IRQ_PRIORITY_DEFAULT);
    NVIC_EnableIRQ(pdm_interrupts[PDM_MODULE]);
}

//*****************************************************************************
//
// PDM interrupt handler.
//
//*****************************************************************************
void am_pdm0_isr(void) {
    uint32_t ui32Status;

    // Capture end time when PDM interrupt occurs
    uint32_t pdm_end_time = am_hal_timer_read(0);

    am_hal_pdm_interrupt_status_get(PDM_Handle, &ui32Status, true);
    am_hal_pdm_interrupt_clear(PDM_Handle, ui32Status);
    am_hal_pdm_interrupt_service(PDM_Handle, ui32Status, &Transfer_PDM);

    if (ui32Status & AM_HAL_PDM_INT_DCMP) {
        PDM_Data_Ready = true;
        PDM_Status = ui32Status;

        // Calculate and store elapsed time for PDM buffer read in microseconds
        // Assuming timer clock is configured appropriately for microsecond calculation
        if (pdm_end_time >= pdm_start_time) {
            uint32_t elapsed_ticks = pdm_end_time - pdm_start_time;
            pdm_elapsed_time_us = elapsed_ticks; // Assuming timer is already in microseconds
        } else {
            // Handle overflow case
            uint32_t elapsed_ticks = (0xFFFFFFFF - pdm_start_time) + pdm_end_time;
            pdm_elapsed_time_us = elapsed_ticks; // Assuming timer is already in microseconds
        }
    }

    if (ui32Status & AM_HAL_PDM_INT_OVF) {
        am_hal_pdm_fifo_count_get(PDM_Handle);
        am_hal_pdm_fifo_flush(PDM_Handle);
        FIFO_OVF_Count++;
    }
}

//*****************************************************************************
//
// I2S initialization.
//
//*****************************************************************************
void i2s_init(void) {
    am_bsp_i2s_pins_enable(I2S_MODULE, false);
    am_hal_i2s_initialize(I2S_MODULE, &I2S_Handle);
    am_hal_i2s_power_control(I2S_Handle, AM_HAL_I2S_POWER_ON, false);

    am_hal_i2s_configure(I2S_Handle, &I2S_Config);

    am_hal_i2s_dma_configure(I2S_Handle, &I2S_Config, &Transfer_I2S);

    // Enable I2S interrupts for TX completion
   //      AP510 ONLY
   am_hal_i2s_interrupt_enable(I2S_Handle, (AM_HAL_I2S_INT_TXDMACPL));

   // Apollo5l_blue_EVB only
   
    am_hal_clkgen_control(AM_HAL_CLKGEN_CONTROL_HFRC2_START, false);
    am_util_delay_us(500);

    if ( (eAM_HAL_I2S_CLKSEL_XTHS_EXTREF_CLK <= I2S_Config.eClock  && I2S_Config.eClock <= eAM_HAL_I2S_CLKSEL_XTHS_500KHz) ) //enable EXTCLK32M
    {
        am_hal_mcuctrl_control_arg_t ctrlArgs = g_amHalMcuctrlArgDefault;
        ctrlArgs.ui32_arg_hfxtal_user_mask  = 1 << (AM_HAL_HCXTAL_II2S_BASE_EN + I2S_MODULE);
        am_hal_mcuctrl_control(AM_HAL_MCUCTRL_CONTROL_EXTCLK32M_NORMAL, (void *)&ctrlArgs);
        am_util_delay_ms(200);
    }

    NVIC_SetPriority(i2s_interrupts[I2S_MODULE], AM_IRQ_PRIORITY_DEFAULT);
    NVIC_EnableIRQ(i2s_interrupts[I2S_MODULE]);
}

//*****************************************************************************
//
// I2S interrupt handler.
//
//*****************************************************************************
void am_dspi2s0_isr(void) {
    uint32_t ui32Status;

    // Capture end time when I2S interrupt occurs
    uint32_t i2s_end_time = am_hal_timer_read(0);

    // Comment out the RTT print in interrupt context
    // SEGGER_RTT_printf(0, "I2S Interrupt!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");

    am_hal_i2s_interrupt_status_get(I2S_Handle, &ui32Status, true);
    am_hal_i2s_interrupt_clear(I2S_Handle, ui32Status);

    am_hal_i2s_interrupt_service(I2S_Handle, ui32Status, &I2S_Config);

    if (ui32Status & AM_HAL_I2S_INT_TXDMACPL) {
        I2S_Data_Ready = true;
        I2S_Status = ui32Status;

        // Calculate and store elapsed time for I2S buffer write in microseconds
        // Assuming timer clock is configured appropriately for microsecond calculation
        if (i2s_end_time >= i2s_start_time) {
            uint32_t elapsed_ticks = i2s_end_time - i2s_start_time;
            i2s_elapsed_time_us = elapsed_ticks; // Assuming timer is already in microseconds
        } else {
            // Handle overflow case
            uint32_t elapsed_ticks = (0xFFFFFFFF - i2s_start_time) + i2s_end_time;
            i2s_elapsed_time_us = elapsed_ticks; // Assuming timer is already in microseconds
        }
    }
}

//*****************************************************************************
//
// Button configuration
//
//*****************************************************************************
ns_button_config_t button_config_nnsp = {
    .api = &ns_button_V1_0_0,
    .button_0_enable = true,
    .button_1_enable = true,
    .button_0_flag = &Button0Pressed,
    .button_1_flag = &Button1Pressed

};

//*****************************************************************************
//
// Timer interrupt handler for periodic status messages
//
//*****************************************************************************
void timer_interrupt_handler(ns_timer_config_t *config) {
    // Increment timer count
    TimerCount++;

    // Display periodic status message every 5 seconds
    // SEGGER_RTT_printf(0, "\n[STATUS] System running - Uptime: %lu seconds\n", TimerCount *
    // 5); SEGGER_RTT_printf(0, "[STATUS] Audio frames processed: %lu\n", audioFrameCount);
    // SEGGER_RTT_printf(0, "[STATUS] FIFO overflows: %lu\n", FIFO_OVF_Count);
    // SEGGER_RTT_printf(0, "[STATUS] Latency: %lu\n", seLatency);
    // SEGGER_RTT_printf(0, "[STATUS] Button presses: %lu\n", button1_press_count);
    // SEGGER_RTT_printf(0, "[STATUS] Speech Enhancement: %s\n\n", enableSE ? "ENABLED" :
    // "DISABLED");
}

//*****************************************************************************
//
// Initialize timer for periodic status messages
//
//*****************************************************************************
void init_periodic_timer(void) {
    // Configure a timer to trigger every 5 seconds
    TimerConfig.api = &ns_timer_V1_0_0;
    TimerConfig.timer = NS_TIMER_INTERRUPT; // Use timer 0
    TimerConfig.enableInterrupt = true;
    TimerConfig.periodInMicroseconds = TIMER_PERIOD_US; // 5 seconds
    TimerConfig.callback = timer_interrupt_handler;

    // Initialize the timer
    if (ns_timer_init(&TimerConfig) != NS_STATUS_SUCCESS) {
        ns_lp_printf("[TIMER] Failed to initialize periodic timer\n");
    } else {
        ns_lp_printf("[TIMER] Periodic timer initialized for 5-second intervals\n");
    }
}

//*****************************************************************************
//
// Performance configuration information.
//
//*****************************************************************************

// Timer and structs for performance profiling
// ns_perf_counters_t start, end, delta;
// int8_t do_it_once = 1;
ns_timer_config_t basic_tickTimer = {
    .api = &ns_timer_V1_0_0,
    .timer = NS_TIMER_COUNTER,
    .enableInterrupt = false,
};


// Add this to your main function to verify no MSPI access
void check_external_memory_access(void) {
    // Check if any MSPI modules are enabled
    if (MCUCTRL->PERIPHERAL_ENABLE & (MCUCTRL_PERIPHERAL_ENABLE_MSPI0_Msk |
                                    MCUCTRL_PERIPHERAL_ENABLE_MSPI1_Msk |
                                    MCUCTRL_PERIPHERAL_ENABLE_MSPI2_Msk |
                                    MCUCTRL_PERIPHERAL_ENABLE_MSPI3_Msk)) {
        ns_lp_printf("WARNING: MSPI peripheral is enabled!\n");
    } else {
        ns_lp_printf("MSPI peripheral is disabled - external memory not in use\n");
    }
    
    // Check if SDIO (for eMMC) is enabled
    if (MCUCTRL->PERIPHERAL_ENABLE & (MCUCTRL_PERIPHERAL_ENABLE_SDIO0_Msk |
                                    MCUCTRL_PERIPHERAL_ENABLE_SDIO1_Msk)) {
        ns_lp_printf("WARNING: SDIO peripheral is enabled!\n");
    } else {
        ns_lp_printf("SDIO peripheral is disabled - eMMC not in use\n");
    }
}
//*****************************************************************************
// Main
// A/O: 2025_10_09
//*****************************************************************************
int main(void) {

    ns_itm_printf_enable();

    // Add a small delay after RTT initialization
    am_util_delay_ms(10);

while(1) {
    check_external_memory_access();
}

            #if (CLOCK_SOURCE == PLL)
                am_hal_clkmgr_clock_config(AM_HAL_CLKMGR_CLK_ID_SYSPLL, 6144000, NULL);
            #elif (CLOCK_SOURCE == HF2ADJ)
                am_hal_clkmgr_clock_config(
                    AM_HAL_CLKMGR_CLK_ID_HFRC2, AM_HAL_CLKMGR_HFRC2_FREQ_ADJ_196P608MHZ, NULL);
            #endif */

    NS_TRY(ns_timer_init(&basic_tickTimer), "Timer init failed.\n");

    am_bsp_low_power_init();

    // Enable the I-Cache and D-Cache.  AP510 ONLY
            am_hal_cachectrl_icache_enable();
            am_hal_cachectrl_dcache_enable(true); 

    ns_lp_printf("[MAIN] Core initialized\n");

    //*********************************************************************************
    // Initialize button system
    //*********************************************************************************
    ns_lp_printf("[MAIN] Initializing button system\n");
    if (ns_peripheral_button_init(&button_config_nnsp) != NS_STATUS_SUCCESS) {
        ns_lp_printf("[MAIN] Button initialization failed!\n");
    }
    ns_lp_printf("[MAIN] Button system initialized\n");

    //*********************************************************************************
    // Initialize LED0 for speech enhancement indication
    //
    //*********************************************************************************

    ns_lp_printf("[MAIN] Initializing LED0\n");

    // Initialize the LED array
    am_devices_led_array_init(am_bsp_psLEDs, AM_BSP_NUM_LEDS);
    am_devices_led_off(am_bsp_psLEDs, 0); // Clear LEDs

    // Turn on LED0 to indicate SE state
    if (enableSE) {
        am_devices_led_on(am_bsp_psLEDs, 0);
    }

    ns_lp_printf("[MAIN] LEDs initialized\n");

    //*********************************************************************************
    // Initialize periodic timer for status messages
    //*********************************************************************************
    ns_lp_printf("[MAIN] Initializing periodic timer\n");
    // init_periodic_timer();
    ns_lp_printf("[MAIN] Periodic timer initialized\n");

    //*********************************************************************************
    // Initialize PDM-to-PCM module
    //
    //*********************************************************************************
    ns_lp_printf("[MAIN] Initializing PDM module\n");
    pdm_init();
    am_hal_pdm_enable(PDM_Handle);
    ns_lp_printf("[MAIN] PDM module initialized and enabled\n");

    //*********************************************************************************
    // Initialize I2S.
    //
    //*********************************************************************************
    ns_lp_printf("[MAIN] Initializing I2S module\n");

    i2s_init();
    am_hal_i2s_enable(I2S_Handle);

    ns_lp_printf("[MAIN] I2S DMA transfer started\n");

    am_hal_i2s_dma_transfer_start(I2S_Handle, &I2S_Config);

    am_hal_interrupt_master_enable();

    am_util_delay_ms(5); // Let's give I2S a head start...

    //*********************************************************************************
    // Start PDM streaming.
    //
    //*********************************************************************************
    ns_lp_printf("[MAIN] Starting PDM DMA streaming\n");
    am_hal_pdm_dma_start(PDM_Handle, &Transfer_PDM);
    ns_lp_printf("[MAIN] PDM DMA streaming started\n");

    // Avoid interrupt coming simultaneously.
    am_util_delay_ms(5);

    //*********************************************************************************
    // Initialize Profiler
    //*********************************************************************************

    ns_lp_printf("MCPS estimation\n");
    ns_init_perf_profiler();
    ns_reset_perf_counters();

    //*********************************************************************************
    // initialize neural nets controller
    //*********************************************************************************
    seCntrlClass_init(&cntrl_inst);
    seCntrlClass_reset(&cntrl_inst);
    //*********************************************************************************
    // Loop forever while sleeping.
    //*********************************************************************************
    ns_lp_printf("[MAIN] Entering main loop\n");
    while (1) {
        main_loop_count++;
        NS_TRY(ns_set_performance_mode(NS_MAXIMUM_PERF), "Set CPU Perf mode failed. ");
        if (PDM_Data_Ready) { // This is set by PDM interrupt handler

            audioFrameCount++;

            PDM_Status = 0;
            I2S_Status = 0;
            I2S_Data_Ready = 0;
            PDM_Data_Ready = 0;

            //*********************************************************************************
            // Get pointer to the PDM buffer that was just read in.
            //*********************************************************************************
            uint32_t *PDM_Data_Buffer = (uint32_t *)Transfer_PDM.ui32TargetAddr;
            Transfer_PDM.ui32TargetAddr =
                (uint32_t)((PDM_Data_Buffer == (uint32_t *)&PDM_Buffer[0])
                               ? ((uint32_t *)&PDM_Buffer[NUM_OF_PCM_SAMPLES])
                               : ((uint32_t *)&PDM_Buffer[0]));

            pdm_start_time = am_hal_timer_read(0); // Get timer value to calculate latency later

            //*********************************************************************************
            // Get the current I2S buffer for immediate processing
            //*********************************************************************************
            uint32_t *I2S_Data_Buffer = (uint32_t *)Transfer_I2S.ui32TxTargetAddr;
            Transfer_I2S.ui32TxTargetAddr =
                (uint32_t)((I2S_Data_Buffer == (uint32_t *)&I2S_Buffer[0])
                               ? ((uint32_t *)&I2S_Buffer[NUM_OF_I2S_SAMPLES])
                               : ((uint32_t *)&I2S_Buffer[0]));

            // Invalidate the cache to make sure we get the latest data
                        am_hal_cachectrl_dcache_invalidate(   // required, or else we get a stuttering noise.....
                            &(am_hal_cachectrl_range_t){(uint32_t)PDM_Data_Buffer, SIZE_OF_PCM_SAMPLES}, false);
                         //*********************************************************************************
            // Start PDM DMA transfer on the next buffer.
            //*********************************************************************************
           // AP510 ONLY
  
                am_hal_pdm_dma_transfer_continue(PDM_Handle, &Transfer_PDM);

            //*********************************************************************************
            // Start I2S DMA transfer on the next buffer.
            //*********************************************************************************
                        am_hal_i2s_dma_transfer_continue(I2S_Handle, &I2S_Config, &Transfer_I2S);
                        am_hal_cachectrl_dcache_invalidate(
                            &(am_hal_cachectrl_range_t){(uint32_t)I2S_Data_Buffer, SIZE_OF_I2S_SAMPLES}, false);
             //*********************************************************************************
            // Speech Enhancement processing of PDM data.
            // Note that SE is st5aeful, so we must send every frame, even if we discard it later.
            //*********************************************************************************
            // Copy the 16-bit signed audio data from PDM_Data_Buffer to SE input buffer
            for (int i = 0; i < NUM_OF_PCM_SAMPLES; i++) {
                PDM_se_input_buffer[i] = (int16_t)(PDM_Data_Buffer[i] & 0xFFFF);
            }
            // Turn on High-Performancemode to speed up SE processing.
            NS_TRY(ns_set_performance_mode(NS_MAXIMUM_PERF), "Set CPU Perf mode failed. ");
            //*********************************************************************************
            // Call SE processing function: output is enhanced audio buffer.
            //*********************************************************************************
            seCntrlClass_exec(&cntrl_inst, PDM_se_input_buffer, PDM_se_output_buffer);
            // Turn off High performance to reduce power consumption
            NS_TRY(ns_set_performance_mode(NS_MINIMUM_PERF), "Set CPU Perf mode failed. ");

            //*********************************************************************************
            // If SE Enable == true, then overlay the the raw PDM with the SE enhanced audio
            //*********************************************************************************
            if (enableSE) { // SE Switch set by button
                // Convert 16-bit SE output back to 24-bit format for I2S
                for (int i = 0; i < NUM_OF_PCM_SAMPLES; i++) {
                    int32_t sample = (int32_t)PDM_se_output_buffer[i];
                    // Sign-extend 16-bit to 32-bit and shift to 24-bit format
                    PDM_Enhanced_Buffer[i] = (uint32_t)(sample);
                }
                memcpy(I2S_Data_Buffer, PDM_Enhanced_Buffer, SIZE_OF_PCM_SAMPLES);
            } else {
                // Now copy the raw or enhanced audio to I2S buffer.
                memcpy(I2S_Data_Buffer, PDM_Data_Buffer, SIZE_OF_PCM_SAMPLES);
            }
            //*********************************************************************************
            // Check for button press to enable/disable Speech Enhancement output.
            //*********************************************************************************
            if (Button0Pressed) {
                button0_press_count++;
                ns_lp_printf("*** BUTTON 0 PRESSED! %d\n ", button0_press_count);
                enableSE = !enableSE; // Toggle speech enhancement
                if (enableSE) {
                    am_devices_led_on(
                        am_bsp_psLEDs, 0); // Turn on the LED to indicate SE is enabled
                } else {
                    am_devices_led_off(am_bsp_psLEDs, 0); // Otherwise, turn LED off
                }
                Button0Pressed = 0; // Reset the button flag
            }
            if (Button1Pressed) {
                button1_press_count++;
                // Second button is not in use for now.
                ns_lp_printf("*** BUTTON 1 PRESSED! %d\n ", button1_press_count);
                Button1Pressed = 0; // Reset the button flag
            }
        }
        //*********************************************************************************
        // Put processor to sleep.
        //*********************************************************************************
        am_bsp_debug_printf_deepsleep_prepare(true);
        am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP);
        am_bsp_debug_printf_deepsleep_prepare(false);
    }
}
