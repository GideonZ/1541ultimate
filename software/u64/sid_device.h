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
    uint8_t pre_mode;
    int socket;
    volatile uint8_t *currentAddress;

    SidDevice(int socket);
    virtual ~SidDevice();

    virtual void SetSidType(int type) { }
    volatile uint8_t *pre(void);
    void post(void);
};

#endif /* SID_DEVICE_H_ */
