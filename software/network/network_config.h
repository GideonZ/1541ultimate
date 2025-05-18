#ifndef NETWORK_CONFIG_H
#define NETWORK_CONFIG_H

#include "config.h"

#define CFG_NETWORK_HOSTNAME               0x10
#define CFG_NETWORK_PASSWORD               0x11
#define CFG_NETWORK_UNIQUE_ID              0x12
#define CFG_NETWORK_ULTIMATE_IDENT_SERVICE 0x21
#define CFG_NETWORK_ULTIMATE_DMA_SERVICE   0x22
#define CFG_NETWORK_TELNET_SERVICE         0x23
#define CFG_NETWORK_FTP_SERVICE            0x24
#define CFG_NETWORK_HTTP_SERVICE           0x25
#define CFG_NETWORK_REMOTE_SYSLOG_SERVER   0x26
#define CFG_NETWORK_NTP_EN                 0x30
#define CFG_NETWORK_NTP_ZONE               0x31
#define CFG_NETWORK_NTP_SRV1               0x32
#define CFG_NETWORK_NTP_SRV2               0x33
#define CFG_NETWORK_NTP_SRV3               0x34


class NetworkConfig : ConfigurableObject {
public:
    NetworkConfig();
    ~NetworkConfig();

    void effectuate_settings(void);
    static void list_unique_id_choices(ConfigItem *it, IndexedList<char *>& strings);
    const char *get_posix_timezone(void);
    ConfigStore *cfg;
};

extern NetworkConfig networkConfig;

#endif /* NETWORK_CONFIG_H */
