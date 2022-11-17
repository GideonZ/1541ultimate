/*
 * sid_device_armsid.cc
 *
 *  Created on: Sep 14, 2019
 *      Author: gideon
 */

#include "sid_device_armsid.h"
#include "config.h"
#include <stdio.h>
#include "c64.h"
#include "dump_hex.h"

#define CFG_ARMSID_TYPE            0x01
#define CFG_ARMSID_PITCH           0x02
#define CFG_ARMSID_AUDIOIN         0x03
#define CFG_ARMSID_6581FILT_STR    0x04
#define CFG_ARMSID_6581FILT_BASE   0x05
#define CFG_ARMSID_8580FILT_MAX    0x06
#define CFG_ARMSID_8580FILT_BASE   0x07
#define CFG_ARMSID_VERSION         0x08

static const char *chips[] = { "6581", "8580" };
static const char *filt6581[] = {
        "Follin-",
        "Follin",
        "Follin+",
        "Galway-",
        "Galway",
        "Galway+",
        "Average-",
        "Average",
        "Average+",
        "Strong-",
        "Strong",
        "Strong+",
        "Extreme-",
        "Extreme",
        "Extreme+",
};

static const char *filtlow6[] = {
        "150",
        "215",
        "310",
};

static const char *filthi8[] = {
        "12 kHz",
        "10 kHz",
        "8 kHz",
        "6 kHz",
        "5 kHz",
        "4 kHz",
        "3 kHz",
};

static const char *filtlow8[] = {
        "  30",
        " ~45",
        " ~70",
        " 100",
        "~150",
        "~220",
        " 330",
};


static struct t_cfg_definition arm_sid_config[] = {
    { CFG_ARMSID_VERSION,         CFG_TYPE_INFO, "Firmware Version",             "%s", NULL,       0, 16, (int)"" },
    { CFG_ARMSID_TYPE,            CFG_TYPE_ENUM, "Fundamental Mode",             "%s", chips,      0,  1, 0 },
    { CFG_ARMSID_6581FILT_STR,    CFG_TYPE_ENUM, "6581 Filter Strength",         "%s", filt6581,   0, 14, 7 },
    { CFG_ARMSID_6581FILT_BASE,   CFG_TYPE_ENUM, "6581 Lowest Filt Freq",        "%s", filtlow6,   0,  2, 1 },
    { CFG_ARMSID_8580FILT_MAX,    CFG_TYPE_ENUM, "8580 Highest Filt Freq",       "%s", filthi8,    0,  6, 3 },
    { CFG_ARMSID_8580FILT_BASE,   CFG_TYPE_ENUM, "8580 Lowest Filt Freq",        "%s", filtlow8,   0,  6, 3 },
    { CFG_TYPE_END,               CFG_TYPE_END,  "",                             "",   NULL,       0,  0, 0 } };


SidDeviceArmSid::SidDeviceArmSid(int socket, volatile uint8_t *base) : SidDevice(socket)
{
    char name[40]; // it is OK to be on the stack, because ConfigStore uses an mstring, which allocates memory for the string.
    sprintf(name, "ARMSID in Socket %d", socket + 1);

    config = new SidDeviceArmSid :: ArmSidConfig(this, base, name);
}


SidDeviceArmSid :: ArmSidConfig :: ArmSidConfig(SidDeviceArmSid *parent, volatile uint8_t *base, const char *name) : ConfigStore(NULL, name, arm_sid_config, NULL)
{
    this->parent = parent;
    ConfigManager::getConfigManager()->add_custom_store(this);
    readParams(base);

    // Make most settings 'hot' ;)
    set_change_hook(CFG_ARMSID_TYPE,    S_cfg_armsid_type);
    set_change_hook(CFG_ARMSID_6581FILT_STR,  S_cfg_armsid_filt);
    set_change_hook(CFG_ARMSID_6581FILT_BASE, S_cfg_armsid_filt);
    set_change_hook(CFG_ARMSID_8580FILT_MAX,  S_cfg_armsid_filt);
    set_change_hook(CFG_ARMSID_8580FILT_BASE, S_cfg_armsid_filt);
}

void SidDeviceArmSid :: ArmSidConfig :: readParams(volatile uint8_t *base)
{
    base[29] = 'S';
    C64_PEEK(2);
    base[30] = 'I';
    C64_PEEK(2);
    base[31] = 'D';

    wait_ms(2);

    base[31] = 'V';
    C64_PEEK(2);
    base[30] = 'I';
    C64_PEEK(2);
    wait_ms(2);

    uint8_t vh = base[27];
    C64_PEEK(2);
    uint8_t vl = base[28];
    C64_PEEK(2);

    base[31] = 'F';
    C64_PEEK(2);
    base[30] = 'I';
    C64_PEEK(2);
    wait_ms(2);

    uint8_t chip = base[27];
    C64_PEEK(2);

    base[31] = 'U';
    C64_PEEK(2);
    base[30] = 'I';
    C64_PEEK(2);

    wait_ms(10);

    uint8_t p = base[27];
    C64_PEEK(2);
    uint8_t q = base[28];
    int v = p * 256 + q;

    base[31] = 'H';
    C64_PEEK(2);
    base[30] = 'I';
    C64_PEEK(2);

    wait_ms(10);

    p = base[27];
    C64_PEEK(2);
    q = base[28];

    // printf("Current ARMSID settings: Chip: %c5xx FW: %d.%d Filt: %b %b  (Voltage = %d mV)\n", chip, vh, vl, p, q, v);

    find_item(CFG_ARMSID_6581FILT_STR )->setValueQuietly(((p >> 4) + 7) & 15);
    find_item(CFG_ARMSID_6581FILT_BASE)->setValueQuietly(((p & 15) + 1) & 15);
    find_item(CFG_ARMSID_8580FILT_MAX )->setValueQuietly(((q >> 4) + 3) & 15);
    find_item(CFG_ARMSID_8580FILT_BASE)->setValueQuietly(((q & 15) + 3) & 15);
    find_item(CFG_ARMSID_TYPE)->setValueQuietly((chip == '6') ? 0 : 1);

    ConfigItem *it = find_item(CFG_ARMSID_VERSION);
    it->setEnabled(false);
    char *version = (char *)it->getString(); // dirty!
    sprintf(version, "%d.%d", vh, vl);

    check_bounds();

    // Exit Config mode
    base[29] = 0;
}

void SidDeviceArmSid :: ArmSidConfig :: at_open_config(void)
{
    volatile uint8_t *base = parent->pre();
    readParams(base);
    parent->post();
}

void SidDeviceArmSid :: ArmSidConfig :: effectuate()
{
    volatile uint8_t *base = parent->pre();
    S_effectuate(base, this);
    parent->post();
}

void SidDeviceArmSid :: ArmSidConfig :: write()
{
    volatile uint8_t *base = parent->pre();
    S_config_mode(base);
    S_save_flash(base);
    set_need_flash_write(false);
    parent->post();
}

void SidDeviceArmSid :: ArmSidConfig :: S_effectuate(volatile uint8_t *base, ConfigStore *cfg)
{
    S_config_mode(base);
    S_set_mode(base, cfg->get_value(CFG_ARMSID_TYPE) ? '8' : '6');
    S_set_filt(base, cfg);
    S_save_ram(base);
    base[29] = 0; // Exit Config Mode
}

SidDeviceArmSid::~SidDeviceArmSid()
{
    // TODO Auto-generated destructor stub
}

volatile uint8_t *SidDeviceArmSid::ArmSidConfig::pre(ConfigItem *it)
{
    SidDeviceArmSid::ArmSidConfig *obj = (SidDeviceArmSid::ArmSidConfig *)it->store;
    volatile uint8_t *base = obj->parent->pre();
    return base;
}

void SidDeviceArmSid::ArmSidConfig::post(ConfigItem *it)
{
    SidDeviceArmSid::ArmSidConfig *obj = (SidDeviceArmSid::ArmSidConfig *)it->store;
    volatile uint8_t *base = obj->parent->currentAddress;
    // Leave Config mode
    base[29] = 0;
    obj->parent->post();
}

void SidDeviceArmSid :: ArmSidConfig :: S_config_mode(volatile uint8_t *base)
{
    base[29] = 'S';
    C64_PEEK(2);
    base[30] = 'I';
    C64_PEEK(2);
    base[31] = 'D';
    wait_ms(10);
}

void SidDeviceArmSid :: ArmSidConfig :: S_set_mode(volatile uint8_t *base, uint8_t mode)
{
    // Mode
    base[29] = 'S';
    C64_PEEK(2);
    base[30] = 'E';
    C64_PEEK(2);
    base[31] = mode;
    wait_ms(10);
}

void SidDeviceArmSid :: ArmSidConfig :: S_set_filt(volatile uint8_t *base, ConfigStore *store)
{
    int a = store->find_item(CFG_ARMSID_6581FILT_STR )->getValue() - 7;
    int b = store->find_item(CFG_ARMSID_6581FILT_BASE)->getValue() - 1;
    int c = store->find_item(CFG_ARMSID_8580FILT_MAX )->getValue() - 3;
    int d = store->find_item(CFG_ARMSID_8580FILT_BASE)->getValue() - 3;

    base[31] = ((uint8_t)(a & 15)) | 0x80;
    C64_PEEK(2);
    base[30] = 'E';
    wait_ms(10);

    base[31] = ((uint8_t)(b & 15)) | 0x90;
    C64_PEEK(2);
    base[30] = 'E';
    wait_ms(10);

    base[31] = ((uint8_t)(c & 15)) | 0xA0;
    C64_PEEK(2);
    base[30] = 'E';
    wait_ms(10);

    base[31] = ((uint8_t)(d & 15)) | 0xB0;
    C64_PEEK(2);
    base[30] = 'E';
    wait_ms(10);
}

void SidDeviceArmSid :: ArmSidConfig :: S_save_ram(volatile uint8_t *base)
{
    base[31] = 0xC0;
    C64_PEEK(2);
    base[30] = 'E';
    wait_ms(10);
}

void SidDeviceArmSid :: ArmSidConfig :: S_save_flash(volatile uint8_t *base)
{
    base[31] = 0xCF;
    C64_PEEK(2);
    base[30] = 'E';
    wait_ms(10);
}

// * HOT Change functions *
int SidDeviceArmSid::ArmSidConfig:: S_cfg_armsid_type(ConfigItem *it)
{
    volatile uint8_t *base = pre(it);
    S_config_mode(base);
    S_set_mode(base, it->getValue() ? '8' : '6');
    post(it);
    return 0;
}

int SidDeviceArmSid::ArmSidConfig:: S_cfg_armsid_filt(ConfigItem *it)
{
    volatile uint8_t *base = pre(it);

    S_config_mode(base);
    S_set_filt(base, it->store);
    S_save_ram(base);

    post(it);
    return 0;
}

void SidDeviceArmSid :: SetSidType(int type)
{
    volatile uint8_t *base = pre();
    pre_mode = C64_MODE;
    C64_MODE = MODE_ULTIMAX; // force I/O range on

    SidDeviceArmSid :: ArmSidConfig :: S_config_mode(base);

    type--; // receive 1 = 6581, 2 = 8580 => map to 0 and 1

    SidDeviceArmSid :: ArmSidConfig :: S_set_mode(base, (type) ? '8' : '6');

    C64_MODE = pre_mode;
    post();
}
