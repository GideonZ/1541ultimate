/*
 * sid_device_fpgasid.cc
 *
 *  Created on: Sep 14, 2019
 *      Author: gideon
 */

#include "sid_device_fpgasid.h"
#include "config.h"
#include <stdio.h>

#define CFG_FPGASID_MODE            0x01
#define CFG_FPGASID_SID1_FILTER     0x10
#define CFG_FPGASID_SID1_CRUNCY     0x11
#define CFG_FPGASID_SID1_MIXEDWAVE  0x12
#define CFG_FPGASID_SID1_REGDELAY   0x13
#define CFG_FPGASID_SID1_READBACK   0x14
#define CFG_FPGASID_SID1_EXTIN      0x15
#define CFG_FPGASID_SID1_DIGIFIX    0x16
#define CFG_FPGASID_SID1_FILTERBIAS 0x17
#define CFG_FPGASID_SID1_OUTPUTMODE 0x18

#define CFG_FPGASID_SID2_FILTER     0x20
#define CFG_FPGASID_SID2_CRUNCY     0x21
#define CFG_FPGASID_SID2_MIXEDWAVE  0x22
#define CFG_FPGASID_SID2_REGDELAY   0x23
#define CFG_FPGASID_SID2_READBACK   0x24
#define CFG_FPGASID_SID2_EXTIN      0x25
#define CFG_FPGASID_SID2_DIGIFIX    0x26
#define CFG_FPGASID_SID2_FILTERBIAS 0x27
#define CFG_FPGASID_SID2_OUTPUTMODE 0x28

static const char *modes[] = { "Mono", "Stereo", "DualSocket" };
static const char *chips[] = { "6581", "8580" };
static const char *readbacks[] = { "Bitrot 6581", "Always Value", "Always $00", "Bitrot 8580" };
static const char *extins[] = { "Analog In", "Disabled", "Other SID", "DigiFix (8580)" };
static const char *outmodes[] = { "Two signals", "One signal" };


static struct t_cfg_definition fpga_sid_config[] = {
    { CFG_FPGASID_MODE,            CFG_TYPE_ENUM, "Fundamental Mode",             "%s", modes,      0,  2, 0 },
    { CFG_FPGASID_SID1_FILTER,     CFG_TYPE_ENUM, "SID1: Filter Mode",            "%s", chips,      0,  1, 0 },
    { CFG_FPGASID_SID1_CRUNCY,     CFG_TYPE_ENUM, "SID1: Crunchy DAC",            "%s", chips,      0,  1, 0 },
    { CFG_FPGASID_SID1_MIXEDWAVE,  CFG_TYPE_ENUM, "SID1: Mixed Waveforms",        "%s", chips,      0,  1, 0 },
    { CFG_FPGASID_SID1_REGDELAY,   CFG_TYPE_ENUM, "SID1: Register Delay",         "%s", chips,      0,  1, 0 },
    { CFG_FPGASID_SID1_READBACK,   CFG_TYPE_ENUM, "SID1: Register Read Back",     "%s", readbacks,  0,  3, 0 },
    { CFG_FPGASID_SID1_EXTIN,      CFG_TYPE_ENUM, "SID1: EXT IN Source",          "%s", extins,     0,  3, 0 },
    { CFG_FPGASID_SID1_DIGIFIX,    CFG_TYPE_VALUE,"SID1: DigiFix Value (8580)",   "%d", NULL,     -128,127,0 },
    { CFG_FPGASID_SID1_FILTERBIAS, CFG_TYPE_VALUE,"SID1: 6581 Filter Bias",       "%d", NULL,       0, 15, 6 },
    { CFG_FPGASID_SID1_OUTPUTMODE, CFG_TYPE_ENUM, "SID1: Output Mode",            "%s", outmodes,   0,  1, 0 },

    { CFG_FPGASID_SID2_FILTER,     CFG_TYPE_ENUM, "SID2: Filter Mode",            "%s", chips,      0,  1, 0 },
    { CFG_FPGASID_SID2_CRUNCY,     CFG_TYPE_ENUM, "SID2: Crunchy DAC",            "%s", chips,      0,  1, 0 },
    { CFG_FPGASID_SID2_MIXEDWAVE,  CFG_TYPE_ENUM, "SID2: Mixed Waveforms",        "%s", chips,      0,  1, 0 },
    { CFG_FPGASID_SID2_REGDELAY,   CFG_TYPE_ENUM, "SID2: Register Delay",         "%s", chips,      0,  1, 0 },
    { CFG_FPGASID_SID2_READBACK,   CFG_TYPE_ENUM, "SID2: Register Read Back",     "%s", readbacks,  0,  3, 0 },
    { CFG_FPGASID_SID2_EXTIN,      CFG_TYPE_ENUM, "SID2: EXT IN Source",          "%s", extins,     0,  3, 0 },
    { CFG_FPGASID_SID2_DIGIFIX,    CFG_TYPE_VALUE,"SID2: DigiFix Value (8580)",   "%d", NULL,     -128,127,0 },
    { CFG_FPGASID_SID2_FILTERBIAS, CFG_TYPE_VALUE,"SID2: 6581 Filter Bias",       "%d", NULL,       0, 15, 6 },
    { CFG_FPGASID_SID2_OUTPUTMODE, CFG_TYPE_ENUM, "SID2: Output Mode",            "%s", outmodes,   0,  1, 0 },

    { CFG_TYPE_END,                CFG_TYPE_END,  "",                             "",   NULL,       0,  0, 0 } };


SidDeviceFpgaSid::SidDeviceFpgaSid(int socket, volatile uint8_t *base) : SidDevice(socket, base), config(this)
{
    // we enter this constructor when the FPGASID is still in diag mode
    for(int i=0; i<8; i++) {
        unique[i] = base[i + 4];
    }
    config.effectuate_settings();
}

SidDeviceFpgaSid :: FpgaSidConfig :: FpgaSidConfig(SidDeviceFpgaSid *parent)
{
    this->parent = parent;
    uint32_t id = 0x46534944 + parent->socket; // FSID
    char name[40];
    sprintf(name, "FPGASID in Socket %d (..%b%b)", parent->socket + 1, parent->unique[6], parent->unique[7]);

    register_store(id, name, fpga_sid_config);
}

void SidDeviceFpgaSid :: FpgaSidConfig :: effectuate_settings()
{
    parent->pre();
    // Config SID 1 mode
    volatile uint8_t *base = parent->currentAddress;

    base[25] = 0x80;
    base[26] = 0x65;

    uint8_t b31 = 0;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID1_FILTER)) << 0;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID1_CRUNCY)) << 1;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID1_MIXEDWAVE)) << 2;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID1_REGDELAY)) << 3;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID1_READBACK)) << 4;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID1_EXTIN)) << 6;

    base[31] = b31;

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

    b30 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID1_OUTPUTMODE)) << 3;
    base[30] = b30;
    base[29] = (uint8_t)((int8_t)cfg->get_value(CFG_FPGASID_SID1_DIGIFIX));
    base[28] = (uint8_t)cfg->get_value(CFG_FPGASID_SID1_FILTERBIAS);

    // Now do the same for SID2
    // Config swap mode
    base[25] = 0x82;
    base[26] = 0x65;

    b31 = 0;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID2_FILTER)) << 0;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID2_CRUNCY)) << 1;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID2_MIXEDWAVE)) << 2;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID2_REGDELAY)) << 3;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID2_READBACK)) << 4;
    b31 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID2_EXTIN)) << 6;

    base[31] = b31;

    b30 = 0;
    b30 |= (uint8_t)(cfg->get_value(CFG_FPGASID_SID2_OUTPUTMODE)) << 3;
    base[30] = b30;
    base[29] = (uint8_t)((int8_t)cfg->get_value(CFG_FPGASID_SID2_DIGIFIX));
    base[28] = (uint8_t)cfg->get_value(CFG_FPGASID_SID2_FILTERBIAS);

    // Leave Config mode
    base[25] = 0;
    base[26] = 0;
    parent->post();
}

SidDeviceFpgaSid::~SidDeviceFpgaSid()
{
    // TODO Auto-generated destructor stub
}
