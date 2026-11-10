/*
 * Host test of the printer's own command paths (iec_printer.cc itself, against the stand-ins in
 * host_printer/): the Flush/Eject menu item posts a flush and comes back within the bound,
 * Turn Off and Reset discard the backlog and reset the page emulation, and a RAW file does not
 * stay open.
 */
#include "host_printer/stubs.h"
#include <pthread.h>
#include <unistd.h>
#include <atomic>
#define private public
#include "iec_printer.h"
#undef private

Record rec;
ConfigStore *host_cfg;
IecInterface *iec_if;
bool host_start_tasks = true;
TaskHandle_t host_dummy_handle = (TaskHandle_t) &host_dummy_handle;
struct PrinterTaskArgs { TaskFunction_t fn; void *arg; };
static void *task_thread(void *a)
{
    PrinterTaskArgs *t = (PrinterTaskArgs *) a;
    t->fn(t->arg);
    return NULL;
}
BaseType_t xTaskCreate(TaskFunction_t fn, const char *, int, void *arg, int, TaskHandle_t *h)
{
    *h = host_dummy_handle;
    if (host_start_tasks) {
        PrinterTaskArgs *t = new PrinterTaskArgs;
        t->fn = fn; t->arg = arg;
        pthread_t th;
        pthread_create(&th, NULL, task_thread, t);
        pthread_detach(th);
    }
    return pdPASS;
}

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static bool wait_until(bool (*f)(void), int ms)
{
    for (int i = 0; i < ms; i++) {
        if (f()) return true;
        usleep(1000);
    }
    return f();
}

#define MENU_ON 0x1230
#define MENU_OFF 0x1231
#define MENU_FLUSH 0x1232
#define MENU_RESET 0x1233

static void configure_printer(int type)
{
    host_cfg->values[0x38] = 1;        /* enabled */
    host_cfg->values[0x30] = 4;
    host_cfg->values[0x32] = type;     /* 0 RAW, 2 PNG */
    host_cfg->values[0x39] = 5;
    host_cfg->values[0x3A] = 60;
    host_cfg->strings[0x31] = "/printer";
}

static void menu(IecPrinter *p, UserInterface *ui, int id)
{
    SubsysCommand c;
    c.user_interface = ui;
    c.functionID = id;
    p->executeCommand(&c);
}

static bool form_feed_done(void) { return rec.form_feed >= 1; }
static bool mps_reset_done(void) { return rec.mps_reset >= 1; }
static bool file_closed(void) { return rec.file_is_open == 0 && rec.file_closes >= 1; }

/* Flush/Eject: the flush reaches the printer task, in stream order, and the menu comes back. */
static void test_flush_served(void)
{
    rec.reset();
    host_cfg = new ConfigStore;
    configure_printer(2);
    iec_if = IecInterface::get_iec_interface();
    host_start_tasks = true;
    IecPrinter *p = new IecPrinter;
    UserInterface ui;
    for (int i = 0; i < 100; i++) p->push_data('A');
    TickType_t t0 = xTaskGetTickCount();
    menu(p, &ui, MENU_FLUSH);
    TickType_t used = xTaskGetTickCount() - t0;
    CHECK(rec.form_feed == 1);                  /* the page was ejected, by the printer task, before the menu came back */
    CHECK(rec.interpreted_bytes == 100);        /* with the bytes accepted before it */
    CHECK(used < IEC_PRINTER_FLUSH_WAIT_MS);
    CHECK(rec.progress_shown == 1 && rec.progress_hidden == 1);
}

/* Nobody serves it: the menu comes back after the bound, and the flush is still there for later. */
static void test_flush_bounded(void)
{
    rec.reset();
    host_cfg = new ConfigStore;
    configure_printer(2);
    host_start_tasks = false;
    IecPrinter *p = new IecPrinter;
    UserInterface ui;
    TickType_t t0 = xTaskGetTickCount();
    menu(p, &ui, MENU_FLUSH);
    TickType_t used = xTaskGetTickCount() - t0;
    CHECK(used >= IEC_PRINTER_FLUSH_WAIT_MS - 20 && used < IEC_PRINTER_FLUSH_WAIT_MS + 1500);
    CHECK(rec.progress_hidden == 1);            /* the dialog went away */
    CHECK(rec.form_feed == 0);
}

/* Turn Off: the backlog is gone, the emulation is reset, the RAW file is closed, the ring is given back. */
static void test_turn_off(void)
{
    rec.reset();
    host_cfg = new ConfigStore;
    configure_printer(0);                       /* RAW */
    host_start_tasks = true;
    IecPrinter *p = new IecPrinter;
    UserInterface ui;
    for (int i = 0; i < 300; i++) p->push_data('B');    /* more than one buffer: the file is open */
    CHECK(wait_until([] { return rec.file_is_open == 1; }, 2000));
    for (int i = 0; i < 100; i++) p->push_data('C');
    CHECK(p->spool.allocated());
    int configures = rec.configure;
    menu(p, &ui, MENU_OFF);
    CHECK(!p->is_enabled());
    CHECK(rec.configure == configures + 1);
    CHECK(wait_until(file_closed, 2000));       /* no RAW file stays open */
    CHECK(wait_until(mps_reset_done, 2000));    /* the emulation is reset */
    for (int i = 0; i < 2000 && p->spool.allocated(); i++) usleep(1000);
    CHECK(!p->spool.allocated());               /* and the ring is given back */
    CHECK(p->spool.pending() == 0);
}

static void test_reset(void)
{
    rec.reset();
    host_cfg = new ConfigStore;
    configure_printer(2);
    host_start_tasks = true;
    IecPrinter *p = new IecPrinter;
    UserInterface ui;
    for (int i = 0; i < 100; i++) p->push_data('A');
    int configures = rec.configure;
    menu(p, &ui, MENU_RESET);
    CHECK(wait_until(mps_reset_done, 2000));
    CHECK(rec.form_feed == 0);                  /* reset is not an eject */
    CHECK(rec.configure == configures);         /* and does not touch the bus */
}

/* One test in a thread with a bounded time: a test that blocks fails a check, it does not hang. */
struct Bounded {
    void (*fn)(void);
    std::atomic<bool> done;
};
static void *bounded_main(void *arg)
{
    Bounded *b = (Bounded *) arg;
    b->fn();
    b->done = true;
    return NULL;
}
static void run_bounded(const char *name, void (*fn)(void), int seconds)
{
    Bounded *b = new Bounded;
    b->fn = fn;
    b->done = false;
    pthread_t t;
    pthread_create(&t, NULL, bounded_main, b);
    for (int i = 0; i < seconds * 100 && !b->done; i++)
        usleep(10000);
    checks++;
    if (b->done) {
        pthread_join(t, NULL);
        delete b;
    } else {
        failures++;
        printf("FAIL %s: did not finish in %d s (blocked)\n", name, seconds);
        pthread_cancel(t);
        pthread_join(t, NULL);
    }
}
#define RUN(t) run_bounded(#t, t, 15)

int main(void)
{
    RUN(test_flush_served);
    RUN(test_flush_bounded);
    RUN(test_turn_off);
    RUN(test_reset);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
