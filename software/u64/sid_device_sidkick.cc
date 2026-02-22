/*
 * sid_device_pdsid.cc
 *
 *  Created on: Feb 22, 2026
 *      Author: gideon
 */

#include "sid_device_sidkick.h"
#include "config.h"
#include <stdio.h>
#include "c64.h"
#include "dump_hex.h"

#define CFG_KICKSID_TYPE 0x01

#define SID_TYPE_6581       0
#define SID_TYPE_8580       1
#define SID_TYPE_8580_BOOST 2
#define SID_TYPE_NONE       3

// 0 = 6581, 1 = 8580, 2 = 8580+digiboost
#define CFG_SID1_TYPE           0

// digiboost strength:  0 .. 15
#define CFG_SID1_DIGIBOOST      1

// volume: 0 .. 14
#define CFG_SID1_VOLUME         3

// 0 = 6581, 1 = 8580, 2 = 8580+digiboost, 3 = none, 4 = Sound Expander, 5 = Sound Expander passive (= write-only)
#define CFG_SID2_TYPE           8

// digiboost strength:  0 .. 15
#define CFG_SID2_DIGIBOOST      9


static const char *chips[] = { "6581", "8580", "8580 DigiBoost", "None" };

static struct t_cfg_definition sidkick_config[] = {
    { CFG_KICKSID_TYPE, CFG_TYPE_ENUM, "Emulation Mode",             "%s", chips,      0,  3, 0 },
    { CFG_TYPE_END,     CFG_TYPE_END,  "",                           "",   NULL,       0,  0, 0 } };


SidDeviceSidKick::SidDeviceSidKick(int socket, volatile uint8_t *base, int subtype) : SidDevice(socket)
{
    char name[40];
    sprintf(name, "SidKick%s in Socket %d", subtype ? " Pico" : "", socket + 1);
    
    config = new SidDeviceSidKick :: SidKickConfig(this, name, sidkick_config);
    ConfigManager::getConfigManager()->add_custom_store(config);
}

void SidDeviceSidKick :: SetSidType(int type)
{
    volatile uint8_t *base = pre();
    pre_mode = C64_MODE;
    C64_MODE = MODE_ULTIMAX; // force I/O range on

    base[0x1f] = 0xFF; // enter config mode
    base[0x1e] = 0x00; // choose profile to update
//     base[0x1d] = 0xFC; // update and afterwards leave config mode

    // Set sid#2 to none for now
    if (type) { // 8580
        base[0x1d] = SID_TYPE_8580_BOOST;
    } else {
        base[0x1d] = SID_TYPE_6581;
    }
    base[0x1f] = 0xfd; // leave config mode
    wait_ms(5);

    // Restore C64 mode
    C64_MODE = pre_mode;
    post();
}

SidDeviceSidKick :: SidKickConfig :: SidKickConfig(SidDeviceSidKick *parent, const char *name, t_cfg_definition *defs)
    : ConfigStore(NULL, name, defs, NULL), parent(parent)
{
    set_hook_object(parent);
    // Make most settings 'hot' ;)
    set_change_hook(CFG_KICKSID_TYPE, S_cfg_pdsid_type);
}


SidDeviceSidKick::~SidDeviceSidKick()
{
    // TODO Auto-generated destructor stub
}

int SidDeviceSidKick::SidKickConfig:: S_cfg_pdsid_type(ConfigItem *it)
{
    SidDeviceSidKick *obj = (SidDeviceSidKick *)it->store->get_hook_object();

    volatile uint8_t *base = obj->pre();
    obj->pre_mode = C64_MODE;
    C64_MODE = MODE_ULTIMAX; // force I/O range on

    base[0x1f] = 0xFF; // enter config mode
    base[0x1e] = 0x00; // choose profile to update
    base[0x1d] = it->getValue() & 3;
    base[0x1f] = 0xfd; // leave config mode

    wait_ms(5);

    // Restore C64 mode
    C64_MODE = obj->pre_mode;
    obj->post();
    return 0;
}

void SidDeviceSidKick::SidKickConfig :: at_open_config(void)
{
    volatile uint8_t *base = parent->pre();
    ConfigItem *i = find_item(CFG_KICKSID_TYPE);

    uint8_t config[64];
    if (i) {
        base[0x1f] = 0xFF; // enter config mode
        base[0x1e] = 0x00; // choose profile to update
        for(int i=0; i<64; i++) {
            config[i] = base[0x1d];
        }
        base[0x1f] = 0xFD; // Leave config mode
        i->setValueQuietly(config[CFG_SID1_TYPE] & 3);

        printf("Current SidKick configuration:\n");
        dump_hex_relative(config, 64);
    }
    parent->post();
    wait_ms(5);
}

int SidDeviceSidKick::detect(volatile uint8_t *base)
{
    uint8_t SIDKickVersion[ 32 ] = {0};

    // enter config mode
    base[0x1f] = 0xff;

    // read version string
    for ( int i = 0; i < 32; i++ ) {
        base[0x1e] = 224 + i;
        SIDKickVersion[ i ] = base[0x1d];
    }

    // leave config mode
    base[0x1f] = 0xfd;

    dump_hex_relative(SIDKickVersion, 32);

    const uint8_t pico[] = { 0x53, 0x4b, 0x10, 0x09, 0x03, 0x0f };
    const uint8_t teen[] = { 0x53, 0x49, 0x44, 0x4b, 0x09, 0x03, 0x0b };

    if ( memcmp(SIDKickVersion, pico, 6) == 0 ) {
        printf( "DETECTED SIDKICK PICO.\n" );
        return 1;
    }
    if ( memcmp( SIDKickVersion, teen, 7) == 0) {
        // SIDKick (Teensy-based) detected
        printf( "DETECTED SIDKICK (TEENSY-BASED).\n" );
        return 2;
    }
    return 0;
}
