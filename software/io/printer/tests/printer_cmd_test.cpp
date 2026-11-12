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
std::atomic<int> host_ring_live(0);
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
static PrinterTaskArgs *held_task;
static void start_task(PrinterTaskArgs *t)
{
    pthread_t th;
    pthread_create(&th, NULL, task_thread, t);
    pthread_detach(th);
}
BaseType_t xTaskCreate(TaskFunction_t fn, const char *, int, void *arg, int, TaskHandle_t *h)
{
    *h = host_dummy_handle;
    PrinterTaskArgs *t = new PrinterTaskArgs;
    t->fn = fn; t->arg = arg;
    if (held_task) delete held_task;    /* one that nobody started */
    held_task = NULL;
    if (host_start_tasks)
        start_task(t);
    else
        held_task = t;                  /* the test starts it when it wants */
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
    CHECK(rec.popups == 0);                     /* the page was done in time: nothing to explain */
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
    CHECK(rec.popups == 1);                     /* and said why: the page is not lost, it is still being printed */
    CHECK(strstr(rec.popup_text, "still printing") != NULL);
    CHECK(rec.form_feed == 0);
    delete p;                                   /* nothing runs it: the object, its ring and its settings go with it */
    delete host_cfg; host_cfg = NULL;
    delete held_task; held_task = NULL;
}

/* Enable/disable and destroy, over and over, with nothing consuming: every ring comes back, no file stays open. */
static void test_teardown_cycles(void)
{
    host_start_tasks = false;
    const int base = host_ring_live;            /* rings of the printers the other tests keep running */
    for (int i = 0; i < 20; i++) {
        rec.reset();
        host_cfg = new ConfigStore;
        configure_printer(i % 2 ? 0 : 2);
        IecPrinter *p = new IecPrinter;
        UserInterface ui;
        CHECK(p->spool.allocated());            /* the ring exists while the printer is on */
        CHECK(host_ring_live == base + 1);
        for (int k = 0; k < 3000; k++) p->push_data('A');   /* more than the small ring holds: the backlog stays queued */
        menu(p, &ui, MENU_OFF);
        uint16_t ev; PrinterSpoolCtl ctl;           /* the printer task's part, done here: it takes the abort, which gives the ring back */
        while (p->spool.next(ev, ctl) != PRINTER_SPOOL_NONE) { }
        CHECK(host_ring_live == base);             /* Turn Off gives it back, backlog or not */
        CHECK(!p->spool.allocated());
        menu(p, &ui, MENU_ON);
        CHECK(host_ring_live == base + 1);
        for (int k = 0; k < 3000; k++) p->push_data('B');
        delete p;                               /* destroyed while on with a backlog: the ring is freed */
        CHECK(host_ring_live == base);
        CHECK(rec.file_is_open == 0);
        delete host_cfg; host_cfg = NULL;
        delete held_task; held_task = NULL;
    }
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

/* #1083: the bus is configured on every settings change, as master does; an address change is not a readdress. */
static void test_settings_configure_bus(void)
{
    rec.reset();
    host_cfg = new ConfigStore;
    configure_printer(2);
    host_start_tasks = true;
    IecPrinter *p = new IecPrinter;
    int configures = rec.configure;
    p->effectuate_settings();                   /* nothing changed */
    CHECK(rec.configure == configures + 1);
    host_cfg->values[0x30] = 5;                 /* another device number */
    p->effectuate_settings();
    CHECK(rec.configure == configures + 2);
    CHECK(rec.readdress == 0);
    host_cfg->values[0x38] = 0;                 /* switched off by the settings: still configure(), and the backlog goes */
    p->effectuate_settings();
    CHECK(rec.configure == configures + 3);
    CHECK(!p->is_enabled());
}

/* RAW -> PNG -> RAW with no byte between (settings changed through the configuration): each change is carried out,
 * so the file is closed by the first and the page ejected by the second, as the old firmware did at once. */
static void test_toggle_burst(void)
{
    rec.reset();
    host_cfg = new ConfigStore;
    configure_printer(0);                       /* RAW */
    host_start_tasks = true;
    IecPrinter *p = new IecPrinter;
    for (int i = 0; i < 300; i++) p->push_data('B');
    CHECK(wait_until([] { return rec.file_is_open == 1; }, 2000));
    host_cfg->values[0x32] = 2; p->effectuate_settings();     /* PNG */
    host_cfg->values[0x32] = 0; p->effectuate_settings();     /* RAW */
    CHECK(wait_until([] { return rec.file_closes >= 1; }, 2000));   /* RAW -> PNG closed the file */
    CHECK(wait_until([] { return rec.form_feed >= 1; }, 2000));     /* PNG -> RAW ejected the page */
    CHECK(rec.form_feed == 1 && rec.file_closes == 1);
}

/* The marker list is full while the printer task has not run: the toggles beyond it are merged, and the
 * output type that is swallowed is still passed on, so the page is ejected once more than without it. */
static void test_toggle_burst_full_list(void)
{
    rec.reset();
    host_cfg = new ConfigStore;
    configure_printer(0);                       /* RAW */
    host_start_tasks = false;
    IecPrinter *p = new IecPrinter;
    for (int i = 0; i < 300; i++) p->push_data('B');
    for (int i = 0; i < PRINTER_SPOOL_CTRL_DEPTH; i++) {
        host_cfg->values[0x32] = (i % 2 == 0) ? 2 : 0;
        p->effectuate_settings();               /* PNG, RAW, PNG, ... : the list is full, the last one is RAW */
    }
    host_cfg->values[0x32] = 2; p->effectuate_settings();     /* merged into the last: PNG ... */
    host_cfg->values[0x32] = 0; p->effectuate_settings();     /* ... and RAW again */
    start_task(held_task);
    CHECK(wait_until([] { return rec.form_feed >= 5; }, 3000));
    usleep(200000);
    CHECK(rec.form_feed == 5);                  /* 3 for the PNG -> RAW changes, 2 for the last marker (the passed-on toggle and the change); the stub counts every FormFeed call, the real MpsPrinter::FormFeed prints nothing on a clean page, so this is not a device page count */
    CHECK(rec.file_closes == 1);
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
    RUN(test_teardown_cycles);
    RUN(test_turn_off);
    RUN(test_reset);
    RUN(test_settings_configure_bus);
    RUN(test_toggle_burst);
    RUN(test_toggle_burst_full_list);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
