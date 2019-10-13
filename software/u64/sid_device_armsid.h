/*
 * sid_device_armsid.h
 *
 *  Created on: Sep 14, 2019
 *      Author: gideon
 */

#ifndef SID_DEVICE_ARMSID_H_
#define SID_DEVICE_ARMSID_H_

#include "sid_device_swinsid.h"
#include "config.h"
#include <stdio.h>

class SidDeviceArmSid: public SidDevice {
    class ArmSidConfig : ConfigStore
    {
        SidDeviceArmSid *parent;
        static void S_effectuate(volatile uint8_t *base, ConfigStore *store);
        static void S_config_mode(volatile uint8_t *base);
        static void S_save_ram(volatile uint8_t *base);
        static void S_save_flash(volatile uint8_t *base);
        static void S_set_mode(volatile uint8_t *base, uint8_t mode);
        static void S_set_filt(volatile uint8_t *base, ConfigStore *store);

        void readParams(volatile uint8_t *base);
    public:
        ArmSidConfig(SidDeviceArmSid *parent, volatile uint8_t *base, const char *name);

        // Implemented
        void effectuate(void);
        void at_open_config(void);
        void write(void);

        // Not Implemented
        //void read(void) { printf("ArmSidRead\n"); }
        //void at_close_config(void) { printf("ArmSidClose\n"); }

        static volatile uint8_t *pre(ConfigItem *it);
        static void post(ConfigItem *it);

        static int  S_cfg_armsid_type           (ConfigItem *it);
        static int  S_cfg_armsid_filt           (ConfigItem *it);
    };

    ArmSidConfig *config;
public:
    SidDeviceArmSid(int socket, volatile uint8_t *base);
    virtual ~SidDeviceArmSid();

};

#endif /* SID_DEVICE_ARMSID_H_ */
