#include <string.h>

#include "config.h"
#include "small_printf.h"
#include "product.h"
#include "network_config.h"
#include "timezones.cc"
#include "sntp_time.h"

static char default_hostname[42];

#define ZONE_COUNT (sizeof(zone_names)/sizeof(const char *))

// Network configuration settings
struct t_cfg_definition network_config[] = {
    { CFG_NETWORK_HOSTNAME,        CFG_TYPE_STRING,     "Host Name",                        "%s", NULL,       3, 31, (int)default_hostname},
    { CFG_NETWORK_UNIQUE_ID,       CFG_TYPE_STRFUNC,    "Unique ID",                        "%s", (const char **)NetworkConfig :: list_unique_id_choices, 0, 16, (int)"Default" },
    { CFG_NETWORK_PASSWORD,        CFG_TYPE_STRPASS,    "Network Password",                 "%s", NULL,       3, 31, (int)"" },
    { 0xFE,                        CFG_TYPE_SEP,        "",                                 "",   NULL,       0,  0, 0 },
    { 0xFE,                        CFG_TYPE_SEP,        "Services",                         "",   NULL,       0,  0, 0 },
    { CFG_NETWORK_ULTIMATE_IDENT_SERVICE, CFG_TYPE_ENUM,"Ultimate Ident Service",           "%s", en_dis,     0,  1, 1 },
    { CFG_NETWORK_ULTIMATE_DMA_SERVICE, CFG_TYPE_ENUM,  "Ultimate DMA Service",             "%s", en_dis,     0,  1, 1 },
    { CFG_NETWORK_TELNET_SERVICE,   CFG_TYPE_ENUM,      "Telnet Remote Menu Service",       "%s", en_dis,     0,  1, 1 },
    { CFG_NETWORK_FTP_SERVICE,      CFG_TYPE_ENUM,      "FTP File Service",                 "%s", en_dis,     0,  1, 1 },
    { CFG_NETWORK_HTTP_SERVICE,     CFG_TYPE_ENUM,      "Web Remote Control Service",       "%s", en_dis,     0,  1, 1 },
    { CFG_NETWORK_REMOTE_SYSLOG_SERVER, CFG_TYPE_STRING,"Log to Syslog Server",             "%s", NULL,       8, 31, (int)"" },
    { 0xFE,                        CFG_TYPE_SEP,        "",                                 "",   NULL,       0,  0, 0 },
    { 0xFE,                        CFG_TYPE_SEP,        "Time Synchronization",             "",   NULL,       0,  0, 0 },
    { CFG_NETWORK_NTP_EN,          CFG_TYPE_ENUM,       "SNTP Enable",                      "%s", en_dis,     0,  1, 1 },
    { CFG_NETWORK_NTP_ZONE,        CFG_TYPE_ENUM,       "TimeZone",                         "%s", zone_names, 0, ZONE_COUNT-1, 16 },
    { CFG_NETWORK_NTP_SRV1,        CFG_TYPE_STRING,     "Time Server 1",                    "%s", NULL,       3,  31, (int)"time.windows.com" },
    { CFG_NETWORK_NTP_SRV2,        CFG_TYPE_STRING,     "Time Server 2",                    "%s", NULL,       3,  31, (int)"time.google.com" },
    { CFG_NETWORK_NTP_SRV3,        CFG_TYPE_STRING,     "Time Server 3",                    "%s", NULL,       3,  31, (int)"pool.ntp.org" },
    { CFG_TYPE_END,                CFG_TYPE_END,        "",                                 "",   NULL,       0,  0, 0 }
};


NetworkConfig :: NetworkConfig()
{
}

NetworkConfig :: ~NetworkConfig()
{
}

void NetworkConfig :: init()
{
    char *hostname = getProductDefaultHostname(default_hostname, sizeof(default_hostname));
    strcpy(default_hostname, hostname ? hostname : "Small Buffer");
    cfg = ConfigManager :: getConfigManager()->register_store(0x4E455400, "Network Settings", network_config, this);
}

void NetworkConfig :: list_unique_id_choices(ConfigItem *it, IndexedList<char *>& strings)
{
    char *empty = new char[1];
    *empty = 0;
    strings.append(empty);

    char *def = new char[12];
    strcpy(def, "Default");
    strings.append(def);

    // The "Enter Manually" option is appended by the config browser automatically
}

const char *NetworkConfig :: get_posix_timezone(void)
{
    const timezone_entry_t *tz = &zones[cfg->get_value(CFG_NETWORK_NTP_ZONE)];
    return tz->posix;
}

void NetworkConfig :: effectuate_settings(void)
{
    printf("**** EFFECTUATE NETWORK SETTINGS ****\n");
    start_sntp(); // restart with possible new settings or new time zone
}

#include "init_function.h"
NetworkConfig networkConfig;
InitFunction init_netcfg("Network Config", [](void *_obj, void *_param) { networkConfig.init(); }, NULL, NULL, 20);

