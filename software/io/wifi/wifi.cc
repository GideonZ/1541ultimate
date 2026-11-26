/*
 * wifi.cc
 *
 *  Created on: Sep 2, 2018
 *      Author: gideon
 */

#if U64 == 2
#include "provider_manager_uart.h"
#ifdef ULTIMATE_PROVIDER_JOURNAL
#include "provider_native_startup.h"
#endif
#endif
#include "wifi.h"
#include "provider_wifi_ingress.h"
#include "wifi_connect.h"
#include "socket.h"
#include "u64.h"
#include "dump_hex.h"
#include <stdarg.h>
#include "userinterface.h"
#include "rpc_calls.h"
#include "filemanager.h"
#include "network_esp32.h"
#if U64 == 2
#include "u64_config.h"
#endif
#include "init_function.h"

#define DEBUG_INPUT  0
#define EVENT_START  0xF1
#define EVENT_TERM   0xF2

WiFi wifi;
bool wifi_provider_receive(command_buf_t *buffer)
{
    if (wifi.ReceiveProviderIdentity(buffer) || wifi.ReceiveProviderControl(buffer)) return true;
    const rpc_header_t *header = (const rpc_header_t *)buffer->data;
    if (header->command == EVENT_DISCONNECTED || header->command == EVENT_DISABLED)
        WiFi::ProviderLinkLoss();
    return false;
}
ultimate_ap_records_t wifi_aps;
InitFunction wifi_init("WiFi Application", WiFi::Init, NULL, NULL, 52); // after LWIP

WiFi :: WiFi()
{
    runModeTask = NULL;
    provider_timer = NULL;
    provider_monitor_enabled = false;
#if U64 == 2
    provider_manager = NULL;
#endif
    uart = NULL;
    state = eWifi_Off;
    state_before_scan = eWifi_NotConnected;
    event_when_enabled = 0;
    event_when_enabled_by = 0;
    pick_cancelled = false;
    connect_attempt = 0;
    connect_answered = 0;
    connect_answer = 0;
    connect_answer_at = 0;
    bzero(my_mac, 6);
    wifi_aps.num_records  = 0;
    my_ip = 0;
    my_gateway = 0;
    my_netmask = 0;
#if U64 != 2
    memset(&radio_switch, 0, sizeof(radio_switch));
    radio_setting_due = false;
    radio_setting_applied = -1;
#endif
}

void WiFi :: Init(void *obj, void *param)
{
    esp32.AttachApplication(&wifi);
    wifi.netstack = new NetworkLWIP_WiFi(&wifi, wifi_tx_packet, wifi_free);
    wifi.netstack->attach_config();

    // Unconditionally enable the WiFi module: it always runs, also with "WiFi Enabled"
    // stored as Disabled, which means its radio is off. On the U64 and U2+L the
    // application tells the module, which starts with its radio off, to turn it on or not.
    wifi.Enable();
    wifi.netstack->effectuate_registered_settings();
}

void WiFi :: RefreshRoot()
{
    // Quick hack to refresh the screen
    FileManager :: getFileManager() -> sendEventToObservers(eRefreshDirectory, "/", "");    
}

void WiFi :: RefreshAPs()
{
    FileManager :: getFileManager() -> sendEventToObservers(eRefreshDirectory, "/WiFi", "");    
}

void WiFi :: Enable()
{
    esp32.StartApp(); 
}

void WiFi :: Disable()
{
    esp32.StopApp();
}

// On the U64 and U2+L, RadioOn and RadioOff run in the caller (the "WiFi Enabled" hook,
// network_esp32.cc) and only mark the setting for the WiFi task to apply, and wake it
// (EVENT_RADIO_SETTING); the task sends the request and waits for its confirmation
// (wifi_switch.h). The mark is what counts: if the wake does not fit in the task's queue,
// the queue is full, and the task's next pass, which comes soon, applies the setting.
void WiFi :: RadioOn()
{
#if U64 != 2
    if (state == eWifi_Off) {
        // The application is held off with a module without the switch (or not running):
        // it is started again, and reads the setting, Enabled, when it finds the module
        Enable();
        return;
    }
    radio_setting_due = true;
    sendEvent(EVENT_RADIO_SETTING);
#else
    WhenEnabledFromNow();
    if (wifi_enable() != 0) {
        event_when_enabled = 0; // the radio does not come on: nothing is to follow it
    }
#endif
}

void WiFi :: RadioOff()
{
    // The module may still report the failure of a pick that was under way (its refusal can
    // come before it confirms the radio off): the user has left that, so nothing is said
    pick_cancelled = true;
#if U64 != 2
    radio_setting_due = true;
    sendEvent(EVENT_RADIO_SETTING);
#else
    wifi_disable();
#endif
}

BaseType_t wifi_rx_isr(command_buf_context_t *context, command_buf_t *buf, BaseType_t *w);

void WiFi::InvalidateProvider()
{
    portENTER_CRITICAL();
    ProviderLinkLoss();
    portEXIT_CRITICAL();
}

void WiFi::ProviderLinkLoss()
{
    wifi.provider_lifecycle.invalidate();
#if U64 == 2
    if (wifi.provider_manager) wifi.provider_manager->invalidate_isr();
#endif
}

#if U64 == 2
bool WiFi::BindProviderManager(ProviderManagerUart& link)
{
    portENTER_CRITICAL();
    bool accepted = !provider_manager && uart && link.bind_uart(uart);
    if (accepted) provider_manager = &link;
    portEXIT_CRITICAL();
    return accepted;
}

bool WiFi::ProviderSnapshot(provider_session::Identity& boot, OperationCancellation& lifetime)
{
    portENTER_CRITICAL();
    bool available = provider_lifecycle.snapshot(boot.bytes, lifetime);
    portEXIT_CRITICAL();
    return available;
}
bool WiFi::ProviderTransportReady()
{
    portENTER_CRITICAL();
    bool ready = provider_monitor_enabled;
    portEXIT_CRITICAL();
    return ready;
}
#endif

bool WiFi::ReceiveProviderControl(command_buf_t *buf)
{
#if U64 == 2
    if (buf->size < 2 || buf->data[0] != CMD_IDENTIFY || (buf->data[1] != 0xfe && buf->data[1] != 0xfd)) return false;
    if (provider_manager && buf->size <= int(sizeof(buf->data)))
        provider_manager->receive_isr(buf->data, buf->size);
    return true;
#else
    return false;
#endif
}

void WiFi::PollProvider(TimerHandle_t timer)
{
    WiFi *self = static_cast<WiFi *>(pvTimerGetTimerID(timer));
    // Serialize against receive IRQ and Terminate. Every queue operation here
    // has a zero wait; the timer task never runs a synchronous Wi-Fi RPC.
    portENTER_CRITICAL();
    if (self->provider_monitor_enabled) {
        uint32_t now = uint32_t(xTaskGetTickCount() * portTICK_PERIOD_MS);
        uint8_t request[PROVIDER_PROBE_SIZE];
        if (self->provider_lifecycle.begin(now, request)) {
            command_buf_t *buf;
            if (self->uart->GetBuffer(&buf, 0) == pdTRUE) {
                memcpy(buf->data, request, sizeof(request));
                buf->size = sizeof(request);
                if (self->uart->TryTransmitPacket(buf) != pdTRUE) self->provider_lifecycle.invalidate();
            } else {
                self->provider_lifecycle.invalidate();
            }
        }
    }
    portEXIT_CRITICAL();
}

bool WiFi::ReceiveProviderIdentity(command_buf_t *buf)
{
    // Called only by the receive ISR. Normal per-task identify replies retain
    // their existing route; this reserved thread has no task notification slot.
    if (buf->size < 4 || buf->data[0] != CMD_IDENTIFY || buf->data[1] != 0xff) return false;
    if (provider_monitor_enabled && buf->size <= int(sizeof(buf->data))) {
        provider_lifecycle.receive(buf->data, buf->size,
            uint32_t(xTaskGetTickCountFromISR() * portTICK_PERIOD_MS));
    }
    return true;
}

// From Esp32Application
void WiFi :: Init(DmaUART *uart, command_buf_context_t *packets)
{
    this->uart = uart;
    this->packets = packets;
#if U64 == 2
    uart->SetLossCallback(ProviderLinkLoss);
    if (!provider_timer) {
        provider_timer = xTimerCreate("ESP Identity", pdMS_TO_TICKS(100), pdTRUE, this, PollProvider);
        if (provider_timer) xTimerStart(provider_timer, 0);
    }
#endif
    wifi_command_init();
}

void WiFi :: Start()
{
    if (runModeTask) {
        printf("** Trying to start Wifi Application, but task is already running?\n");
        return;
    }
    state = eWifi_NotDetected;
    portENTER_CRITICAL();
    ProviderLinkLoss();
    provider_monitor_enabled = false;
    portEXIT_CRITICAL();
    xTaskCreate( WiFi :: RunModeTaskStart, "WiFi Command Task", configMINIMAL_STACK_SIZE, this, PRIO_DRIVER, &runModeTask );
}

void WiFi :: Terminate()
{
    portENTER_CRITICAL();
    provider_monitor_enabled = false;
    ProviderLinkLoss();
    portEXIT_CRITICAL();
    if (!runModeTask) {
        printf("** Trying to terminate Wifi Application, but task is not running?\n");
        return;
    }
    vTaskDelete(runModeTask);
    runModeTask = NULL;
    netstack->stop(); // remove from list with available network stacks
    uart->ResetReceiveCallback();
    state = eWifi_Off;
    RefreshRoot();
}

bool WiFi::RequestEcho(void)
{
    command_buf_t *buf;
    uart->txDebug = false;

    uart->SetBaudRate(115200);

    while (uart->ReceivePacket(&buf, 100) == pdTRUE) {
        printf("%d Bytes received in buffer %d\n", buf->size, buf->bufnr);
        uart->FreeBuffer(buf);
    }

    char string[40];
    uint16_t major, minor;
    if (wifi_detect(&major, &minor, string, 40) == pdTRUE) {
        printf("ESP32 module V%d.%d detected: %s\n", major, minor, string);
    }

    const int rates[] = { 115200, 230400, 460800, 921600, 1000000, 2000000, 4000000, 6666666 };

    for(int rate = 0; rate < 8; rate ++) {
        wifi_setbaud(rates[rate], 1); // uart->SetBaudRate(rate); + remote switch as well     uart->FlowControl(false);

        for(int i=800; i<=1000;i+=50) {
            uint16_t start, stop;
            if (uart->GetBuffer(&buf, 100) != pdTRUE) {
                return false;
            }
            memset(buf->data, 0x00, CMD_BUF_SIZE);
            rpc_identify_req *req = (rpc_identify_req *)buf->data;
            req->hdr.command = CMD_ECHO;
            buf->data[i-1] = 2;
            int txbuf = buf->bufnr;
            buf->size = i;
            uart->TransmitPacket(buf, &start);

            if (uart->ReceivePacket(&buf, 200) == pdTRUE) {
                stop = getMsTimer();
                int t = 0;
                for (int j=0;j<buf->size;j++) {
                    t += (int)buf->data[j];
                }
                bool ok = (t == 3) && (buf->data[0] == 1) && (buf->data[i-1] == 2) && (buf->size == i);
                printf("%d Bytes received from buffer %d in buffer %d after %d ms Verdict: %s\n", buf->size, txbuf, buf->bufnr, stop - start, ok ? "OK!" : "FAIL!");
                if (!ok) {
                    dump_hex_relative(buf->data, buf->size);
                }
                uart->FreeBuffer(buf);
            }
        }
    }
    uart->txDebug = false;
    return true;
}

void WiFi::RunModeTaskStart(void *context)
{
    WiFi *w = (WiFi *) context;
    w->RunModeThread();
}

void WiFi :: RunModeThread()
{
    uint16_t major, minor;
    BaseType_t suc = pdFALSE;
    command_buf_t *buf;
    rpc_header_t *hdr;
    // rpc_wifi_connect_req *conn_req;
    event_pkt_connected *ev2;
    event_pkt_got_ip *ev;
    int result;
    state = eWifi_NotDetected;
    uint8_t conn;
    uint32_t ip;
    WifiConnectFeedback feedback;
    char feedback_msg[80];
    uint8_t attempt = 0;      // the connect from the AP list that the feedback follows
    uint8_t answer_taken = 0; // the last attempt whose answer was taken
    uint8_t answered;
    int answer;
    uint32_t answer_at;
    TickType_t wait_ticks;

    RefreshRoot();
    static char boot_message[256];
    int len;
    voltages_t voltages;
#if U64 != 2
    int32_t wait;
#endif

    while(1) {
        switch(state) {
        case eWifi_Off:
            printf("RunMode Thread running, but state is off...\n");
            vTaskDelay(200);
            break;

        case eWifi_NotDetected:
#ifndef U64 // wifi module is always present in U64, but not on the U2+L cartridge, so we detect it here
            len = esp32.DetectModule(boot_message, 256);
            if(len) {
                const char *bm = boot_message;
                for(int i=0;i<3;i++) {
                    if (*bm != 'E') {
                        bm++;
                    }
                }
                if (strncmp(bm, "ESP-ROM:", 8) == 0) {
                    for(int i=8;i<32;i++) {
                        if (bm[i] == '-') {
                            moduleType[i-8] = 0;
                            break;
                        } else {
                            moduleType[i-8] = bm[i];
                        }
                    }
                } else {
                    strcpy(moduleType, "esp32-wroom");
                }
                dump_hex(boot_message, len);
                state = eWifi_ModuleDetected;
                RefreshRoot();
                esp32.EnableRunMode();
                wifi_command_init();
            } else {
                printf("No Boot message.\n");
                vTaskDelay(portMAX_DELAY); // basically stall
            }
#else // U64 
            // Make sure it is on, without turning it off (might have been off)
            wifi.uart->ModuleCtrl(ESP_MODE_RUN);
            strcpy(moduleType, "ESP32 on U64");
            wifi_command_init();

            // esp32.EnableRunMode(); on the U64, the machine should boot with the module in run mode
            // on the U64, this should be enforced in the init of the esp32.cc code, and for the U64-II
            // the module is already running before the FPGA even loads. So we're NOT calling EnableRunMode here.
            state = eWifi_ModuleDetected;
            RefreshRoot();
#endif

        case eWifi_ModuleDetected:
            uart->txDebug = false;
            if (wifi_detect(&major, &minor, moduleName, 32)) {
                state = eWifi_AppDetected;

#if U64 == 2
                memset(&voltages, 0, sizeof(voltages));
                result = wifi_get_voltages(&voltages);
                printf("Result get voltages: %d %d %d %d %d %d %d %d\n", result, voltages.vbus, voltages.vaux, voltages.v50, voltages.v33, voltages.v18, voltages.v10, voltages.vusb);
                if (voltages.vbus < 8500) {
                    UserInterface :: postMessage("Low input voltage.");
                }
                // These settings live in the module's NVS; keep them in sync.
                U64Config :: pushPowerOnMode();
                U64Config :: pushWakeOnWifi();
                U64Config :: pushRecoveryUrl();
#endif
                RefreshRoot();
            }
            break;

        case eWifi_AppDetected:
            uart->txDebug = false;

            wifi_getmac(my_mac);
            netstack->set_mac_address(my_mac);
#if U64 != 2
            // The setting is applied here; a change from now on, by the event loop
            radio_setting_due = false;
            radio_setting_applied = netstack->enabled_in_config() ? 1 : 0;
            // Stored as Disabled: the radio goes off before the interface is up, and the
            // link stays down. Disabled is shown once the module confirms (wifi_switch.h).
            if (wifi_switch_wants_off(&radio_switch, radio_setting_applied != 0)) {
                result = wifi_disable();
                netstack->start(); // link down
                // Only a module without the switch is held off; any other error, or a
                // request that never left, is asked again at the deadline
                if (wifi_switch_unsupported(result)) {
                    // A module without the switch cannot keep its radio off: hold the
                    // module off. It has run to answer (its radio is on from its own boot,
                    // so it may have associated for a moment). From state Off on, an
                    // Enable starts the application again (RadioOn); one that came while
                    // the module answered is in the setting, and the module stays on.
                    printf("WiFi module cannot turn its radio off: %d\n", result);
                    state = eWifi_Off;
                    if (!netstack->enabled_in_config()) {
                        uart->ModuleCtrl(ESP_MODE_OFF);
                        RefreshRoot();
                        vTaskSuspend(NULL); // until that start ends this task
                        break;
                    }
                    state = eWifi_NotConnected;
                    if ((wifi_is_connected(conn) == 0) && conn) {
                        wifi_modem_enable(true); // take control!
                        netstack->link_up();
                        state = eWifi_Connected;
                    }
                    RefreshRoot();
                    break;
                }
                wifi_switch_asked_off(&radio_switch, xTaskGetTickCount(), pdMS_TO_TICKS(WIFI_SWITCH_CONFIRM_MS));
                state = eWifi_NotConnected;
                RefreshRoot();
                break;
            }
            // Stored as Enabled: the module starts with its radio off and turns it on now.
            // A module without the switch, whose radio is on already, ignores this. Not
            // taken as applied when it fails: the next effectuate asks again, and so does
            // the task at its deadlines (wifi_switch.h).
            result = wifi_enable();
            if ((result != 0) && !wifi_switch_unsupported(result)) {
                radio_setting_applied = -1;
                wifi_switch_on_failed(&radio_switch, xTaskGetTickCount(), pdMS_TO_TICKS(WIFI_SWITCH_CONFIRM_MS));
            }
#endif
            netstack->start(); // always starts in link down state
            state = eWifi_NotConnected;
            if (wifi_is_connected(conn) == 0) {
                if (conn) {
                    wifi_modem_enable(true); // take control!
                    uart->txDebug = false;
                    netstack->link_up();
                    state = eWifi_Connected;
                }
            }
            RefreshRoot();
#if U64 == 2
            // Do not spend the provider identity deadline on application
            // detection or initial synchronous Wi-Fi configuration requests.
            portENTER_CRITICAL();
            provider_monitor_enabled = true;
            portEXIT_CRITICAL();
#ifdef ULTIMATE_PROVIDER_JOURNAL
            provider_journal_start_owner(*this);
#endif
#endif
            break;

        case eWifi_Scanning:
            wifi_aps.num_records = 0;
            wifi_scan(&wifi_aps);
            // Back to where it was: a scan does not take the link down. Events the module
            // sent while it scanned are taken next, from the queue (as many as the receive
            // buffers hold while this task waits for the scan's answer).
            state = state_before_scan;
            RefreshRoot();
            RefreshAPs();
            break;

        case eWifi_NotConnected:
        case eWifi_Connected:
        case eWifi_Disabled:
#if U64 != 2
            // "WiFi Enabled" changed, or the settings were reset or loaded (RadioOn,
            // RadioOff, network_esp32.cc). The request is sent from here, so that the
            // requests go out one at a time and in the order of the changes, and the wait
            // for a radio off's confirmation is timed by this pass.
            if (radio_setting_due) {
                radio_setting_due = false;
                radio_setting_applied = netstack->enabled_in_config() ? 1 : 0;
                if (radio_setting_applied) {
                    wifi_switch_on(&radio_switch); // ends a wait for a radio-off confirmation
                    WhenEnabledFromNow();
                    result = wifi_enable();
                    if (result != 0) {
                        event_when_enabled = 0; // the radio does not come on: nothing is to follow it
                        // Not applied (a module without the switch has its radio on anyway):
                        // the next effectuate or change asks again, not every pass, and so
                        // does the task at its deadlines (wifi_switch.h)
                        if (!wifi_switch_unsupported(result)) {
                            radio_setting_applied = -1;
                            wifi_switch_on_failed(&radio_switch, xTaskGetTickCount(), pdMS_TO_TICKS(WIFI_SWITCH_CONFIRM_MS));
                        }
                    }
                } else if (!wifi_switch_unsupported(result = wifi_disable())) {
                    // Disabled is shown when the module confirms (wifi_switch.h); an open AP
                    // list says WiFi is disabled from now on
                    wifi_switch_asked_off(&radio_switch, xTaskGetTickCount(), pdMS_TO_TICKS(WIFI_SWITCH_CONFIRM_MS));
                    RefreshAPs();
                } else {
                    // A module without the radio switch: hold it off, as at boot. Not with
                    // Disable(): that stops the application, which would end this task
                    // while it runs this. From state Off on, an Enable starts the
                    // application again (RadioOn); one that came while the module answered
                    // is in the setting, and its change is due: the module stays on.
                    printf("WiFi module cannot turn its radio off: %d\n", result);
                    WifiState_t was = state;
                    state = eWifi_Off;
                    if (netstack->enabled_in_config()) {
                        state = was;
                    } else {
                        feedback.cancel();
                        event_when_enabled = 0;
                        netstack->link_down();
                        uart->ModuleCtrl(ESP_MODE_OFF);
                        RefreshRoot();
                        vTaskSuspend(NULL); // until an Enable starts the application again, which ends this task
                        break;
                    }
                }
            }
#endif
            if (pick_cancelled) {
                pick_cancelled = false;
                feedback.cancel();
            }
            // The module's answer to the connect request of the attempt that is followed
            // (ConnectAnswered); taken before the timeout is looked at
            ENTER_SAFE_SECTION;
            answered = connect_answered;
            answer = connect_answer;
            answer_at = connect_answer_at;
            LEAVE_SAFE_SECTION;
            if ((attempt != 0) && (answered == attempt) && (answer_taken != attempt)) {
                answer_taken = attempt;
                if (answer == 0) {
                    feedback.accepted(answer_at);
                } else if (feedback.rejected(feedback_msg, sizeof(feedback_msg))) {
                    UserInterface :: postMessage(feedback_msg);
                }
            }
            if (feedback.onTick(xTaskGetTickCount(), feedback_msg, sizeof(feedback_msg))) {
                UserInterface :: postMessage(feedback_msg);
            }
            wait_ticks = feedback.waitTicks(xTaskGetTickCount());
#if U64 != 2
            wait = wifi_switch_wait(&radio_switch, xTaskGetTickCount());
            if ((wait >= 0) && ((TickType_t)wait < wait_ticks)) {
                wait_ticks = wait;
            }
#endif
            if (cmd_buffer_received(packets, &buf, wait_ticks) != pdTRUE) {
#if U64 != 2
                // The radio off was not confirmed in time: it is asked for again, and its
                // link events are still dropped. After WIFI_SWITCH_ASKS waits the user is
                // told, the link goes down, and the radio off is asked for at a slower pace
                // for as long as WiFi stays Disabled: the link is never taken with Disabled
                // stored.
                if (wifi_switch_expired(&radio_switch, xTaskGetTickCount())) {
                    printf("WiFi module did not confirm the radio off\n");
                    wifi_disable();
                    if (wifi_switch_ask_again(&radio_switch, xTaskGetTickCount(), pdMS_TO_TICKS(WIFI_SWITCH_CONFIRM_MS), pdMS_TO_TICKS(WIFI_SWITCH_SLOW_MS))) {
                        UserInterface :: postMessage("WiFi module did not turn its radio off");
                        if (state == eWifi_Connected) {
                            netstack->link_down();
                        }
                        // The module's frames are not taken while the link is held down
                        wifi_modem_enable(false);
                        state = eWifi_NotConnected;
                        RefreshRoot();
                    }
                } else if (wifi_switch_on_due(&radio_switch, xTaskGetTickCount())) {
                    // A radio on that failed is asked for again, at the same pace
                    printf("WiFi module did not take the radio on\n");
                    result = wifi_enable();
                    if ((result == 0) || wifi_switch_unsupported(result)) {
                        radio_setting_applied = 1;
                    } else if (wifi_switch_on_again(&radio_switch, xTaskGetTickCount(), pdMS_TO_TICKS(WIFI_SWITCH_CONFIRM_MS), pdMS_TO_TICKS(WIFI_SWITCH_SLOW_MS))) {
                        UserInterface :: postMessage("WiFi module did not turn its radio on");
                    }
                }
#endif
                break; // the wait for the outcome of a connect ran out; the next pass reports it
            }
            if (pick_cancelled) { // the Disable came while this event was on its way
                pick_cancelled = false;
                feedback.cancel();
            }
            hdr = (rpc_header_t *)(buf->data);
#if DEBUG_INPUT
            printf("Pkt %d. Ev %b. Sz %d. Seq: %d\n", buf->bufnr, hdr->command, buf->size, hdr->sequence);
#endif
#if U64 != 2
            if (wifi_switch_drop_event(&radio_switch, hdr->command)) {
                uart->FreeBuffer(buf);
                break;
            }
#endif
            switch(hdr->command) {
            case EVENT_RESCAN: // requested from user interface
                uart->FreeBuffer(buf);
                // Disabled means no RF, also while the radio off waits to be confirmed:
                // the module does not scan with its radio off
                if ((state == eWifi_Disabled) || RadioOffAwaited()) {
                    break;
                }
                // The module scans while it stays associated, so the interface stays up;
                // no packet goes out until the scan is done (wifi_tx_packet sends only
                // when Connected)
                state_before_scan = state;
                state = eWifi_Scanning;
                RefreshRoot();
                break;

#if U64 != 2
            case EVENT_RADIO_SETTING: // a wake: the setting is applied at the top of the pass
                uart->FreeBuffer(buf);
                break;

            // The module restarted on its own (radio_boot.h). It put its radio back as it
            // was, or, with no record of that, turned it on: while Disabled, the radio off
            // is asked for again, and confirmed as at boot.
            case EVENT_RESTARTED:
                uart->FreeBuffer(buf);
                if (state == eWifi_Disabled) {
                    wifi_disable();
                    wifi_switch_asked_off(&radio_switch, xTaskGetTickCount(), pdMS_TO_TICKS(WIFI_SWITCH_CONFIRM_MS));
                }
                break;

#endif
            case EVENT_DISABLED:
                InvalidateProvider();
                uart->FreeBuffer(buf);
                state = eWifi_Disabled;
                feedback.cancel(); // a pick that was under way ends with the radio
                event_when_enabled = 0;
                wifi_aps.num_records = 0; // nothing to pick while the radio is off
                netstack->link_down();
                RefreshRoot();
                RefreshAPs(); // an open AP list shows that
                break;

            case EVENT_CONNECTED:
                ev2 = (event_pkt_connected *)buf->data;
                memcpy(last_ap, ev2->ssid, 32);
                feedback.onConnected(ev2->ssid);
                uart->FreeBuffer(buf);
                if (state == eWifi_Connected) { // already connected!
                    netstack->link_down();
                }
                wifi_modem_enable(true); // take control!
                state = eWifi_Connected;
                uart->txDebug = false;
                netstack->link_up();

                RefreshRoot();
                break;

            case EVENT_CONNECT_PENDING: // a connect from the AP list is about to go out
                // With the radio off (Disabled, or a radio off not confirmed yet) the module
                // reports the start of its radio first
                ev2 = (event_pkt_connected *)buf->data;
                feedback.begin(ev2->ssid, xTaskGetTickCount(), (state == eWifi_Disabled) || RadioOffAwaited());
                attempt = (uint8_t)ev2->hdr.sequence;
                uart->FreeBuffer(buf);
                break;

            case EVENT_CONNECT_ANSWER: // a wake: the answer is taken at the top of the pass
                uart->FreeBuffer(buf);
                break;

            case EVENT_DISCONNECTED:
                InvalidateProvider();
                // the reason is read before the buffer goes back to the UART, which may refill it
                if (!wifi_disconnect_is_radio_start(buf->data, buf->size) &&
                    feedback.onDisconnected(wifi_disconnect_reason(buf->data, buf->size), feedback_msg, sizeof(feedback_msg))) {
                    UserInterface :: postMessage(feedback_msg);
                }
                uart->FreeBuffer(buf);
                state = eWifi_NotConnected;
                if(netstack) {
                    netstack->link_down();
                }
                // The radio is on again: what the user asked for, if it is still due. The
                // module may have queued the Enable and not started its radio; a link loss
                // much later is no sign that the radio came on for it.
                if (event_when_enabled) {
                    if ((int32_t)(event_when_enabled_by - xTaskGetTickCount()) > 0) {
                        sendEvent(event_when_enabled);
                    }
                    event_when_enabled = 0;
                }
                RefreshRoot();
                break;

            case EVENT_RECV_PACKET:
                if (netstack) {
                    rpc_rx_pkt *pkt = (rpc_rx_pkt *)buf->data;
#if DEBUG_INPUT
                    printf("In %d\n", pkt->len);
                    dump_hex_relative(&pkt->data, 64);
#endif
                    // A copy: an unread socket holds its segments, and one connection can queue
                    // more of them than the UART has receive buffers, which stops the interface.
                    netstack->input_copy((uint8_t*)&pkt->data, pkt->len);
                } else {
                    puts("Packet received, but no net stack!");
                }
                uart->FreeBuffer(buf);
                break;

            case EVENT_GOTIP:
                ev = (event_pkt_got_ip *)buf->data;
                ip = ntohl(ev->ip);
                printf("-> ESP32 received IP from DHCP: %d.%d.%d.%d (changed: %d)\n", (ip >> 24),
                    (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF, ev->changed);
                uart->FreeBuffer(buf);
                break;

            default:
                printf("Unexpected Event type: Pkt %d. Ev %b. Sz %d. Seq: %d. Thread: %d.\n", buf->bufnr, hdr->command, buf->size, hdr->sequence, hdr->thread);
                uart->FreeBuffer(buf);
                break;
            }
        }
    }

    // Wait forever
    vTaskDelay(portMAX_DELAY);
}

bool WiFi :: sendEvent(uint8_t code)
{
    command_buf_t *buf;
    cmd_buffer_get(packets, &buf, portMAX_DELAY);
    rpc_header_t *hdr = (rpc_header_t *)(buf->data);
    hdr->command = code;
    hdr->sequence = 0;
    hdr->thread = 0xFF;
    if (cmd_buffer_loopback(packets, buf) != pdTRUE) { // internal event; the queue is full
        uart->FreeBuffer(buf);
        return false;
    }
    return true;
}

// A connect from the AP list, for the task to follow (wifi_connect.h). ConnectStart numbers
// it; EVENT_CONNECT_PENDING goes before the request; ConnectAnswered leaves the module's
// answer for the task (in the WiFi object, so a full queue does not lose it) and wakes it.
uint8_t WiFi :: ConnectStart(void)
{
    uint8_t a = connect_attempt + 1;
    if (a == 0) {
        a = 1; // 0 is no attempt
    }
    connect_attempt = a;
    return a;
}

void WiFi :: ConnectAnswered(uint8_t attempt, int answer)
{
    ENTER_SAFE_SECTION;
    connect_answer = answer;
    connect_answer_at = xTaskGetTickCount();
    connect_answered = attempt;
    LEAVE_SAFE_SECTION;
    sendEvent(EVENT_CONNECT_ANSWER);
}

bool WiFi :: sendConnectPending(const char *ssid, uint8_t attempt)
{
    command_buf_t *buf;
    cmd_buffer_get(packets, &buf, portMAX_DELAY);
    event_pkt_connected *ev = (event_pkt_connected *)(buf->data);
    ev->hdr.command = EVENT_CONNECT_PENDING;
    ev->hdr.sequence = attempt;
    ev->hdr.thread = 0xFF;
    bzero(ev->ssid, 32);
    strncpy((char *)ev->ssid, ssid, 32);
    if (cmd_buffer_loopback(packets, buf) != pdTRUE) { // internal event
        uart->FreeBuffer(buf);
        return false;
    }
    return true;
}

void WiFi :: freeBuffer(command_buf_t *buf)
{
    uart->FreeBuffer(buf);
}

int WiFi ::getAccessPointItems(Browsable *parent, IndexedList<Browsable *> &list)
{
    ultimate_ap_records_t *aps = &wifi_aps;
    // While Disabled nothing scans, so the list is empty until WiFi is enabled. While a radio
    // off waits for its confirmation the task drops a rescan, and the APs of an earlier scan
    // are not shown: WiFi is off as far as the user is concerned.
    if (RadioOffAwaited()) {
        list.append(new BrowsableStatic("WiFi is disabled"));
        return 0;
    }
    if (aps->num_records == 0) {
        list.append(new BrowsableStatic((state == eWifi_Disabled) ? "WiFi is disabled" : "Scanning..."));
        return 0;
    }
    ENTER_SAFE_SECTION;
    for (int i = 0; i < aps->num_records; i++) {
        list.append(new BrowsableWifiAP(parent, (char *)aps->aps[i].ssid, aps->aps[i].rssi, aps->aps[i].authmode,
                                        aps->aps[i].bssid, aps->aps[i].channel));
    }
    LEAVE_SAFE_SECTION;
    return aps->num_records;
}

#include "subsys.h"
void BrowsableWifiAP :: fetch_context_items(IndexedList<Action *>&items)
{
    items.append(new Action("Connect", BrowsableWifiAP :: connect_ap, (int)this, 0));
}

SubsysResultCode_e BrowsableWifiAP :: connect_ap(SubsysCommand *cmd)
{
    char password[65] = { 0 }; // string_box takes 64 characters plus the terminator
    BrowsableWifiAP *bap = (BrowsableWifiAP *)cmd->functionID;
    // Also while a radio off waits for its confirmation: WiFi is off as far as the user is
    // concerned, and a connect request would turn the radio on again
    if (((wifi.getState() == eWifi_Disabled) || wifi.RadioOffAwaited()) && !NetworkLWIP_WiFi :: ask_enable(cmd->user_interface)) {
        return SSRET_OK;
    }
    if (bap->auth) {
        if(cmd->user_interface) {
            if (cmd->user_interface->string_box("Enter password", password, 64) <= 0) {
                return SSRET_OK; // cancelled: nothing to connect with
            }
        }
    }
    // The task notes the attempt before the request goes out: the module can report the
    // outcome before its answer to the request arrives, and the task takes its events in
    // the order they were queued
    uint8_t attempt = wifi.ConnectStart();
    if (!wifi.sendConnectPending(bap->ssid, attempt)) {
        char msg[64];
        wifi_connect_failed_text(msg, sizeof(msg), bap->ssid);
        UserInterface :: postMessage(msg);
        return SSRET_OK;
    }
    // The AP picked, not just any AP with its SSID
    int result = wifi_wifi_connect(bap->ssid, password, bap->auth, bap->bssid, bap->channel);
    printf("Connect result: %d\n", result);
    // Accepted, the outcome is timed from now; refused, the task ends the attempt and says so
    wifi.ConnectAnswered(attempt, result);
    return SSRET_OK;
}

err_t wifi_tx_packet(void *driver, void *buffer, int length)
{
    if (wifi.getState() != eWifi_Connected) {
        return ERR_CONN;
    }
    
    BUFARGS_NOREPLY(send_eth, CMD_SEND_PACKET, ERR_MEM); // the module does not answer frames

    uint32_t chunkSize = length;
    if (chunkSize > CMD_BUF_PAYLOAD) {
        chunkSize = CMD_BUF_PAYLOAD;
    }
    args->length = chunkSize;
    memcpy(&args->data, buffer, chunkSize);
    buf->size += (chunkSize - 4);

    esp32.uart->TransmitPacket(buf);
    //TRANSMIT(espcmd);
    //RETURN_ESP;
    return ERR_OK;
}

void wifi_free(void *driver, void *buffer)
{
    // driver points to the WiFi object
    // buffer points to the cmd_buffer_t object
    WiFi *w = (WiFi *)driver;
    w->freeBuffer((command_buf_t *)buffer);
}

extern "C" {
    void print_uart_status()
    {
        wifi.PrintUartStatus();
    }
}
