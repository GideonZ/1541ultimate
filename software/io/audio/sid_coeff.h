/*
 * sid_coeff.h
 *
 *  Created on: Oct 14, 2018
 *      Author: gideon
 */

#ifndef SID_COEFF_H_
#define SID_COEFF_H_

extern const uint16_t sid6581_filter_coefficients[];
extern const uint16_t sid8580_filter_coefficients[];
extern const uint16_t sid6581_filter_coefficients_temp[];
extern const uint16_t sid_curve_original[];

void set_sid_coefficients(volatile uint8_t *filter_ram, const uint16_t *coefficients, int mul, int div);

#endif /* SID_COEFF_H_ */
