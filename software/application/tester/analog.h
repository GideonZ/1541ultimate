/*
 * analog.h
 *
 *  Created on: Nov 13, 2016
 *      Author: gideon
 */

#ifndef ANALOG_H_
#define ANALOG_H_

#include <stdint.h>

void configure_adc(void);
void adc_read_all(uint8_t *buffer);
uint16_t adc_read_channel(int channel);
void report_analog(void);
int validate_analog(int);
uint16_t adc_read_current(void);
uint32_t adc_read_channel_corrected(int channel);

#endif /* ANALOG_H_ */
