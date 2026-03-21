/*
 * sid_device_pdsid.h
 *
 *  Created on: Feb 22, 2026
 *      Author: gideon
 */

#ifndef SID_DEVICE_SIDKICK_H_
#define SID_DEVICE_SIDKICK_H_

#include "sid_device.h"
#include "config.h"

class SidDeviceSidKick: public SidDevice {

    class SidKickConfig : public ConfigStore
    {
        SidDeviceSidKick *parent;
    public:
        SidKickConfig(SidDeviceSidKick *parent, const char *name, t_cfg_definition *defs);
        ~SidKickConfig() {  }

        void read(bool ignore) { }
        void write(void) { }
        void effectuate(void) { }
        void at_open_config(void);
        void at_close_config(void) { }

        //static volatile uint8_t *pre(ConfigItem *it);
        //static void post(ConfigItem *it);
        static int  S_cfg_pdsid_type(ConfigItem *it);
    };

    SidKickConfig *config;
public:
    SidDeviceSidKick(int socket, volatile uint8_t *base, int subtype);
    virtual ~SidDeviceSidKick();

    void SetSidType(int type);
    static int detect(volatile uint8_t *base);
};

#endif /* SID_DEVICE_SWINSID_H_ */
