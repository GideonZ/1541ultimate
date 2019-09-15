/*
 * sid_device_fpgasid.h
 *
 *  Created on: Sep 14, 2019
 *      Author: gideon
 */

#ifndef SID_DEVICE_FPGASID_H_
#define SID_DEVICE_FPGASID_H_

#include "sid_device.h"
#include "config.h"

class SidDeviceFpgaSid: public SidDevice {
    uint8_t unique[8];

    class FpgaSidConfig : ConfigurableObject
    {
        SidDeviceFpgaSid *parent;
    public:
        FpgaSidConfig(SidDeviceFpgaSid *parent);
        void effectuate_settings();
    };

    FpgaSidConfig config;
public:
    SidDeviceFpgaSid(int socket, volatile uint8_t *base);
    virtual ~SidDeviceFpgaSid();

};

#endif /* SID_DEVICE_FPGASID_H_ */
