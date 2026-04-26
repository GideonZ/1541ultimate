/*
 * jtag.c
 *
 *  Created on: Nov 13, 2016
 *      Author: gideon
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "pinout.h"
#include "jtag.h"

#define XILINX_USER1    0x02
#define XILINX_USER2    0x03
#define XILINX_USER3    0x22
#define XILINX_USER4    0x23
#define XILINX_IDCODE   0x09
#define XILINX_USERCODE 0x08
#define XILINX_PROGRAM  0x0B
#define XILINX_START    0x0C
#define XILINX_SHUTDOWN 0x0D
#define XILINX_EXTEST   0x26
#define XILINX_CFG_IN   0x05
#define XILINX_CFG_OUT  0x04

#define VJI_USERID   0x00
#define VJI_GPIOIN   0x01
#define VJI_GPIOOUT  0x02
#define VJI_DEBUG    0x03
#define VJI_READFIFO 0x04
#define VJI_MEMCMD   0x05
#define VJI_MEMDATA  0x06
#define VJI_CONSOLE1 0x0A
#define VJI_CONSOLE2 0x0B

static const char *TAG = "jtag";

// Precondition: Clock is low
static void jtag_raw_tms(uint32_t data, int length)
{
    for (int i=0; i < length; i++) {
        gpio_set_level(IO_FPGA_TMS, data & 1);
        data >>= 1;
        gpio_set_level(IO_FPGA_TCK, 1);
        gpio_set_level(IO_FPGA_TCK, 0);
    }
}

static void jtag_raw_send_with_tms(uint32_t data, int length)
{
    for (int i=0; i < length; i++) {
        gpio_set_level(IO_FPGA_TDI, data & 1);
        gpio_set_level(IO_FPGA_TMS, (i == length-1) ? 1 : 0);
        data >>= 1;
        gpio_set_level(IO_FPGA_TCK, 1);
        gpio_set_level(IO_FPGA_TCK, 0);
    }
}

static void jtag_raw_data(uint32_t data, int length)
{
    for (int i=0; i < length; i++) {
        gpio_set_level(IO_FPGA_TDI, data & 1);
        data >>= 1;
        gpio_set_level(IO_FPGA_TCK, 1);
        gpio_set_level(IO_FPGA_TCK, 0);
    }
}

void jtag_reset_to_idle()
{
    jtag_raw_tms(0x1F, 6); // 1, 1, 1, 1, 1, 0
}

// Precondition: We are in Idle state
void jtag_instruction(uint32_t inst, int len)
{
    jtag_raw_tms(0x03, 4); // send 4 bits: 1, 1, 0, 0 to get into ShiftIR state
    jtag_raw_send_with_tms(inst, len); // send 10 bits of instruction data, we are now in Exit1-IR state
    jtag_raw_tms(0x01, 2); // send 2 bits: 1, 0 on TMS to get back to idle state
}

void jtag_clocks(int clocks)
{
    for (int i = 0; i < clocks; i++) {
        gpio_set_level(IO_FPGA_TCK, 1);
        gpio_set_level(IO_FPGA_TCK, 0);
    }
}

void jtag_start()
{
    gpio_config_t io_conf_jtag_out = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << IO_FPGA_TCK) | (1ULL << IO_FPGA_TMS) | (1ULL << IO_FPGA_TDI),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config_t io_conf_jtag_in = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << IO_FPGA_TDO),
        .pull_down_en = 0,
        .pull_up_en = 1,
    };
    gpio_set_level(IO_FPGA_TCK, 0);
    gpio_set_level(IO_FPGA_TMS, 0);
    gpio_set_level(IO_FPGA_TDI, 0);
    gpio_config(&io_conf_jtag_out);
    gpio_config(&io_conf_jtag_in);

    jtag_reset_to_idle();
}

void jtag_disable_io()
{
    gpio_config_t io_conf_jtag_in = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << IO_FPGA_TDI) | (1ULL << IO_FPGA_TCK) | (1ULL << IO_FPGA_TMS) | (1ULL << IO_FPGA_TDO),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };

    gpio_config(&io_conf_jtag_in);
}

void jtag_stop()
{
    jtag_raw_tms(0x1F, 5); // 1, 1, 1, 1, 1  Go to reset state
    jtag_disable_io();
}

/*
void jtag_instruction_pause_ir(volatile uint32_t *host, uint32_t inst)
{
	host[0] = JTAG_TMSSEL | (3 << 16) | 0x03; // send 4 bits: 1, 1, 0, 0 to get into ShiftIR state
	host[0] = JTAG_LAST   | (9 << 16) | inst; // send 10 bits of instruction data, we are now in Exit1-IR state
	host[0] = JTAG_TMSSEL | (0 << 16) | 0x00; // send 1 bits: 0 on TMS to get to pause_ir state
}

void jtag_pause_ir_instruction(volatile uint32_t *host, uint32_t inst)
{
	host[0] = JTAG_TMSSEL | (5 << 16) | 0x0F; // send 6 bits: 1, 1, 1, 1, 0, 0 to get into ShiftIR state
	host[0] = JTAG_LAST   | (9 << 16) | inst; // send 10 bits of instruction data, we are now in Exit1-IR state
	host[0] = JTAG_TMSSEL | (0 << 16) | 0x00; // send 1 bits: 0 on TMS to get to pause_ir state
}

void jtag_exit_pause_ir(volatile uint32_t *host)
{
	host[0] = JTAG_TMSSEL | (2 << 16) | 0x03; // send 3 bits: 1, 1, 0 to get into idle state
}
*/

// Precondition: We are in Idle state
void jtag_go_to_shift_dr()
{
    jtag_raw_tms(0x01, 3); // send 3 bits: 1, 0, 0 to get into ShiftDR state
}

void jtag_send_dr_data(const uint8_t *data, int length, uint8_t *readback)
{
    if (!readback) {
        while (length > 8) {
            uint8_t byte = *(data++);
            for (int i = 0; i < 8; i++) {
                gpio_set_level(IO_FPGA_TDI, byte & 1);
                byte >>= 1;
                gpio_set_level(IO_FPGA_TCK, 1);
                gpio_set_level(IO_FPGA_TCK, 0);
            }
            length -= 8;
        }

        uint8_t byte = *(data++);
        for (int i = 0; i < length; i++) {
            gpio_set_level(IO_FPGA_TDI, byte & 1);
            gpio_set_level(IO_FPGA_TMS, (i == length - 1) ? 1 : 0);
            byte >>= 1;
            gpio_set_level(IO_FPGA_TCK, 1);
            gpio_set_level(IO_FPGA_TCK, 0);
        }
    } else { // with readback
        while (length > 8) {
            uint8_t rb = 0;
            uint8_t byte = *(data++);
            for (int i = 0; i < 8; i++) {
                rb >>= 1;
                rb |= (gpio_get_level(IO_FPGA_TDO) & 1) << 7;
                gpio_set_level(IO_FPGA_TDI, byte & 1);
                byte >>= 1;
                gpio_set_level(IO_FPGA_TCK, 1);
                gpio_set_level(IO_FPGA_TCK, 0);
            }
            (*readback++) = rb;
            length -= 8;
        }

        uint8_t byte = *(data++);
        uint8_t rb = 0;
        for (int i = 0; i < length; i++) {
            gpio_set_level(IO_FPGA_TDI, byte & 1);
            rb |= (gpio_get_level(IO_FPGA_TDO) & 1) << i;
            gpio_set_level(IO_FPGA_TMS, (i == length - 1) ? 1 : 0);
            byte >>= 1;
            gpio_set_level(IO_FPGA_TCK, 1);
            gpio_set_level(IO_FPGA_TCK, 0);
        }
        (*readback++) = rb;
    }
    jtag_raw_tms(0x01, 2); // send 2 bits: 1, 0 on TMS to get back to idle state
}

void jtag_send_dr_data_rev(const uint8_t *data, int length)
{
    while (length > 8) {
        uint8_t byte = *(data++);
        for (int i = 0; i < 8; i++) {
            gpio_set_level(IO_FPGA_TDI, byte >> 7);
            byte <<= 1;
            gpio_set_level(IO_FPGA_TCK, 1);
            gpio_set_level(IO_FPGA_TCK, 0);
        }
        length -= 8;
    }

    uint8_t byte = *(data++);
    for (int i = 0; i < length; i++) {
        gpio_set_level(IO_FPGA_TDI, byte >> 7);
        gpio_set_level(IO_FPGA_TMS, (i == length - 1) ? 1 : 0);
        byte <<= 1;
        gpio_set_level(IO_FPGA_TCK, 1);
        gpio_set_level(IO_FPGA_TCK, 0);
    }
    jtag_raw_tms(0x01, 2); // send 2 bits: 1, 0 on TMS to get back to idle state
}

uint32_t jtag_get_id_code()
{
    uint32_t id_code = 0;
    jtag_reset_to_idle(); // the reset will select the ID code
    jtag_go_to_shift_dr();
    jtag_send_dr_data((uint8_t *)&id_code, 32, (uint8_t *)&id_code);
    return id_code;
}

static void set_user_ir(uint8_t ir)
{
    jtag_instruction(XILINX_USER4, 6);
    ir <<= 1;
    ir |= 1;
    jtag_go_to_shift_dr();
    jtag_send_dr_data(&ir, 5, NULL);
    jtag_instruction(XILINX_USER4, 6);
    jtag_raw_tms(0x01, 3); // send 3 bits: 1, 0, 0 to get into ShiftDR state
    jtag_raw_data(0, 1); // Clock in one zero, and remain in shift_dr
}

uint32_t vji_get_user_code()
{
    uint32_t user_id;
    set_user_ir(VJI_USERID);
    // do NOT go to shift_dr again, we are already there
    jtag_send_dr_data((uint8_t *)&user_id, 32, (uint8_t *)&user_id);
    // after send_dr, we are in idle
    return user_id;
}

static int vji_read_fifo(uint8_t cmd, uint8_t *data, int expected, uint8_t stopOnEmpty)
{
    uint8_t available = 0;
    int total_read = 0;

    while(expected > 0) {
        set_user_ir(cmd);
        jtag_send_dr_data(&available, 8, &available);    
        ESP_LOGI(TAG, "Number of words available in FIFO: %d, need: %d", available, expected);

        if (available == 0) {
            if (stopOnEmpty) {
                break;
            } else {
                ESP_LOGW(TAG, "No more bytes in fifo?!");
                jtag_reset_to_idle();
                break;
            }
        }
        memset(data, 0, available);
        data[available-1] = 0xF0; // no read on last
        jtag_send_dr_data(data, 8 * available, data);
        expected -= available;
        data += available;
        total_read += available;
    }
    return total_read;
}

void vji_write_memory(const uint32_t address, const uint8_t *data, const int bytes)
{
    uint8_t command[12];
    command[0] = (address >> 0) & 0xFF;
    command[1] = 4;
    command[2] = (address >> 8) & 0xFF;
    command[3] = 5;
    command[4] = (address >> 16) & 0xFF;
    command[5] = 6;
    command[6] = (address >> 24) & 0xFF;
    command[7] = 7;
    command[8] = 0x80;
    command[9] = 0x01;

    set_user_ir(VJI_MEMCMD);
    jtag_send_dr_data(command, 80, NULL);
    // now in idle

    set_user_ir(VJI_MEMDATA);
    jtag_send_dr_data(data, bytes * 8, NULL);
    // now in idle
}

void vji_read_memory(const uint32_t address, uint8_t *data, const int bytes)
{
    uint8_t command[12];
    uint32_t addr = address;
    int len = bytes / 4;
    while(len > 0) {
        int now = len < 256 ? len : 256;
        command[0] = (addr >> 0) & 0xFF;
        command[1] = 4;
        command[2] = (addr >> 8) & 0xFF;
        command[3] = 5;
        command[4] = (addr >> 16) & 0xFF;
        command[5] = 6;
        command[6] = (addr >> 24) & 0xFF;
        command[7] = 7;
        command[8] = (uint8_t)(now - 1);
        command[9] = 0x03;

        set_user_ir(VJI_MEMCMD);
        jtag_send_dr_data(command, 80, NULL);
        // now in idle, memory controller is pushing data into fifo

        int bytes = vji_read_fifo(VJI_READFIFO, data, now*4, 0);
        if (bytes != now*4) {
            ESP_LOGW(TAG, "Read %d bytes, expected %d", bytes, now*4);
            break;
        }
        len -= now;
        addr += 4 * now;
        data += 4 * now;
    }
}

extern const uint8_t fpga_start[] asm("_binary_u64_mk2_artix_bit_start");
extern const uint8_t fpga_end[]   asm("_binary_u64_mk2_artix_bit_end");

void jtag_configure_fpga(void)
{
    int size = 8 * (fpga_end - fpga_start);
    jtag_reset_to_idle();
    jtag_instruction(XILINX_PROGRAM, 6);
    jtag_clocks(10000);  // stay in idle
    jtag_instruction(XILINX_CFG_IN, 6);
    jtag_go_to_shift_dr();
    ESP_LOGI(TAG, "Sending %d bits to FPGA", size);
    jtag_send_dr_data_rev(fpga_start, size);
    ESP_LOGI(TAG, "Done.. Starting..");
    jtag_instruction(XILINX_START, 6);
    jtag_clocks(32);
}
