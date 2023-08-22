/*
 * network_esp32.c
 *
 *  Created on: Nov 11, 2021
 *      Author: gideon
 */

#include "FreeRTOS.h"
#include "network_esp32.h"
#include "rpc_calls.h"
#include <string.h>
#include "dump_hex.h"

// Concept:
// 1) Take buffer from cmd buffer free queue (may block)
// 2) Fill buffer with command and task and sequence code
// 3) Fill buffer with params
// 4) Send buffer by putting it in the transmit queue
// 5) Block task with 'wait notify'
// 6) Use notification value as pointer to receive packet
// 7) Take response from receive packet, like retval and errno, and/or data
// 8) Return buffer to cmd buffer free queue

//#define ENTRY(t) cmd_buffer_get

//extern command_buf_context_t *command_buf_context;
static uint16_t sequence_nr = 0;

#include "wifi.h"
extern WiFi wifi;

TaskHandle_t tasksWaitingForReply[NUM_BUFFERS];

#define BUFARGS(x, cmd)     command_buf_t *buf; \
                            wifi.uart->GetBuffer(&buf, portMAX_DELAY); \
                            rpc_ ## x ## _req *args = (rpc_ ## x ## _req *)buf->data; \
                            args->hdr.command = cmd; \
                            args->hdr.sequence = sequence_nr++; \
                            args->hdr.thread = (uint8_t)buf->bufnr; \
                            buf->size = sizeof(rpc_ ## x ## _req); \
                            tasksWaitingForReply[buf->bufnr] = xTaskGetCurrentTaskHandle();


#define TRANSMIT(x)         wifi.uart->TransmitPacket(buf); \
                            xTaskNotifyWait(0, 0, (uint32_t *)&buf, portMAX_DELAY); \
                            printf("Received buffer %d, size %d:\n", buf->bufnr, buf->size); \
                            dump_hex_relative(buf->data, buf->size > 32 ? 32 : buf->size); \
                            rpc_ ## x ## _resp *result = (rpc_ ## x ## _resp *)buf->data; \
                            tasksWaitingForReply[result->hdr.thread] = NULL;

#define RETURN_STD          errno = result->xerrno; \
                            int retval = result->retval; \
                            wifi.uart->FreeBuffer(buf); \
                            return retval;

#define RETURN_ESP          int retval = result->esp_err; \
                            wifi.uart->FreeBuffer(buf); \
                            return retval;


#include "itu.h"
void hex(uint8_t h)
{
    static const uint8_t hexchars[] = "0123456789ABCDEF";
    ioWrite8(UART_DATA, hexchars[h >> 4]);
    ioWrite8(UART_DATA, hexchars[h & 15]);
}

BaseType_t u64_buffer_received_isr(command_buf_context_t *context, command_buf_t *buf, BaseType_t *w)
{
    rpc_header_t *hdr = (rpc_header_t *)buf->data;
    BaseType_t res;

/*
    hex(hdr->thread);
    ioWrite8(UART_DATA, '-');
    uint32_t th = (uint32_t)tasksWaitingForReply[hdr->thread];
    hex((uint8_t)(th >> 16));
    hex((uint8_t)(th >> 8));
    hex((uint8_t)(th >> 0));
    ioWrite8(UART_DATA, ';');

*/
    if ((hdr->thread < NUM_BUFFERS) && (tasksWaitingForReply[hdr->thread])) {
        res = xTaskNotifyFromISR(tasksWaitingForReply[hdr->thread], (uint32_t)buf, eSetValueWithOverwrite, w);
    } else {
        res = cmd_buffer_received_isr(context, buf, w);
    }
    return res;
}


int wifi_setbaud(int baudrate, uint8_t flowctrl)
{
    BUFARGS(setbaud, CMD_SET_BAUD);

    args->baudrate = baudrate;
    args->flowctrl = flowctrl;
    args->inversions = 0;

    wifi.uart->TransmitPacket(buf);
    vTaskDelay(100); // wait until transmission must have completed (no handshake!) (~ half second, remote side will send reply at new rate after one second)
    wifi.uart->SetBaudRate(baudrate);
    wifi.uart->FlowControl(flowctrl != 0);

    BaseType_t success = xTaskNotifyWait(0, 0, (uint32_t *)&buf, 1000); // 5 seconds
    if (success == pdTRUE) {
        // copy results
        rpc_espcmd_resp *result = (rpc_espcmd_resp *)buf->data;
        wifi.uart->FreeBuffer(buf);
        tasksWaitingForReply[result->hdr.thread] = NULL;
        return result->esp_err;
    }
    return -1;
}

BaseType_t wifi_detect(uint16_t *major, uint16_t *minor, char *str, int maxlen)
{
    BUFARGS(identify, CMD_IDENTIFY);

    wifi.uart->TransmitPacket(buf);
    // now block thread for reply
    BaseType_t success = xTaskNotifyWait(0, 0, (uint32_t *)&buf, 1000); // 5 seconds
    // BaseType_t success = wifi.uart->ReceivePacket(&buf, 1000);
    if (success == pdTRUE) {
        // copy results
        rpc_identify_resp *result = (rpc_identify_resp *)buf->data;
        *major = result->major;
        *minor = result->minor;
        strncpy(str, &result->string, maxlen);
        str[maxlen-1] = 0;
        wifi.uart->FreeBuffer(buf);
        tasksWaitingForReply[result->hdr.thread] = NULL;
    }
    return success;
}

int wifi_getmac(uint8_t *mac)
{
    BUFARGS(identify, CMD_WIFI_GETMAC);
    TRANSMIT(getmac);
    if (result->esp_err == 0) {
        memcpy(mac, result->mac, 6);
    } else {
        printf("Get MAC returned %d as error code.\n", result->esp_err);
    }
    RETURN_ESP;
}

int wifi_scan(void *a)
{
    ultimate_ap_records_t *aps = (ultimate_ap_records_t *)a;

    BUFARGS(identify, CMD_WIFI_SCAN);
    TRANSMIT(scan);
    ENTER_SAFE_SECTION;
    memcpy(aps, &(result->rec), sizeof(ultimate_ap_records_t));
    LEAVE_SAFE_SECTION;
    RETURN_ESP;
}

int wifi_wifi_connect(char *ssid, char *password, uint8_t auth)
{
    BUFARGS(wifi_connect, CMD_WIFI_CONNECT);

    bzero(args->ssid, 32);
    bzero(args->password, 64);
    strncpy(args->ssid, ssid, 32);
    strncpy(args->password, password, 64);
    args->auth_mode = auth;

    TRANSMIT(scan);
    RETURN_ESP;
}

int wifi_wifi_disconnect()
{
    BUFARGS(wifi_connect, CMD_WIFI_DISCONNECT);
    TRANSMIT(espcmd);
    RETURN_ESP;
}

uint8_t wifi_tx_packet(void *driver, void *buffer, int length)
{
    BUFARGS(send_eth, CMD_SEND_PACKET);

    uint32_t chunkSize = length;
    if (chunkSize > CMD_BUF_PAYLOAD) {
        chunkSize = CMD_BUF_PAYLOAD;
    }
    args->length = chunkSize;
    memcpy(&args->data, buffer, chunkSize);
    buf->size += (chunkSize - 4);

    wifi.uart->TransmitPacket(buf);
    //TRANSMIT(espcmd);
    //RETURN_ESP;
}

void wifi_free(void *driver, void *buffer)
{
    // driver points to the WiFi object
    // buffer points to the cmd_buffer_t object
    WiFi *w = (WiFi *)driver;
    w->freeBuffer((command_buf_t *)buffer);
}

/*
int wifi_socket(int domain, int type, int protocol)
{
    while(wifi.getState() < eWifi_NotConnected) {
        printf("Task %p is waiting for ESP32 to create socket.\n", xTaskGetCurrentTaskHandle());
        vTaskDelay(1000);
    }

    BUFARGS(socket, CMD_SOCKET);

    args->domain = domain;
    args->type = type;
    args->protocol = protocol;

    TRANSMIT(socket);

    printf("Socket created: %d\n", result->retval);
    RETURN_STD;
}

int wifi_connect(int s, const struct sockaddr *name, socklen_t namelen)
{
    BUFARGS(connect, CMD_CONNECT);

    args->socket = s;
    args->namelen = (uint32_t)namelen;
    args->name = *name;

    TRANSMIT(connect);
    RETURN_STD;
}

int wifi_accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    BUFARGS(accept, CMD_ACCEPT);

    args->socket = s;
    args->namelen = (uint32_t)*addrlen;

    TRANSMIT(accept);

    *addr = result->name;
    *addrlen = result->namelen;

    RETURN_STD;
}

int wifi_bind(int s, const struct sockaddr *name, socklen_t namelen)
{
    BUFARGS(bind, CMD_BIND);
    args->socket = s;
    args->namelen = namelen;
    memcpy(&args->name, name, namelen);
    TRANSMIT(bind);
    RETURN_STD;
}

int wifi_shutdown(int s, int how)
{
    BUFARGS(shutdown, CMD_SHUTDOWN);
    args->socket = s;
    args->how = how;
    TRANSMIT(shutdown);
    RETURN_STD;
}

int wifi_close(int s)
{
    BUFARGS(close, CMD_CLOSE);
    args->socket = s;
    TRANSMIT(close);
    RETURN_STD;
}

int wifi_listen(int s, int backlog)
{
    BUFARGS(listen, CMD_LISTEN);
    args->socket = s;
    args->backlog = backlog;
    TRANSMIT(listen);
    RETURN_STD;
}

int wifi_read(int s, void *mem, size_t len)
{
    BUFARGS(read, CMD_READ);
    args->socket = s;
    args->length = len;
    TRANSMIT(read);
    int ret = result->retval;
    if (ret > len) { ret = (int)len; } // should never happen
    if (ret > 0) {
        memcpy(mem, &result->data, ret);
    }
    RETURN_STD;
}

int wifi_recv(int s, void *mem, size_t len, int flags)
{
    BUFARGS(recvfrom, CMD_RECV);
    args->socket = s;
    args->length = len;
    args->flags = flags;
    TRANSMIT(read);
    int ret = result->retval;
    if (ret > len) { ret = (int)len; } // should never happen
    if (ret > 0) {
        memcpy(mem, &result->data, ret);
    }
    RETURN_STD;
}

int wifi_recvfrom(int s, void *mem, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    BUFARGS(recvfrom, CMD_RECVFROM);
    args->socket = s;
    args->length = len;
    args->flags = flags;
    TRANSMIT(recvfrom);
    int ret = result->retval;
    if (ret > len) { ret = (int)len; } // should never happen
    if (ret > 0) {
        memcpy(mem, &result->data, ret);
    }
    int maxlen = *fromlen;
    if (result->fromlen > maxlen) {
        maxlen = result->fromlen;
    }
    *fromlen = result->fromlen;
    memcpy(from, &result->from, maxlen);
    RETURN_STD;
}

#define MAX_PAYLOAD 1024
int wifi_write(int s, const void *dataptr, size_t size)
{
    printf("Requested write to socket %d of size %d.\n", s, size);
    dump_hex_relative(dataptr, size);

    BUFARGS(write, CMD_WRITE);
    args->socket = s;
    uint32_t chunkSize = size;
    if (chunkSize > MAX_PAYLOAD) {
        chunkSize = MAX_PAYLOAD;
    }
    args->length = chunkSize;
    memcpy(&args->data, dataptr, chunkSize);
    buf->size += (chunkSize - 4);
    TRANSMIT(write);
    RETURN_STD;
}

int wifi_send(int s, const void *dataptr, size_t size, int flags)
{
    printf("Requested send to socket %d of size %d.\n", s, size);
    dump_hex_relative(dataptr, size);

    BUFARGS(send, CMD_SEND);
    args->socket = s;
    uint32_t chunkSize = size;
    if (chunkSize > MAX_PAYLOAD) {
        chunkSize = MAX_PAYLOAD;
    }
    args->length = chunkSize;
    args->flags = flags;
    memcpy(&args->data, dataptr, chunkSize);
    buf->size += (chunkSize - 4);
    TRANSMIT(send);
    RETURN_STD;
}

int wifi_sendto(int s, const void *dataptr, size_t size, int flags, const struct sockaddr *to, socklen_t tolen)
{
    BUFARGS(sendto, CMD_SENDTO);
    args->socket = s;
    args->tolen = tolen;
    args->to = *to;
    uint32_t chunkSize = size;
    if (chunkSize > MAX_PAYLOAD) {
        chunkSize = MAX_PAYLOAD;
    }
    args->length = chunkSize;
    args->flags = flags;
    memcpy(&args->data, dataptr, chunkSize);
    buf->size += (chunkSize - 4);
    TRANSMIT(sendto);
    RETURN_STD;
}

int wifi_gethostbyname()
{

}

int wifi_gethostbyname_r(const char *name, struct hostent *ret, char *client_buf, size_t buflen, struct hostent **client_result, int *h_errnop)
{
    BUFARGS(gethostbyname, CMD_GETHOSTBYNAME);
    strcpy(&args->name, name);
    TRANSMIT(gethostbyname);

    struct hostent *out = (struct hostent *)client_buf;
    struct in_addr *addr = (struct in_addr *)(client_buf + sizeof(struct hostent));
    // char **addr_list = (char **)(client_buf + sizeof(struct hostent) + sizeof(struct in_addr));

    // Output list
    //addr_list[0] = (char *)addr;
    //addr_list[1] = NULL;

    out->h_addrtype = AF_INET;
    out->h_length = sizeof(in_addr);
    out->h_name = NULL;
    out->h_aliases = NULL;
    out->h_addr_list[0] = (char *)addr;
    out->h_addr_list[1] = NULL;

    memcpy(addr, &result->addr, sizeof(in_addr));

    *client_result = out;
    *h_errnop = result->xerrno;
    RETURN_STD;
}

int wifi_getpeername (int s, struct sockaddr *name, socklen_t *namelen)
{
    return -1;
}

int wifi_getsockname (int s, struct sockaddr *name, socklen_t *namelen)
{
    BUFARGS(getsockname, CMD_GETSOCKNAME);
    args->socket = s;
    args->namelen = *namelen;
    int maxlen = *namelen;
    TRANSMIT(getsockname);
    *namelen = result->namelen;
    if (result->namelen < maxlen) {
        maxlen = result->namelen;
    }
    memcpy(name, &result->name, maxlen);
    RETURN_STD;
}

int wifi_getsockopt (int s, int level, int optname, void *optval, socklen_t *optlen)
{
    BUFARGS(getsockopt, CMD_GETSOCKOPT);
    args->socket = s;
    args->level;
    args->optname = optname;
    args->optlen = *optlen;
    int maxlen = *optlen;
    TRANSMIT(getsockopt);
    *optlen = result->optlen;
    if (result->optlen < maxlen) {
        maxlen = result->optlen;
    }
    memcpy(optval, &result->optval, maxlen);
    RETURN_STD;
}

int wifi_setsockopt (int s, int level, int optname, const void *optval, socklen_t optlen)
{
    BUFARGS(setsockopt, CMD_SETSOCKOPT);
    args->socket = s;
    args->level;
    args->optname = optname;
    args->optlen = optlen;
    buf->size += (optlen - 4);
    memcpy(&args->optval, optval, optlen); // not bounded!
    TRANSMIT(setsockopt);
    RETURN_STD;
}

int wifi_ioctl(int s, long cmd, void *argp)
{
    return -1;
}

int wifi_fcntl(int s, int cmd, int val)
{
    return -1;
}
*/

