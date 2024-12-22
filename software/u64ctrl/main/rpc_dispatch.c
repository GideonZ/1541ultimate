/*
 * rpc_dispatch.c
 *
 *  Created on: Nov 9, 2021
 *      Author: gideon
 */


#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "rpc_calls.h"
#include "rpc_dispatch.h"
#include "my_uart.h"
#include "driver/gpio.h"
#include "pinout.h"
#include "button_handler.h"
#include "wifi_modem.h"
#include "sntp.h"

// not officially supported
#include "../lwip/esp_netif_lwip_internal.h"
#include "esp_netif_net_stack.h"

#define UART_CHAN UART_NUM_1

static const char *TAG = "dispatch";

void cmd_echo(command_buf_t *buf)
{
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_identify(command_buf_t *buf)
{
    rpc_identify_resp *resp = (rpc_identify_resp *)buf->data;
    resp->major = IDENT_MAJOR;
    resp->minor = IDENT_MINOR;
    strcpy(&resp->string, IDENT_STRING);
    buf->size = sizeof(rpc_identify_resp) + strlen(&resp->string);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_getmac(command_buf_t *buf)
{
    rpc_getmac_resp *resp = (rpc_getmac_resp *)buf->data;
    resp->esp_err = esp_wifi_get_mac(WIFI_IF_STA, resp->mac);
    buf->size = sizeof(rpc_getmac_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_setbaud(command_buf_t *buf)
{
    rpc_setbaud_req *req = (rpc_setbaud_req *)buf->data;
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;

    esp_err_t err = uart_set_baudrate(UART_CHAN, req->baudrate);
    if (err == ESP_OK) {
        err = uart_set_hw_flow_ctrl(UART_CHAN, req->flowctrl ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE, 122);
    }
    if (err == ESP_OK) {
        err = uart_set_line_inverse(UART_CHAN, req->inversions);
    }

#if 1 || UART_DEBUG
    ESP_LOGI(TAG, "Baud Rate set to %d -> Err = %d", req->baudrate, err);
#endif
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait one second

    resp->esp_err = err;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_scan(command_buf_t *buf)
{
    rpc_scan_resp *resp = (rpc_scan_resp *)buf->data;
    resp->esp_err = wifi_scan(&resp->rec);
    buf->size = sizeof(resp->hdr) + sizeof(resp->esp_err) + (resp->rec.num_records * sizeof(ultimate_ap_record_t));
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_wifi_connect(command_buf_t *buf)
{
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));

    rpc_wifi_connect_req *param = (rpc_wifi_connect_req *)buf->data;
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;

    wifi_config.sta.threshold.authmode = param->auth_mode;
    memcpy(&wifi_config.sta.ssid, param->ssid, 32);
    memcpy(&wifi_config.sta.password, param->password, 64);
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    wifi_store_ap(param->ssid, param->password, param->auth_mode);

    if (buf->size < 98) {
        resp->esp_err = ESP_ERR_INVALID_ARG;
    } else {
        resp->esp_err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        if (resp->esp_err == ESP_OK) {
            resp->esp_err = esp_wifi_connect();
        }
    }
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_wifi_disconnect(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    resp->esp_err = esp_wifi_disconnect();
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

extern esp_netif_t *my_sta_netif;

void cmd_send_eth_packet(command_buf_t *buf)
{
    rpc_send_eth_req *param = (rpc_send_eth_req *)buf->data;
//    rpc_send_eth_resp *resp = (rpc_send_eth_resp *)buf->data;
    err_t _err = esp_netif_transmit(my_sta_netif, &param->data, param->length);
    my_uart_free_buffer(UART_CHAN, buf);
    //buf->size = sizeof(rpc_header_t);
    //my_uart_transmit_packet(UART_CHAN, buf);
}

/*
void cmd_socket(command_buf_t *buf)
{
    rpc_socket_req *param = (rpc_socket_req *)buf->data;
    rpc_socket_resp *resp = (rpc_socket_resp *)buf->data;
    resp->retval = socket(param->domain, param->type, param->protocol);
    resp->xerrno = errno;
    buf->size = sizeof(rpc_socket_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_connect(command_buf_t *buf)
{
    rpc_connect_req *param = (rpc_connect_req *)buf->data;
    rpc_connect_resp *resp = (rpc_connect_resp *)buf->data;
    resp->retval = connect(param->socket, &(param->name), param->namelen);
    resp->xerrno = errno;
    buf->size = sizeof(rpc_connect_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_accept(command_buf_t *buf)
{
    rpc_accept_req *param = (rpc_accept_req *)buf->data;
    rpc_accept_resp *resp = (rpc_accept_resp *)buf->data;
    socklen_t namelen = (socklen_t)param->namelen;
    namelen = (namelen > 100) ? 100 : namelen;
    resp->retval = accept(param->socket, &resp->name, &namelen);
    resp->xerrno = errno;
    resp->namelen = (uint32_t)namelen;
    buf->size = sizeof(rpc_accept_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_bind(command_buf_t *buf)
{
    rpc_bind_req *param = (rpc_bind_req *)buf->data;
    rpc_bind_resp *resp = (rpc_bind_resp *)buf->data;
    resp->retval = bind(param->socket, (struct sockaddr *)&(param->name), param->namelen);
    resp->xerrno = errno;
    buf->size = sizeof(rpc_bind_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_shutdown(command_buf_t *buf)
{
    rpc_shutdown_req *param = (rpc_shutdown_req *)buf->data;
    rpc_shutdown_resp *resp = (rpc_shutdown_resp *)buf->data;
    resp->retval = shutdown(param->socket, param->how);
    resp->xerrno = errno;
    buf->size = sizeof(rpc_shutdown_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_close(command_buf_t *buf)
{
    rpc_close_req *param = (rpc_close_req *)buf->data;
    rpc_close_resp *resp = (rpc_close_resp *)buf->data;
    resp->retval = close(param->socket);
    resp->xerrno = errno;
    buf->size = sizeof(rpc_close_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_listen(command_buf_t *buf)
{
    rpc_listen_req *param = (rpc_listen_req *)buf->data;
    rpc_listen_resp *resp = (rpc_listen_resp *)buf->data;
    resp->retval = listen(param->socket, param->backlog);
    resp->xerrno = errno;
    buf->size = sizeof(rpc_listen_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_read(command_buf_t *buf)
{
    rpc_read_req *param = (rpc_read_req *)buf->data;
    rpc_read_resp *resp = (rpc_read_resp *)buf->data;
    // chip off the byte that was used as a placeholder
    int return_size = sizeof(rpc_read_resp) - 1;
    if (param->length > (CMD_BUF_SIZE - return_size)) {
        param->length = CMD_BUF_SIZE - return_size;
    }
    resp->retval = read(param->socket, &resp->data, param->length);
    resp->xerrno = errno;
    buf->size = return_size;
    // if data was returned; append it by setting the size to a bigger value
    if (resp->retval > 0) {
        buf->size += resp->retval;
    }
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_recv(command_buf_t *buf)
{
    rpc_recvfrom_req *param = (rpc_recvfrom_req *)buf->data;
    rpc_read_resp *resp = (rpc_read_resp *)buf->data;
    // chip off the byte that was used as a placeholder
    int return_size = sizeof(rpc_read_resp) - 1;
    if (param->length > (CMD_BUF_SIZE - return_size)) {
        param->length = CMD_BUF_SIZE - return_size;
    }
    resp->retval = recv(param->socket, &resp->data, param->length, param->flags);
    resp->xerrno = errno;
    buf->size = return_size;
    // if data was returned; append it by setting the size to a bigger value
    if (resp->retval > 0) {
        buf->size += resp->retval;
    }
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_recvfrom(command_buf_t *buf)
{
    rpc_recvfrom_req *param = (rpc_recvfrom_req *)buf->data;
    rpc_recvfrom_resp *resp = (rpc_recvfrom_resp *)buf->data;
    // chip off the byte that was used as a placeholder
    int return_size = sizeof(rpc_recvfrom_resp) - 1;
    if (param->length > (CMD_BUF_SIZE - return_size)) {
        param->length = CMD_BUF_SIZE - return_size;
    }
    socklen_t fromlen = sizeof(struct sockaddr);
    resp->retval = recvfrom(param->socket, &resp->data, param->length, param->flags, &resp->from, &fromlen);
    resp->xerrno = errno;
    resp->fromlen = (uint32_t)fromlen;
    buf->size = return_size;
    // if data was returned; append it by setting the size to a bigger value
    if (resp->retval > 0) {
        buf->size += resp->retval;
    }
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_write(command_buf_t *buf)
{
    rpc_write_req *param = (rpc_write_req *)buf->data;
    rpc_write_resp *resp = (rpc_write_resp *)buf->data;
    resp->retval = write(param->socket, &param->data, param->length);
    resp->xerrno = errno;
    buf->size = sizeof(rpc_write_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_send(command_buf_t *buf)
{
    rpc_send_req *param = (rpc_send_req *)buf->data;
    rpc_send_resp *resp = (rpc_send_resp *)buf->data;
    resp->retval = send(param->socket, &param->data, param->length, param->flags);
    resp->xerrno = errno;
    buf->size = sizeof(rpc_send_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_sendto(command_buf_t *buf)
{
    rpc_sendto_req *param = (rpc_sendto_req *)buf->data;
    rpc_sendto_resp *resp = (rpc_sendto_resp *)buf->data;
    resp->retval = sendto(param->socket, &param->data, param->length, param->flags, &param->to, param->tolen);
    resp->xerrno = errno;
    buf->size = sizeof(rpc_sendto_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_gethostbyname(command_buf_t *buf)
{
    rpc_gethostbyname_req *param = (rpc_gethostbyname_req *)buf->data;
    rpc_gethostbyname_resp *resp = (rpc_gethostbyname_resp *)buf->data;

    struct hostent hent;
    struct hostent *hent_result = NULL;
    int h_errno;

    const char *hostname = (const char *)&(param->name);
    char *buffer = ((char *)buf->data) + (CMD_BUF_SIZE / 2);
    ESP_LOGI(TAG, "Trying to resolve %s", hostname);
    resp->retval = gethostbyname_r(hostname, &hent, buffer, CMD_BUF_SIZE / 2, &hent_result, &h_errno);
    if (hent_result) {
        struct in_addr *s = (struct in_addr *)hent_result->h_addr_list[0];
        memcpy(&resp->addr, s, sizeof(struct in_addr));
    }
    resp->xerrno = h_errno;
    buf->size = sizeof(rpc_gethostbyname_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_getsockname(command_buf_t *buf)
{
    rpc_getsockname_req *param = (rpc_getsockname_req *)buf->data;
    rpc_getsockname_resp *resp = (rpc_getsockname_resp *)buf->data;
    socklen_t len = param->namelen;
    socklen_t newlen = 0;
    resp->retval = getsockname(param->socket, &resp->name, &newlen);
    resp->xerrno = errno;
    if (newlen > len) {
        newlen = len;
    }
    buf->size = sizeof(rpc_getsockname_resp) + newlen - 1;
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_getsockopt(command_buf_t *buf)
{
    rpc_getsockopt_req *param = (rpc_getsockopt_req *)buf->data;
    rpc_getsockopt_resp *resp = (rpc_getsockopt_resp *)buf->data;
    socklen_t len = param->optlen;
    socklen_t newlen = 0;
    resp->retval = getsockopt(param->socket, param->level, param->optname, &resp->optval, &newlen);
    resp->xerrno = errno;
    if (newlen > len) {
        newlen = len;
    }
    buf->size = sizeof(rpc_getsockopt_resp) + newlen - 1;
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_setsockopt(command_buf_t *buf)
{
    rpc_setsockopt_req *param = (rpc_setsockopt_req *)buf->data;
    rpc_setsockopt_resp *resp = (rpc_setsockopt_resp *)buf->data;
    resp->retval = setsockopt(param->socket, param->level, param->optname, &param->optval, param->optlen);
    resp->xerrno = errno;
    buf->size = sizeof(rpc_setsockopt_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}
*/
void cmd_off(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    resp->esp_err = ESP_OK;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    extern_button_event(BUTTON_OFF);
}

void cmd_modem_on(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    resp->esp_err = ESP_OK;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
    enable_hook();
}

void cmd_modem_off(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    resp->esp_err = ESP_OK;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
    disable_hook();
}

void cmd_wifi_disable(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    resp->esp_err = ESP_ERR_NOT_SUPPORTED;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_wifi_enable(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    resp->esp_err = ESP_ERR_NOT_SUPPORTED;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_get_voltages(command_buf_t *buf)
{
    extern esp_err_t read_adc_channels(uint16_t *adc_data);

    rpc_get_voltages_resp *resp = (rpc_get_voltages_resp *)buf->data;
    resp->esp_err = ESP_OK;
    read_adc_channels(&(resp->vbus));
    buf->size = sizeof(rpc_get_voltages_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_get_connection(command_buf_t *buf)
{
    rpc_get_connection_resp *resp = (rpc_get_connection_resp *)buf->data;
    resp->esp_err = ESP_OK;
    resp->status = wifi_get_connection();
    buf->size = sizeof(rpc_get_connection_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_get_time(command_buf_t *buf)
{
    rpc_get_time_req *param = (rpc_get_time_req *)buf->data;
    rpc_get_time_resp *resp = (rpc_get_time_resp *)buf->data;
    sntp_get_time(param->timezone, &resp->datetime);
    resp->esp_err = ESP_OK;
    buf->size = sizeof(rpc_get_time_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_clear_aps(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    buf->size = sizeof(rpc_espcmd_resp);
    resp->esp_err = wifi_clear_aps();
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_not_implemented(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    resp->esp_err = ESP_ERR_NOT_SUPPORTED;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void send_keepalive()
{
    command_buf_t *reply;
    if (my_uart_get_buffer(UART_NUM_1, &reply, 100)) {
        rpc_header_t *hdr = (rpc_header_t *)reply->data;
        hdr->command = EVENT_KEEPALIVE;
        hdr->thread = 0xFF;
        hdr->sequence = 0;
        reply->size = sizeof(rpc_header_t);
        my_uart_transmit_packet(UART_NUM_1, reply);
    } else {
        ESP_LOGW(TAG, "No buffer to keepalive.");
    }
}

void send_button_event(uint8_t button)
{
    const uint8_t button_codes[] = { EVENT_BUTTON, EVENT_MENU, EVENT_RESET, EVENT_FREEZE, EVENT_BUTTON, EVENT_BUTTON, EVENT_BUTTON } ;
    command_buf_t *reply;
    if (my_uart_get_buffer(UART_NUM_1, &reply, 100)) {
        rpc_header_t *hdr = (rpc_header_t *)reply->data;
        hdr->command = button_codes[button];
        hdr->thread = 0xFF;
        hdr->sequence = 0;
        reply->size = sizeof(rpc_header_t);
        my_uart_transmit_packet(UART_NUM_1, reply);
    } else {
        ESP_LOGW(TAG, "No buffer to send button event.");
    }
}

void dispatch(void *ct)
{
    QueueHandle_t queue = (QueueHandle_t)ct;
    command_buf_t *pbuffer;
    rpc_header_t *hdr;

    int led = 0;
    printf("Dispatcher Start. Queue = %p\n", queue);
    while(1) {
        pbuffer = NULL;
        BaseType_t received = xQueueReceive(queue, &pbuffer, 2000 / portTICK_PERIOD_MS);
        led = 1 - led;
        gpio_set_level(IO_ESP_LED, led);
        if (received == pdFALSE) {
            wifi_check_connection();
            continue;
        }
#if UART_DEBUG
        printf("Received buffer %d with %d bytes.\n", pbuffer->bufnr, pbuffer->size);
#endif
        hdr = (rpc_header_t *)(pbuffer->data);
        switch(hdr->command) {
        case CMD_SEND_PACKET:
            cmd_send_eth_packet(pbuffer);
            break;
        case CMD_ECHO:
            cmd_echo(pbuffer);
            break;
        case CMD_IDENTIFY:
            cmd_identify(pbuffer);
            break;
        case CMD_SET_BAUD:
            cmd_setbaud(pbuffer);
            break;
        case CMD_WIFI_SCAN:
            cmd_scan(pbuffer);
            break;
        case CMD_WIFI_CONNECT:
            cmd_wifi_connect(pbuffer);
            break;
        case CMD_WIFI_DISCONNECT:
            cmd_wifi_disconnect(pbuffer);
            break;
        case CMD_WIFI_GETMAC:
            cmd_getmac(pbuffer);
            break;
        case CMD_MACHINE_OFF:
            cmd_off(pbuffer);
            break;
        case CMD_MODEM_ON:
            cmd_modem_on(pbuffer);
            break;
        case CMD_MODEM_OFF:
            cmd_modem_off(pbuffer);
            break;
        case CMD_WIFI_DISABLE:
            cmd_wifi_disable(pbuffer);
            break;
        case CMD_WIFI_ENABLE:
            cmd_wifi_enable(pbuffer);
            break;
        case CMD_GET_VOLTAGES:
            cmd_get_voltages(pbuffer);
            break;
        case CMD_WIFI_IS_CONNECTED:
            cmd_get_connection(pbuffer);
            break;
        case CMD_GET_TIME:
            cmd_get_time(pbuffer);
            break;
        case CMD_CLEAR_APS:
            cmd_clear_aps(pbuffer);
            break;
        default:
            cmd_not_implemented(pbuffer);
            break;
        }
    }
}

void start_dispatch(QueueHandle_t queue)
{
    xTaskCreate( dispatch, "Dispatch Task", DISPATCHER_STACK, queue, tskIDLE_PRIORITY + 1, NULL );
}

#if MULTITHREADED
void create_dispatchers(int stacksize, command_buf_context_t *bufs)
{
    static dispatcher_t dispatchers[NUM_BUFFERS];
    for (int i=0; i < NUM_BUFFERS; i++) {
        dispatchers[i].queue = xQueueCreate(1, sizeof(command_buf_t*));
        xTaskCreate( dispatch, "Dispatch Task", stacksize, dispatchers[i].queue, tskIDLE_PRIORITY + 1, &(dispatchers[i].task) );
        bufs->bufs[i].object = &dispatchers[i];
    }
}
#endif
