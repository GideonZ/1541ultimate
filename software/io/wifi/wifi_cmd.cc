#include "wifi_cmd.h"
#if !defined(RECOVERYAPP) && !defined(WIFI_COMMANDS_ONLY)
#include "provider_wifi_ingress.h"
#endif
#include "dump_hex.h"

/// C like functions to 'talk' with the WiFi Module
static uint16_t sequence_nr = 0;
static wifi_reply_slot_t replySlots[WIFI_REPLY_SLOTS];

void hex(uint8_t h)
{
    static const uint8_t hexchars[] = "0123456789ABCDEF";
    ioWrite8(UART_DATA, hexchars[h >> 4]);
    ioWrite8(UART_DATA, hexchars[h & 15]);
}

BaseType_t wifi_rx_isr(command_buf_context_t *context, command_buf_t *buf, BaseType_t *w)
{
    if (buf->size < int(sizeof(rpc_header_t)) || buf->size > int(sizeof(buf->data))
#if !defined(RECOVERYAPP) && !defined(WIFI_COMMANDS_ONLY)
        || wifi_provider_receive(buf)
#endif
    ) {
        return cmd_buffer_free_isr(context, buf, w);
    }
    rpc_header_t *hdr = (rpc_header_t *)buf->data;
    BaseType_t res;

    TaskHandle_t thread = (TaskHandle_t)wifi_reply_take(replySlots, hdr->thread, hdr->sequence);
    if (thread) {
        ioWrite8(UART_DATA, 'N');
        res = xTaskNotifyFromISR(thread, (uint32_t)buf, eSetValueWithOverwrite, w);
    } else {
        ioWrite8(UART_DATA, '~');
        res = cmd_buffer_received_isr(context, buf, w);
    }
    return res;
}

int wifi_reply_register(rpc_header_t *hdr)
{
    wifi_reply_tag_t tag;
    ENTER_SAFE_SECTION;
    int ok = wifi_reply_claim(replySlots, &sequence_nr, xTaskGetCurrentTaskHandle(), &tag);
    LEAVE_SAFE_SECTION;
    if (ok) {
        hdr->thread = tag.thread;
        hdr->sequence = tag.sequence;
    }
    return ok;
}

// The request buffer is freed by the UART once sent, so the tag is read first.
command_buf_t *wifi_transmit(command_buf_t *buf, int timeout_ms)
{
    rpc_header_t hdr = *(rpc_header_t *)buf->data;
    esp32.uart->TransmitPacket(buf);
    return wifi_wait_reply(hdr, timeout_ms);
}

command_buf_t *wifi_wait_reply(rpc_header_t hdr, int timeout_ms)
{
    command_buf_t *reply = NULL;
    if (xTaskNotifyWait(0, 0, (uint32_t *)&reply, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        return reply;
    }
    wifi_reply_tag_t tag = { hdr.thread, hdr.sequence };
    ENTER_SAFE_SECTION;
    wifi_reply_abandon(replySlots, &tag);
    LEAVE_SAFE_SECTION;
    // The reply may have been delivered just before the slot was released
    if (xTaskNotifyWait(0, 0, (uint32_t *)&reply, 0) == pdTRUE) {
        return reply;
    }
    return NULL;
}

void wifi_command_init(void)
{
    // A task that was deleted while it waited must not be notified later
    ENTER_SAFE_SECTION;
    for (int i = 0; i < WIFI_REPLY_SLOTS; i++) {
        replySlots[i].task = NULL;
    }
    LEAVE_SAFE_SECTION;
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

    rpc_header_t hdr = args->hdr;
    vTaskDelay(1000);

    esp32.uart->TransmitPacket(buf);
    vTaskDelay(100); // wait until transmission must have completed (no handshake!) (~ half second, remote side will send reply at new rate after one second)
    esp32.uart->SetBaudRate(baudrate);
    esp32.uart->FlowControl(flowctrl != 0);

    buf = wifi_wait_reply(hdr, 5000);
    if (buf) {
        // copy results
        rpc_espcmd_resp *result = (rpc_espcmd_resp *)buf->data;
        int retval = result->esp_err;
        esp32.uart->FreeBuffer(buf);
        return retval;
    }
    return -1;
}

BaseType_t wifi_detect(uint16_t *major, uint16_t *minor, char *str, int maxlen)
{
    BUFARGS_NOREPLY(identify, CMD_IDENTIFY, pdFALSE); // the result is a success flag here
    if (!wifi_reply_register(&args->hdr)) {
        esp32.uart->FreeBuffer(buf);
        printf(no_wifi_slot);
        return pdFALSE;
    }

    // now block thread for reply
    printf("Get ident...");
    buf = wifi_transmit(buf, 500);
    BaseType_t success = (buf) ? pdTRUE : pdFALSE;
    if (success == pdTRUE) {
        // From this moment on, "buf" points to the receive buffer!!
        // copy results
        rpc_identify_resp *result = (rpc_identify_resp *)buf->data;
        *major = result->major;
        *minor = result->minor;
        strncpy(str, &result->string, maxlen);
        str[maxlen-1] = 0;
        printf("Identify: %s\n", str);
        esp32.uart->FreeBuffer(buf);
    } else {
        printf("No reply...\n");
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

int wifi_set_power_mode(uint8_t mode)
{
    BUFARGS(set_power_mode, CMD_SET_POWER_MODE);
    args->mode = mode;
    TRANSMIT(espcmd);
    RETURN_ESP;
}

int wifi_get_power_mode(uint8_t *mode, uint8_t *last_state)
{
    BUFARGS(identify, CMD_GET_POWER_MODE);
    TRANSMIT(get_power_mode);
    if (result->esp_err == 0) {
        if (mode) {
            *mode = result->mode;
        }
        if (last_state) {
            *last_state = result->last_state;
        }
    } else {
        printf("Get Power Mode returned %d as error code.\n", result->esp_err);
    }
    RETURN_ESP;
}

int wifi_set_wake_on_wifi(uint8_t enabled)
{
    BUFARGS(set_wake_on_wifi, CMD_SET_WAKE_ON_WIFI);
    args->enabled = enabled;
    TRANSMIT(espcmd);
    RETURN_ESP;
}

int wifi_get_wake_on_wifi(uint8_t *enabled)
{
    BUFARGS(identify, CMD_GET_WAKE_ON_WIFI);
    TRANSMIT(get_wake_on_wifi);
    if (result->esp_err == 0) {
        if (enabled) {
            *enabled = result->enabled;
        }
    } else {
        printf("Get Wake On WiFi returned %d as error code.\n", result->esp_err);
    }
    RETURN_ESP;
}

int wifi_set_recovery_url(const char *url)
{
    BUFARGS(set_recovery_url, CMD_SET_RECOVERY_URL);
    strncpy(args->url, url, RPC_RECOVERY_URL_LEN - 1);
    args->url[RPC_RECOVERY_URL_LEN - 1] = 0;
    TRANSMIT(espcmd);
    RETURN_ESP;
}

int wifi_get_recovery_url(char *url, int maxlen)
{
    BUFARGS(identify, CMD_GET_RECOVERY_URL);
    TRANSMIT(get_recovery_url);
    if (result->esp_err == 0) {
        strncpy(url, result->url, maxlen - 1);
        url[maxlen - 1] = 0;
    } else {
        url[0] = 0;
        printf("Get Recovery URL returned %d as error code.\n", result->esp_err);
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
    TRANSMIT_T(scan, WIFI_SLOW_TIMEOUT_MS);
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

    TRANSMIT_T(scan, WIFI_SLOW_TIMEOUT_MS);
    RETURN_ESP;
}

int wifi_wifi_autoconnect()
{
    BUFARGS(identify, CMD_WIFI_AUTOCONNECT);
    TRANSMIT_T(espcmd, WIFI_SLOW_TIMEOUT_MS);
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
const char *no_wifi_slot = "*** NO REPLY SLOT FOR THE WIFI MODULE.\n";

