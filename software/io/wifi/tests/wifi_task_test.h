/*
 * wifi_task_test.h
 *
 * The scripted world of the WiFi application host test (wifi_task_stubs.cpp) as a case file
 * sees it. A case boots the WiFi task (WiFi :: RunModeThread, cut out of wifi.cc as it is)
 * with the module found, feeds it the events of a script in order, and looks at the log of
 * what the task did and at its state. The task runs until it waits for an event that does
 * not come. Built for U64=1, the U2+L (U64 not defined) and U64=2; a case that is for
 * some of them says so with #if.
 *
 * Log letters: d detect, m get MAC, O radio off asked (CMD_WIFI_DISABLE), N radio on asked
 * (CMD_WIFI_ENABLE), Q connection state asked, s netstack start, U link up, u link down,
 * C scan, A application stopped, B application started, M modem taken over, F module held
 * off, D/E U2+L module detection and run mode, ! a message to the user.
 */

#ifndef WIFI_TASK_TEST_H_
#define WIFI_TASK_TEST_H_

#include <initializer_list>
#include "../../usb/tests/host_test/host_test.h"
#include "wifi_task_env.h"

// What the case sets before it boots the task
struct World {
    bool enabled;         // "WiFi Enabled" as stored
    int disable_answer;   // the module's answer to a radio off (0: queued; 0x106: no switch)
    int enable_answer;    // and to a radio on
    uint8_t link;         // what the module says to the connection query
    int voltages_result;  // wifi_get_voltages()'s answer (U64=2)
    uint16_t voltages_vbus;
    bool keep_switch;     // the next boot keeps the radio switch state (once)
};
extern World world;
void reset_world(void); // every case starts with it

extern int saves;          // how often the WiFi settings were written
extern bool saved_enabled; // "WiFi Enabled" as written
extern char rlog[256];     // what the task did, one letter each
extern char messages[256]; // what the user was told, one message per line
extern const char *ended;  // why the task stopped running
extern TickType_t now;     // the task's clock, in ticks
extern int first_wait_state; // the task's state when it first waits for an event
extern WiFi wifi;
extern NetworkLWIP_WiFi net;

// A step of the script: an event of the module
struct Step {
    int code;
    const char *ssid;
    int reason;
    Step(int c) : code(c), ssid(NULL), reason(-1) {}
    Step(int c, const char *s) : code(c), ssid(s), reason(-1) {}
};

// Starts the WiFi task with the module found, and these steps in order
void boot(std::initializer_list<Step> steps);
const char *after(char c); // the log after the first occurrence of c

// The case's check: a failure says what was expected and what the task did
void check_failed(const char *what, const char *file, int line);
#define CHECK(cond, what) do { if (!(cond)) check_failed(what, __FILE__, __LINE__); } while (0)

#endif /* WIFI_TASK_TEST_H_ */
