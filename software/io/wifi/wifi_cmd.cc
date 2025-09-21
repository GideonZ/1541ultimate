#include "wifi_cmd.h"
#include "dump_hex.h"

/// C like functions to 'talk' with the WiFi Module
uint16_t sequence_nr = 0;
TaskHandle_t tasksWaitingForReply[NUM_BUFFERS];

void hex(uint8_t h)
{
    static const uint8_t hexchars[] = "0123456789ABCDEF";
    ioWrite8(UART_DATA, hexchars[h >> 4]);
    ioWrite8(UART_DATA, hexchars[h & 15]);
}

BaseType_t wifi_rx_isr(command_buf_context_t *context, command_buf_t *buf, BaseType_t *w)
{
    rpc_header_t *hdr = (rpc_header_t *)buf->data;
    BaseType_t res;

    if ((hdr->thread < NUM_BUFFERS) && (tasksWaitingForReply[hdr->thread])) {
        TaskHandle_t thread = tasksWaitingForReply[hdr->thread];
        tasksWaitingForReply[hdr->thread] = NULL;
        ioWrite8(UART_DATA, 'N');
        res = xTaskNotifyFromISR(thread, (uint32_t)buf, eSetValueWithOverwrite, w);
    } else {
        ioWrite8(UART_DATA, '~');
        res = cmd_buffer_received_isr(context, buf, w);
    }
    return res;
}

void wifi_command_init(void)
{
#if (CLOCK_FREQ == 66666667)
    esp32.uart->SetBaudRate(6666666);
#else
    esp32.uart->SetBaudRate(5000000);
#endif
    esp32.uart->FlowControl(true);
    esp32.uart->ClearRxBuffer();
    esp32.uart->EnableSlip(true);
    esp32.uart->SetReceiveCallback(wifi_rx_isr);
    esp32.uart->EnableIRQ(true);
}


int wifi_setbaud(int baudrate, uint8_t flowctrl)
{
    BUFARGS(setbaud, CMD_SET_BAUD);

    args->baudrate = baudrate;
    args->flowctrl = flowctrl;
    args->inversions = 0;

    vTaskDelay(1000);

    esp32.uart->TransmitPacket(buf);
    vTaskDelay(100); // wait until transmission must have completed (no handshake!) (~ half second, remote side will send reply at new rate after one second)
    esp32.uart->SetBaudRate(baudrate);
    esp32.uart->FlowControl(flowctrl != 0);

    BaseType_t success = xTaskNotifyWait(0, 0, (uint32_t *)&buf, 1000); // 5 seconds
    if (success == pdTRUE) {
        // copy results
        rpc_espcmd_resp *result = (rpc_espcmd_resp *)buf->data;
        esp32.uart->FreeBuffer(buf);
        tasksWaitingForReply[result->hdr.thread] = NULL;
        return result->esp_err;
    }
    return -1;
}

BaseType_t wifi_detect(uint16_t *major, uint16_t *minor, char *str, int maxlen)
{
    BUFARGS(identify, CMD_IDENTIFY);

    esp32.uart->TransmitPacket(buf);
    // now block thread for reply
    printf("Get ident...");
    BaseType_t success = xTaskNotifyWait(0, 0, (uint32_t *)&buf, 100); // .5 seconds
    if (success == pdTRUE) {
        // From this moment on, "buf" points to the receive buffer!!
        // copy results
        rpc_identify_resp *result = (rpc_identify_resp *)buf->data;
        *major = result->major;
        *minor = result->minor;
        strncpy(str, &result->string, maxlen);
        str[maxlen-1] = 0;
        printf("Identify: %s\n", str);
    } else {
        printf("No reply...\n");
        // 'buf' still points to the transmit buffer.
        // It is freed by the TxIRQ, so the content is invalid
        // Let's try to get messages from the receive buffer to see what has been received.
        // printf("Receive queue:\n");
        while (esp32.uart->ReceivePacket(&buf, 0) == pdTRUE) {
            printf("Buf %02x. Size: %4d. Data:\n", buf->bufnr, buf->size);
            dump_hex(buf->data, buf->size > 64 ? 64 : buf->size);
            esp32.uart->FreeBuffer(buf);
        }
        // printf("/end of Receive queue\n");
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

#if U64 == 2
int wifi_set_serial(const char *serial)
{
    BUFARGS(set_serial, CMD_SET_SERIAL);
    strncpy(args->serial, serial, 16);
    TRANSMIT(espcmd);
    RETURN_ESP;
}

int wifi_get_serial(char *serial)
{
    BUFARGS(identify, CMD_GET_SERIAL);
    TRANSMIT(get_serial);
    if (result->esp_err == 0) {
        memcpy(serial, result->serial, 16);
    } else {
        strcpy(serial, "-Not set-");
        printf("Get Serial returned %d as error code.\n", result->esp_err);
    }
    RETURN_ESP;
}
#endif

int wifi_is_connected(uint8_t &status)
{
    BUFARGS(identify, CMD_WIFI_IS_CONNECTED);
    TRANSMIT(get_connection);
    if (result->esp_err == 0) {
        status = result->status;
    } else {
        printf("Get connection status returned %d as error code.\n", result->esp_err);
    }
    RETURN_ESP;
}

/*
int wifi_get_time(const char *timezone, esp_datetime_t *time)
{
    BUFARGS(get_time, CMD_GET_TIME);
    TRANSMIT(get_time);
    if (result->esp_err == 0) {
        time->year = result->datetime.year;
        time->month = result->datetime.month;
        time->day = result->datetime.day;
        time->hour = result->datetime.hour;
        time->minute = result->datetime.minute;
        time->second = result->datetime.second;
        time->weekday = result->datetime.weekday;
    } else {
        printf("Get time returned %d as error code.\n", result->esp_err);
    }
    RETURN_ESP;
}
*/
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

int wifi_wifi_connect(const char *ssid, const char *password, uint8_t auth)
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

int wifi_wifi_autoconnect()
{
    BUFARGS(identify, CMD_WIFI_AUTOCONNECT);
    TRANSMIT(espcmd);
    RETURN_ESP;
}

int wifi_wifi_disconnect()
{
    BUFARGS(identify, CMD_WIFI_DISCONNECT);
    TRANSMIT(espcmd);
    RETURN_ESP;
}

int wifi_machine_off()
{
    BUFARGS(identify, CMD_MACHINE_OFF);
    TRANSMIT(espcmd);
    RETURN_ESP;
}

#if U64 == 2
int wifi_machine_reboot()
{
    BUFARGS(identify, CMD_MACHINE_REBOOT);
    TRANSMIT(espcmd);
    RETURN_ESP;
}
#endif

int wifi_forget_aps()
{
    BUFARGS(identify, CMD_CLEAR_APS);
    TRANSMIT(espcmd);
    RETURN_ESP;
}


#if U64 == 2
int wifi_enable()
{
    BUFARGS(identify, CMD_WIFI_ENABLE);
    TRANSMIT(espcmd);
    RETURN_ESP;
}

int wifi_disable()
{
    BUFARGS(identify, CMD_WIFI_DISABLE);
    TRANSMIT(espcmd);
    RETURN_ESP;
}

int wifi_get_voltages(voltages_t *voltages)
{
    BUFARGS(identify, CMD_GET_VOLTAGES);
    TRANSMIT(get_voltages);
    voltages->vbus = result->vbus;
    voltages->vaux = result->vaux;
    voltages->v50  = result->v50;
    voltages->v33  = result->v33;
    voltages->v18  = result->v18;
    voltages->v10  = result->v10;
    voltages->vusb = result->vusb;
    RETURN_ESP;
}
#endif

int wifi_modem_enable(bool enable)
{
    BUFARGS(identify, (enable) ? CMD_MODEM_ON : CMD_MODEM_OFF);
    TRANSMIT(espcmd);
    RETURN_ESP;
}

const char *no_wifi_buf = "*** NO BUFFER TO TRANSMIT ANYTHING.\n";

