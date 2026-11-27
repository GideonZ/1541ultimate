// The part of the configuration that modem.cc uses.
#ifndef HOST_MODEM_CONFIG_H
#define HOST_MODEM_CONFIG_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SORT_ORDER_CFG_MODEM 650
#define CFG_TYPE_ENUM 1
#define CFG_TYPE_STRING 2
#define CFG_TYPE_VALUE 3
#define CFG_TYPE_SEP 4
#define CFG_TYPE_END 0xFF
struct t_cfg_definition {
    uint8_t id;
    uint8_t type;
    const char *item_text;
    const char *item_format;
    const char **items;
    int min, max;
    intptr_t def;
};
class ConfigStore {
public:
    int get_value(uint8_t) { return 0; }
    const char *get_string(uint8_t) { return "3000"; }
    void set_sort_order(int) {}
    void effectuate() {}
};
class ConfigurableObject {
public:
    ConfigStore *cfg = new ConfigStore;
    void register_store(uint32_t, const char *, t_cfg_definition *) {}
};
static const char *en_dis[] = { "Disabled", "Enabled" };
#endif
