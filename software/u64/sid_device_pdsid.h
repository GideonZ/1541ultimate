/*
 * sid_device_pdsid.h
 *
 *  Created on: Feb 17, 2026
 *      Author: gideon
 */

#ifndef SID_DEVICE_PDSID_H_
#define SID_DEVICE_PDSID_H_

#include "sid_device.h"
#include "config.h"

class SidDevicePdSid: public SidDevice {

    class PdSidConfig : public ConfigStore
    {
        SidDevicePdSid *parent;
    public:
        PdSidConfig(SidDevicePdSid *parent, const char *name, t_cfg_definition *defs);
        ~PdSidConfig() {  }

        void read(bool ignore) { }
        void write(void) { }
        void effectuate(void) { }
        void at_open_config(void);
        void at_close_config(void) { }

        static int  S_cfg_pdsid_type(ConfigItem *it);
    };

    PdSidConfig *config;
public:
    SidDevicePdSid(int socket, volatile uint8_t *base);
    virtual ~SidDevicePdSid();

    void SetSidType(int type);
    static bool detect(volatile uint8_t *base);
};

#endif /* SID_DEVICE_SWINSID_H_ */
