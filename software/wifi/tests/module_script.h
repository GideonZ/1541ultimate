/*
 * module_script.h
 *
 * The scripted world of module_stubs.c as the cases (C++) see it: run the connector with a
 * list of commands, then look at the trace of what it did and why it stopped. Trace
 * letters: L connect to the last AP, S scan, c connect to a scanned AP, s connect to a
 * stored AP, P radio stopped, T radio started, t a radio start that failed (start_fails),
 * D disconnect, X EVENT_DISABLED sent, Z EVENT_DISCONNECTED sent, Y EVENT_RESTARTED sent,
 * R reply sent, K the AP associated (a connect event).
 */

#ifndef MODULE_SCRIPT_H_
#define MODULE_SCRIPT_H_

#include "module_env.h"
#ifdef __cplusplus
#include "host_test/host_test.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern char trace[256];
extern const char *ended;
extern int stale_disabled;     // EVENT_DISCONNECTED sent while disabled_g still said off
extern bool last_ap_reachable; // the next run's connect to the last AP succeeds (false again
                               // after each run); every other connect attempt fails
extern int start_fails;        // the next radio_start() calls fail; 0 again after each run
extern int retry_timeouts;     // the next idle waits with a timeout run out, one each (the retry timer); 0 again after each run
extern int recorded;           // the last radio_record_set() of a run (1 on, 0 off, -1 none)
extern uint8_t nvs_wifi_enabled; // u64ctrl: the module's copy of "WiFi Enabled" (power_get_wifi_enabled), 1 again after each run
extern uint8_t nvs_wifi_last;    // what the copy read when the run ended
extern int nvs_wifi_writes;      // power_set_wifi_enabled() calls of a run
extern char boot_trace[16];      // u64ctrl: what wifi_start_at_boot() did before the connector ran
extern bool recovery_active;     // recovery_running() answers this (u64ctrl)
extern struct netif sta_netif;   // u64ctrl: the station netif; lwIP attaches its slots at the radio start (netif_attach)
void netif_attach(void);
extern netif_input_fn default_input;
extern netif_output_fn default_output;
err_t lwip_input(struct pbuf *p, struct netif *inp);

// In a command list: not a command for the connector, but ...
#define CONNECTOR_ASK          0xFE // the Ultimate asking for the connection state (cmd_get_connection)
#define CONNECTOR_SEND_DISABLE 0xFD // CMD_WIFI_DISABLE through the dispatcher (cmd_wifi_endisable)
#define CONNECTOR_SEND_ENABLE  0xFC // CMD_WIFI_ENABLE through the dispatcher
#define CONNECTOR_IDENTIFY     0xFB // CMD_IDENTIFY through the dispatcher (cmd_identify)

// Runs connect_thread() from its first state until it waits for something the script does
// not give it. aps_found is what a scan finds; disconnected_before stands for an explicit
// Disconnect from the user before the commands; the commands are taken one by one wherever
// the connector looks for one.
void run_with(int aps_found, bool disconnected_before, const uint8_t *commands, int n);
void run(bool disconnected_before, const uint8_t *commands, int n);

// The trace after the first occurrence of c
const char *after(char c);

// The scripted air and the dispatcher, for the AP selection cases
#define AIR_MAX 16
typedef struct {
    uint8_t ssid[33];
    uint8_t bssid[6];
    uint8_t channel;
    int8_t rssi;
    wifi_auth_mode_t auth;
} air_ap_t;
extern air_ap_t air[AIR_MAX];
extern wifi_config_t last_config; // the last station config set
extern int joined;                // index into air[] of the AP that config made the station join, -1 none
extern ConnectCommand_t queued;   // the last command the dispatcher queued for the connector
extern int queued_count;
extern int transmitted;           // packets sent to the Ultimate
void air_reset(void);
int air_add(const char *ssid, uint8_t last_bssid_byte, uint8_t channel, int8_t rssi, wifi_auth_mode_t auth);
void air_to_scan_list(const int *order, int n);

// The Ultimate's packets the module sent on its own (events)
extern command_buf_t sent;
extern int sent_n;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
// A check that evaluates its condition once, and prints the trace of the run when it fails
#define CHECK(cond, what) do { \
    bool check_ok_ = (cond); \
    if (!check_ok_) { \
        fprintf(stderr, "  check \"%s\" (%s), trace \"%s\", ended: %s\n", what, #cond, trace, ended); \
    } \
    EXPECT_TRUE(check_ok_); \
} while (0)
#endif

#endif /* MODULE_SCRIPT_H_ */
