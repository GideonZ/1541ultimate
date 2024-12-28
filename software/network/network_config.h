#ifndef NETWORK_CONFIG_H
#define NETWORK_CONFIG_H

#include "config.h"

#define CFG_NETWORK_HOSTNAME               0x10


class NetworkConfig : ConfigurableObject {
public:
    NetworkConfig();
    ~NetworkConfig();

    ConfigStore *cfg;
};

extern NetworkConfig networkConfig;

#endif /* NETWORK_CONFIG_H */
