/*
 * sid_device_fpgasid.cc
 *
 *  Created on: Sep 14, 2019
 *      Author: gideon
 */

#include "sid_device_swinsid.h"
#include "config.h"
#include <stdio.h>
#include "c64.h"
#include "dump_hex.h"

#define CFG_SWINSID_TYPE            0x01
#define CFG_SWINSID_PITCH           0x02
#define CFG_SWINSID_AUDIOIN         0x03

static const char *chips[] = { "6581", "8580" };
static const char *extins[] = { "Disabled", "Enabled" };
static const char *pitches[] = { "PAL", "NTSC" };

static struct t_cfg_definition swin_sid_config[] = {
    { CFG_SWINSID_TYPE,            CFG_TYPE_ENUM, "Fundamental Mode",             "%s", chips,      0,  1, 0 },
    { CFG_SWINSID_PITCH,           CFG_TYPE_ENUM, "Pitch",                        "%s", pitches,    0,  1, 0 },
    { CFG_SWINSID_AUDIOIN,         CFG_TYPE_ENUM, "Audio In",                     "%s", extins,     0,  1, 0 },
    { CFG_TYPE_END,                CFG_TYPE_END,  "",                             "",   NULL,       0,  0, 0 } };


SidDeviceSwinSid::SidDeviceSwinSid(int socket, volatile uint8_t *base) : SidDevice(socket)
{
    config = new SidDeviceSwinSid :: SwinSidConfig(this);
    config->effectuate_settings();
}

void SidDeviceSwinSid :: SetSidType(int type)
{
    volatile uint8_t *base = pre();
    pre_mode = C64_MODE;
    C64_MODE = MODE_ULTIMAX; // force I/O range on

    type--; // receive 1 = 6581, 2 = 8580 => map to 0 and 1

    base[29] = 'S';
    C64_PEEK(2);
    base[30] = 'E';
    C64_PEEK(2);
    base[31] = (type) ? '8' : '6';
    C64_PEEK(2);

    C64_MODE = pre_mode;
    post();
}


SidDeviceSwinSid :: SwinSidConfig :: SwinSidConfig(SidDeviceSwinSid *parent)
{
    this->parent = parent;
    uint32_t id = 0x5357494E + parent->socket; // SWIN
    char name[40];
    sprintf(name, "SwinSID Ultimate in Socket %d", parent->socket + 1);

    register_store(id, name, swin_sid_config);

    // Make most settings 'hot' ;)
    cfg->set_change_hook(CFG_SWINSID_TYPE,    S_cfg_swinsid_type);
    cfg->set_change_hook(CFG_SWINSID_PITCH,   S_cfg_swinsid_pitch);
    cfg->set_change_hook(CFG_SWINSID_AUDIOIN, S_cfg_swinsid_audioin );
}

void SidDeviceSwinSid :: SwinSidConfig :: effectuate_settings()
{
    volatile uint8_t *base = parent->pre();
    S_effectuate(base, cfg);
    parent->post();
}

void SidDeviceSwinSid :: SwinSidConfig :: S_effectuate(volatile uint8_t *base, ConfigStore *cfg)
{
    // Mode
    base[29] = 'S';
    C64_PEEK(2);
    base[30] = 'E';
    C64_PEEK(2);
    base[31] = cfg->get_value(CFG_SWINSID_TYPE) ? '8' : '6';
    wait_ms(5);

    // Pitch
    base[31] = cfg->get_value(CFG_SWINSID_PITCH) ? 'S' : 'L';
    wait_ms(5);

    // Audio In
    base[31] = cfg->get_value(CFG_SWINSID_AUDIOIN) ? 'A' : 'D';
    wait_ms(5);

    // Exit config mode
    base[29] = 0;
}

SidDeviceSwinSid::~SidDeviceSwinSid()
{
    // TODO Auto-generated destructor stub
}

volatile uint8_t *SidDeviceSwinSid::SwinSidConfig::pre(ConfigItem *it)
{
    SidDeviceSwinSid::SwinSidConfig *obj = (SidDeviceSwinSid::SwinSidConfig *)it->store->get_first_object();
    volatile uint8_t *base = obj->parent->pre();
    return base;
}

void SidDeviceSwinSid::SwinSidConfig::post(ConfigItem *it)
{
    SidDeviceSwinSid::SwinSidConfig *obj = (SidDeviceSwinSid::SwinSidConfig *)it->store->get_first_object();
    volatile uint8_t *base = obj->parent->currentAddress;
    // Leave Config mode
    base[29] = 0;
    obj->parent->post();
}

int SidDeviceSwinSid::SwinSidConfig:: S_cfg_swinsid_type(ConfigItem *it)
{
    volatile uint8_t *base = pre(it);
    // Mode
    base[29] = 'S';
    C64_PEEK(2);
    base[30] = 'E';
    C64_PEEK(2);
    base[31] = it->getValue() ? '8' : '6';
    wait_ms(5);
    post(it);
    return 0;
}

int SidDeviceSwinSid::SwinSidConfig:: S_cfg_swinsid_pitch(ConfigItem *it)
{
    volatile uint8_t *base = pre(it);
    // Pitch
    base[29] = 'S';
    C64_PEEK(2);
    base[30] = 'E';
    C64_PEEK(2);
    base[31] = it->getValue() ? 'S' : 'L';
    wait_ms(5);
    post(it);
    return 0;
}

int SidDeviceSwinSid::SwinSidConfig:: S_cfg_swinsid_audioin(ConfigItem *it)
{
    volatile uint8_t *base = pre(it);
    // Audio In
    base[29] = 'S';
    C64_PEEK(2);
    base[30] = 'E';
    C64_PEEK(2);
    base[31] = it->getValue() ? 'A' : 'D';
    wait_ms(5);
    post(it);
    return 0;
}

