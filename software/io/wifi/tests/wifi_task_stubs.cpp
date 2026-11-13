/*
 * wifi_task_stubs.cpp
 *
 * Recording stubs for everything the extracted WiFi application code calls, and the
 * scripted event source. See wifi_task_test.h for what a case file uses. The tests are
 * registered by the case files (software/io/usb/tests/host_test_main.cpp runs them).
 */

#include <setjmp.h>
#include "wifi_task_test.h"

World world;
WiFi wifi;
NetworkLWIP_WiFi net(NULL, NULL, NULL);
ultimate_ap_records_t wifi_aps;

char rlog[256];
char messages[256];
const char *ended = "";
TickType_t now;
int first_wait_state;
static jmp_buf done;
static DmaUART uart;
static command_buf_context_t packets;
static int calls;
static int waits;

static void note(char c)
{
    size_t n = strlen(rlog);
    if (n + 1 < sizeof(rlog)) {
        rlog[n] = c;
        rlog[n + 1] = 0;
    }
}

// The task ends here: it waits for an event that does not come, or sleeps for good
static void stop(const char *why)
{
    ended = why;
    longjmp(done, 1);
}

// Every stub the task can loop through counts, so that a defect that loops ends the case
static void count(void)
{
    if (++calls > 200) {
        stop("runaway");
    }
}

int saves;
bool saved_enabled;

// The WiFi settings store: a change of "WiFi Enabled" runs its hook, as ConfigItem :: setChanged does
static ConfigStore store;
static ConfigManager manager;
ConfigManager *ConfigManager :: getConfigManager(void) { return &manager; }
ConfigStore *ConfigManager :: find_store(uint32_t page_id) { return (page_id == 0x57494649) ? &store : NULL; }
void ConfigStore :: set_value(uint8_t id, int value)
{
    if (id == CFG_WIFI_ENABLE) {
        world.enabled = (value != 0);
#if U64 != 2
        ConfigItem it;
        it.value = value;
        NetworkLWIP_WiFi :: wifi_enable_change(&it);
#endif
    }
}
int ConfigStore :: get_value(uint8_t id) { return (id == CFG_WIFI_ENABLE) ? (world.enabled ? 1 : 0) : 0; }
void ConfigStore :: write(void) { saves++; saved_enabled = world.enabled; }

void reset_world(void)
{
    saves = 0;
    saved_enabled = false;
    memset(&world, 0, sizeof(world));
    esp32.uart = &uart; // for a request made outside a script
    requests = 0;
    world.voltages_vbus = 12000; // above the low input voltage warning (U64=2)
    world.pick_ssid = "HomeNet";
    static const uint8_t bssid[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
    memcpy(world.pick_bssid, bssid, 6);
    world.pick_channel = 6;
    world.password = "";
    string_box_result = 1;
    world.popup_answer = BUTTON_YES;
    popups = 0;
}

void check_failed(const char *what, const char *file, int line)
{
    char msg[512];
    snprintf(msg, sizeof(msg), "%s:%d: %s (log \"%s\", messages \"%s\", ended: %s)", file, line, what, rlog, messages, ended);
    host_test::record_failure(msg, false);
}

// The queue of the task: the script, and what the task queues itself
#define ACT 0x7777 // marks an action of the user in the queue (its bufnr)
static command_buf_t queue[16];
static int queue_n, queue_i;
static command_buf_t pool[8];
static int pool_i;
TickType_t waited;
// The script is done and the task waits only at intervals (it asks for a radio off that the
// module never confirms): the case ends once it has waited this long with nothing to come
static TickType_t quiet;
TickType_t timed_waits[8];
int timed_waits_n;
TickType_t message_at;
static TickType_t queue_delay[16];
int requests;
command_buf_t request;
int string_box_result = 1;
int popups;
static command_buf_t txbuf, replybuf;

TickType_t xTaskGetTickCount(void) { return now; }
// The task that runs: the WiFi task while boot() runs it, else another one (the user
// interface, the REST API); an action of the script runs as another task
static int wifi_task_id, other_task_id;
static TaskHandle_t current_task = &other_task_id;
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return current_task; }
void vTaskDelay(TickType_t t) { stop("delayed"); }
void vTaskSuspend(TaskHandle_t t) { stop("suspended"); }

void DmaUART :: ModuleCtrl(int mode) { if (mode == ESP_MODE_OFF) note('F'); }
// The UART refills a buffer that is given back, so a read after it finds anything
int frees; // buffers given back to the UART
void DmaUART :: FreeBuffer(command_buf_t *buf) { frees++; memset(buf->data, 0xA5, sizeof(buf->data)); buf->size = -1; }
BaseType_t DmaUART :: GetBuffer(command_buf_t **b, TickType_t t)
{
    if (world.no_tx_buffer) {
        return pdFALSE;
    }
    memset(&txbuf, 0xAA, sizeof(txbuf)); // a request has to set every field it sends
    *b = &txbuf;
    return pdTRUE;
}

void UserInterface :: postMessage(const char *msg)
{
    note('!');
    message_at = now;
    strncat(messages, msg, sizeof(messages) - strlen(messages) - 2);
    strcat(messages, "\n");
}

int UserInterface :: popup(const char *msg, uint8_t flags)
{
    note('?');
    popups++;
    now += world.popup_ticks;
    return world.popup_answer;
}

void UserInterface :: activate_uiobject(TreeBrowser *tb) { note('T'); }

SubsysResultCode_e NetworkLWIP_WiFi :: manual_connect(SubsysCommand *cmd) { note('k'); return SSRET_OK; }
int wifi_wifi_autoconnect() { note('a'); return 0; }

int UserInterface :: string_box(const char *msg, char *buffer, int maxlen)
{
    strncpy(buffer, world.password, maxlen);
    return string_box_result;
}
void BrowsableWifiAP :: fetch_context_items(IndexedList<Action *> &items) {}

static void arrive(const command_buf_t *b);

// The request channel: wifi_wifi_connect (cut out of wifi_cmd.cc) sends through these. The
// request is kept as it went out, and answered as the case says.
const char *no_wifi_buf = "No buffer\n";
const char *no_wifi_slot = "No slot\n";
int wifi_reply_register(rpc_header_t *hdr) { return 1; }
command_buf_t *wifi_transmit(command_buf_t *buf, int timeout_ms)
{
    requests++;
    request = *buf;
    if (world.joined_before_reply) {
        // the module joins the AP and reports it before its reply to the request reaches
        // the Ultimate
        command_buf_t ev;
        memset(&ev, 0, sizeof(ev));
        ((rpc_header_t *)ev.data)->command = EVENT_CONNECTED;
        strncpy((char *)((event_pkt_connected *)ev.data)->ssid, ((rpc_wifi_connect_req *)buf->data)->ssid, 32);
        ev.size = sizeof(event_pkt_connected);
        arrive(&ev);
    }
    if (world.connect_noreply) {
        return NULL;
    }
    memset(&replybuf, 0, sizeof(replybuf));
    ((rpc_espcmd_resp *)replybuf.data)->esp_err = world.connect_result;
    return &replybuf;
}

BaseType_t cmd_buffer_get(command_buf_context_t *context, command_buf_t **b, TickType_t t)
{
    *b = &pool[pool_i++ % 8];
    memset(*b, 0, sizeof(**b));
    return pdTRUE;
}

// What has arrived in the task's queue (its own events, and the module's during a request)
// is the first `arrived` entries from queue_i. A new arrival goes behind them, as the RTOS
// queue appends; the steps of the script after them are still to come.
static int arrived;
static void arrive(const command_buf_t *b)
{
    int at = queue_i + arrived;
    memmove(&queue[at + 1], &queue[at], (queue_n - at) * sizeof(queue[0]));
    memmove(&queue_delay[at + 1], &queue_delay[at], (queue_n - at) * sizeof(queue_delay[0]));
    queue[at] = *b;
    queue_delay[at] = 0;
    queue_n++;
    arrived++;
}

BaseType_t cmd_buffer_loopback(command_buf_context_t *context, command_buf_t *b)
{
    if (world.loopback_fails) {
        return pdFALSE;
    }
    if ((world.loopback_room > 0) && (--world.loopback_room == 0)) {
        world.loopback_fails = true; // that was the last one that fitted
    }
    arrive(b);
    return pdTRUE;
}

BaseType_t cmd_buffer_received(command_buf_context_t *context, command_buf_t **b, TickType_t t)
{
    count();
    if (++waits == 1) {
        first_wait_state = wifi.getState();
    }
    if (t != portMAX_DELAY && timed_waits_n < 8) {
        timed_waits[timed_waits_n++] = t;
    }
    if (queue_i < queue_n && queue[queue_i].bufnr == ACT) {
        // An action comes after its delay (late()), as an event does; the rest of the wait
        // is left for what follows it
        TickType_t d = queue_delay[queue_i];
        if (d != 0 && t != portMAX_DELAY && d >= t) {
            queue_delay[queue_i] -= t; // the wait ran out first
            now += t;
            waited += t;
            return pdFALSE;
        }
        now += d;
        waited += d;
        if (t != portMAX_DELAY) {
            t -= d;
        }
        void (*act)(void) = (void (*)(void))queue[queue_i++].object;
        current_task = &other_task_id;
        act();
        current_task = &wifi_task_id;
    }
    if (queue_i < queue_n) {
        TickType_t d = queue_delay[queue_i];
        if (d == 0 || t == portMAX_DELAY || d < t) {
            now += d; // the time it waited for this one
            waited += d;
            if (arrived) {
                arrived--;
            }
            *b = &queue[queue_i++];
            quiet = 0;
            return pdTRUE;
        }
        queue_delay[queue_i] -= t; // the wait ran out first
    }
    if (t == portMAX_DELAY) {
        stop("waiting for an event");
    }
    now += t; // the time it waited, with nothing coming
    waited += t;
    if (queue_i >= queue_n) {
        quiet += t;
        if (quiet > QUIET_TICKS) {
            stop("asking at intervals");
        }
    }
    return pdFALSE;
}

// The firmware's printf and puts (small_printf.cc, linked as fw_printf and fw_puts) write here
extern "C" void outbyte(int c) { putchar(c); }

void wifi_command_init(void) {}
Esp32 :: Esp32() : uart(NULL) {}
Esp32 esp32;
void Esp32 :: AttachApplication(Esp32Application *app) {}
void Esp32 :: StartApp() { note('B'); }
void Esp32 :: StopApp() { note('A'); wifi.state = eWifi_Off; }
#ifndef U64
// The U2+L finds its module by its boot message
int Esp32 :: DetectModule(void *buffer, int len) { note('D'); strcpy((char *)buffer, "ESP-ROM:esp32c3-api1-20210207"); return 29; }
void Esp32 :: EnableRunMode(void) { note('E'); }
#endif

BaseType_t wifi_detect(uint16_t *major, uint16_t *minor, char *str, int maxlen) { count(); note('d'); strcpy(str, "test"); return pdTRUE; }
int wifi_getmac(uint8_t *mac) { note('m'); return 0; }
int wifi_scan(void *) { count(); note('C'); return 0; }
int wifi_enable()
{
    note('N');
    if (world.enable_answer_n < 0) {
        return 0; // the requests that fail are done
    }
    if ((world.enable_answer_n > 0) && (--world.enable_answer_n == 0)) {
        world.enable_answer_n = -1;
    }
    return world.enable_answer;
}
int wifi_disable()
{
    note('O');
    void (*during)(void) = world.during_disable;
    if (during) {
        world.during_disable = NULL;
        TaskHandle_t caller = current_task;
        current_task = &other_task_id;
        during();
        current_task = caller;
    }
    return world.disable_answer;
}
int wifi_modem_enable(bool on) { note(on ? 'M' : 'V'); return 0; }
int wifi_get_voltages(voltages_t *v)
{
    if (world.voltages_result == 0) {
        v->vbus = world.voltages_vbus;
    }
    return world.voltages_result;
}
int wifi_is_connected(uint8_t &status) { count(); note('Q'); status = world.link; return 0; }

err_t wifi_tx_packet(void *driver, void *buffer, int length) { return 0; }
void wifi_free(void *driver, void *buffer) {}

// As NetworkInterface :: start, it applies the settings (effectuate_settings)
void NetworkLWIP_WiFi :: start(void) { note('s'); effectuate_settings(); }
void NetworkLWIP_WiFi :: link_up(void) { note('U'); }
void NetworkLWIP_WiFi :: link_down(void) { note('u'); }
bool NetworkLWIP_WiFi :: enabled_in_config(void) { return world.enabled; }

void U64Config :: pushPowerOnMode(void) {}
void U64Config :: pushWakeOnWifi(void) {}
void U64Config :: pushRecoveryUrl(void) {}

void WiFi :: RefreshRoot() {}
int ap_refreshes;
int aps_at_refresh;
void WiFi :: RefreshAPs() { ap_refreshes++; aps_at_refresh = wifi_aps.num_records; }
void WiFi :: Start() {}
void WiFi :: Terminate() {}

void boot(std::initializer_list<Step> steps)
{
#if U64 != 2
    if (!world.keep_switch) {
        memset(&wifi.radio_switch, 0, sizeof(wifi.radio_switch));
    }
#endif
    world.keep_switch = false;
    wifi.connect_attempt = 0; // the attempts of a fresh start (the script's EVENT_CONNECT_PENDING is 1)
    wifi.connect_answered = 0;
    rlog[0] = 0;
    messages[0] = 0;
    ended = "";
    calls = 0;
    waits = 0;
    now = 1000;
    waited = 0;
    quiet = 0;
    timed_waits_n = 0;
    message_at = 0;
    ap_refreshes = 0;
    aps_at_refresh = -1;
    requests = 0;
    pool_i = 0;
    queue_n = queue_i = 0;
    arrived = 0;
    for (std::initializer_list<Step>::const_iterator s = steps.begin(); s != steps.end(); ++s) {
        command_buf_t *b = &queue[queue_n++];
        memset(b, 0, sizeof(*b));
        queue_delay[queue_n - 1] = s->delay;
        if (s->act) {
            b->bufnr = ACT;
            b->object = (void *)s->act;
            continue;
        }
        ((rpc_header_t *)b->data)->command = s->code;
        b->size = sizeof(rpc_header_t);
        if (s->code == EVENT_CONNECT_PENDING) {
            ((rpc_header_t *)b->data)->sequence = 1; // the first attempt (WiFi :: ConnectStart)
        }
        if (s->code == EVENT_CONNECTED || s->code == EVENT_CONNECT_PENDING) {
            // the module and sendConnectPending send the whole structure, with or without a name in it (wifi_event_size.h)
            if (s->ssid) {
                strncpy((char *)((event_pkt_connected *)b->data)->ssid, s->ssid, 32);
            }
            b->size = sizeof(event_pkt_connected);
        } else if (s->code == EVENT_GOTIP) {
            b->size = sizeof(event_pkt_got_ip); // the module sends it whole
        } else if (s->code == EVENT_DISCONNECTED && s->reason >= 0) {
            ((event_pkt_disconnected *)b->data)->reason = s->reason;
            b->size = sizeof(event_pkt_disconnected);
        } else if (s->code == EVENT_DISCONNECTED) {
            b->data[sizeof(rpc_header_t)] = 15; // a bare header: what follows it is no reason
        }
    }
    wifi.netstack = &net;
    wifi.uart = &uart;
    wifi.packets = &packets;
    esp32.uart = &uart;
    wifi.runModeTask = &wifi_task_id;
    current_task = &wifi_task_id;
    if (setjmp(done) == 0) {
        wifi.RunModeThread();
    }
    current_task = &other_task_id;
}

int take_queued(int code)
{
    int n = 0;
    for (int i = queue_i; i < queue_n; i++) {
        if ((queue[i].bufnr != ACT) && (((rpc_header_t *)queue[i].data)->command == code)) {
            n++;
        }
    }
    queue_i = queue_n = arrived = 0;
    return n;
}

const char *after(char c)
{
    const char *p = strchr(rlog, c);
    return p ? p + 1 : "";
}

void pick(void)
{
    BrowsableWifiAP ap(NULL, (char *)world.pick_ssid, -50, world.pick_auth, world.pick_bssid, world.pick_channel);
    UserInterface ui;
    SubsysCommand cmd;
    cmd.functionID = (intptr_t)&ap;
    cmd.user_interface = &ui;
    BrowsableWifiAP :: connect_ap(&cmd);
}
