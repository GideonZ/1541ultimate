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
    world.voltages_vbus = 12000; // above the low input voltage warning (U64=2)
    world.pick_ssid = "HomeNet";
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
int requests;
command_buf_t request;
int string_box_result = 1;
int popups;
static command_buf_t txbuf, replybuf;

TickType_t xTaskGetTickCount(void) { return now; }
void vTaskDelay(TickType_t t) { stop("delayed"); }
void vTaskSuspend(TaskHandle_t t) { stop("suspended"); }

void DmaUART :: ModuleCtrl(int mode) { if (mode == ESP_MODE_OFF) note('F'); }
// The UART refills a buffer that is given back, so a read after it finds anything
void DmaUART :: FreeBuffer(command_buf_t *buf) { memset(buf->data, 0xA5, sizeof(buf->data)); buf->size = -1; }
BaseType_t DmaUART :: GetBuffer(command_buf_t **b, TickType_t t)
{
    if (world.no_tx_buffer) {
        return pdFALSE;
    }
    memset(&txbuf, 0, sizeof(txbuf));
    *b = &txbuf;
    return pdTRUE;
}

void UserInterface :: postMessage(const char *msg)
{
    note('!');
    strncat(messages, msg, sizeof(messages) - strlen(messages) - 2);
    strcat(messages, "\n");
}

int UserInterface :: popup(const char *msg, uint8_t flags)
{
    note('?');
    popups++;
    return world.popup_answer;
}

void UserInterface :: activate_uiobject(TreeBrowser *tb) { note('T'); }

SubsysResultCode_e NetworkLWIP_WiFi :: manual_connect(SubsysCommand *cmd) { note('k'); return SSRET_OK; }
int wifi_wifi_autoconnect() { note('a'); return 0; }

int UserInterface :: string_box(const char *msg, char *buffer, int maxlen) { buffer[0] = 0; return string_box_result; }
void BrowsableWifiAP :: fetch_context_items(IndexedList<Action *> &items) {}

// The request channel: wifi_wifi_connect (cut out of wifi_cmd.cc) sends through these. The
// request is kept as it went out, and answered as the case says.
const char *no_wifi_buf = "No buffer\n";
const char *no_wifi_slot = "No slot\n";
int wifi_reply_register(rpc_header_t *hdr) { return 1; }
command_buf_t *wifi_transmit(command_buf_t *buf, int timeout_ms)
{
    requests++;
    request = *buf;
    if (world.connect_noreply) {
        return NULL;
    }
    memset(&replybuf, 0, sizeof(replybuf));
    ((rpc_espcmd_resp *)replybuf.data)->esp_err = world.connect_result;
    return &replybuf;
}

// The next to be delivered; the others were queued before it
BaseType_t cmd_buffer_get(command_buf_context_t *context, command_buf_t **b, TickType_t t)
{
    *b = &pool[pool_i++ % 8];
    memset(*b, 0, sizeof(**b));
    return pdTRUE;
}

BaseType_t cmd_buffer_loopback(command_buf_context_t *context, command_buf_t *b)
{
    if (world.loopback_fails) {
        return pdFALSE;
    }
    memmove(&queue[queue_i + 1], &queue[queue_i], (queue_n - queue_i) * sizeof(queue[0]));
    queue[queue_i] = *b;
    queue_n++;
    return pdTRUE;
}

BaseType_t cmd_buffer_received(command_buf_context_t *context, command_buf_t **b, TickType_t t)
{
    count();
    if (++waits == 1) {
        first_wait_state = wifi.getState();
    }
    if (queue_i < queue_n && queue[queue_i].bufnr == ACT) {
        void (*act)(void) = (void (*)(void))queue[queue_i++].object;
        act();
    }
    if (queue_i < queue_n) {
        *b = &queue[queue_i++];
        return pdTRUE;
    }
    if (t == portMAX_DELAY) {
        stop("waiting for an event");
    }
    now += t; // the time it waited, with nothing coming
    waited += t;
    return pdFALSE;
}

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
int wifi_enable() { note('N'); return world.enable_answer; }
int wifi_disable() { note('O'); return world.disable_answer; }
int wifi_modem_enable(bool) { note('M'); return 0; }
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

void NetworkLWIP_WiFi :: start(void) { note('s'); }
void NetworkLWIP_WiFi :: link_up(void) { note('U'); }
void NetworkLWIP_WiFi :: link_down(void) { note('u'); }
bool NetworkLWIP_WiFi :: enabled_in_config(void) { return world.enabled; }

void U64Config :: pushPowerOnMode(void) {}
void U64Config :: pushWakeOnWifi(void) {}
void U64Config :: pushRecoveryUrl(void) {}

void WiFi :: RefreshRoot() {}
void WiFi :: RefreshAPs() {}
void WiFi :: Start() {}
void WiFi :: Terminate() {}

static DmaUART uart;
static command_buf_context_t packets;

void boot(std::initializer_list<Step> steps)
{
#if U64 != 2
    if (!world.keep_switch) {
        memset(&wifi.radio_switch, 0, sizeof(wifi.radio_switch));
    }
#endif
    world.keep_switch = false;
    rlog[0] = 0;
    messages[0] = 0;
    ended = "";
    calls = 0;
    waits = 0;
    now = 1000;
    waited = 0;
    requests = 0;
    pool_i = 0;
    queue_n = queue_i = 0;
    for (std::initializer_list<Step>::const_iterator s = steps.begin(); s != steps.end(); ++s) {
        command_buf_t *b = &queue[queue_n++];
        memset(b, 0, sizeof(*b));
        if (s->act) {
            b->bufnr = ACT;
            b->object = (void *)s->act;
            continue;
        }
        ((rpc_header_t *)b->data)->command = s->code;
        b->size = sizeof(rpc_header_t);
        if (s->code == EVENT_CONNECTED && s->ssid) {
            strncpy((char *)((event_pkt_connected *)b->data)->ssid, s->ssid, 32);
            b->size = sizeof(event_pkt_connected);
        }
    }
    wifi.netstack = &net;
    wifi.uart = &uart;
    wifi.packets = &packets;
    esp32.uart = &uart;
    if (setjmp(done) == 0) {
        wifi.RunModeThread();
    }
}

const char *after(char c)
{
    const char *p = strchr(rlog, c);
    return p ? p + 1 : "";
}

void pick(void)
{
    BrowsableWifiAP ap(NULL, (char *)world.pick_ssid, -50, world.pick_auth);
    UserInterface ui;
    SubsysCommand cmd;
    cmd.functionID = (intptr_t)&ap;
    cmd.user_interface = &ui;
    BrowsableWifiAP :: connect_ap(&cmd);
}
