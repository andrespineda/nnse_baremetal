# Hardware Modification Notes

## Apollo5_EVB J8 Pin Header Configuration

### Pin Assignments
The J8 pin header on the Apollo5_EVB defines I2S Module 0 with the following pin assignments:
- GPIO5: I2S0_CLK (Bit Clock)
- GPIO6: I2S_DATA (Data Line)
- GPIO13: I2S0_WS (Word Select/LR Clock)

### I2S0_WS Pin Change

#### Background
For this specific implementation, the I2S0_WS (Word Select/LR Clock) pin has been modified from the default GPIO 7 to GPIO 13 in the apollo510_evb Board Support Package (BSP) to match the J8 pin header configuration.

#### Reason for Change
This modification was necessary to connect the I2S DAC to the J8 header on the Apollo5_EVB, which uses GPIO13 for I2S0_WS rather than the default GPIO7.

#### Files Affected
In the AmbiqSuite BSP files for apollo510_evb:
- `am_bsp_pins.h` - Pin definitions
- Possibly other BSP configuration files

#### Implementation Details
In AmbiqSuite R5.2.0, the I2S0_WS pin was defined as:
```c
#define AM_BSP_GPIO_I2S0_WS 7
```

In the modified version (R5.3.0 or custom), it was changed to:
```c
#define AM_BSP_GPIO_I2S0_WS 13
```

#### Impact
This change affects the hardware connections required for proper operation:
- The I2S Word Select signal must be connected to GPIO 13 instead of GPIO 7
- Any existing hardware designs using GPIO 7 for I2S WS will need to be updated

#### Required Actions
1. Connect the I2S DAC to the J8 header with proper pin mapping
2. Ensure the hardware is connected correctly with I2S WS on GPIO 13
3. If using a different BSP version, update the pin definitions accordingly
4. Verify that no other peripherals are using GPIO 13 that might conflict with I2S operation

### Apollo5 I2S Clock Source Requirement
On Apollo5 Rev.B0, I2S can only use PLL as the clock source, not HFRC. Using HFRC will prevent I2S from receiving data. Always configure I2S with `eAM_HAL_I2S_CLKSEL_PLL_FOUT3` and ensure proper PLL setup.

This is already handled in the current code with the following configuration:
```c
#if (CLOCK_SOURCE == PLL)
    .eClock = eAM_HAL_I2S_CLKSEL_PLL_FOUT3,
    .eDiv3 = 0,
#endif
```