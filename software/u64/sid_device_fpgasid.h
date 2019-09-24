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
        static void S_effectuate(volatile uint8_t *base, ConfigStore *store);
    public:
        FpgaSidConfig(SidDeviceFpgaSid *parent);
        void effectuate_settings();
        ConfigStore *getConfigStore(void) { return cfg; }

        static volatile uint8_t *pre(ConfigItem *it, int sid);
        static void post(ConfigItem *it);

        static uint8_t getByte31Sid1(ConfigStore *cfg);
        static uint8_t getByte31Sid2(ConfigStore *cfg);
        static uint8_t getByte30Sid1(ConfigStore *cfg);
        static uint8_t getByte30Sid2(ConfigStore *cfg);
        static void setItemsEnable(ConfigItem *it);

        static int  S_cfg_fpgasid_mode           (ConfigItem *it);
        static int  S_cfg_fpgasid_leds           (ConfigItem *it);
        static int  S_cfg_fpgasid_sid1_quick     (ConfigItem *it);
        static int  S_cfg_fpgasid_sid1_byte31    (ConfigItem *it);
        static int  S_cfg_fpgasid_sid1_digifix   (ConfigItem *it);
        static int  S_cfg_fpgasid_sid1_filterbias(ConfigItem *it);
        static int  S_cfg_fpgasid_outputmode(ConfigItem *it);

        static int  S_cfg_fpgasid_sid2_quick     (ConfigItem *it);
        static int  S_cfg_fpgasid_sid2_byte30    (ConfigItem *it);
        static int  S_cfg_fpgasid_sid2_byte31    (ConfigItem *it);
        static int  S_cfg_fpgasid_sid2_digifix   (ConfigItem *it);
        static int  S_cfg_fpgasid_sid2_filterbias(ConfigItem *it);
    };

    FpgaSidConfig *config;
public:
    SidDeviceFpgaSid(int socket, volatile uint8_t *base);
    virtual ~SidDeviceFpgaSid();

    void SetSidType(int type);
};

#endif /* SID_DEVICE_FPGASID_H_ */
