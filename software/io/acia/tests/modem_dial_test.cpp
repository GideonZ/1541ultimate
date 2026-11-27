/*
 * Runs the modem's real dial code (Modem::Caller in modem.cc) against a scripted network,
 * and counts the sockets it opens and closes. Every dial that does not end in a connection
 * has to give its socket back: the stack has 16 netconns for all services together.
 */
#include "modem.cc"

#include <pthread.h>
#include <unistd.h>

// ---- the scripted network ------------------------------------------------------------
static int next_fd = 10;
static int opened = 0, closed = 0;
static bool resolve_ok = true;
static bool connect_ok = false;
static volatile int responses = 0;

extern "C" {
int host_modem_socket(int, int, int) { opened++; return next_fd++; }
int host_modem_connect(int, const struct sockaddr *, socklen_t) { return connect_ok ? 0 : -1; }
int host_modem_closesocket(int) { closed++; return 0; }
ssize_t host_modem_recv(int, void *, size_t, int) { return 0; }
ssize_t host_modem_send(int, const void *, size_t n, int) { return (ssize_t)n; }
int host_modem_setsockopt(int, int, int, const void *, socklen_t) { return 0; }
int host_modem_gethostbyname_r(const char *, struct hostent *h, char *, size_t, struct hostent **out, int *)
{
    static char addr[4] = { 10, 0, 0, 1 };
    static char *list[2] = { addr, NULL };
    if (!resolve_ok) {
        *out = NULL;
        return 1;
    }
    h->h_addr_list = list;
    h->h_length = 4;
    *out = h;
    return 0;
}
}

// ---- the hardware and RTOS around it -------------------------------------------------
Acia acia(0);
Acia::Acia(uint32_t) {}
int Acia::SendToRx(uint8_t *, int) { responses = responses + 1; return 0; }
int Acia::init(uint16_t, bool, QueueHandle_t, QueueHandle_t, DataBuffer *) { return 0; }
void Acia::deinit(void) {}
void Acia::SetHS(uint8_t) {}
void Acia::SetDCD(uint8_t) {}
void Acia::SetDSR(uint8_t) {}
void Acia::SetCTS(uint8_t) {}
void Acia::EnableRTSInRx(uint8_t) {}
void Acia::SetRxPushback(uint8_t) {}
int Acia::GetRxSpace(void) { return 0; }
volatile uint8_t *Acia::GetRxPointer(void) { return NULL; }
void Acia::AdvanceRx(int) {}
InitFunction::InitFunction(const char *, initFunction_t, void *, void *, int) {}
InitFunction::~InitFunction() {}
uint32_t getFpgaCapabilities(void) { return 0; }
ListenerSocket::ListenerSocket(const char *, SpawnFunction_t, const char *) {}
ListenerSocket::~ListenerSocket() {}
int ListenerSocket::Start(int) { return 0; }

static Modem *m;
static void *caller_thread(void *) { m->Caller(); return NULL; }

static void dial(const char *host)
{
    ModemCommand_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    strcpy(cmd.command, host);
    cmd.length = (int)strlen(host);
    xQueueSend(m->connectQueue, &cmd, portMAX_DELAY);
}

// How long to wait for the modem task, in ms: for a result code, and for the socket to be given back.
static const int kWaitMs = 5000;

// Dials `n` times, one after the other, and waits for each result code. The result code is sent
// before the socket is closed, so after the last dial it also waits until every socket that was
// opened has been closed (or kWaitMs has passed, which check() then reports as a leak).
static void dial_n(int n, const char *host)
{
    for (int i = 0; i < n; i++) {
        int target = responses + (connect_ok ? 2 : 1);
        dial(host);
        for (int spins = 0; responses < target && spins < kWaitMs; spins++) {
            usleep(1000);
        }
    }
    for (int spins = 0; opened != closed && spins < kWaitMs; spins++) {
        usleep(1000);
    }
}

static int failures = 0;
static int base_opened = 0, base_closed = 0;
static void check(const char *what)
{
    int leaked = (opened - base_opened) - (closed - base_closed);
    printf("%-36s opened=%d closed=%d ", what, opened - base_opened, closed - base_closed);
    base_opened = opened;
    base_closed = closed;
    bool ok = leaked == 0;
    printf("leaked=%d %s\n", leaked, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

int main()
{
    m = new Modem();
    pthread_t t;
    pthread_create(&t, NULL, caller_thread, NULL);

    // a closed or unreachable host: the connect fails
    resolve_ok = true; connect_ok = false;
    dial_n(12, "closed.example:9");
    check("12 dials, connect refused");

    // a name that does not resolve never opens a socket at all
    int before = opened;
    resolve_ok = false;
    dial_n(3, "nowhere.example:9");
    check("3 dials, name lookup fails");
    if (opened != before) { printf("FAIL: a socket was opened for an unresolved name\n"); failures++; }

    // a dial that connects hands the socket back when the call ends
    resolve_ok = true; connect_ok = true;
    dial_n(2, "open.example:23");
    check("2 dials, connected then hung up");

    printf(failures ? "FAILED\n" : "PASSED\n");
    return failures ? 1 : 0;
}
