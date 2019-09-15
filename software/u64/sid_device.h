/*
 * sid_device.h
 *
 *  Created on: Sep 14, 2019
 *      Author: gideon
 */

#ifndef SID_DEVICE_H_
#define SID_DEVICE_H_

#include <stdint.h>

class SidDevice {
public:
    int socket;
    volatile uint8_t *currentAddress;

    SidDevice(int socket, volatile uint8_t *base);
    virtual ~SidDevice();

    void set_address(volatile uint8_t *base);
    void pre(void);
    void post(void);
};

#endif /* SID_DEVICE_H_ */
