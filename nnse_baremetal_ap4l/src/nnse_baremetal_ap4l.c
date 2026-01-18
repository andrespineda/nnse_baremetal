//*****************************************************************************
//
// nnse_baremetal_ap4l_production.c - Production NeuralSPOT Speech Enhancement
//
// Purpose: Final implementation using proven Apollo4 PDM-to-I2S working code
//          as foundation, with speech enhancement processing integrated
//
//*****************************************************************************

#include "am_mcu_apollo.h"
#include "am_bsp.h"
#include "am_util.h"
#include "SEGGER_RTT.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// Audio processing includes
#include "def_nn3_se.h"
#include "seCntrlClass.h"
#include "ns_peripherals_button.h"  // NeuralSPOT button peripheral

//*****************************************************************************
// Configuration (matching working example)
//*****************************************************************************
#define PDM_MODULE 0  // Only PDM0 available on Apollo4L
#define I2S_MODULE 0  // Only I2S0 available on Apollo4L

// Audio buffer sizes (from working example)
#define PDM_STREAM_SIZE 320  // 20ms at 16kHz
#define DMA_SIZE PDM_STREAM_SIZE
#define PDM_STREAM_BYTES (PDM_STREAM_SIZE * 4)

// Button configuration for Apollo4L Blue EVB
#define BUTTON0_GPIO 18  // Button 0 pin from BSP file (GPIO 18 = SW1)

//*****************************************************************************
// Global variables
//*****************************************************************************
static bool se_enabled = true;
static uint32_t audioFrameCount = 0;
static volatile bool g_bPDMDataReady = false;

// Button peripheral configuration (like working nnse.cc)
static int volatile g_intButtonPressed = 0;
static ns_button_config_t button_config_nnsp = {
    .api = &ns_button_V1_0_0,
    .button_0_enable = true,
    .button_1_enable = false,
    .button_0_flag = &g_intButtonPressed,
    .button_1_flag = NULL};

// Audio buffers (using proven Apollo4L setup)
AM_SHARED_RW uint32_t g_ui32PingPongBuffer[DMA_SIZE + DMA_SIZE + 3];
AM_SHARED_RW int32_t I2S_Buffer[2 * 160] __attribute__((aligned(32)));
int16_t PDM_se_input_buffer[160] __attribute__((aligned(32)));
int16_t PDM_se_output_buffer[160] __attribute__((aligned(32)));
int32_t PDM_Enhanced_Buffer[160] __attribute__((aligned(32)));

// Handles
void *PDMHandle;
void *I2SHandle;

// Speech Enhancement Control Structure
seCntrlClass cntrl_inst;

//*****************************************************************************
// Transfer structures (from working example)
//*****************************************************************************
am_hal_pdm_transfer_t g_sTransferPDM = {
    .ui32TargetAddr = 0x0,
    .ui32TargetAddrReverse = 0x0,
    .ui32TotalCount = PDM_STREAM_BYTES,
};

am_hal_i2s_transfer_t g_sTransferI2S = {
    .ui32TxTotalCount = DMA_SIZE,
    .ui32TxTargetAddr = 0x0,
    .ui32TxTargetAddrReverse = 0x0,
};

//*****************************************************************************
// PDM configuration (from working example)
//*****************************************************************************
am_hal_pdm_config_t g_sPdmConfig = {
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
};

//*****************************************************************************
// I2S configuration (from working example)
//*****************************************************************************
static am_hal_i2s_io_signal_t g_sI2SIOConfig = {
    .eFyncCpol = AM_HAL_I2S_IO_FSYNC_CPOL_HIGH,
    .eTxCpol = AM_HAL_I2S_IO_TX_CPOL_FALLING,
    .eRxCpol = AM_HAL_I2S_IO_RX_CPOL_RISING,
};

static am_hal_i2s_data_format_t g_sI2SDataConfig = {
    .ePhase = AM_HAL_I2S_DATA_PHASE_SINGLE,
    .ui32ChannelNumbersPhase1 = 2,
    .ui32ChannelNumbersPhase2 = 0,
    .eDataDelay = 0x1,
    .eDataJust = AM_HAL_I2S_DATA_JUSTIFIED_LEFT,
    .eChannelLenPhase1 = AM_HAL_I2S_FRAME_WDLEN_32BITS,
    .eChannelLenPhase2 = AM_HAL_I2S_FRAME_WDLEN_32BITS,
    .eSampleLenPhase1 = AM_HAL_I2S_SAMPLE_LENGTH_24BITS,
    .eSampleLenPhase2 = AM_HAL_I2S_SAMPLE_LENGTH_24BITS
};

static am_hal_i2s_config_t g_sI2SConfig = {
    .eClock = eAM_HAL_I2S_CLKSEL_HFRC2_3MHz,
    .eDiv3 = 1,
    .eASRC = 0,
    .eMode = AM_HAL_I2S_IO_MODE_MASTER,
    .eXfer = AM_HAL_I2S_XFER_TX,
    .eData = &g_sI2SDataConfig,
    .eIO = &g_sI2SIOConfig
};

//*****************************************************************************
// PDM interrupt handler (from working example)
//*****************************************************************************
void am_pdm0_isr(void) {
    uint32_t ui32Status;
    
    am_hal_pdm_interrupt_status_get(PDMHandle, &ui32Status, true);
    am_hal_pdm_interrupt_clear(PDMHandle, ui32Status);
    am_hal_pdm_interrupt_service(PDMHandle, ui32Status, &g_sTransferPDM);
    
    if (ui32Status & AM_HAL_PDM_INT_DCMP) {
        g_bPDMDataReady = true;
    }
}

//*****************************************************************************
// I2S interrupt handler (from working example)
//*****************************************************************************
void am_dspi2s0_isr(void) {
    uint32_t ui32Status;
    
    am_hal_i2s_interrupt_status_get(I2SHandle, &ui32Status, true);
    am_hal_i2s_interrupt_clear(I2SHandle, ui32Status);
    am_hal_i2s_interrupt_service(I2SHandle, ui32Status, &g_sI2SConfig);
}

//*****************************************************************************
// PDM initialization (from working example)
//*****************************************************************************
void pdm_init(void) {
    // Initialize PDM
    am_hal_pdm_initialize(PDM_MODULE, &PDMHandle);
    am_hal_pdm_power_control(PDMHandle, AM_HAL_PDM_POWER_ON, false);
    
    // Clock setup (essential for Apollo4L)
    am_hal_mcuctrl_control_arg_t ctrlArgs = g_amHalMcuctrlArgDefault;
    ctrlArgs.ui32_arg_hfxtal_user_mask = 1 << (AM_HAL_HCXTAL_PDM_BASE_EN + PDM_MODULE);
    am_hal_mcuctrl_control(AM_HAL_MCUCTRL_CONTROL_EXTCLK32M_KICK_START, (void *)&ctrlArgs);
    
    // Enable HFRC2 and set up frequency adjustment
    am_hal_clkgen_control(AM_HAL_CLKGEN_CONTROL_HFRC2_START, false);
    am_util_delay_us(500);
    am_hal_clkgen_control(AM_HAL_CLKGEN_CONTROL_HF2ADJ_ENABLE, false);
    am_util_delay_us(500);
    
    // Configure PDM with working settings
    am_hal_pdm_configure(PDMHandle, &g_sPdmConfig);
    
    // Enable PDM pins
    am_bsp_pdm_pins_enable(PDM_MODULE);
    
    // Set FIFO threshold
    am_hal_pdm_fifo_threshold_setup(PDMHandle, 16);
    
    // Enable interrupts
    am_hal_pdm_interrupt_enable(PDMHandle, (AM_HAL_PDM_INT_DCMP | AM_HAL_PDM_INT_OVF));
    NVIC_SetPriority(PDM0_IRQn, AM_IRQ_PRIORITY_DEFAULT);
    NVIC_EnableIRQ(PDM0_IRQn);
}

//*****************************************************************************
// I2S initialization (from working example)
//*****************************************************************************
void i2s_init(void) {
    // Enable HFRC2 for I2S
    am_hal_clkgen_control(AM_HAL_CLKGEN_CONTROL_HFRC2_START, false);
    am_util_delay_us(500);
    
    // Enable I2S pins
    am_bsp_i2s_pins_enable(I2S_MODULE, false);
    
    // Initialize and configure I2S
    am_hal_i2s_initialize(I2S_MODULE, &I2SHandle);
    am_hal_i2s_power_control(I2SHandle, AM_HAL_I2S_POWER_ON, false);
    am_hal_i2s_configure(I2SHandle, &g_sI2SConfig);
    am_hal_i2s_enable(I2SHandle);
    
    // Enable I2S interrupt
    NVIC_SetPriority(I2S0_IRQn, AM_IRQ_PRIORITY_DEFAULT);
    NVIC_EnableIRQ(I2S0_IRQn);
}

//*****************************************************************************
// Audio processing initialization
//*****************************************************************************
void init_audio_processing(void) {
    // Set up DMA buffers with proper alignment (16-byte boundary)
    uint32_t ui32PDMDataPtr = (uint32_t)((uint32_t)(g_ui32PingPongBuffer + 3) & ~0xF);
    g_sTransferPDM.ui32TargetAddr = ui32PDMDataPtr;
    g_sTransferPDM.ui32TargetAddrReverse = g_sTransferPDM.ui32TargetAddr + g_sTransferPDM.ui32TotalCount;
    
    // Initialize speech enhancement engine
    seCntrlClass_init(&cntrl_inst);
    seCntrlClass_reset(&cntrl_inst);
    
    SEGGER_RTT_printf(0, "Audio processing initialized successfully\n");
}

//*****************************************************************************
// Audio frame processing
//*****************************************************************************
void process_audio_frame(void) {
    // Diagnostic counter - increment on every call, not just when data is ready
    static uint32_t diag_counter = 0;
    diag_counter++;
    
    // Button processing using neuralSPOT peripheral (like working nnse.cc)
    if (g_intButtonPressed) {
        se_enabled = !se_enabled;
        SEGGER_RTT_printf(0, "Button pressed! Speech Enhancement %s\n", 
                         se_enabled ? "ENABLED" : "DISABLED");
        
        // Control LED1 based on SE state (active-LOW LEDs)
        if (se_enabled) {
            am_hal_gpio_output_clear(15);  // LED1 ON (GPIO 15)
        } else {
            am_hal_gpio_output_set(15);    // LED1 OFF
        }
        
        g_intButtonPressed = 0;  // Clear immediately like working code
    }
    
    // DIAGNOSTICS DISABLED - Uncomment to re-enable
    /*
    // Show diagnostics every 20 calls (~800 times/second at 16kHz)
    if (diag_counter % 20 == 0) {
        // Get current PDM buffer even if no new data ready
        uint32_t *PDM_Data_Buffer = (uint32_t *)am_hal_pdm_dma_get_buffer(PDMHandle);
        SEGGER_RTT_printf(0, "PDM: 0x%X ", (unsigned int)PDM_Data_Buffer);
        
        // Show first 4 samples to verify data is changing
        for (int i = 0; i < 4; i++) {
            SEGGER_RTT_printf(0, "%08X ", PDM_Data_Buffer[i]);
        }
        SEGGER_RTT_printf(0, "\n");
    }
    */
    
    if (!g_bPDMDataReady) return;
    
    // Get current PDM buffer from DMA (needed for audio processing)
    uint32_t *PDM_Data_Buffer = (uint32_t *)am_hal_pdm_dma_get_buffer(PDMHandle);
    
    // Get current I2S buffer
    uint32_t *I2S_Data_Buffer = (uint32_t *)g_sTransferI2S.ui32TxTargetAddr;
    
    // Extract 16-bit samples from PDM data for speech enhancement
    for (int i = 0; i < 160; i++) {
        PDM_se_input_buffer[i] = (int16_t)(PDM_Data_Buffer[i] & 0xFFFF);
    }
    
    // Process audio through speech enhancement
    seCntrlClass_exec(&cntrl_inst, PDM_se_input_buffer, PDM_se_output_buffer);
    
    // Apply enhanced audio to I2S output when SE is enabled
    if (se_enabled) {
        for (int i = 0; i < 160; i++) {
            int32_t sample = (int32_t)PDM_se_output_buffer[i];
            PDM_Enhanced_Buffer[i] = (uint32_t)(sample);
        }
        memcpy(I2S_Data_Buffer, PDM_Enhanced_Buffer, 160 * sizeof(int32_t));
    } else {
        // Pass through raw audio when SE is disabled
        memcpy(I2S_Data_Buffer, PDM_Data_Buffer, 160 * sizeof(int32_t));
    }
    
    // Show I2S output diagnostics every 20 calls
    // DIAGNOSTICS DISABLED - Uncomment to re-enable
    /*
    if (diag_counter % 20 == 0) {
        if (se_enabled) {
            SEGGER_RTT_printf(0, "I2S_SE: ");
            for (int i = 0; i < 4; i++) {
                SEGGER_RTT_printf(0, "%d ", (int32_t)PDM_Enhanced_Buffer[i]);
            }
        } else {
            SEGGER_RTT_printf(0, "I2S_RAW: ");
            for (int i = 0; i < 4; i++) {
                int32_t raw_sample = (int32_t)(PDM_Data_Buffer[i] & 0xFFFF);
                SEGGER_RTT_printf(0, "%d ", raw_sample);
            }
        }
        SEGGER_RTT_printf(0, "\n");
    }
    */
    
    // Set up next I2S buffer (ping-pong)
    g_sTransferI2S.ui32TxTargetAddr = (g_sTransferI2S.ui32TxTargetAddr == (uint32_t)&I2S_Buffer[0]) 
        ? (uint32_t)&I2S_Buffer[160] 
        : (uint32_t)&I2S_Buffer[0];
    
    // Continue DMA transfers
    am_hal_pdm_dma_start(PDMHandle, &g_sTransferPDM);
    am_hal_i2s_dma_transfer_continue(I2SHandle, &g_sI2SConfig, &g_sTransferI2S);
    
    g_bPDMDataReady = false;
    audioFrameCount++;
    
    // Minimal status reporting
    if (audioFrameCount % 2000 == 0) {  // Every ~2.5 seconds at 16kHz
        SEGGER_RTT_printf(0, "Frames: %d, SE: %s\n", audioFrameCount, se_enabled ? "ON" : "OFF");
    }
}

//*****************************************************************************
// Main application
//*****************************************************************************
int main(void) {
    // Enable debug printing
    am_bsp_itm_printf_enable();
    am_util_stdio_terminal_clear();
    
    SEGGER_RTT_printf(0, "=== NeuralSPOT Apollo4L Speech Enhancement ===\n");
    SEGGER_RTT_printf(0, "Using proven Apollo4 PDM-to-I2S working code\n");
    
    // Initialize system
    am_bsp_low_power_init();
    
    // Initialize button peripheral (like working nnse.cc)
    ns_peripheral_button_init(&button_config_nnsp);
    
    // Initialize LED1 (GPIO 15) for SE state indication to avoid I2S pin conflict
    am_hal_gpio_pinconfig(15, g_AM_BSP_GPIO_LED1);
    am_hal_gpio_output_set(15);  // Start with LED OFF (active-LOW)
    
    SEGGER_RTT_printf(0, "Button peripheral and LED1 initialized\n");
    
    // Initialize audio subsystems using working example code
    pdm_init();
    i2s_init();
    init_audio_processing();
    
    // Start PDM streaming
    am_hal_pdm_enable(PDMHandle);
    am_hal_pdm_dma_start(PDMHandle, &g_sTransferPDM);
    
    // Small delay to prevent simultaneous interrupts
    am_util_delay_ms(5);
    
    // Set up I2S transfer using PDM reverse buffer (proven technique)
    g_sTransferI2S.ui32TxTargetAddr = am_hal_pdm_dma_get_buffer(PDMHandle);
    g_sTransferI2S.ui32TxTargetAddrReverse = (g_sTransferI2S.ui32TxTargetAddr == g_sTransferPDM.ui32TargetAddr) 
        ? g_sTransferPDM.ui32TargetAddrReverse 
        : g_sTransferPDM.ui32TargetAddr;
    
    // Configure and start I2S DMA
    am_hal_i2s_dma_configure(I2SHandle, &g_sI2SConfig, &g_sTransferI2S);
    am_hal_i2s_dma_transfer_start(I2SHandle, &g_sI2SConfig);
    
    // Enable interrupts and enter main loop
    am_hal_interrupt_master_enable();
    
    SEGGER_RTT_printf(0, "System operational - Speech Enhancement ready\n");
    
    while (1) {
        process_audio_frame();
        // Remove deep sleep for diagnostics - CPU will stay awake for fast updates
        // am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP);
    }
}