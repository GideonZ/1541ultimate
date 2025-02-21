#ifndef NETWORK_CONFIG_H
#define NETWORK_CONFIG_H

#include "config.h"

#define CFG_NETWORK_HOSTNAME               0x10
#define CFG_NETWORK_PASSWORD               0x11
#define CFG_NETWORK_ULTIMATE_IDENT_SERVICE 0x21
#define CFG_NETWORK_ULTIMATE_DMA_SERVICE   0x22
#define CFG_NETWORK_TELNET_SERVICE         0x23
#define CFG_NETWORK_FTP_SERVICE            0x24
#define CFG_NETWORK_HTTP_SERVICE           0x25


class NetworkConfig : ConfigurableObject {
public:
    NetworkConfig();
    ~NetworkConfig();

    ConfigStore *cfg;
};

extern NetworkConfig networkConfig;

#endif /* NETWORK_CONFIG_H */
