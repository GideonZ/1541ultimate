/*
 * sid_device_fpgasid.cc
 *
 *  Created on: Sep 14, 2019
 *      Author: gideon
 */

#include "sid_device_fpgasid.h"
#include "config.h"
#include <stdio.h>
#include "c64.h"
#include "dump_hex.h"

#define CFG_FPGASID_MODE            0x01
#define CFG_FPGASID_LEDS            0x02
#define CFG_FPGASID_OUTPUTMODE      0x18

#define CFG_FPGASID_SID1_QUICK      0x1F
#define CFG_FPGASID_SID1_FILTER     0x10
#define CFG_FPGASID_SID1_CRUNCY     0x11
#define CFG_FPGASID_SID1_MIXEDWAVE  0x12
#define CFG_FPGASID_SID1_REGDELAY   0x13
#define CFG_FPGASID_SID1_READBACK   0x14
#define CFG_FPGASID_SID1_EXTIN      0x15
#define CFG_FPGASID_SID1_DIGIFIX    0x16
#define CFG_FPGASID_SID1_FILTERBIAS 0x17
#define CFG_FPGASID_SID1_VOICEMUTE  0x19

#define CFG_FPGASID_SID2_QUICK      0x2F
#define CFG_FPGASID_SID2_FILTER     0x20
#define CFG_FPGASID_SID2_CRUNCY     0x21
#define CFG_FPGASID_SID2_MIXEDWAVE  0x22
#define CFG_FPGASID_SID2_REGDELAY   0x23
#define CFG_FPGASID_SID2_READBACK   0x24
#define CFG_FPGASID_SID2_EXTIN      0x25
#define CFG_FPGASID_SID2_DIGIFIX    0x26
#define CFG_FPGASID_SID2_FILTERBIAS 0x27
#define CFG_FPGASID_SID2_VOICEMUTE  0x29
#define CFG_FPGASID_SEPARATOR       0xFE

static const char *modes[] = { "Mono", "Stereo", "DualSocket" };
static const char *chips[] = { "6581", "8580" };
static const char *readbacks[] = { "Bitrot 6581", "Always Value", "Always $00", "Bitrot 8580" };
static const char *extins[] = { "Analog In", "Disabled", "Other SID", "DigiFix (8580)" };
static const char *outmodes[] = { "Two signals", "One signal" };
static const char *ledmodes[] = { "On", "Only voice", "Only heart", "Off" };
// static const char *onoff[] = { "On", "Off" };
static const char *voices[] = { "All", ". 2 3", "1 . 3", ". . 3", "1 2 .", ". 2 .", "1 . .", "None" };

static struct t_cfg_definition fpga_sid_config[] = {
    { CFG_FPGASID_MODE,            CFG_TYPE_ENUM, "Fundamental Mode",             "%s", modes,      0,  2, 0 },
    { CFG_FPGASID_OUTPUTMODE,      CFG_TYPE_ENUM, "Output Mode",                  "%s", outmodes,   0,  1, 0 },
    { CFG_FPGASID_LEDS,            CFG_TYPE_ENUM, "LEDs",                         "%s", ledmodes,   0,  3, 0 },

    { CFG_FPGASID_SEPARATOR,       CFG_TYPE_SEP,  "",                             "",   NULL,       0,  0, 0 },
    { CFG_FPGASID_SID1_QUICK,      CFG_TYPE_ENUM, "SID1: Quick type select",      "%s", chips,      0,  1, 0 },
    { CFG_FPGASID_SID1_FILTER,     CFG_TYPE_ENUM, "SID1: Filter Mode",            "%s", chips,      0,  1, 0 },
    { CFG_FPGASID_SID1_CRUNCY,     CFG_TYPE_ENUM, "SID1: Crunchy DAC",            "%s", chips,      0,  1, 0 },
    { CFG_FPGASID_SID1_MIXEDWAVE,  CFG_TYPE_ENUM, "SID1: Mixed Waveforms",        "%s", chips,      0,  1, 0 },
    { CFG_FPGASID_SID1_REGDELAY,   CFG_TYPE_ENUM, "SID1: Register Delay",         "%s", chips,      0,  1, 0 },
    { CFG_FPGASID_SID1_READBACK,   CFG_TYPE_ENUM, "SID1: Register Read Back",     "%s", readbacks,  0,  3, 0 },
    { CFG_FPGASID_SID1_EXTIN,      CFG_TYPE_ENUM, "SID1: EXT IN Source",          "%s", extins,     0,  3, 0 },
    { CFG_FPGASID_SID1_DIGIFIX,    CFG_TYPE_VALUE,"SID1: DigiFix Value (8580)",   "%d", NULL,     -128,127,0 },
    { CFG_FPGASID_SID1_FILTERBIAS, CFG_TYPE_VALUE,"SID1: 6581 Filter Bias",       "%d", NULL,       0, 15, 6 },
    { CFG_FPGASID_SID1_VOICEMUTE,  CFG_TYPE_ENUM, "SID1: Enabled Voices",         "%s", voices,     0,  7, 0 },
    { CFG_FPGASID_SEPARATOR,       CFG_TYPE_SEP,  "",                             "",   NULL,       0,  0, 0 },
    { CFG_FPGASID_SID2_QUICK,      CFG_TYPE_ENUM, "SID2: Quick type select",      "%s", chips,      0,  1, 0 },
    { CFG_FPGASID_SID2_FILTER,     CFG_TYPE_ENUM, "SID2: Filter Mode",            "%s", chips,      0,  1, 0 },
    { CFG_FPGASID_SID2_CRUNCY,     CFG_TYPE_ENUM, "SID2: Crunchy DAC",            "%s", chips,      0,  1, 0 },
    { CFG_FPGASID_SID2_MIXEDWAVE,  CFG_TYPE_ENUM, "SID2: Mixed Waveforms",        "%s", chips,      0,  1, 0 },
    { CFG_FPGASID_SID2_REGDELAY,   CFG_TYPE_ENUM, "SID2: Register Delay",         "%s", chips,      0,  1, 0 },
    { CFG_FPGASID_SID2_READBACK,   CFG_TYPE_ENUM, "SID2: Register Read Back",     "%s", readbacks,  0,  3, 0 },
    { CFG_FPGASID_SID2_EXTIN,      CFG_TYPE_ENUM, "SID2: EXT IN Source",          "%s", extins,     0,  3, 0 },
    { CFG_FPGASID_SID2_DIGIFIX,    CFG_TYPE_VALUE,"SID2: DigiFix Value (8580)",   "%d", NULL,     -128,127,0 },
    { CFG_FPGASID_SID2_FILTERBIAS, CFG_TYPE_VALUE,"SID2: 6581 Filter Bias",       "%d", NULL,       0, 15, 6 },
    { CFG_FPGASID_SID2_VOICEMUTE,  CFG_TYPE_ENUM, "SID2: Enabled Voices",         "%s", voices,     0,  7, 0 },

    { CFG_TYPE_END,                CFG_TYPE_END,  "",                             "",   NULL,       0,  0, 0 } };


SidDeviceFpgaSid::SidDeviceFpgaSid(int socket, volatile uint8_t *base) : SidDevice(socket)
{
    // we enter this constructor when the FPGASID is still in diag mode
    for(int i=0; i<8; i++) {
        unique[i] = base[i + 4];
    }
    config = new SidDeviceFpgaSid :: FpgaSidConfig(this);
    config->effectuate_settings();
}

void SidDeviceFpgaSid :: SetSidType(int type)
{
    volatile uint8_t *base = pre();
    pre_mode = C64_MODE;
    C64_MODE = MODE_ULTIMAX; // force I/O range on

    type--; // receive 1 = 6581, 2 = 8580 => map to 0 and 1
    base[25] = 0x80;
    base[26] = 0x65;

    base[31] = (type) ? 0xFF : 0x40; // set bit bit 7 and 5..0 to '1' when 8580. ExtIn is DigiFix for 8580 and disabled for 6581

    base[25] = 0x00;
    base[26] = 0x00;

    C64_MODE = pre_mode;
    post();
}


SidDeviceFpgaSid :: FpgaSidConfig :: FpgaSidConfig(SidDeviceFpgaSid *parent)
{
    this->parent = parent;
    uint32_t id = 0x46534944 + parent->socket; // FSID
    char name[40];
    sprintf(name, "FPGASID in Socket %d (..%b%b)", parent->socket + 1, parent->unique[6], parent->unique[7]);

    register_store(id, name, fpga_sid_config);

    // Make most settings 'hot' ;)
    cfg->set_change_hook(CFG_FPGASID_MODE           , S_cfg_fpgasid_mode);
    cfg->set_change_hook(CFG_FPGASID_OUTPUTMODE,      S_cfg_fpgasid_outputmode);
    cfg->set_change_hook(CFG_FPGASID_LEDS,            S_cfg_fpgasid_leds);
    cfg->set_change_hook(CFG_FPGASID_SID1_QUICK     , S_cfg_fpgasid_sid1_quick );
    cfg->set_change_hook(CFG_FPGASID_SID1_FILTER    , S_cfg_fpgasid_sid1_byte31 );
    cfg->set_change_hook(CFG_FPGASID_SID1_CRUNCY    , S_cfg_fpgasid_sid1_byte31 );
    cfg->set_change_hook(CFG_FPGASID_SID1_MIXEDWAVE , S_cfg_fpgasid_sid1_byte31 );
    cfg->set_change_hook(CFG_FPGASID_SID1_REGDELAY  , S_cfg_fpgasid_sid1_byte31 );
    cfg->set_change_hook(CFG_FPGASID_SID1_READBACK  , S_cfg_fpgasid_sid1_byte31 );
    cfg->set_change_hook(CFG_FPGASID_SID1_EXTIN     , S_cfg_fpgasid_sid1_byte31 );
    cfg->set_change_hook(CFG_FPGASID_SID1_DIGIFIX   , S_cfg_fpgasid_sid1_digifix   );
    cfg->set_change_hook(CFG_FPGASID_SID1_FILTERBIAS, S_cfg_fpgasid_sid1_filterbias);
    cfg->set_change_hook(CFG_FPGASID_SID1_VOICEMUTE , S_cfg_fpgasid_outputmode );
    cfg->set_change_hook(CFG_FPGASID_SID2_QUICK     , S_cfg_fpgasid_sid2_quick );
    cfg->set_change_hook(CFG_FPGASID_SID2_FILTER    , S_cfg_fpgasid_sid2_byte31 );
    cfg->set_change_hook(CFG_FPGASID_SID2_CRUNCY    , S_cfg_fpgasid_sid2_byte31 );
    cfg->set_change_hook(CFG_FPGASID_SID2_MIXEDWAVE , S_cfg_fpgasid_sid2_byte31 );
    cfg->set_change_hook(CFG_FPGASID_SID2_REGDELAY  , S_cfg_fpgasid_sid2_byte31 );
    cfg->set_change_hook(CFG_FPGASID_SID2_READBACK  , S_cfg_fpgasid_sid2_byte31 );
    cfg->set_change_hook(CFG_FPGASID_SID2_EXTIN     , S_cfg_fpgasid_sid2_byte31 );
    cfg->set_change_hook(CFG_FPGASID_SID2_DIGIFIX   , S_cfg_fpgasid_sid2_digifix   );
    cfg->set_change_hook(CFG_FPGASID_SID2_FILTERBIAS, S_cfg_fpgasid_sid2_filterbias);
    cfg->set_change_hook(CFG_FPGASID_SID2_VOICEMUTE , S_cfg_fpgasid_sid2_byte30 );

    ConfigItem *it = cfg->find_item(CFG_FPGASID_MODE);
    setItemsEnable(it);
}

uint8_t SidDeviceFpgaSid :: FpgaSidConfig :: getByte31Sid1(ConfigStore *cfg)
{
    uint8_t b31 = 0;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID1_FILTER)) << 0;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID1_CRUNCY)) << 1;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID1_MIXEDWAVE)) << 2;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID1_REGDELAY)) << 3;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID1_READBACK)) << 4;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID1_EXTIN)) << 6;
    return b31;
}

uint8_t SidDeviceFpgaSid :: FpgaSidConfig :: getByte31Sid2(ConfigStore *cfg)
{
    uint8_t b31 = 0;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID2_FILTER)) << 0;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID2_CRUNCY)) << 1;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID2_MIXEDWAVE)) << 2;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID2_REGDELAY)) << 3;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID2_READBACK)) << 4;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID2_EXTIN)) << 6;
    return b31;
}

uint8_t SidDeviceFpgaSid :: FpgaSidConfig :: getByte30Sid1(ConfigStore *cfg)
{
    uint8_t b30 = 0;
    switch (cfg->get_value(CFG_FPGASID_MODE)) {
    case 0: // Mono:
        b30 = 0x00;
        break;
    case 1: // Stereo
        b30 = 0x01; // Always use A5
        break;
    case 2: // Dual Socket mode
        b30 = 0x04; // Use DE00 (IO1)
        break;
    default:
        break;
    }

    b30 |= (uint8_t)(cfg->get_value(CFG_FPGASID_OUTPUTMODE)) << 3;

    if (cfg->get_value(CFG_FPGASID_LEDS) & 1) {
        b30 |= 0x80;
    }

    b30 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID1_VOICEMUTE) << 4);

    return b30;
}

uint8_t SidDeviceFpgaSid :: FpgaSidConfig :: getByte30Sid2(ConfigStore *cfg)
{
    uint8_t b30 = 0;

    if (cfg->get_value(CFG_FPGASID_LEDS) & 2) {
        b30 |= 0x80;
    }

    b30 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID2_VOICEMUTE) << 4);

    return b30;
}

void SidDeviceFpgaSid :: FpgaSidConfig :: effectuate_settings()
{
    // Config SID 1 mode
    volatile uint8_t *base = parent->pre();
    S_effectuate(base, cfg);
    parent->post();
}

void SidDeviceFpgaSid :: FpgaSidConfig :: S_effectuate(volatile uint8_t *base, ConfigStore *cfg)
{
    base[25] = 0x80;
    base[26] = 0x65;

    base[30] = getByte30Sid1(cfg);
    base[31] = getByte31Sid1(cfg);
    base[28] = (uint8_t)cfg->get_value(CFG_FPGASID_SID1_FILTERBIAS);
    base[29] = (uint8_t)((int8_t)cfg->get_value(CFG_FPGASID_SID1_DIGIFIX));

    // Now do the same for SID2
    // Config swap mode
    base[25] = 0x82;
    base[26] = 0x65;

    base[30] = getByte30Sid2(cfg);
    base[31] = getByte31Sid2(cfg);
    base[28] = (uint8_t)cfg->get_value(CFG_FPGASID_SID2_FILTERBIAS);
    base[29] = (uint8_t)((int8_t)cfg->get_value(CFG_FPGASID_SID2_DIGIFIX));

    // Leave Config mode
    base[25] = 0;
    base[26] = 0;
}

SidDeviceFpgaSid::~SidDeviceFpgaSid()
{
    // TODO Auto-generated destructor stub
}

volatile uint8_t *SidDeviceFpgaSid::FpgaSidConfig::pre(ConfigItem *it, int sid)
{
    SidDeviceFpgaSid::FpgaSidConfig *obj = (SidDeviceFpgaSid::FpgaSidConfig *)it->store->get_first_object();
    volatile uint8_t *base = obj->parent->pre();
    base[25] = (sid) ? 0x82 : 0x80;
    base[26] = 0x65;
    return base;
}

void SidDeviceFpgaSid::FpgaSidConfig::post(ConfigItem *it)
{
    SidDeviceFpgaSid::FpgaSidConfig *obj = (SidDeviceFpgaSid::FpgaSidConfig *)it->store->get_first_object();
    volatile uint8_t *base = obj->parent->currentAddress;
    base[25] = 0x00;
    base[26] = 0x00;
    obj->parent->post();
}

void SidDeviceFpgaSid::FpgaSidConfig::setItemsEnable(ConfigItem *it)
{
    ConfigStore *store = it->store;
    switch(it->getValue()) {
    case 0: // mono
        store->disable(CFG_FPGASID_SID2_QUICK);
        store->disable(CFG_FPGASID_SID2_FILTER);
        store->disable(CFG_FPGASID_SID2_CRUNCY    );
        store->disable(CFG_FPGASID_SID2_MIXEDWAVE );
        store->disable(CFG_FPGASID_SID2_REGDELAY  );
        store->disable(CFG_FPGASID_SID2_READBACK  );
        store->disable(CFG_FPGASID_SID2_EXTIN     );
        store->disable(CFG_FPGASID_SID2_DIGIFIX   );
        store->disable(CFG_FPGASID_SID2_FILTERBIAS);
        break;
    default:
        store->enable(CFG_FPGASID_SID2_QUICK);
        store->enable(CFG_FPGASID_SID2_FILTER);
        store->enable(CFG_FPGASID_SID2_CRUNCY    );
        store->enable(CFG_FPGASID_SID2_MIXEDWAVE );
        store->enable(CFG_FPGASID_SID2_REGDELAY  );
        store->enable(CFG_FPGASID_SID2_READBACK  );
        store->enable(CFG_FPGASID_SID2_EXTIN     );
        store->enable(CFG_FPGASID_SID2_DIGIFIX   );
        store->enable(CFG_FPGASID_SID2_FILTERBIAS);
        break;
    }
}

int SidDeviceFpgaSid::FpgaSidConfig::S_cfg_fpgasid_mode(ConfigItem *it)
{
    //setItemsEnable(it);
    volatile uint8_t *base = pre(it, 0);
    S_effectuate(base, it->store);
    post(it);
    return 1;
}

int SidDeviceFpgaSid::FpgaSidConfig::S_cfg_fpgasid_sid1_byte31(ConfigItem* it)
{
    volatile uint8_t *base = pre(it, 0);  // get access to socket and put SID1 in config mode
    base[31] = getByte31Sid1(it->store);
    post(it); // restore
    return 0;
}

int SidDeviceFpgaSid::FpgaSidConfig::S_cfg_fpgasid_sid1_digifix(ConfigItem* it)
{
    volatile uint8_t *base = pre(it, 0);  // get access to socket and put SID1 in config mode
    base[29] = (uint8_t)((int8_t)it->getValue());
    post(it); // restore
    return 0;
}

int SidDeviceFpgaSid::FpgaSidConfig::S_cfg_fpgasid_sid1_filterbias(ConfigItem* it)
{
    volatile uint8_t *base = pre(it, 0);  // get access to socket and put SID1 in config mode
    base[28] = (uint8_t)((int8_t)it->getValue());
    post(it); // restore
    return 0;
}

int SidDeviceFpgaSid::FpgaSidConfig::S_cfg_fpgasid_leds(ConfigItem* it)
{
    volatile uint8_t *base = pre(it, 0);  // get access to socket and put SID1 in config mode
    base[30] = getByte30Sid1(it->store);
    base[25] = 0x82;
    base[30] = getByte30Sid2(it->store);
    post(it); // restore
    return 0;
}

int SidDeviceFpgaSid::FpgaSidConfig::S_cfg_fpgasid_outputmode(ConfigItem* it)
{
    volatile uint8_t *base = pre(it, 0);  // get access to socket and put SID1 in config mode
    base[30] = getByte30Sid1(it->store);
    post(it); // restore
    return 0;
}

int SidDeviceFpgaSid::FpgaSidConfig::S_cfg_fpgasid_sid2_byte30(ConfigItem* it)
{
    volatile uint8_t *base = pre(it, 1);  // get access to socket and put SID2 in config mode
    base[30] = getByte30Sid2(it->store);
    post(it); // restore
    return 0;
}

int SidDeviceFpgaSid::FpgaSidConfig::S_cfg_fpgasid_sid2_byte31(ConfigItem* it)
{
    volatile uint8_t *base = pre(it, 1);  // get access to socket and put SID2 in config mode
    base[31] = getByte31Sid2(it->store);
    post(it); // restore
    return 0;
}

int SidDeviceFpgaSid::FpgaSidConfig::S_cfg_fpgasid_sid2_digifix(ConfigItem* it)
{
    volatile uint8_t *base = pre(it, 1);  // get access to socket and put SID2 in config mode
    base[29] = (uint8_t)((int8_t)it->getValue());
    post(it); // restore
    return 0;
}

int SidDeviceFpgaSid::FpgaSidConfig::S_cfg_fpgasid_sid2_filterbias(ConfigItem* it)
{
    volatile uint8_t *base = pre(it, 1);  // get access to socket and put SID2 in config mode
    base[28] = (uint8_t)((int8_t)it->getValue());
    post(it); // restore
    return 0;
}

int SidDeviceFpgaSid::FpgaSidConfig::S_cfg_fpgasid_sid1_quick(ConfigItem* it)
{
    volatile uint8_t *base = pre(it, 0);  // get access to socket and put SID1 in config mode
    int value = it->getValue();
    it->store->find_item(CFG_FPGASID_SID1_FILTER)->setValueQuietly(value);
    it->store->find_item(CFG_FPGASID_SID1_CRUNCY)->setValueQuietly(value);
    it->store->find_item(CFG_FPGASID_SID1_MIXEDWAVE)->setValueQuietly(value);
    it->store->find_item(CFG_FPGASID_SID1_REGDELAY)->setValueQuietly(value);
    it->store->find_item(CFG_FPGASID_SID1_READBACK)->setValueQuietly(value * 3);
    base[31] = getByte31Sid1(it->store);
    post(it); // restore
    return 1;
}

int SidDeviceFpgaSid::FpgaSidConfig::S_cfg_fpgasid_sid2_quick(ConfigItem* it)
{
    volatile uint8_t *base = pre(it, 1);  // get access to socket and put SID1 in config mode
    int value = it->getValue();
    it->store->find_item(CFG_FPGASID_SID2_FILTER)->setValueQuietly(value);
    it->store->find_item(CFG_FPGASID_SID2_CRUNCY)->setValueQuietly(value);
    it->store->find_item(CFG_FPGASID_SID2_MIXEDWAVE)->setValueQuietly(value);
    it->store->find_item(CFG_FPGASID_SID2_REGDELAY)->setValueQuietly(value);
    it->store->find_item(CFG_FPGASID_SID2_READBACK)->setValueQuietly(value * 3);
    base[31] = getByte31Sid2(it->store);
    post(it); // restore
    return 1;
}
