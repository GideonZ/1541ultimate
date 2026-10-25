#include <deque>
#include <stdio.h>
#include <string.h>
#include "command_intf.h"

// Exercise the real dispatcher and ISR functions with bounded fake hardware
// and queue operations. This does not emulate interrupt timing or the FPGA.
uint8_t command_registers[4096] = {0, 0, 0, 0, 0, 112, 112, 224, 224};
static void (*server)(void *);
static void *server_context;
static std::deque<CommandInterfaceEvent> events;
static bool drop_event;
static int critical, checks, failures;
static void expect(bool value, const char *label)
{
    ++checks;
    if (!value) { ++failures; printf("FAIL %s\n", label); }
}
void enter_critical() { ++critical; }
void exit_critical() { --critical; }
QueueHandle_t xQueueCreate(int, int) { return 0; }
void xTaskCreate(void (*fn)(void *), const char *name, int, void *arg, int, TaskHandle_t *)
{
    if (!strcmp(name, "UCI Server")) { server = fn; server_context = arg; }
}
BaseType_t xQueueSendFromISR(QueueHandle_t, const void *value, BaseType_t *woken)
{
    expect(*woken == pdFALSE, "ISR wake flag initialized");
    if (drop_event) return pdFALSE;
    events.push_back(*static_cast<const CommandInterfaceEvent *>(value));
    return pdTRUE;
}
struct Done {};
void xQueueReceive(QueueHandle_t, void *value, unsigned)
{
    expect(critical == 0, "no blocking receive in critical section");
    if (events.empty()) throw Done();
    *static_cast<CommandInterfaceEvent *>(value) = events.front(); events.pop_front();
}
extern "C" void ResetInterruptHandlerCmdIf();
extern "C" BaseType_t command_interface_irq();
extern "C" void outbyte(int) {}

static void signal(bool reset)
{
    expect(critical == 0, "interrupt injected outside publication lock");
    if (reset) ResetInterruptHandlerCmdIf();
    else { CMD_IF_STATUSBYTE = CMD_ABORT_DATA; CMD_IF_IRQMASK = 0; command_interface_irq(); }
}
static void queue_event(uint8_t flags)
{
    CMD_IF_STATUSBYTE = flags; CMD_IF_IRQMASK = 0;
    command_interface_irq();
}
struct Target : CommandTarget {
    OperationCancellation token;
    bool interrupt, restart;
    int parsed, aborted, resets, continued;
    Target() : interrupt(false), restart(false), parsed(0), aborted(0), resets(0), continued(0) {}
    void begin_command(const OperationCancellation& t) override { token = t; }
    void reply(Message **data, Message **status) {
        static Message result = {1, true, (uint8_t *)"X"};
        *data = &result; *status = &c_status_ok;
        if (interrupt) {
            signal(restart);
            expect(token.cancelled(), "in-flight owner sees interrupt before queued cleanup");
            expect(aborted == 0 && resets == 0, "ISR did not invoke owner cleanup");
        }
    }
    void parse_command(Message *, Message **data, Message **status) override { ++parsed; reply(data, status); }
    void get_more_data(Message **data, Message **status) override { ++continued; reply(data, status); }
    void abort(int) override { ++aborted; }
    void c64_reset() override { ++resets; }
};
static void drain()
{
    try { server(server_context); } catch (const Done&) {}
    expect(critical == 0, "dispatcher leaves publication lock");
}
int main()
{
    queue_event(CMD_DATA_ACCEPTED); drain();
    expect(CMD_IF_HANDSHAKE_OUT == HANDSHAKE_ACCEPT_NEXTDATA, "legacy no-target continuation acknowledged");
    CMD_IF_COMMAND_LEN_L = CMD_IF_COMMAND_LEN_H = 0;
    queue_event(CMD_NEW_COMMAND); drain();
    expect(CMD_IF_HANDSHAKE_OUT == HANDSHAKE_VALIDATE_LAST && CMD_IF_STATUS_LENGTH == 0,
           "legacy empty command acknowledged");
    CMD_IF_COMMAND_LEN_L = 2;
    CMD_IF_COMMAND_LEN_H = 0;
    CMD_IF_RAM(0) = 6;
    CMD_IF_RAM(1) = 1;
    for (bool restart : {false, true}) for (bool dropped : {false, true}) for (bool more : {false, true}) {
        Target target;
        command_targets[6] = &target;
        target.restart = restart;
        drop_event = false;
        if (more) { queue_event(CMD_NEW_COMMAND); drain(); }
        command_registers[0x800 + 896] = '?';
        CMD_IF_HANDSHAKE_OUT = 0;
        target.interrupt = true;
        queue_event(more ? CMD_DATA_ACCEPTED : CMD_NEW_COMMAND);
        // A queued event remains available when an ISR queue post is lost.
        if (dropped) {
            queue_event(CMD_DATA_ACCEPTED);
            queue_event(CMD_NEW_COMMAND);
            queue_event(CMD_DATA_ACCEPTED);
        }
        drop_event = dropped;
        drain();
        expect(target.parsed == 1 && target.continued == int(more), "no stale queued continuation executed");
        expect(target.aborted == 1 && target.resets == int(restart), "owner drains sticky abort/reset exactly once");
        expect(command_registers[0x800 + 896] == '?', "interrupted reply never copied to shared RAM");
        expect(CMD_IF_HANDSHAKE_OUT == HANDSHAKE_RESET, "interrupted command resets handshake");
        expect(CMD_IF_IRQMASK_CLEAR == 7, "abort releases all stale event masks");
        target.interrupt = false; drop_event = false;
        queue_event(CMD_NEW_COMMAND); drain();
        expect(!target.token.cancelled() && target.parsed == 2, "fresh command gets new live token");
        expect(command_registers[0x800 + 896] == 'X', "fresh response published normally");
        command_targets[6] = NULL;
    }
    printf("Command cancellation: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
