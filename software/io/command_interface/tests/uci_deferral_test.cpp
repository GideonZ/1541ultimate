/*
 * The UCI Server's deferred reply (command_defer), against a model of the gateware in
 * fpga/io/command_interface/vhdl_source/command_protocol.vhd. The test plays the C64, the
 * UCI Server task (one queue item at a time) and the target's own task.
 */
#include "command_intf.h"

#include <stdio.h>
#include <string.h>

extern "C" BaseType_t command_interface_irq(void);
extern "C" void ResetInterruptHandlerCmdIf(void);
extern "C" void outbyte(int c) { putchar(c); }

static int checks = 0;
static int failures = 0;
static const char *current = "";

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s: %s (line %d)\n", current, #cond, __LINE__); } } while (0)

// --- the gateware -------------------------------------------------------------------

// Buffer layout of the model, in bytes from the start of the command interface RAM.
#define CMD_START  0x000
#define RESP_START 0x380
#define STAT_START 0x700

static struct Gateware {
    uint8_t memory[0x1000];     // registers, then the 2K RAM at 0x800
    uint8_t slot_base, slot_enable;
    int state;                  // slot_status(5 downto 4)
    uint8_t handshake_in;       // slot_status(2 downto 0)
    bool error_busy;
    uint8_t irq_mask;
    int command_length, response_length, status_length, response_read;
    int idle_validates;         // VALIDATE written while the state was 00
    bool hold_irq;              // the C64 acts, and the ISR has not run yet
    bool in_irq;
} gw;

uint8_t *uci_model_memory(void) { return gw.memory; }

static void gw_irq(void)
{
    // io_irq <= '1' when (handshake_in and not irq_mask) /= "000"
    if (!gw.hold_irq && !gw.in_irq && (gw.handshake_in & ~gw.irq_mask & 7)) {
        gw.in_irq = true;
        command_interface_irq();
        gw.in_irq = false;
    }
}

uint8_t uci_model_read(int reg)
{
    switch (reg) {
    case 0x0: return gw.slot_base;
    case 0x1: return gw.slot_enable;
    case 0x2: return (uint8_t)(gw.state << 4);
    case 0x3: return (uint8_t)((gw.state << 4) | (gw.error_busy ? 8 : 0) | gw.handshake_in);
    case 0x4: return CMD_START / 8;
    case 0x5: return (RESP_START - 1) / 8;
    case 0x6: return RESP_START / 8;
    case 0x7: return (STAT_START - 1) / 8;
    case 0x8: return STAT_START / 8;
    case 0x9: return 0xFF;
    case 0xA: return 0;
    case 0xB: return gw.irq_mask;
    case 0xC: return (uint8_t)(RESP_START + gw.response_read);
    case 0xD: return (uint8_t)((RESP_START + gw.response_read) >> 8);
    case 0xE: return (uint8_t)gw.command_length;
    case 0xF: return (uint8_t)(gw.command_length >> 8);
    }
    return 0xFF;
}

void uci_model_write(int reg, uint8_t value)
{
    switch (reg) {
    case 0x0: gw.slot_base = value; break;
    case 0x1: gw.slot_enable = value; break;
    case 0x2: // c_cif_io_handshake_out
        if (value & 0x01) { gw.handshake_in &= ~1; gw.command_length = 0; }
        if (value & 0x02) { gw.handshake_in &= ~2; }
        if (value & 0x04) { gw.handshake_in &= ~4; }
        if (value & 0x10) {
            if (gw.state == 0) {
                gw.idle_validates++;
            }
            gw.state = 2 | ((value & 0x20) ? 1 : 0);
            gw.response_read = 0;
        }
        if (value & 0x80) { gw.state = 0; gw.response_read = 0; }
        break;
    case 0x4: gw.irq_mask |= value & 7; break;
    case 0x5: gw.irq_mask &= ~value & 7; break;
    case 0xA: gw.status_length = value; break;
    case 0xB: gw.irq_mask = value & 7; break;
    case 0xC: gw.response_length = (gw.response_length & 0x700) | value; gw.response_read = 0; break;
    case 0xD: gw.response_length = (gw.response_length & 0xFF) | ((value & 7) << 8); break;
    }
    gw_irq();
}

// --- the C64 --------------------------------------------------------------------------

static void c64_push(const uint8_t *command, int length)
{
    memcpy(gw.memory + 0x800 + CMD_START, command, length);
    gw.command_length = length;
    if (gw.state == 0) { // slot_req.data(0)='1'
        gw.state = 1;
        gw.handshake_in |= 1;
    } else {
        gw.error_busy = true;
    }
    gw_irq();
}

static void c64_abort(void)
{
    gw.handshake_in |= 4;
    gw_irq();
}

static void c64_reset(void)
{
    ResetInterruptHandlerCmdIf(); // the gateware itself is not reset
}

static bool response_is(const char *text)
{
    int length = (int)strlen(text);
    return (gw.state & 2) && (gw.response_length == length) &&
           !memcmp(gw.memory + 0x800 + RESP_START, text, length);
}

// --- the UCI Server task ----------------------------------------------------------------

static void run_server(void)
{
    uint8_t item;
    while (xQueueReceive(cmd_if.queue, &item, 0) == pdTRUE) {
        cmd_if.handle_event(item);
    }
}

static void fill_queue(uint8_t item)
{
    while (xQueueSend(cmd_if.queue, &item, 0) == pdTRUE) {
    }
}

// --- a target that defers -----------------------------------------------------------

static Message c_reply_now  = { 3, true, (uint8_t *)"NOW" };
static Message c_reply_late = { 4, true, (uint8_t *)"LATE" };

class FakeTarget : public CommandTarget
{
public:
    bool defer_next;
    bool finish_at_once;
    uint32_t ticket;
    int parses, aborts, resets, replies;
    UBaseType_t parse_priority, abort_priority, reset_priority, reply_priority;
    int state_at_abort, state_at_reset;

    void clear(void)
    {
        defer_next = finish_at_once = false;
        ticket = 0;
        parses = aborts = resets = replies = 0;
        parse_priority = abort_priority = reset_priority = reply_priority = 99;
        state_at_abort = state_at_reset = -1;
    }
    void parse_command(Message *, Message **reply, Message **status)
    {
        parses++;
        parse_priority = uxTaskPriorityGet(NULL);
        *reply = &c_reply_now;
        *status = &c_status_ok;
        if (defer_next) {
            ticket = command_defer();
            if (finish_at_once) {
                command_deferred_done(ticket);
            }
        }
    }
    void deferred_reply(Message **reply, Message **status)
    {
        replies++;
        reply_priority = uxTaskPriorityGet(NULL);
        *reply = &c_reply_late;
        *status = &c_status_ok;
    }
    bool is_quick(Message *command) { return command->message[1] == 0x01; }
    void abort(int) { aborts++; abort_priority = uxTaskPriorityGet(NULL); state_at_abort = gw.state; }
    void c64_reset(void) { resets++; reset_priority = uxTaskPriorityGet(NULL); state_at_reset = gw.state; }
};

static FakeTarget fake;
static const uint8_t c_cmd_fake[]          = { 0x07, 0x10 };
static const uint8_t c_cmd_fake_no_reply[] = { 0x87, 0x10 };

static void begin(const char *name)
{
    current = name;
    fake.clear();
    // The C64 is idle, as after the abort that every test ends with.
    c64_abort();
    run_server();
    CHECK(gw.state == 0 && gw.handshake_in == 0 && gw.irq_mask == 0);
    fake.clear();
    gw.idle_validates = 0;
    gw.error_busy = false;
}

static void start_deferred(const uint8_t *command, int length)
{
    fake.defer_next = true;
    c64_push(command, length);
    run_server();
    fake.defer_next = false;
}

static void expect_next_command_works(void)
{
    c64_push(c_cmd_fake, sizeof(c_cmd_fake));
    run_server();
    CHECK(!gw.error_busy);
    CHECK(response_is("NOW"));
}

// --- the cases ------------------------------------------------------------------------

static void test_reply_when_done(void)
{
    begin("deferred reply");
    start_deferred(c_cmd_fake, sizeof(c_cmd_fake));
    CHECK(gw.state == 1);                  // BUSY, as while a target works
    CHECK(gw.handshake_in & 1);            // the command is not accepted yet
    CHECK(gw.irq_mask & CMD_NEW_COMMAND);  // and no second new-command interrupt
    CHECK(fake.replies == 0);
    command_deferred_done(fake.ticket);
    run_server();
    CHECK(fake.replies == 1);
    CHECK(response_is("LATE"));
    CHECK(gw.status_length == 5 && !memcmp(gw.memory + 0x800 + STAT_START, "00,OK", 5));
    CHECK(gw.handshake_in == 0 && gw.irq_mask == 0);
}

static void test_done_before_return(void)
{
    begin("done before parse_command returns");
    fake.finish_at_once = true;
    start_deferred(c_cmd_fake, sizeof(c_cmd_fake));
    CHECK(fake.replies == 1);
    CHECK(response_is("LATE"));
    CHECK(gw.irq_mask == 0);
}

static void test_abort_while_pending(void)
{
    begin("abort while pending");
    start_deferred(c_cmd_fake, sizeof(c_cmd_fake));
    c64_abort();
    run_server();
    CHECK(fake.aborts == 1);
    CHECK(gw.state == 0 && gw.handshake_in == 0);
    CHECK(gw.irq_mask == 0);
    // The late result
    command_deferred_done(fake.ticket);
    run_server();
    CHECK(fake.replies == 0);
    CHECK(gw.state == 0 && gw.idle_validates == 0);
    expect_next_command_works();
}

static void test_abort_before_wake(void)
{
    begin("abort raised before the completion is seen");
    start_deferred(c_cmd_fake, sizeof(c_cmd_fake));
    gw.hold_irq = true;
    c64_abort();                        // its interrupt has not run yet
    command_deferred_done(fake.ticket); // so the wake-up is first in the queue
    run_server();
    CHECK(fake.replies == 0);
    CHECK(gw.idle_validates == 0 && !(gw.state & 2));
    gw.hold_irq = false;
    gw_irq();
    run_server();
    CHECK(fake.aborts == 1 && fake.replies == 0);
    CHECK(gw.state == 0 && gw.irq_mask == 0);
    expect_next_command_works();
}

static void test_reset_while_pending(void)
{
    begin("reset while pending");
    start_deferred(c_cmd_fake, sizeof(c_cmd_fake));
    c64_reset();
    run_server();
    CHECK(fake.resets == 1);
    CHECK(gw.state == 0 && gw.handshake_in == 0 && gw.irq_mask == 0);
    command_deferred_done(fake.ticket);
    run_server();
    CHECK(fake.replies == 0);
    CHECK(gw.state == 0 && gw.idle_validates == 0);
    expect_next_command_works();
}

static void test_reset_item_dropped(void)
{
    begin("reset while pending, its queue item dropped");
    start_deferred(c_cmd_fake, sizeof(c_cmd_fake));
    fill_queue(0x08); // no flag the server acts on
    c64_reset();      // the ISR cannot post its abort
    run_server();
    CHECK(fake.resets == 1);
    CHECK(gw.state == 0 && gw.irq_mask == 0);
    expect_next_command_works();
}

static void test_late_result_after_abort(void)
{
    begin("late result after abort, next command idle");
    start_deferred(c_cmd_fake, sizeof(c_cmd_fake));
    uint32_t old_ticket = fake.ticket;
    c64_abort();
    run_server();
    // The C64 reads state 00 and moves on; the old result must not appear as a reply.
    command_deferred_done(old_ticket);
    run_server();
    CHECK(gw.state == 0);
    CHECK(gw.idle_validates == 0);
    c64_push(c_cmd_fake, sizeof(c_cmd_fake));
    CHECK(!gw.error_busy);
    run_server();
    CHECK(response_is("NOW"));
}

static void test_ticket_mismatch(void)
{
    begin("ticket mismatch");
    start_deferred(c_cmd_fake, sizeof(c_cmd_fake));
    uint32_t first = fake.ticket;
    c64_abort();
    run_server();
    start_deferred(c_cmd_fake, sizeof(c_cmd_fake));
    uint32_t second = fake.ticket;
    CHECK(second != first);
    command_deferred_done(first); // the cancelled job finishes while the second one waits
    run_server();
    CHECK(fake.replies == 0);
    CHECK(gw.state == 1);
    command_deferred_done(second);
    run_server();
    CHECK(fake.replies == 1);
    CHECK(response_is("LATE"));
}

static void test_full_queue(void)
{
    begin("full wake queue");
    start_deferred(c_cmd_fake, sizeof(c_cmd_fake));
    fill_queue(0x08);
    UBaseType_t before = uxQueueMessagesWaiting(cmd_if.queue);
    command_deferred_done(fake.ticket); // its wake-up does not fit
    CHECK(uxQueueMessagesWaiting(cmd_if.queue) == before);
    run_server();
    CHECK(fake.replies == 1);
    CHECK(response_is("LATE"));
}

static void test_no_reply(void)
{
    begin("NO_REPLY stays BUSY until done");
    start_deferred(c_cmd_fake_no_reply, sizeof(c_cmd_fake_no_reply));
    CHECK(gw.state == 1);
    command_deferred_done(fake.ticket);
    run_server();
    CHECK(fake.replies == 1);
    CHECK(gw.state == 0 && gw.handshake_in == 0 && gw.irq_mask == 0);
    CHECK(gw.idle_validates == 0);
    expect_next_command_works();
}

// The server answers the C64 above the network services; a target's own work, which can
// be long, runs at the level the services share, so it cannot starve them.
static void test_priorities(void)
{
    begin("priority ladder");
    CHECK(uxTaskPriorityGet(NULL) == PRIO_C64_SERVICE);
    start_deferred(c_cmd_fake, sizeof(c_cmd_fake));
    CHECK(fake.parse_priority == PRIO_HW_SERVICE);
    CHECK(uxTaskPriorityGet(NULL) == PRIO_C64_SERVICE);
    command_deferred_done(fake.ticket);
    run_server();
    CHECK(fake.reply_priority == PRIO_HW_SERVICE);
    CHECK(uxTaskPriorityGet(NULL) == PRIO_C64_SERVICE);
    c64_abort(); // the C64 is done with that reply
    run_server();
    start_deferred(c_cmd_fake, sizeof(c_cmd_fake));
    c64_reset();
    run_server();
    // The C64 is released first, at the server's level; the targets clean up at theirs.
    CHECK(fake.reset_priority == PRIO_HW_SERVICE && fake.state_at_reset == 0);
    CHECK(fake.abort_priority == PRIO_HW_SERVICE && fake.state_at_abort == 0);
    CHECK(uxTaskPriorityGet(NULL) == PRIO_C64_SERVICE);
    CHECK(PRIO_HW_SERVICE < PRIO_C64_SERVICE && PRIO_NETSERVICE < PRIO_C64_SERVICE);
    // A quick command (here IDENTIFY) runs at the server's own level.
    expect_next_command_works();
    c64_abort();
    run_server();
    const uint8_t identify[] = { 0x07, 0x01 };
    c64_push(identify, sizeof(identify));
    run_server();
    CHECK(fake.parse_priority == PRIO_C64_SERVICE);
    CHECK(response_is("NOW"));
    c64_abort();
    run_server();
    expect_next_command_works();
}

// On the device the server, being higher, runs as soon as the worker posts its wake-up,
// before the worker goes on. It must find the ticket already written.
static void run_server_on_post(QueueHandle_t)
{
    host_rtos_send_hook() = NULL;
    run_server();
}

static void test_wake_preempts(void)
{
    begin("the server runs at the post of the wake-up");
    start_deferred(c_cmd_fake, sizeof(c_cmd_fake));
    host_rtos_send_hook() = run_server_on_post;
    command_deferred_done(fake.ticket);
    host_rtos_send_hook() = NULL;
    CHECK(fake.replies == 1);
    CHECK(response_is("LATE"));
}

// A late report of an older ticket must not hide a newer one that is not seen yet: a
// second deferring target, or a worker preempted between its done and its report.
static void test_older_report_after_newer(void)
{
    begin("an older ticket reported after a newer one");
    start_deferred(c_cmd_fake, sizeof(c_cmd_fake));
    uint32_t first = fake.ticket;
    c64_abort();
    run_server();
    start_deferred(c_cmd_fake, sizeof(c_cmd_fake));
    uint32_t second = fake.ticket;
    fill_queue(0x08);                  // the server has not run since
    command_deferred_done(second);
    command_deferred_done(first);      // the preempted worker of the aborted command
    run_server();
    CHECK(fake.replies == 1);
    CHECK(response_is("LATE"));
}

// Where the gateware is reset with the C64 (U2: sys_reset), the command it held is gone
// before the reset interrupt has run. A completion seen in between must not be sent: the
// interface reads idle, not BUSY.
static void test_gateware_reset_first(void)
{
    begin("gateware reset before its interrupt");
    start_deferred(c_cmd_fake, sizeof(c_cmd_fake));
    gw.state = 0;
    gw.handshake_in = 0;
    gw.command_length = 0;
    gw.hold_irq = true;
    command_deferred_done(fake.ticket);
    run_server();
    CHECK(fake.replies == 0);
    CHECK(gw.state == 0 && gw.idle_validates == 0);
    gw.hold_irq = false;
    c64_reset();
    run_server();
    CHECK(gw.state == 0 && gw.irq_mask == 0);
    expect_next_command_works();
}

int main(int argc, char **argv)
{
    const char *only = (argc > 1) ? argv[1] : NULL;
    command_targets[7] = &fake;
    CMD_IF_IRQMASK_CLEAR = 7; // as run_task does
    vTaskPrioritySet(NULL, PRIO_C64_SERVICE); // as the task is created

    struct { const char *name; void (*run)(void); } cases[] = {
        { "reply",        test_reply_when_done },
        { "early",        test_done_before_return },
        { "abort",        test_abort_while_pending },
        { "abort-first",  test_abort_before_wake },
        { "reset",        test_reset_while_pending },
        { "reset-drop",   test_reset_item_dropped },
        { "late",         test_late_result_after_abort },
        { "ticket",       test_ticket_mismatch },
        { "full-queue",   test_full_queue },
        { "no-reply",     test_no_reply },
        { "priority",     test_priorities },
        { "preempt",      test_wake_preempts },
        { "older",        test_older_report_after_newer },
        { "gw-reset",     test_gateware_reset_first },
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (!only || !strcmp(only, cases[i].name)) {
            cases[i].run();
        }
    }
    printf("UCI deferral: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
