
#include "am_mcu_apollo.h"
#include "am_bsp.h"
#include "am_util.h"
#include "SEGGER_RTT.h"
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

// Normally, only the left channel is Speech Enhanced

// The following variable will enable Right channel speech enhancement if enabled

#ifndef ENABLE_RIGHT_CHANNEL_SE
    #define ENABLE_RIGHT_CHANNEL_SE 1
#endif

#define CLOCK_SOURCE (PLL)
#define HFRC (0)
#define PLL (1)
#define HF2ADJ (2)
#define PDM_MODULE 0
#define I2S_MODULE 0
#define FIFO_THRESHOLD_CNT 16
#define NUM_OF_PCM_SAMPLES 320
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
uint32_t g_ui32FifoOVFCount = 0;
volatile int g_bPDMDataReady = 0;

// Sine wave generation variables
static float g_fPhase = 0.0f;
static float g_fPhaseIncrement = 0.0f;

//
// PDM and I2S handlers.
//
void *PDMHandle;
void *I2SHandle;

//
// PDM and I2S interrupt number.
//
static const IRQn_Type pdm_interrupts[] = {
    PDM0_IRQn,
};
static const IRQn_Type i2s_interrupts[] = {I2S0_IRQn, I2S1_IRQn};

//
// Used as the ping-pong buffer of PDM DMA.
// Aligned to 32 bytes to meet data cache requirements.
//
AM_SHARED_RW uint32_t g_ui32PingPongBuffer[2 * NUM_OF_PCM_SAMPLES] __attribute__((aligned(32)));
//
// Take over the interrupt handler for whichever PDM we're using.
//
#define example_pdm_isr am_pdm_isr1(PDM_MODULE)
#define am_pdm_isr1(n) am_pdm_isr(n)
#define am_pdm_isr(n) am_pdm##n##_isr

//*****************************************************************************
//
// PDM configuration information.
//
//  1.536 MHz PDM CLK OUT:
//      PDM_CLK_OUT = ePDMClkSpeed / (eClkDivider + 1) / (ePDMAClkOutDivder + 1)
//  16 kHz 24bit Sampling:
//      DecimationRate = 48
//      SAMPLEING_FREQ = PDM_CLK_OUT / (ui32DecimationRate * 2)
//
//*****************************************************************************
am_hal_pdm_config_t g_sPdmConfig = {
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
    .ePDMClkSpeed = AM_HAL_PDM_CLK_HFRC2_31MHZ,
#endif
    .ui32DecimationRate = 48,
    .eLeftGain = AM_HAL_PDM_GAIN_0DB,
    .eRightGain = AM_HAL_PDM_GAIN_0DB,
    .eStepSize = AM_HAL_PDM_GAIN_STEP_0_13DB,
    .bHighPassEnable = AM_HAL_PDM_HIGH_PASS_ENABLE,
    .ui32HighPassCutoff = 0x3,
    .bDataPacking = 1,
    .ePCMChannels = AM_HAL_PDM_CHANNEL_STEREO,
    .bPDMSampleDelay = AM_HAL_PDM_CLKOUT_PHSDLY_NONE,
    .ui32GainChangeDelay = AM_HAL_PDM_CLKOUT_DELAY_NONE,
    .bSoftMute = 0,
    .bLRSwap = 0,
};

//
// Ping pong buffer settings.

am_hal_pdm_transfer_t g_sTransferPDM = {
    .ui32TargetAddr = (uint32_t)(&g_ui32PingPongBuffer[0]),
    .ui32TargetAddrReverse = (uint32_t)(&g_ui32PingPongBuffer[NUM_OF_PCM_SAMPLES]),
    .ui32TotalCount = NUM_OF_PCM_SAMPLES * sizeof(uint32_t),
};

//*****************************************************************************
//
// I2S configurations:
//  - 2 channels
//  - 16 kHz sample rate
//  - Standard I2S format
//  - 32 bits word width
//  - 24 bits bit-depth
//
// SCLK = 16000 * 2 * 32 = 1.024 MHz
//
//*****************************************************************************
static am_hal_i2s_io_signal_t g_sI2SIOConfig = {
    .sFsyncPulseCfg =
        {
            .eFsyncPulseType = AM_HAL_I2S_FSYNC_PULSE_ONE_SUBFRAME,
        },
    .eFyncCpol = AM_HAL_I2S_IO_FSYNC_CPOL_LOW,
    .eTxCpol = AM_HAL_I2S_IO_TX_CPOL_FALLING,
    .eRxCpol = AM_HAL_I2S_IO_RX_CPOL_RISING,
};

static am_hal_i2s_data_format_t g_sI2SDataConfig = {
    .ePhase = AM_HAL_I2S_DATA_PHASE_SINGLE,
    .eChannelLenPhase1 = AM_HAL_I2S_FRAME_WDLEN_32BITS,
    .eChannelLenPhase2 = AM_HAL_I2S_FRAME_WDLEN_32BITS,
    .ui32ChannelNumbersPhase1 = 2,
    .ui32ChannelNumbersPhase2 = 0,
    .eDataDelay = 0x1,
    .eSampleLenPhase1 = AM_HAL_I2S_SAMPLE_LENGTH_24BITS,
    .eSampleLenPhase2 = AM_HAL_I2S_SAMPLE_LENGTH_24BITS,
    .eDataJust = AM_HAL_I2S_DATA_JUSTIFIED_LEFT,
};

static am_hal_i2s_config_t g_sI2SConfig = {
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
    .eData = &g_sI2SDataConfig,
    .eIO = &g_sI2SIOConfig,
};

//
// Ping pong buffer settings.
//
static am_hal_i2s_transfer_t g_sTransferI2S = {
    .ui32TxTotalCount = NUM_OF_PCM_SAMPLES,
    .ui32TxTargetAddr = 0x0,
    .ui32TxTargetAddrReverse = 0x0,
};

//*****************************************************************************
//
// Additional variables for Speech enhancement.
//
//*************************************************************************

uint32_t audioFrameCount = 0; // Track number of audio frames processed

// Button state variables
volatile uint32_t button0_press_count = 0;
volatile uint32_t button1_press_count = 0;
int volatile g_intButton0Pressed = 0;
int volatile g_intButton1Pressed = 0;
bool enableSE = false; // Speech enhancement toggle - default to false

// Timer variables for periodic status message
uint32_t g_ui32TimerCount = 0;
ns_timer_config_t g_sTimerConfig;

// Speech Enhancement Control Structure
seCntrlClass cntrl_inst;

// Audio buffer for speech enhancement

int16_t g_left_audioFrame[SAMPLES_FRM_NNCNTRL_CLASS] __attribute__((aligned(16)));
int16_t g_seOutputLeft[SAMPLES_FRM_NNCNTRL_CLASS] __attribute__((aligned(16))); // For left channel SE output

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

// AXI Scratch buffer
// On Apollo4B, need to allocate 20 Words even though we only need 16, to ensure we have 16 Byte
// alignment
#ifdef AM_PART_APOLLO4B
AM_SHARED_RW uint32_t axiScratchBuf[20];
#endif

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//*****************************************************************************
//
// I2S configurations:
//  - 2 channels
//  - 16 kHz sample rate
//  - Standard I2S format
//  - 32 bits word width
//  - 24 bits bit-depth
//
// SCLK = 16000 * 2 * 32 = 1.024 MHz
//
//*************************************************************************

#if USE_DMA
// #define BUFFER_SIZE_BYTES               PDM_FFT_BYTES
//! RX size = TX size * output sample rate /internal sample rate
//! Notice .eClock from g_sI2SConfig and g_sI2SConfig_slave.
// #define BUFFER_SIZE_ASRC_RX_BYTES       PDM_FFT_BYTES
#endif

//*****************************************************************************
//
// I2S configuration information.
//
//*****************************************************************************

//*****************************************************************************
//
// PDM initialization.
//
//*****************************************************************************
void pdm_init(void) {
    SEGGER_RTT_printf(0, "pdm_init: Enter\n");

    // Initialize sine wave parameters
    g_fPhaseIncrement = 2.0f * M_PI * SINE_WAVE_FREQ / SAMPLE_RATE;

    //
    // Configure the necessary pins.
    //
    am_bsp_pdm_pins_enable(PDM_MODULE);
    //
    // Initialize, power-up, and configure the PDM.
    //
    am_hal_pdm_initialize(PDM_MODULE, &PDMHandle);
    am_hal_pdm_power_control(PDMHandle, AM_HAL_PDM_POWER_ON, false);
    am_hal_pdm_configure(PDMHandle, &g_sPdmConfig);

    //
    // Setup the FIFO threshold.
    //
    am_hal_pdm_fifo_threshold_setup(PDMHandle, FIFO_THRESHOLD_CNT);

    //
    // Configure and enable PDM interrupts (set up to trigger on DMA
    // completion).
    //
    am_hal_pdm_interrupt_enable(
        PDMHandle,
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

#if DATA_VERIFY
    am_hal_gpio_output_set(PDM_ISR_TEST_PAD);
#endif

    //
    // Read the interrupt status.
    //
    am_hal_pdm_interrupt_status_get(PDMHandle, &ui32Status, true);
    am_hal_pdm_interrupt_clear(PDMHandle, ui32Status);

#if DATA_VERIFY
    am_hal_pdm_state_t *pState = (am_hal_pdm_state_t *)PDMHandle;
    static uint32_t ui32Switch = 0;
    if (ui32Switch) {
        ui32Switch = 0;
        for (int i = 0; i < DMA_COUNT; i++) {
            ((uint32_t *)pState->ui32BufferPtr)[i] = (i & 0xFF) | 0xAB0000;
        }
    } else {
        ui32Switch = 1;
        for (int i = 0; i < DMA_COUNT; i++) {
            ((uint32_t *)pState->ui32BufferPtr)[i] = (i & 0xFF) | 0xCD0000;
        }
    }

    // am_util_stdio_printf("pdm isr addr = %x.\n", pState->ui32BufferPtr);
#endif

    //
    // Swich ping pong buffer.
    //
    am_hal_pdm_interrupt_service(PDMHandle, ui32Status, &g_sTransferPDM);

    if (ui32Status & AM_HAL_PDM_INT_DCMP) {
        g_bPDMDataReady = true;
    }

    if (ui32Status & AM_HAL_PDM_INT_OVF) {
        am_hal_pdm_fifo_count_get(PDMHandle);
        am_hal_pdm_fifo_flush(PDMHandle);
        g_ui32FifoOVFCount++;
    }

#if DATA_VERIFY
    am_hal_gpio_output_clear(PDM_ISR_TEST_PAD);
#endif
}

//*****************************************************************************
//
// I2S initialization.
//
//*****************************************************************************
void i2s_init(void) {
    SEGGER_RTT_printf(0, "i2s_init\n");
    //
    // Configure the necessary pins.
    //
    am_bsp_i2s_pins_enable(I2S_MODULE, false);

    //
    // Configure the I2S.
    //
    am_hal_i2s_initialize(I2S_MODULE, &I2SHandle);
    am_hal_i2s_power_control(I2SHandle, AM_HAL_I2S_POWER_ON, false);

    if (AM_HAL_STATUS_SUCCESS != am_hal_i2s_configure(I2SHandle, &g_sI2SConfig)) {
        am_util_stdio_printf(
            "ERROR: Invalid I2S configuration.\nNote: For Apollo5 Rev.B0, I2S can only use PLL as "
            "the clock source.\n");
    }
    am_hal_i2s_enable(I2SHandle);
}

//*****************************************************************************
//
// I2S interrupt handler.
//
//*****************************************************************************
#define i2s_isr am_dspi2s_isrx(I2S_MODULE)
#define am_dspi2s_isrx(n) am_dspi2s_isr(n)
#define am_dspi2s_isr(n) am_dspi2s##n##_isr

void i2s_isr(void) {
    uint32_t ui32Status;

#if DATA_VERIFY
    am_hal_gpio_output_set(I2S_ISR_TEST_PAD);
#endif

    am_hal_i2s_interrupt_status_get(I2SHandle, &ui32Status, true);
    am_hal_i2s_interrupt_clear(I2SHandle, ui32Status);

    //
    // Swich ping pong buffer.
    //
    am_hal_i2s_interrupt_service(I2SHandle, ui32Status, &g_sI2SConfig);

#if DATA_VERIFY
    am_hal_gpio_output_clear(I2S_ISR_TEST_PAD);
#endif
}

//*****************************************************************************
//
// pdm_deinit
//
//*****************************************************************************
void pdm_deinit(void *pHandle) {
    am_hal_pdm_interrupt_clear(
        pHandle,
        (AM_HAL_PDM_INT_DERR | AM_HAL_PDM_INT_DCMP | AM_HAL_PDM_INT_UNDFL | AM_HAL_PDM_INT_OVF));

    am_hal_pdm_interrupt_disable(
        pHandle,
        (AM_HAL_PDM_INT_DERR | AM_HAL_PDM_INT_DCMP | AM_HAL_PDM_INT_UNDFL | AM_HAL_PDM_INT_OVF));

    NVIC_DisableIRQ(pdm_interrupts[PDM_MODULE]);

    am_bsp_pdm_pins_disable(PDM_MODULE);

    am_hal_pdm_disable(pHandle);
    am_hal_pdm_power_control(pHandle, AM_HAL_PDM_POWER_OFF, false);
    am_hal_pdm_deinitialize(pHandle);
}

//*****************************************************************************
//
// i2s_deinit
//
//*****************************************************************************
void i2s_deinit(void *pHandle) {
    am_hal_i2s_dma_transfer_complete(pHandle);

    am_hal_i2s_interrupt_clear(pHandle, (AM_HAL_I2S_INT_RXDMACPL | AM_HAL_I2S_INT_TXDMACPL));
    am_hal_i2s_interrupt_disable(pHandle, (AM_HAL_I2S_INT_RXDMACPL | AM_HAL_I2S_INT_TXDMACPL));

    NVIC_DisableIRQ(i2s_interrupts[I2S_MODULE]);

    am_bsp_i2s_pins_disable(I2S_MODULE, false);

    am_hal_i2s_disable(pHandle);
    am_hal_i2s_power_control(pHandle, AM_HAL_I2S_POWER_OFF, false);
    am_hal_i2s_deinitialize(pHandle);
}

//*****************************************************************************
//
// New code for Speech Enhancement related logic
//
//*****************************************************************************

//*****************************************************************************
//
// Button configuration
//
//*****************************************************************************
ns_button_config_t button_config_nnsp = {
    .api = &ns_button_V1_0_0,
    .button_0_enable = true,
    .button_1_enable = true,
    .button_0_flag = &g_intButton0Pressed,
    .button_1_flag = &g_intButton1Pressed

};

//*****************************************************************************
//
// Timer interrupt handler for periodic status messages
//
//*****************************************************************************
void timer_interrupt_handler(ns_timer_config_t *config) {
    // Increment timer count
    g_ui32TimerCount++;

    // Display periodic status message every 5 seconds
    // SEGGER_RTT_printf(0, "\n[STATUS] System running - Uptime: %lu seconds\n", g_ui32TimerCount *
    // 5); SEGGER_RTT_printf(0, "[STATUS] Audio frames processed: %lu\n", audioFrameCount);
    // SEGGER_RTT_printf(0, "[STATUS] FIFO overflows: %lu\n", g_ui32FifoOVFCount);
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
    g_sTimerConfig.api = &ns_timer_V1_0_0;
    g_sTimerConfig.timer = NS_TIMER_INTERRUPT; // Use timer 0
    g_sTimerConfig.enableInterrupt = true;
    g_sTimerConfig.periodInMicroseconds = TIMER_PERIOD_US; // 5 seconds
    g_sTimerConfig.callback = timer_interrupt_handler;

    // Initialize the timer
    if (ns_timer_init(&g_sTimerConfig) != NS_STATUS_SUCCESS) {
        SEGGER_RTT_printf(0, "[TIMER] Failed to initialize periodic timer\n");
    } else {
        SEGGER_RTT_printf(0, "[TIMER] Periodic timer initialized for 5-second intervals\n");
    }
}

//*****************************************************************************
//
// Performance configuration information.
//
//*****************************************************************************

// Custom power mode for BLE+Audio
const ns_power_config_t ns_power_ble = {
    .api = &ns_power_V1_0_0,
    .eAIPowerMode = NS_MAXIMUM_PERF,
    .bNeedAudAdc = false,
    .bNeedSharedSRAM = false,
    .bNeedCrypto = false,
    .bNeedBluetooth = true,
    .bNeedUSB = false,
    .bNeedIOM = false,
    .bNeedAlternativeUART = false,
    .b128kTCM = false,
    .bEnableTempCo = false,
    .bNeedITM = false,
    .bNeedXtal = true};

// Timer and structs for performance profiling
// ns_perf_counters_t start, end, delta;
// int8_t do_it_once = 1;
ns_timer_config_t basic_tickTimer = {
    .api = &ns_timer_V1_0_0,
    .timer = NS_TIMER_COUNTER,
    .enableInterrupt = false,
};

//*****************************************************************************
//
// Main
//
//*****************************************************************************
int main(void) {
    SEGGER_RTT_Init();
    SEGGER_RTT_printf(0, "main: Enter\n");
    // am_bsp_itm_printf_enable();

    NS_TRY(ns_timer_init(&basic_tickTimer), "Timer init failed.\n");

    // Generate a 400hz sin wave (for debugging)
    for (int i = 0; i < SAMPLES_FRM_NNCNTRL_CLASS; i++) {
        sinWave[i] = (int16_t)(sin(2 * 3.14159 * 400 * i / SAMPLING_RATE) * 32767);
    }

    // initialize neural nets controller
    seCntrlClass_init(&cntrl_inst);
    seCntrlClass_reset(&cntrl_inst);

    SEGGER_RTT_printf(0, "[MAIN] Core initialized\n");
    SEGGER_RTT_printf(0, "==============================================\n");
    SEGGER_RTT_printf(0, "PDM_I2S streaming example with button 1 control.\n\n");

    am_bsp_low_power_init();

    // Enable the I-Cache and D-Cache.
    //
    am_hal_cachectrl_icache_enable();
    am_hal_cachectrl_dcache_enable(true);

    //
#if (CLOCK_SOURCE == PLL)
    {
        am_hal_clkmgr_clock_config(AM_HAL_CLKMGR_CLK_ID_SYSPLL, 6144000, NULL);
    }
#elif (CLOCK_SOURCE == HF2ADJ)
    {
        am_hal_clkmgr_clock_config(
            AM_HAL_CLKMGR_CLK_ID_HFRC2, AM_HAL_CLKMGR_HFRC2_FREQ_ADJ_196P608MHZ, NULL);
    }
#endif
    //
    // Initialize button system
    //
    SEGGER_RTT_printf(0, "[MAIN] Initializing button system\n");
    if (ns_peripheral_button_init(&button_config_nnsp) != NS_STATUS_SUCCESS) {
        SEGGER_RTT_printf(0, "[MAIN] Button initialization failed!\n");
    }
    SEGGER_RTT_printf(0, "[MAIN] Button system initialized\n");

    //*********************************************************************************
    // Initialize LED0 for speech enhancement indication
    //
    //*********************************************************************************

    SEGGER_RTT_printf(0, "[MAIN] Initializing LED0\n");

    // Initialize the LED array
    am_devices_led_array_init(am_bsp_psLEDs, AM_BSP_NUM_LEDS);

    // Clear LEDs
    am_devices_led_array_out(am_bsp_psLEDs, AM_BSP_NUM_LEDS, 0x00);

    // Turn on LED0 to indicate SE is enabled by default
    if (enableSE) {
        am_devices_led_array_out(am_bsp_psLEDs, AM_BSP_NUM_LEDS, 0xff);
    }

    SEGGER_RTT_printf(0, "[MAIN] LEDs initialized\n");

    //*********************************************************************************
    //
    // Initialize periodic timer for status messages
    //
    //*********************************************************************************
    SEGGER_RTT_printf(0, "[MAIN] Initializing periodic timer\n");
    init_periodic_timer();
    SEGGER_RTT_printf(0, "[MAIN] Periodic timer initialized\n");

    //*********************************************************************************
    //
    // Enable interrupts
    //
    //*********************************************************************************
    SEGGER_RTT_printf(0, "[MAIN] Enabling interrupts\n");
    ns_interrupt_master_enable();
    am_hal_interrupt_master_enable(); // Also enable the HAL interrupt master
    SEGGER_RTT_printf(0, "[MAIN] Interrupts enabled\n");

    //*********************************************************************************
    //
    // Initialize PDM-to-PCM module
    //
    //*********************************************************************************
    SEGGER_RTT_printf(0, "[MAIN] Initializing PDM module\n");
    pdm_init();
    am_hal_pdm_enable(PDMHandle);
    SEGGER_RTT_printf(0, "[MAIN] PDM module initialized and enabled\n");

    //*********************************************************************************
    //
    // Initialize I2S.
    //
    //*********************************************************************************
    SEGGER_RTT_printf(0, "[MAIN] Initializing I2S module\n");
    i2s_init();
    NVIC_SetPriority(i2s_interrupts[I2S_MODULE], AM_IRQ_PRIORITY_DEFAULT);
    NVIC_EnableIRQ(i2s_interrupts[I2S_MODULE]);
    am_hal_interrupt_master_enable();

    //*********************************************************************************
    //
    // Start PDM streaming.
    //
    //*********************************************************************************
    SEGGER_RTT_printf(0, "[MAIN] Starting PDM DMA streaming\n");
    am_hal_pdm_dma_start(PDMHandle, &g_sTransferPDM);
    SEGGER_RTT_printf(0, "[MAIN] PDM DMA streaming started\n");

    // Avoid interrupt coming simultaneously.
    am_util_delay_ms(5);

    //*********************************************************************************
    // use the reverse buffer of PDM
    //*********************************************************************************

    g_sTransferI2S.ui32TxTargetAddr = am_hal_pdm_dma_get_buffer(PDMHandle);
    g_sTransferI2S.ui32TxTargetAddrReverse =
        (g_sTransferI2S.ui32TxTargetAddr == g_sTransferPDM.ui32TargetAddr)
            ? g_sTransferPDM.ui32TargetAddrReverse
            : g_sTransferPDM.ui32TargetAddr;
    // Start I2S data transaction.
    am_hal_i2s_dma_configure(I2SHandle, &g_sI2SConfig, &g_sTransferI2S);
    am_hal_i2s_dma_transfer_start(I2SHandle, &g_sI2SConfig);
    SEGGER_RTT_printf(0, "[MAIN] I2S DMA transfer started\n");

    //*********************************************************************************
    //
    // Initialize Speech Enhancement
    //
    //*********************************************************************************
    SEGGER_RTT_printf(0, "[MAIN] Initializing Speech Enhancement\n");
    seCntrlClass_init(&cntrl_inst);
    seCntrlClass_reset(&cntrl_inst); // Make sure to reset the SE module
    SEGGER_RTT_printf(0, "[MAIN] Speech Enhancement initialized\n");

    //*********************************************************************************
    // Initializw Profiler
    //*********************************************************************************

    SEGGER_RTT_printf(0, "MCPS estimation\n");
    ns_init_perf_profiler();
    ns_reset_perf_counters();

    //*********************************************************************************
    // Main Loop
    //
    // Loop forever while sleeping.
    //*********************************************************************************

    SEGGER_RTT_printf(0, "[MAIN] Entering main loop\n");
    uint32_t main_loop_count = 0;

    while (1) {
        main_loop_count++;

        // Check for button press
        if (g_intButton0Pressed) {
            button0_press_count++;
            SEGGER_RTT_printf(0, "*** BUTTON 0 PRESSED! %d\n ", button0_press_count);

            enableSE = !enableSE; // Toggle speech enhancement
            if (enableSE) {
                am_devices_led_array_out(am_bsp_psLEDs, AM_BSP_NUM_LEDS, 0xff);
            } else {
                am_devices_led_array_out(am_bsp_psLEDs, AM_BSP_NUM_LEDS, 0x00);
            }
            g_intButton0Pressed = 0; // Reset the button flag
        }
        if (g_intButton1Pressed) {
            button1_press_count++;
            SEGGER_RTT_printf(0, "*** BUTTON 1 PRESSED! %d\n ", button1_press_count);
            g_intButton1Pressed = 0;
        }


        if (g_bPDMDataReady) {
            {
                audioFrameCount++;
                g_bPDMDataReady = 0;

                int32_t *pPDMData = (int32_t *)am_hal_pdm_dma_get_buffer(PDMHandle);

                // Set sample count to process - now matches PDM_STREAM_SIZE and
                // SAMPLES_FRM_NNCNTRL_CLASS
                int sampleCount = SAMPLES_FRM_NNCNTRL_CLASS;

                // Process stereo audio data (32-bit words with 24-bit audio)
                // Extract only left channel for efficiency
                for (int i = 0; i < SAMPLES_FRM_NNCNTRL_CLASS && i < sampleCount; i++) {
                    // Extract 24-bit data from 32-bit word and convert to 16-bit
                    // Assuming interleaved stereo data (left/right pairs)
                    int32_t rawLeftSample = pPDMData[i*2];    // Left channel

                    // Extract left channel (lower 24 bits) and convert to 16-bit
                    int32_t left24Bit = (rawLeftSample & 0x00FFFFFF);
                    // Sign extend if needed (if 24th bit is set)
                    if (left24Bit & 0x00800000) {
                        left24Bit |= 0xFF000000;
                    }
                    // Convert 24-bit to 16-bit by shifting right 8 bits
                    g_left_audioFrame[i] = (int16_t)(left24Bit >> 8);
                }

                // Apply speech enhancement to left channel (always enabled)
                NS_TRY(ns_set_performance_mode(NS_MAXIMUM_PERF), "Set CPU Perf mode failed. ");

                // Start MCPS measurement for left channel SE processing
                ns_start_perf_profiler();
                seCntrlClass_exec(&cntrl_inst, g_left_audioFrame, g_seOutputLeft);
                // Right channel processing removed - only processing left channel
                // Stop MCPS measurement for  SE processing
                ns_stop_perf_profiler();
                ns_capture_perf_profiler(&pp);
                // Calculate and print latency in microseconds
                // Using 250MHz clock for high performance mode
                latency_us = (pp.cyccnt * 1000000) / 250000000;
                SEGGER_RTT_printf(0, "Left Channel SE Latency: %lu microseconds\n", latency_us);


                NS_TRY(ns_set_performance_mode(NS_MINIMUM_PERF), "Set CPU Perf mode failed. ");

                // If speech enhancement is enabled, overlay the enhanced left channel back to the original
                // The right channel data remains unchanged
                if (enableSE) {
                    // Convert 16-bit SE output back to 24-bit format for I2S
                    for (int i = 0; i < SAMPLES_FRM_NNCNTRL_CLASS && i < sampleCount; i++) {
                        // Convert 16-bit enhanced left audio back to 24-bit
                        int32_t enhancedLeft24Bit = ((int32_t)g_seOutputLeft[i]) << 8;

                        // Pack back into 32-bit word preserving stereo format
                        // Update the left channel (lower 24 bits) with enhanced data
                        int32_t originalSample = pPDMData[i*2];
                        // Clear the left channel (lower 24 bits) and insert enhanced data
                        pPDMData[i*2] = (originalSample & 0xFF000000) | (enhancedLeft24Bit & 0x00FFFFFF);

                        // Right channel data is not modified - it passes through unchanged
                        // This preserves the original right channel audio
                    }
                }
            }
        }

        am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP);
    }
}

//*****************************************************************************
//
// Generate sine wave samples
//
//*****************************************************************************
void generate_sine_wave(int32_t *buffer, uint32_t num_samples) {
    for (uint32_t i = 0; i < num_samples; i++) {
        // Calculate sine wave value
        float sine_value = sinf(g_fPhase) * AMPLITUDE;

        // Convert to integer
        int32_t sample = (int32_t)sine_value;

        // Pack stereo samples into 32-bit words
        // For stereo 24-bit audio in 32-bit words:
        // - Left channel: bits 0-23 (lower 24 bits)
        // - Right channel: bits 8-31 (upper 24 bits, shifted left by 8)
        // Since we want the same sine wave on both channels:
        buffer[i] = ((sample & 0x00FFFFFF) << 8) | (sample & 0x00FFFFFF);

        // Update phase
        g_fPhase += g_fPhaseIncrement;
        if (g_fPhase >= 2.0f * M_PI) {
            g_fPhase -= 2.0f * M_PI;
        }
    }
}
