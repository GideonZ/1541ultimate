/*
 * sid_device_fpgasid.h
 *
 *  Created on: Sep 14, 2019
 *      Author: gideon
 */

#ifndef SID_DEVICE_SWINSID_H_
#define SID_DEVICE_SWINSID_H_

#include "sid_device.h"
#include "config.h"

class SidDeviceSwinSid: public SidDevice {
    class SwinSidConfig : ConfigurableObject
    {
        SidDeviceSwinSid *parent;
        static void S_effectuate(volatile uint8_t *base, ConfigStore *store);
    public:
        SwinSidConfig(SidDeviceSwinSid *parent);
        void effectuate_settings();

        static volatile uint8_t *pre(ConfigItem *it);
        static void post(ConfigItem *it);

        static int  S_cfg_swinsid_type           (ConfigItem *it);
        static int  S_cfg_swinsid_pitch          (ConfigItem *it);
        static int  S_cfg_swinsid_audioin        (ConfigItem *it);
    };

    SwinSidConfig *config;
public:
    SidDeviceSwinSid(int socket, volatile uint8_t *base);
    virtual ~SidDeviceSwinSid();

    void SetSidType(int type);
};

#endif /* SID_DEVICE_SWINSID_H_ */
