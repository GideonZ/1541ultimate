#include "esp_eth.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "mbedtls/version.h"
#include "lwip/dns.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

static EventGroupHandle_t events;
static void got_ip(void *context, esp_event_base_t base, int32_t id, void *data)
{
    (void)context;
    (void)base;
    (void)id;
    (void)data;
    xEventGroupSetBits(events, 1);
}

bool provider_runtime_network(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    events = xEventGroupCreate();
    configASSERT(events);
    esp_netif_config_t net_config = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&net_config);
    configASSERT(netif);
    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));
    esp_netif_ip_info_t ip = {0};
    IP4_ADDR(&ip.ip, 192, 0, 2, 2);
    IP4_ADDR(&ip.netmask, 255, 255, 255, 252);
    /* No gateway: this guest only talks to the private fixture subnet. */
    ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip));
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.reset_gpio_num = -1;
    phy_config.autonego_timeout_ms = 100;
    esp_eth_mac_t *mac = esp_eth_mac_new_openeth(&mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_dp83848(&phy_config);
    configASSERT(mac && phy);
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t driver;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &driver));
    uint8_t address[] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
    ESP_ERROR_CHECK(esp_eth_ioctl(driver, ETH_CMD_S_MAC_ADDR, address));
    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(driver);
    configASSERT(glue);
    ESP_ERROR_CHECK(esp_netif_attach(netif, glue));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, got_ip, NULL));
    ESP_ERROR_CHECK(esp_eth_start(driver));
    bool connected = (xEventGroupWaitBits(events, 1, pdFALSE, pdTRUE, pdMS_TO_TICKS(5000)) & 1) != 0;
    ip_addr_t dns; IP_ADDR4(&dns,192,0,2,1); dns_setserver(0,&dns);
    return connected;
}

