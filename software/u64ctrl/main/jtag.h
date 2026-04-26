/*
 * jtag.h
 *
 *  Created on: Nov 13, 2016
 *  Rewritten on: Oct 13, 2024
 *      Author: gideon
 */

#ifndef JTAG_H_
#define JTAG_H_

#include <stdint.h>

void jtag_disable_io();
void jtag_start();
void jtag_stop();

void jtag_reset_to_idle();
void jtag_instruction(uint32_t inst, int len);
void jtag_go_to_shift_dr();
void jtag_send_dr_data(const uint8_t *data, int length, uint8_t *readback);

void jtag_configure_fpga(void);
uint32_t jtag_get_id_code();
uint32_t vji_get_user_code();
void vji_write_memory(const uint32_t address, const uint8_t *data, const int bytes);
void vji_read_memory(const uint32_t address, uint8_t *data, const int bytes);

#endif /* JTAG_H_ */
