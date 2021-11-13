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

#define UART_CHAN UART_NUM_1

void cmd_echo(command_buf_t *buf)
{
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_identify(command_buf_t *buf)
{
    rpc_identify_resp *resp = (rpc_identify_resp *)buf->data;
    resp->major = 0;
    resp->minor = 9;
    strcpu(&resp->string, "ESP-32 RPC Socket Layer V0.9");
    buf->size = sizeof(rpc_identify_resp) + strlen(&resp->string);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_setbaud(command_buf_t *buf)
{
}

esp_err_t wifi_scan(ultimate_ap_records_t *ult_records);
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
    ESP_LOGI("GetHostByName", "Trying to resolve %s", hostname);
    resp->retval = gethostbyname_r(hostname, &hent, buffer, CMD_BUF_SIZE / 2, &hent_result, &h_errno);
    if (hent_result) {
        struct in_addr *s = (struct in_addr *)hent_result->h_addr_list[0];
        memcpy(&resp->addr, s, sizeof(struct in_addr));
    }
    resp->xerrno = h_errno;
    buf->size = sizeof(rpc_gethostbyname_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_not_implemented(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    resp->esp_err = ESP_ERR_NOT_SUPPORTED;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void dispatch(void *ct)
{
    QueueHandle_t queue = (QueueHandle_t)ct;
    command_buf_t *pbuffer;
    rpc_header_t *hdr;

    while(1) {
        xQueueReceive(queue, &pbuffer, portMAX_DELAY);
        hdr = (rpc_header_t *)(pbuffer->data);
        switch(hdr->command) {
        case CMD_ECHO:
            cmd_echo(pbuffer);
            break;
        case CMD_IDENTIFY:
            cmd_identify(pbuffer);
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
        case CMD_SOCKET:
            cmd_socket(pbuffer);
            break;
        case CMD_CONNECT:
            cmd_connect(pbuffer);
            break;
        case CMD_ACCEPT:
            cmd_accept(pbuffer);
            break;
        case CMD_BIND:
            cmd_bind(pbuffer);
            break;
        case CMD_SHUTDOWN:
            cmd_shutdown(pbuffer);
            break;
        case CMD_CLOSE:
            cmd_close(pbuffer);
            break;
        case CMD_LISTEN:
            cmd_listen(pbuffer);
            break;
        case CMD_READ:
            cmd_read(pbuffer);
            break;
        case CMD_RECV:
            cmd_recv(pbuffer);
            break;
        case CMD_RECVFROM:
            cmd_recvfrom(pbuffer);
            break;
        case CMD_WRITE:
            cmd_write(pbuffer);
            break;
        case CMD_SEND:
            cmd_send(pbuffer);
            break;
        case CMD_SENDTO:
            cmd_sendto(pbuffer);
            break;
        case CMD_GETHOSTBYNAME:
            cmd_gethostbyname(pbuffer);
            break;
        case CMD_GETPEERNAME:
        case CMD_GETSOCKNAME:
        case CMD_GETSOCKOPT:
        case CMD_SETSOCKOPT:
        case CMD_IOCTL:
        case CMD_FCNTL:
        case CMD_SET_BAUD:
        default:
            cmd_not_implemented(pbuffer);
            break;
        }
    }
}

void create_dispatchers(int stacksize, command_buf_context_t *bufs)
{
    static dispatcher_t dispatchers[NUM_BUFFERS];
    for (int i=0; i < NUM_BUFFERS; i++) {
        dispatchers[i].queue = xQueueCreate(1, sizeof(command_buf_t*));
        xTaskCreate( dispatch, "Dispatch Task", stacksize, dispatchers[i].queue, tskIDLE_PRIORITY + 1, &(dispatchers[i].task) );
        bufs->bufs[i].object = &dispatchers[i];
    }
}
