/* LwIP SNTP example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "esp_netif_sntp.h"
#include "lwip/ip_addr.h"
#include "esp_sntp.h"
#include "rpc_calls.h"

static const char *TAG = "sntp";

void ____sntp_sync_time(struct timeval *tv)
{
   settimeofday(tv, NULL);
   ESP_LOGI(TAG, "Time is synchronized from custom code");
   sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
}

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event, time set to %ld s %ld us", (long int)tv->tv_sec, (long int)tv->tv_usec);
    settimeofday(tv, NULL);
}

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 48
#endif

static void print_servers(void)
{
    ESP_LOGI(TAG, "List of configured NTP servers:");

    for (uint8_t i = 0; i < SNTP_MAX_SERVERS; ++i){
        if (esp_sntp_getservername(i)){
            ESP_LOGI(TAG, "server %d: %s", i, esp_sntp_getservername(i));
        } else {
            // we have either IPv4 or IPv6 address, let's print it
            char buff[INET6_ADDRSTRLEN];
            ip_addr_t const *ip = esp_sntp_getserver(i);
            if (ipaddr_ntoa_r(ip, buff, INET6_ADDRSTRLEN) != NULL)
                ESP_LOGI(TAG, "server %d: %s", i, buff);
        }
    }
}

#define CONFIG_SNTP_TIME_SERVER0 "pool.ntp.org"
#define CONFIG_SNTP_TIME_SERVER2 "time.google.com"
#define CONFIG_SNTP_TIME_SERVER1 "time.windows.com"
#define CONFIG_SNTP_TIME_SERVER4 "time.apple.com"
#define CONFIG_SNTP_TIME_SERVER5 "time.facebook.com"

void configure_sntp_dhcp(void)
{
    /**
     * NTP server address could be acquired via DHCP
     *
     * NOTE: This call should be made BEFORE esp acquires IP address from DHCP,
     * otherwise NTP option would be rejected by default.
     */
    ESP_LOGI(TAG, "Initializing SNTP using DHCP");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER1);
    config.start = false;                       // start SNTP service explicitly (after connecting)
    config.server_from_dhcp = true;             // accept NTP offers from DHCP server, if any (need to enable *before* connecting)
    config.renew_servers_after_new_IP = true;   // let esp-netif update configured SNTP server(s) after receiving DHCP lease
    config.index_of_first_server = 1;           // updates from server num 1, leaving server 0 (from DHCP) intact
    // configure the event on which we renew servers
    config.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
    config.sync_cb = time_sync_notification_cb; // only if we need the notification function
    esp_netif_sntp_init(&config);
}

void configure_sntp_servers(void)
{
    ESP_LOGI(TAG, "Initializing SNTP using servers");
    /* This demonstrates configuring more than one server
     */
     esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER1);

    // esp_sntp_config_t config =  ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2,
    //                                 ESP_SNTP_SERVER_LIST(
    //                                     CONFIG_SNTP_TIME_SERVER1,
    //                                     CONFIG_SNTP_TIME_SERVER2
//                                        CONFIG_SNTP_TIME_SERVER3,
//                                        CONFIG_SNTP_TIME_SERVER4,
//                                        CONFIG_SNTP_TIME_SERVER5
//                                    )
//                                );
    config.sync_cb = time_sync_notification_cb;     // Note: This is only needed if we want
#if 0 // def CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
    config.smooth_sync = true;
#endif
    esp_netif_sntp_init(&config);
    print_servers();
}

time_t now;
struct tm timeinfo;

void obtain_time(void)
{
    ESP_LOGI(TAG, "Starting SNTP");
    esp_netif_sntp_start();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 15;
    while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    }
    time(&now);
    localtime_r(&now, &timeinfo);
}

void print_time(void)
{
    char strftime_buf[64];

    // Set timezone to Eastern Standard Time and print local time
    setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0", 1);
    tzset();
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in New York is: %s", strftime_buf);

    // Set timezone to China Standard Time
    setenv("TZ", "CST-8", 1);
    tzset();
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in Shanghai is: %s", strftime_buf);

    if (sntp_get_sync_mode() == SNTP_SYNC_MODE_SMOOTH) {
        struct timeval outdelta;
        while (sntp_get_sync_status() == SNTP_SYNC_STATUS_IN_PROGRESS) {
            adjtime(NULL, &outdelta);
            ESP_LOGI(TAG, "Waiting for adjusting time ... outdelta = %jd sec: %li ms: %li us",
                        (intmax_t)outdelta.tv_sec,
                        outdelta.tv_usec/1000,
                        outdelta.tv_usec%1000);
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
    }
}

void print_time_for_zone(const char *zone)
{
    char strftime_buf[64];
    setenv("TZ", zone, 1);
    tzset();
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in zone %s is: %s", zone, strftime_buf);
}

void sntp_get_time(const char *zone, esp_datetime_t *datetime)
{
    setenv("TZ", zone, 1);
    tzset();
    time(&now);
    localtime_r(&now, &timeinfo);
    datetime->year = timeinfo.tm_year + 1900;
    datetime->month = timeinfo.tm_mon + 1;
    datetime->day = timeinfo.tm_mday;
    datetime->weekday = timeinfo.tm_wday;
    datetime->hour = timeinfo.tm_hour;
    datetime->minute = timeinfo.tm_min;
    datetime->second = timeinfo.tm_sec;
}

typedef struct {
    const char *timezone;
    const char *utc;
    const char *location;
    const char *posix;
} timezone_entry_t;

const timezone_entry_t zones[] = {
    { "AoE",   "UTC -12",   "US Baker Island", "AOE12" },
    { "NUT",   "UTC -11",   "America Samoa",   "NUT11" },
    { "HST",   "UTC -10",   "Hawaii", "HST11HDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "MART",  "UTC -9:30", "French Polynesia", "MART9:30,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "AKST",  "UTC -9",    "Alaska", "ASKT9AKDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "PDT",   "UTC -8",    "Los Angeles", "PST8PDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "MST",   "UTC -7",    "Denver, Colorado", "MST7MDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "MST",   "UTC -7",    "Phoenix, Arizona", "MST7" },
    { "CST",   "UTC -6",    "Chicago, Illinois", "CST6CDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "EST",   "UTC -5",    "New York", "EST5EDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "ART",   "UTC -3",    "Argentina", "ART3" },
    { "NDT",   "UTC -2:30", "Newfoundland", "NDT3:30NST,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "WGST",  "UTC -2",    "Greenland", "WGST3WGT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "CVT",   "UTC -1",    "Cabo Verde", "CVT1" },
    { "GMT",   "UTC 0",     "Iceland", "GMT" },
    { "BST",   "UTC +1",    "UK", "BST0GMT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "CEST",  "UTC +2",    "Germany", "CEST-1CET,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "MSK",   "UTC +3",    "Greece", "MSK-3" },
    { "GST",   "UTC +4",    "Azerbaijan", "GST-4" },
    { "IRDT",  "UTC +4:30", "Iran", "IRDT3:30IRST,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "UZT",   "UTC +5",    "Pakistan", "UZT-5" },
    { "IST",   "UTC +5:30", "India", "IST-5:30" },
    { "NPT",   "UTC +5:45", "Nepal", "NPT-5:45" },
    { "BST",   "UTC +6",    "Bangladesh", "BST-6" },
    { "MMT",   "UTC +6:30", "Myanmar", "MMT-6:30" },
    { "WIB",   "UTC +7",    "Indonesia", "WIB-7" },
    { "CST",   "UTC +8",    "China", "CST-8" },
    { "ACWST", "UTC +8:45", "Western Australia", "ACWST-8:45" },
    { "JST",   "UTC +9",    "Japan", "JST-9" },
    { "ACST",  "UTC +9:30", "Central Australia", "ACST8:30ACDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "AEST",  "UTC +10",   "Eastern Australia", "AEST9AEDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "LHST",  "UTC +10:30","Lord Howe Island", "LHST9:30LHDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "SBT",   "UTC +11",   "Solomon Islands", "SBT-11" },
    { "ANAT",  "UTC +12",   "New Zealand", "ANAT-12" },
    { "CHAST", "UTC +12:45","Chatham Islands", "CHAST11:45CHADT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "TOT",   "UTC +13",   "Tonga", "TOT-12TOST,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "LINT",  "UTC +14",   "Christmas Island", "LINT-14" },
};

void print_all_zones(void)
{
    for (int i = 0; i < sizeof(zones) / sizeof(zones[0]); i++) {
        print_time_for_zone(zones[i].posix);
    }
}