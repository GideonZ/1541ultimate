#include <stdio.h>
#include "nau8822.h"
#include "FreeRTOS.h"
#include "task.h"
#define NAU8822_I2C_ADDRESS 0x34
#include "u2p.h"
#include "itu.h"

// Function to initialize the NAU8822
void nau8822_init(int channel)
{
    printf("NAU8822 initialization...\n");

    i2c->set_channel(channel);

    // Soft reset
    i2c->i2c_write_nau(NAU8822_I2C_ADDRESS, 0x00, 0x000); // Reset all registers
    wait_ms(2);

    // Power management settings
    i2c->i2c_write_nau(NAU8822_I2C_ADDRESS, 0x01, 0x0CF); // Enable AUX1, AUX2, BIAS, IOBUF, REF=3K (bit5 = PLL)
    i2c->i2c_write_nau(NAU8822_I2C_ADDRESS, 0x02, 0x03F); // Enable ADC, PGA and input mixers for left and right
    i2c->i2c_write_nau(NAU8822_I2C_ADDRESS, 0x03, 0x18F); // Enable AUX out, Main mixers, and DACs

    // Interface settings
    i2c->i2c_write_nau(NAU8822_I2C_ADDRESS, 0x04, 0x050); // Default value: Left justified, 24 bit I2S 

    // Clocking settings
    i2c->i2c_write_nau(NAU8822_I2C_ADDRESS, 0x06, 0x000); // Use MCLK as input, divide by 1 to get 256fs, do not use PLL
    i2c->i2c_write_nau(NAU8822_I2C_ADDRESS, 0x07, 0x000); // 48 kHz is the output rate

    // DAC Control
    i2c->i2c_write_nau(NAU8822_I2C_ADDRESS, 0x0A, 0x008); // 128x oversampling for better SNR
    // Volume (reg 0x0B and 0x0C) are by default max (0xFF)

    // ADC Control
    i2c->i2c_write_nau(NAU8822_I2C_ADDRESS, 0x0E, 0x108); // 128x oversampling for better SNR, HPF enabled
    // Volume (reg 0x0F and 0x10) are by default max (0xFF)

    // Analog audio path control
    i2c->i2c_write_nau(NAU8822_I2C_ADDRESS, 0x2F, 0x007); // Enable input from LAUXIN to left ADC (+6 dB)
    i2c->i2c_write_nau(NAU8822_I2C_ADDRESS, 0x30, 0x007); // Enable input from RAUXIN to right ADC (+6 dB)

    printf("NAU8822 initialization complete.\n");
}
