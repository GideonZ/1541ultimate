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
#include "nvs_flash.h"
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
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    ConnectCommand_t cmd;
    cmd.buf = buf;
    cmd.command = CMD_WIFI_SCAN;

    if (xQueueSend(connect_commands, &cmd, 1000) == pdFALSE) {
        resp->esp_err = ESP_ERR_NO_MEM;
        my_uart_transmit_packet(UART_CHAN, buf);
        return;
    }
    xSemaphoreGive(connect_semaphore);
    // command is now in the queue, so the connect handler will process it further
}

void cmd_wifi_connect(command_buf_t *buf)
{
    rpc_wifi_connect_req *param = (rpc_wifi_connect_req *)buf->data;
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;

    if (buf->size < 98) {
        resp->esp_err = ESP_ERR_INVALID_ARG;
        my_uart_transmit_packet(UART_CHAN, buf);
        return;
    }

    buf->size = sizeof(rpc_espcmd_resp);
    ConnectCommand_t ev;
    ev.buf = buf;
    ev.command = CMD_WIFI_CONNECT;
    ev.auth_mode = param->auth_mode;
    memcpy(&ev.ssid, param->ssid, 32);
    memcpy(&ev.pw, param->password, 64);

    if (xQueueSend(connect_commands, &ev, 1000) == pdFALSE) {
        resp->esp_err = ESP_ERR_NO_MEM;
        my_uart_transmit_packet(UART_CHAN, buf);
        return;
    }
    xSemaphoreGive(connect_semaphore);
    // command is now in the queue, so the connect handler will process it further
}

void cmd_wifi_autoconnect(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;

    buf->size = sizeof(rpc_espcmd_resp);
    ConnectCommand_t ev;
    ev.buf = buf;
    ev.command = CMD_WIFI_AUTOCONNECT;

    if (xQueueSend(connect_commands, &ev, 1000) == pdFALSE) {
        resp->esp_err = ESP_ERR_NO_MEM;
        my_uart_transmit_packet(UART_CHAN, buf);
        return;
    } else {
        resp->esp_err = ESP_OK;
        my_uart_transmit_packet(UART_CHAN, buf);
    }
    xSemaphoreGive(connect_semaphore);
    // command is now in the queue, so the connect handler will process it further
}

void cmd_wifi_disconnect(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    ESP_LOGI(TAG, "Disconnecting from WiFi.");
    resp->esp_err = esp_wifi_disconnect();
    ESP_LOGI(TAG, "Disconnected from WiFi, sending response. (%d)", resp->esp_err);
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

extern esp_netif_t *my_sta_netif;

void cmd_send_eth_packet(command_buf_t *buf)
{
    rpc_send_eth_req *param = (rpc_send_eth_req *)buf->data;
    err_t _err = esp_netif_transmit(my_sta_netif, &param->data, param->length);
    my_uart_free_buffer(UART_CHAN, buf);
}

void cmd_off(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    resp->esp_err = ESP_OK;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    extern_button_event(BUTTON_OFF);
}

void cmd_reboot(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    resp->esp_err = ESP_OK;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    extern_button_event(BUTTON_OFF);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    extern_button_event(BUTTON_ON);
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

void cmd_wifi_endisable(command_buf_t *buf)
{
    rpc_header_t *hdr = (rpc_header_t *)buf->data;
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;

    buf->size = sizeof(rpc_espcmd_resp);
    ConnectCommand_t ev;
    ev.buf = buf;
    ev.command = hdr->command;

    if (xQueueSend(connect_commands, &ev, 1000) == pdFALSE) {
        resp->esp_err = ESP_ERR_NO_MEM;
        my_uart_transmit_packet(UART_CHAN, buf);
        return;
    } else {
        resp->esp_err = ESP_OK;
        my_uart_transmit_packet(UART_CHAN, buf);
    }
    xSemaphoreGive(connect_semaphore);
    // command is now in the queue, so the connect handler will process it further
}

void cmd_get_voltages(command_buf_t *buf)
{
    extern esp_err_t read_adc_channels(uint16_t *adc_data);

    rpc_get_voltages_resp *resp = (rpc_get_voltages_resp *)buf->data;
    resp->esp_err = ESP_OK;
    read_adc_channels(&(resp->vbus));
    ESP_LOGI(TAG, "Vbus = %d, Vaux = %d, V50 = %d, V33 = %d, V18 = %d, V10 = %d, Vusb = %d", resp->vbus, resp->vaux, resp->v50, resp->v33, resp->v18, resp->v10, resp->vusb);
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

    // if connected; also send the connected event, so that the SSID is communicated.
    wifi_send_connected_event();
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
    if (resp->esp_err == ESP_OK) {
        resp->esp_err = esp_wifi_disconnect();
    }
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_set_serial(command_buf_t *buf)
{
    rpc_set_serial_req *param = (rpc_set_serial_req *)buf->data;
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;

    nvs_handle_t handle;
    esp_err_t err = nvs_open("board", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        size_t len;
        esp_err_t err2 = nvs_get_str(handle, "serial", NULL, &len);
        if (err2 == ESP_ERR_NVS_NOT_FOUND) {
            param->serial[15] = 0;
            err = nvs_set_str(handle, "serial", param->serial);
            if (err == ESP_OK) {
                err = nvs_commit(handle);
            }
        }
        nvs_close(handle);
    }
    resp->esp_err = err;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_get_serial(command_buf_t *buf)
{
    rpc_get_serial_resp *resp = (rpc_get_serial_resp *)buf->data;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("board", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t length = 16;
        err = nvs_get_str(handle, "serial", resp->serial, &length);
        nvs_close(handle);
    }
    resp->esp_err = err;
    buf->size = sizeof(rpc_get_serial_resp);
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

    printf("Dispatcher Start. Queue = %p\n", queue);
    while(1) {
        pbuffer = NULL;
        // BaseType_t received = xQueueReceive(queue, &pbuffer, 200 / portTICK_PERIOD_MS);
        // if (received == pdFALSE) {
        //     gpio_set_level(IO_ESP_LED, 1); // OFF
        //     continue;
        // }
        // gpio_set_level(IO_ESP_LED, 0); // ON!
        xQueueReceive(queue, &pbuffer, portMAX_DELAY); // No LED at all.
#if UART_DEBUG_RX
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
        case CMD_WIFI_AUTOCONNECT:
            cmd_wifi_autoconnect(pbuffer);
            break;
        case CMD_WIFI_GETMAC:
            cmd_getmac(pbuffer);
            break;
        case CMD_MACHINE_OFF:
            cmd_off(pbuffer);
            break;
        case CMD_MACHINE_REBOOT:
            cmd_reboot(pbuffer);
            break;
        case CMD_MODEM_ON:
            cmd_modem_on(pbuffer);
            break;
        case CMD_MODEM_OFF:
            cmd_modem_off(pbuffer);
            break;
        case CMD_WIFI_DISABLE:
        case CMD_WIFI_ENABLE:
            cmd_wifi_endisable(pbuffer);
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
        case CMD_SET_SERIAL:
            cmd_set_serial(pbuffer);
            break;
        case CMD_GET_SERIAL:
            cmd_get_serial(pbuffer);
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
