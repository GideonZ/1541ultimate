// Host test of the SD card driver (io/sd_card/sd_card.cc, sdio.cc, built with OS) against a
// simulated card in SPI mode. A write leaves the card busy, and a read makes it think,
// for thousands of polls, as a slow card does. The driver must do those waits with
// interrupts on, holding the card's mutex instead, and must not yield in them (equal-priority
// tasks already share the CPU by time slicing once interrupts are on; a yield per few polls
// hands a busy peer the rest of every tick); and the data must still arrive. The mutex is simulated as a real non-recursive FreeRTOS mutex: taking
// it twice, giving it when not held, or returning with it held fails the test.
#include <vector>
#include <deque>
#include <string>
#include <stdlib.h>
#include "sd_sim.h"
#include "sd_card.h"

// ---- simulated card ---------------------------------------------------------------------
SdSimData sd_sim_data;
SdSimData32 sd_sim_data32;
volatile uint8_t sd_sim_speed, sd_sim_ctrl, sd_sim_crc;
SdSimStats sd_sim;

static std::deque<uint8_t> miso;              // bytes the card sends next
static std::vector<uint8_t> cmd;              // command being received
static std::vector<std::vector<uint8_t> > sectors(64, std::vector<uint8_t>(512, 0));
static int busy_polls = 0, think_polls = 0;   // how slow the card is
static uint8_t read_r1 = 0x00;                // R1 the card gives to the next reads
static uint8_t read_token = 0xFE;             // token before the data (an error token stops the read)
static enum { IDLE, WANT_TOKEN, DATA, CRC } wstate = IDLE;
static uint32_t waddr; static std::vector<uint8_t> wbuf; static int crc_left;
static const uint8_t cid[16] = { 0x03, 'S', 'D', 'S', 'L', '0', '8', 'G', 0x80, 1, 2, 3, 4, 0x01, 0x23, 0x01 };
// CSD 2.0, C_SIZE 0x001DFF: (0x1DFF + 1) << 10 sectors
static const uint8_t csd[16] = { 0x40, 0x0E, 0x00, 0x32, 0x5B, 0x59, 0x00, 0x00, 0x1D, 0xFF, 0x7F, 0x80, 0x0A, 0x40, 0x00, 0x01 };

// ---- time model (SD_MODEL=1 only): what a same-priority, CPU-bound task costs the driver ----
// Each SPI byte takes t_byte (one poll is about 40 system clocks at 100 MHz, from spi.vhd and the
// io handshake). The tick is 5 ms. With a competitor of the same priority that never blocks:
// a tick that lands while the driver runs with interrupts on hands the competitor a whole tick
// (time slicing); a tick that lands with interrupts off waits until they are back on, then the
// competitor runs to the next tick; a yield hands the competitor the CPU until the next tick.
// Without a competitor a yield costs 1 us.
static bool model, competitor;
static double clk_us, t_byte_us = 0.4, tick_us = 5000.0;
static bool tick_pending;
static double next_tick(double t) { return (double)((long)(t / tick_us) + 1) * tick_us; }
static void model_advance(double dt)
{
    double before = clk_us;
    clk_us += dt;
    if (!competitor || (long)(clk_us / tick_us) == (long)(before / tick_us)) return;
    if (sd_sim.irq_depth) tick_pending = true;
    else clk_us += tick_us;
}

static void count_access(void)
{
    if (model) model_advance(t_byte_us);
    sd_sim.accesses++;
    if (sd_sim.irq_depth) {
        sd_sim.accesses_irq_off++;
        if (++sd_sim.stretch > sd_sim.longest_irq_off) sd_sim.longest_irq_off = sd_sim.stretch;
    }
    if (!sd_sim.lock_depth) sd_sim.accesses_unlocked++;
}

static void command(void)
{
    uint8_t c = cmd[0] & 0x3F;
    uint32_t arg = ((uint32_t)cmd[1] << 24) | ((uint32_t)cmd[2] << 16) | ((uint32_t)cmd[3] << 8) | cmd[4];
    uint32_t sector = arg / 512;   // the card answers CMD58 as standard capacity: byte addresses
    miso.clear();
    miso.push_back(0xFF);
    switch (c) {
    case 0:  miso.push_back(0x01); break;                             // GO_IDLE: idle
    case 8:  miso.push_back(0x01);                                    // SEND_IF_COND: V2 card
             miso.push_back(0x00); miso.push_back(0x00); miso.push_back(0x01); miso.push_back(0xAA); break;
    case 55: miso.push_back(0x01); break;                             // APP_CMD
    case 41: miso.push_back(0x00); break;                             // SD_SEND_OP_COND: ready
    case 58: miso.push_back(0x00);                                    // READ_OCR: not SDHC
             miso.push_back(0x80); miso.push_back(0xFF); miso.push_back(0xFF); miso.push_back(0xFF); break;
    case 9:  case 10:                                                 // SEND_CSD / SEND_CID
             miso.push_back(0x00); miso.push_back(0xFE);
             for (int i = 0; i < 16; i++) miso.push_back(c == 9 ? csd[i] : cid[i]);
             break;
    case 17:                                                          // READ_SINGLE_BLOCK
        miso.push_back(read_r1);
        if (read_r1) break;
        for (int i = 0; i < think_polls; i++) miso.push_back(0xFF);
        miso.push_back(read_token);
        if (read_token != 0xFE) break;
        for (int i = 0; i < 512; i++) miso.push_back(sectors[sector][i]);
        miso.push_back(0x12); miso.push_back(0x34); // CRC
        break;
    case 24:                                                          // WRITE_BLOCK
        miso.push_back(0x00);
        wstate = WANT_TOKEN; waddr = sector; wbuf.clear();
        break;
    default: miso.push_back(0x04); break;                             // illegal command
    }
}

static uint8_t exchange(uint8_t mosi)
{
    count_access();
    uint8_t out = 0xFF;
    if (!miso.empty()) { out = miso.front(); miso.pop_front(); }
    if (wstate == WANT_TOKEN) {
        if (mosi == 0xFE) wstate = DATA;
        return out;
    }
    if (wstate == DATA) {
        wbuf.push_back(mosi);
        if (wbuf.size() == 512) { wstate = CRC; crc_left = 2; }
        return out;
    }
    if (wstate == CRC) {
        if (--crc_left == 0) {
            sectors[waddr] = wbuf;
            wstate = IDLE;
            miso.clear();
            miso.push_back(0x05);                  // data accepted
            for (int i = 0; i < busy_polls; i++) miso.push_back(0x00);   // busy
        }
        return out;
    }
    if (cmd.empty() && (mosi & 0xC0) == 0x40) cmd.push_back(mosi);
    else if (!cmd.empty()) {
        cmd.push_back(mosi);
        if (cmd.size() == 6) { command(); cmd.clear(); }
    }
    return out;
}

SdSimData::operator uint8_t() { return exchange(0xFF); }
SdSimData &SdSimData::operator=(uint8_t v) { exchange(v); return *this; }
SdSimData32::operator uint32_t()
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)exchange(0xFF) << (8 * i);
    return v;
}
SdSimData32 &SdSimData32::operator=(uint32_t v)
{
    for (int i = 0; i < 4; i++) exchange((uint8_t)(v >> (8 * i)));
    return *this;
}

// ---- RTOS -------------------------------------------------------------------------------------
void sd_sim_irq_off(void) { sd_sim.irq_depth++; }
void sd_sim_irq_on(void)
{
    if (--sd_sim.irq_depth) return;
    sd_sim.stretch = 0;
    if (model && tick_pending) { tick_pending = false; clk_us = next_tick(clk_us); }
}
static int the_mutex;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &the_mutex; }
void vSemaphoreDelete(SemaphoreHandle_t) { }
// One task runs the driver. A non-recursive mutex taken again by its holder would block
// forever on the target; here it is counted and the test fails. When `contended`, another
// task holds the mutex for longer than any bounded timeout: only portMAX_DELAY gets it.
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t ticks)
{
    sd_sim.takes++;
    if (sd_sim.lock_depth) sd_sim.retakes++;
    if (sd_sim.contended && ticks != portMAX_DELAY) { sd_sim.take_fails++; return 0; }
    sd_sim.lock_depth++;
    return 1;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t)
{
    if (!sd_sim.lock_depth) { sd_sim.gives_unheld++; return 0; }
    sd_sim.lock_depth--;
    return 1;
}
BaseType_t xTaskGetSchedulerState(void) { return sd_sim.scheduler_state; }
void vTaskDelay(TickType_t) { sd_sim_yield(); }
void vTaskDelayUntil(TickType_t *, TickType_t) { sd_sim_yield(); }
BaseType_t xTaskDelayUntil(TickType_t *, TickType_t) { sd_sim_yield(); return 1; }

// giving up the CPU in the driver (taskYIELD, portYIELD, vTaskDelay, vTaskDelayUntil,
// xTaskDelayUntil) is a failure (see the top); counted so a test can say where
void sd_sim_yield(void)
{
    sd_sim.yields++;
    if (model) clk_us = competitor ? next_tick(clk_us) : clk_us + 1.0;
    if (sd_sim.irq_depth) sd_sim.yields_irq_off++;
    if (!sd_sim.lock_depth) sd_sim.yields_unlocked++;
}

// ---- tests ------------------------------------------------------------------------------------
static int failures, passes;
static void check(bool ok, const char *name, const std::string &d)
{
    printf("%s %s - %s\n", ok ? "PASS" : "FAIL", name, d.c_str());
    if (ok) passes++; else failures++;
}

static void reset(void)
{
    memset(&sd_sim, 0, sizeof sd_sim);
    sd_sim.scheduler_state = taskSCHEDULER_RUNNING;   // a yield, if any, would hand the CPU over
    busy_polls = 0; think_polls = 0; read_r1 = 0x00; read_token = 0xFE;
}

static std::string stats(void)
{
    char b[512];
    snprintf(b, sizeof b, "%ld accesses: %ld with interrupts off (longest stretch %ld), %ld without the mutex; "
             "%ld yields (%ld with interrupts off, %ld without the mutex); "
             "mutex: %ld takes, %ld retaken by its holder, %ld given unheld, %ld gave up, depth now %d",
             sd_sim.accesses, sd_sim.accesses_irq_off, sd_sim.longest_irq_off, sd_sim.accesses_unlocked,
             sd_sim.yields, sd_sim.yields_irq_off, sd_sim.yields_unlocked,
             sd_sim.takes, sd_sim.retakes, sd_sim.gives_unheld, sd_sim.take_fails, sd_sim.lock_depth);
    return b;
}

// every call: no access with interrupts off or without the mutex, the mutex used properly
// and released on return
static bool clean(void)
{
    return sd_sim.accesses_irq_off == 0 && sd_sim.accesses_unlocked == 0 && sd_sim.irq_depth == 0 &&
           sd_sim.lock_depth == 0 && sd_sim.retakes == 0 && sd_sim.gives_unheld == 0 && sd_sim.take_fails == 0 &&
           sd_sim.takes > 0 && sd_sim.yields == 0;
}

int main()
{
    SdCard card;
    std::vector<uint8_t> data(512);
    for (int i = 0; i < 512; i++) data[i] = (uint8_t)(i * 13 + 7);

    // a card is inserted: init() runs the whole SPI-mode handshake under the mutex
    reset();
    DSTATUS st = card.init();
    check(st == 0 && clean(), "init_runs_under_the_mutex", stats());

    // CID and CSD (the drive size FatFs asks for when the disk is attached)
    reset();
    uint32_t size = 0;
    DRESULT r = card.ioctl(GET_SECTOR_COUNT, &size);
    check(r == RES_OK && size == (uint32_t)(0x1DFF + 1) << 10 && clean(), "cid_csd_run_under_the_mutex", stats());

    // a write with a slow card: 20000 busy polls (about 8 ms of SPI bytes at 25 MHz)
    reset();
    busy_polls = 20000;
    r = card.write(data.data(), 5, 1);
    check(r == RES_OK && sectors[5] == data && clean(), "write_completes_with_interrupts_on_under_the_mutex", stats());
    check(sd_sim.yields == 0, "write_busy_wait_does_not_yield", stats());

    // a read the card takes a while to answer: 10000 polls before the data token
    reset();
    think_polls = 10000;
    std::vector<uint8_t> back(512, 0);
    r = card.read(back.data(), 5, 1);
    check(r == RES_OK && back == data && clean(), "read_returns_the_data_with_interrupts_on_under_the_mutex", stats());
    check(sd_sim.yields == 0, "read_wait_does_not_yield", stats());

    // several sectors in one call
    reset();
    busy_polls = 100; think_polls = 10;
    std::vector<uint8_t> three(3 * 512);
    for (size_t i = 0; i < three.size(); i++) three[i] = (uint8_t)(i * 5 + 1);
    r = card.write(three.data(), 10, 3);
    std::vector<uint8_t> back3(3 * 512, 0);
    DRESULT r2 = card.read(back3.data(), 10, 3);
    check(r == RES_OK && r2 == RES_OK && back3 == three && clean(), "three_sectors_round_trip", stats());

    // another task holds the mutex: the driver waits for it (no bounded take that gives up)
    reset();
    sd_sim.contended = true;
    busy_polls = 100; think_polls = 100;
    std::vector<uint8_t> backc(512, 0);
    r = card.write(data.data(), 20, 1);
    r2 = card.read(backc.data(), 20, 1);
    DRESULT r3 = card.ioctl(GET_SECTOR_COUNT, &size);
    DSTATUS st2 = card.init();
    check(r == RES_OK && r2 == RES_OK && r3 == RES_OK && st2 == 0 && backc == data && clean(),
          "contended_mutex_is_waited_for", stats());

    // error paths give the mutex back: R1 error on a read, an error token, a token timeout,
    // and a write the card never finishes
    reset();
    read_r1 = 0x04;
    r = card.read(back.data(), 5, 1);
    check(r == RES_ERROR && clean(), "read_r1_error_releases_the_mutex", stats());
    reset();
    think_polls = 50; read_token = 0x09;
    r = card.read(back.data(), 5, 1);
    check(r == RES_ERROR && clean(), "read_error_token_releases_the_mutex", stats());
    reset();
    think_polls = 240001;
    r = card.read(back.data(), 5, 1);
    check(r == RES_ERROR && clean(), "read_token_timeout_releases_the_mutex", stats());
    reset();
    busy_polls = 600001;
    r = card.write(data.data(), 6, 1);
    check(r == RES_ERROR && clean(), "write_busy_timeout_releases_the_mutex", stats());

    if (getenv("SD_MODEL")) {
        // 16 sectors read, then 16 written, per card speed, alone and with the competitor
        struct { const char *card; int think, busy; } cases[] = {
            { "fast card: read access 100 us, programming 250 us", 250, 625 },
            { "average card: 1 ms, 1 ms", 2500, 2500 },
            { "slow write: 1 ms, 10 ms", 2500, 25000 },
        };
        std::vector<uint8_t> buf16(16 * 512, 0x5A);
        model = true;
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            for (int comp = 0; comp < 2; comp++) {
                reset();
                competitor = comp; tick_pending = false;
                think_polls = cases[i].think; busy_polls = cases[i].busy;
                clk_us = 1234.5;                  // not on a tick
                double t0 = clk_us;
                card.write(buf16.data(), 30, 16);
                double tw = clk_us - t0; t0 = clk_us;
                card.read(buf16.data(), 30, 16);
                double tr = clk_us - t0;
                printf("MODEL %s, %s: read %.0f KB/s, write %.0f KB/s, %ld yields\n", cases[i].card,
                       comp ? "with a busy same-priority task" : "alone", 8.0 / (tr / 1e6), 8.0 / (tw / 1e6), sd_sim.yields);
            }
        }
        model = false;
    }

    printf("sd_card: %d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
