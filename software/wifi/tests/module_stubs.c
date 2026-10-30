/*
 * module_stubs.c
 *
 * Scripted stand-ins for everything the WiFi modules' connector, dispatcher and event
 * handler call, and a model of how the IDF station chooses an access point in
 * esp_wifi_connect(). Makefile includes this file at the end of the module's extracted
 * code (output/<module>/module_code.c), so that it can reach the static functions there.
 * See module_script.h for what the cases get.
 *
 * The station model follows the station config table of ESP-IDF 5.3.1
 * (docs/en/api-guides/wifi.rst, "Station Basic Configuration"):
 *  - The scan visits the config's channel first when it is 1..13, then channels 1..13
 *    without it; without a channel, 1..13 in order.
 *  - WIFI_FAST_SCAN: the scan ends at the first AP whose SSID (and BSSID, if bssid_set)
 *    matches and that passes the threshold; that is the one joined.
 *  - WIFI_ALL_CHANNEL_SCAN: every channel is scanned and the matching APs are sorted, by
 *    RSSI (WIFI_CONNECT_AP_BY_SIGNAL) or by security (WIFI_CONNECT_AP_BY_SECURITY); the
 *    first after sorting is joined.
 *  - threshold.rssi 0 means -127; an AP whose auth mode is below threshold.authmode is
 *    skipped.
 */

#include <setjmp.h>
#include "module_script.h"

// What the extracted code refers to
const char *TAG = "test";
volatile bool user_disconnected;
volatile bool off_asked; // the U64 and U2+L modules: a Disable since the last CMD_IDENTIFY
uint16_t connected_g, disabled_g, ap_count;
wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
ConnectCommand_t last_connect;
int wake_machine_on;
void wake_on_wifi_update(int machine_on) { }
static int commands_tag, events_tag, semaphore_tag;
QueueHandle_t connect_commands = &commands_tag;
QueueHandle_t connect_events = &events_tag;
SemaphoreHandle_t connect_semaphore = &semaphore_tag;
#ifdef MODULE_HAS_SWITCH
radio_record_t radio_record;
radio_boot_t boot_mode;  // RADIO_BOOT_WAIT unless a case sets it; again after each run
int recorded = -1;       // the last radio_record_set(): 1 on, 0 off, -1 none in this run
void radio_record_set(radio_record_t *rec, bool on) { recorded = on; }
#else
int recorded = -1;
// The control module has no such call
void wifi_note_identify(void) { }
#endif

// The script
static ConnectCommand_t script[8];
static int script_n, script_i;
static command_buf_t reply_buf, request_buf, event_buf;
command_buf_t sent;
int sent_n;
int transmitted;
char trace[256];
const char *ended;
static jmp_buf done;
static int calls;
static int scanned = 3; // APs the scan finds
static uint8_t pending_event;
bool last_ap_reachable;
int start_fails;
int retry_timeouts; // the next idle waits with a timeout that run out, once each; 0 again after each run
int stale_disabled;
ConnectCommand_t queued;
int queued_count;

static void note(char c)
{
    size_t n = strlen(trace);
    if (n + 1 < sizeof(trace)) {
        trace[n] = c;
        trace[n + 1] = 0;
    }
}

static void stop(const char *why)
{
    ended = why;
    longjmp(done, 1);
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait)
{
    if (++calls > 2000) {
        stop("runaway");
    }
    if (q == connect_commands) {
        // A step of the script that is the Ultimate talking to the dispatcher: it answers
        // now, while the connector goes on with its commands
        while ((script_i < script_n) && (script[script_i].command >= CONNECTOR_IDENTIFY)) {
            uint8_t step = script[script_i++].command;
            memset(&request_buf, 0, sizeof(request_buf));
            if (step == CONNECTOR_ASK) {
                cmd_get_connection(&reply_buf);
            } else if (step == CONNECTOR_IDENTIFY) {
                cmd_identify(&request_buf);
            } else {
                ((rpc_header_t *)request_buf.data)->command = (step == CONNECTOR_SEND_DISABLE) ? CMD_WIFI_DISABLE : CMD_WIFI_ENABLE;
                cmd_wifi_endisable(&request_buf);
            }
        }
        if (script_i < script_n) {
            *(ConnectCommand_t *)item = script[script_i++];
            return pdTRUE;
        }
        if (wait == portMAX_DELAY) {
            stop("waiting for a command");
        }
        return pdFALSE;
    }
    // An event the stubs made happen: the association to a reachable AP, and the
    // disconnect that follows esp_wifi_disconnect() or a connect that found no AP
    if (pending_event) {
        ((ConnectEvent_t *)item)->event_code = pending_event;
        connected_g = (pending_event == EVENT_CONNECTED);
        if (connected_g) {
            note('K');
        }
        pending_event = 0;
        return pdTRUE;
    }
    if (wait == portMAX_DELAY) {
        stop("waiting for an event"); // no other connect attempt succeeds
    }
    return pdFALSE;
}

BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait)
{
    if (q != connect_commands) {
        return pdTRUE; // connect_events: the event handler's copy, nobody reads it
    }
    queued = *(const ConnectCommand_t *)item;
    queued_count++;
    if (script_n >= (int)(sizeof(script) / sizeof(script[0]))) {
        return pdFALSE;
    }
    script[script_n++] = queued;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    return pdTRUE;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait)
{
    if (script_i < script_n) {
        return pdTRUE;
    }
    if ((wait != portMAX_DELAY) && (retry_timeouts > 0)) {
        retry_timeouts--;
        return pdFALSE; // the retry timer ran out
    }
    stop((wait == portMAX_DELAY) ? "idle without a timeout" : "idle until the retry timer");
    return pdFALSE;
}

// The radio and the UART
#ifdef MODULE_HAS_SWITCH
// The U64 and U2+L modules start and stop the radio through radio_start() and radio_stop()
// (wifi/common) and nowhere else: a direct call fails and leaves a trace letter
esp_err_t esp_wifi_start(void) { note('!'); return ESP_FAIL; }
esp_err_t esp_wifi_stop(void) { note('!'); return ESP_FAIL; }
#else
esp_err_t esp_wifi_start(void) { note('T'); return ESP_OK; }
esp_err_t esp_wifi_stop(void) { note('P'); return ESP_OK; }
#endif
esp_err_t esp_wifi_disconnect(void) { note('D'); pending_event = EVENT_DISCONNECTED; return ESP_OK; }
int radio_start(void)
{
    if (start_fails > 0) {
        start_fails--;
        note('t');
        return ESP_FAIL;
    }
    note('T');
    return ESP_OK;
}
int radio_stop(void) { note('P'); return ESP_OK; }

int my_uart_get_buffer(int uart, command_buf_t **buf, int ticks)
{
    memset(&event_buf, 0xEE, sizeof(event_buf)); // what a reused buffer holds
    *buf = &event_buf;
    return 1;
}

BaseType_t my_uart_transmit_packet(uint8_t uart_num, command_buf_t *buf)
{
    if (buf != &event_buf) {
        transmitted++;
        if ((buf == &reply_buf) || (buf == &request_buf)) {
            note('R');
        }
        return pdTRUE;
    }
    sent = *buf;
    sent_n++;
    if (buf->data[0] == EVENT_DISABLED) {
        note('X');
#ifdef EVENT_RESTARTED
    } else if (buf->data[0] == EVENT_RESTARTED) {
        note('Y');
#endif
    } else if (buf->data[0] == EVENT_DISCONNECTED) {
        note('Z');
        if (disabled_g) {
            stale_disabled++;
        }
    }
    return pdTRUE;
}

// The connector's walks: the last AP and the stored ones are scripted, the scanned ones
// are tried through the real wifi_connect_to_scanned() and attempt_connect()
esp_err_t wifi_connect_to_last(void)
{
    note('L');
    if (!last_ap_reachable) {
        return ESP_FAIL;
    }
    last_ap_reachable = false;
    pending_event = EVENT_CONNECTED;
    return ESP_OK;
}
esp_err_t wifi_connect_to_stored(int index) { note('s'); return ESP_FAIL; }
esp_err_t wifi_connect_to_hotspot(void) { return ESP_FAIL; }
esp_err_t wifi_store_ap(ConnectCommand_t *cmd) { cmd->list_index = 0; return ESP_OK; }
esp_err_t wifi_set_last_ap(int index) { return ESP_OK; }
esp_err_t wifi_scan(ultimate_ap_records_t *rec) { note('S'); return ESP_OK; }
void wifi_list_aps(void)
{
    memset(ap_info, 0, sizeof(ap_info));
    ap_count = scanned;
    for (int i = 0; i < scanned; i++) {
        snprintf((char *)ap_info[i].ssid, sizeof(ap_info[i].ssid), "net%d", i);
        ap_info[i].authmode = WIFI_AUTH_WPA2_PSK;
    }
}
bool recovery_running(void) { return false; }
uint8_t wifi_get_connection(void) { return (uint8_t)connected_g; }

// Every SSID is known, with this password
esp_err_t wifi_find_password(const uint8_t *ssid, char *pw, size_t maxlen, int16_t *list_idx)
{
    strncpy(pw, "secret-pw", maxlen);
    *list_idx = 0;
    return ESP_OK;
}

// The air
air_ap_t air[AIR_MAX];
static int air_count;
wifi_config_t last_config;
int joined;

void air_reset(void)
{
    memset(air, 0, sizeof(air));
    air_count = 0;
    memset(&last_config, 0, sizeof(last_config));
    joined = -1;
    connected_g = 0;
    memset(&queued, 0, sizeof(queued));
    queued_count = 0;
    transmitted = 0;
    script_n = script_i = 0;
    calls = 0;
    memset(&last_connect, 0, sizeof(last_connect));
    memset(ap_info, 0, sizeof(ap_info));
    ap_count = 0;
    pending_event = 0;
}

int air_add(const char *ssid, uint8_t last_bssid_byte, uint8_t channel, int8_t rssi, wifi_auth_mode_t auth)
{
    air_ap_t *ap = &air[air_count];
    memset(ap, 0, sizeof(*ap));
    strncpy((char *)ap->ssid, ssid, 32);
    const uint8_t bssid[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, last_bssid_byte };
    memcpy(ap->bssid, bssid, 6);
    ap->channel = channel;
    ap->rssi = rssi;
    ap->auth = auth;
    return air_count++;
}

// What the driver's own scan list would hold, in the order given
void air_to_scan_list(const int *order, int n)
{
    memset(ap_info, 0, sizeof(ap_info));
    for (int i = 0; i < n; i++) {
        const air_ap_t *ap = &air[order[i]];
        memcpy(ap_info[i].bssid, ap->bssid, 6);
        memcpy(ap_info[i].ssid, ap->ssid, 32);
        ap_info[i].primary = ap->channel;
        ap_info[i].rssi = ap->rssi;
        ap_info[i].authmode = ap->auth;
    }
    ap_count = n;
}

static int matches(const wifi_sta_config_t *sta, const air_ap_t *ap)
{
    int8_t min_rssi = sta->threshold.rssi ? sta->threshold.rssi : -127;
    if (strncmp((const char *)sta->ssid, (const char *)ap->ssid, 32) != 0) {
        return 0;
    }
    if (sta->bssid_set && memcmp(sta->bssid, ap->bssid, 6) != 0) {
        return 0;
    }
    return (ap->rssi >= min_rssi) && (ap->auth >= sta->threshold.authmode);
}

// Is a better than b for this sort method; on a tie, scan order decides
static int better(const wifi_sta_config_t *sta, const air_ap_t *a, const air_ap_t *b)
{
    if (sta->sort_method == WIFI_CONNECT_AP_BY_SECURITY && a->auth != b->auth) {
        return a->auth > b->auth;
    }
    return a->rssi > b->rssi;
}

static int pick(const wifi_sta_config_t *sta)
{
    int order[14];
    int n = 0;
    if ((sta->channel >= 1) && (sta->channel <= 13)) {
        order[n++] = sta->channel;
    }
    for (int ch = 1; ch <= 13; ch++) {
        if (ch != sta->channel) {
            order[n++] = ch;
        }
    }
    int best = -1;
    for (int c = 0; c < n; c++) {
        for (int i = 0; i < air_count; i++) {
            if ((air[i].channel != order[c]) || !matches(sta, &air[i])) {
                continue;
            }
            if (sta->scan_method == WIFI_FAST_SCAN) {
                return i;
            }
            if ((best < 0) || better(sta, &air[i], &air[best])) {
                best = i;
            }
        }
    }
    return best;
}

esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf)
{
    last_config = *conf;
    return ESP_OK;
}

// The connect is accepted; the outcome comes as an event
esp_err_t esp_wifi_connect(void)
{
    note('c');
    joined = pick(&last_config.sta);
    pending_event = (joined >= 0) ? EVENT_CONNECTED : EVENT_DISCONNECTED;
    return ESP_OK;
}

void run_with(int aps_found, bool disconnected_before, const uint8_t *commands, int n)
{
    scanned = aps_found;
    air_reset();
    memset(script, 0, sizeof(script));
    for (int i = 0; i < n; i++) {
        script[i].command = commands[i];
        script[i].buf = &reply_buf;
    }
    script_n = n;
    trace[0] = 0;
    ended = "";
    calls = 0;
    connected_g = ap_count = 0;
    disabled_g = 0;
#ifdef MODULE_HAS_SWITCH
    disabled_g = 1; // as the module starts: radio off, until the Ultimate asks for it
    recorded = -1;
#endif
    stale_disabled = 0;
    sent_n = 0;
    user_disconnected = disconnected_before;
    off_asked = false;
    if (setjmp(done) == 0) {
        connect_thread(NULL);
    }
    last_ap_reachable = false;
    start_fails = 0;
    retry_timeouts = 0;
#ifdef MODULE_HAS_SWITCH
    boot_mode = RADIO_BOOT_WAIT;
#endif
}

void run(bool disconnected_before, const uint8_t *commands, int n)
{
    run_with(3, disconnected_before, commands, n);
}

const char *after(char c)
{
    const char *p = strchr(trace, c);
    return p ? p + 1 : "";
}
