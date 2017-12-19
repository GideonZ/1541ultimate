/*
 * fpll.h
 *
 *  Created on: Dec 15, 2017
 *      Author: gideon
 */

#ifndef FPLL_H_
#define FPLL_H_

extern "C" void SetScanMode(int modeIndex);
extern "C" void pllOffsetHz(int Hz);
extern "C" void pllOffsetPpm(int ppm);

#endif /* FPLL_H_ */
