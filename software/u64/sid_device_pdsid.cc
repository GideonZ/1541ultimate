/*
 * sid_device_pdsid.cc
 *
 *  Created on: Feb 17, 2026
 *      Author: gideon
 */

#include "sid_device_pdsid.h"
#include "config.h"
#include <stdio.h>
#include "c64.h"

#define CFG_PDSID_TYPE            0x01

static const char *chips[] = { "6581", "8580" };

static struct t_cfg_definition pd_sid_config[] = {
    { CFG_PDSID_TYPE,  CFG_TYPE_ENUM, "Emulation Mode",             "%s", chips,      0,  1, 0 },
    { CFG_TYPE_END,    CFG_TYPE_END,  "",                           "",   NULL,       0,  0, 0 } };


SidDevicePdSid::SidDevicePdSid(int socket, volatile uint8_t *base) : SidDevice(socket)
{
    char name[40];
    sprintf(name, "PDSID in Socket %d", socket + 1);
    
    config = new SidDevicePdSid :: PdSidConfig(this, name, pd_sid_config);
    ConfigManager::getConfigManager()->add_custom_store(config);
}

void SidDevicePdSid :: SetSidType(int type)
{
    volatile uint8_t *base = pre();
    pre_mode = C64_MODE;
    C64_MODE = MODE_ULTIMAX; // force I/O range on

    base[29] = 'P';
    C64_PEEK(2);
    base[30] = 'D';
    C64_PEEK(2);
    base[31] = (type == 1) ? 0x00 : 0x01;
    C64_PEEK(2);

    // Exit config mode
    base[29] = 0;
    C64_PEEK(2);

    // Restore C64 mode
    C64_MODE = pre_mode;
    post();
}


SidDevicePdSid :: PdSidConfig :: PdSidConfig(SidDevicePdSid *parent, const char *name, t_cfg_definition *defs)
    : ConfigStore(NULL, name, defs, NULL), parent(parent)
{
    set_hook_object(parent);
    // Make most settings 'hot' ;)
    set_change_hook(CFG_PDSID_TYPE, S_cfg_pdsid_type);
}

SidDevicePdSid::~SidDevicePdSid()
{
    // TODO Auto-generated destructor stub
}

int SidDevicePdSid::PdSidConfig:: S_cfg_pdsid_type(ConfigItem *it)
{
    SidDevicePdSid *obj = (SidDevicePdSid *)it->store->get_hook_object();
    obj->SetSidType(it->getValue());
    return 0;
}

void SidDevicePdSid::PdSidConfig :: at_open_config(void)
{
    volatile uint8_t *base = parent->pre();
    ConfigItem *i = find_item(CFG_PDSID_TYPE);
    if (i) {
        base[29] = 'P';
        C64_PEEK(2);
        base[30] = 'D';
        C64_PEEK(2);
        i->setValueQuietly(base[31] & 1);
        // Exit Config mode
        base[29] = 0;
    }
    parent->post();
}

bool SidDevicePdSid::detect(volatile uint8_t *base)
{
    base[29] = 'P';
    C64_PEEK(2);
    base[30] = 'D';
    C64_PEEK(2);
    return (base[30] == 'S');
}
