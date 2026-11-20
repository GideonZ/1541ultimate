// Host test of wifi_dedicated.cc: GET_RANDOM through the dedicated channel, and
// the channel's own waits, in the scripted world of world.cc.
//   make -C target/pc/linux/wifidedicated test
#include <string.h>
#include <initializer_list>
#include <vector>
#include "world.h"
#include "wifi_dedicated.h"
#include "wifi_random.h"
#include "check.h"
#undef printf

// The replies random_maker builds. 0 to 3 are the right answer.
enum {
    RIGHT = 0,
    LATE = 4,          // the answer to the previous request
    OTHER_COMMAND = 5, // the answer to another command
    REFUSED = 6,       // esp_err set, no bytes
    NO_SOURCE = 7,     // bytes, but no entropy source
    TRUNCATED = 8,     // cut short at a random size
    LONGER = 9,        // one byte more than asked for
    SHORT = 10,        // one byte missing; scripts only
};

// The bytes of every right answer made in this world, by sequence number.
struct made_t {
    uint16_t sequence;
    std::vector<uint8_t> data;
};
static std::vector<made_t> made;

static void random_maker(command_buf_t *rx, const uint8_t *req, int req_size, int kind)
{
    const rpc_get_random_req *request = (const rpc_get_random_req *)req;
    uint16_t sequence = (req_size >= 0) ? request->hdr.sequence : (uint16_t)world_rand(65536);
    uint16_t len = (req_size >= 0) ? request->length : 32;
    if (len > RANDOM_MAX_BYTES) {
        len = RANDOM_MAX_BYTES;
    }
    rpc_get_random_resp *r = (rpc_get_random_resp *)rx->data;
    memset(rx->data, 0, sizeof(rx->data));
    r->hdr.command = CMD_GET_RANDOM;
    r->hdr.thread = WIFI_DEDICATED_THREAD;
    r->hdr.sequence = sequence;
    r->esp_err = 0;
    r->source = ENTROPY_RADIO;
    r->length = len;
    for (int i = 0; i < len; i++) {
        r->data[i] = (uint8_t)world_rand(256);
    }
    rx->size = sizeof(rpc_get_random_resp) + len;
    switch (kind) {
        case LATE: r->hdr.sequence = sequence - 1; break;
        case OTHER_COMMAND: r->hdr.command = (uint8_t)(CMD_GET_RANDOM + 1 + world_rand(255)); break;
        case REFUSED: r->esp_err = 0x103; r->length = 0; rx->size = sizeof(rpc_get_random_resp); break;
        case NO_SOURCE: r->source = ENTROPY_NONE; break;
        case TRUNCATED: rx->size = world_rand(sizeof(rpc_get_random_resp) + len); break;
        case LONGER: r->length = len + 1; break;
        case SHORT: rx->size--; break;
        default: made.push_back({ sequence, std::vector<uint8_t>(r->data, r->data + len) }); break;
    }
}

static int count(const std::string &text, const std::string &what)
{
    int n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) {
        n++;
    }
    return n;
}

static bool printed(const char *line)
{
    return world_trace.find(std::string("print: ") + line) != std::string::npos;
}

// The right answer made for sequence, if its bytes are those at buf.
static bool from_right_answer(uint16_t sequence, const uint8_t *buf, uint16_t len)
{
    for (const made_t &m : made) {
        if ((m.sequence == sequence) && (m.data.size() == len) && (memcmp(m.data.data(), buf, len) == 0)) {
            return true;
        }
    }
    return false;
}

// A script step: kinds arriving delay ticks into the wait.
static world_step_t at(TickType_t delay, std::initializer_list<int> kinds, bool stale = false,
                       TickType_t preempt = 0)
{
    world_step_t s;
    memset(&s, 0, sizeof(s));
    s.delay = delay;
    s.preempt = preempt;
    for (int kind : kinds) {
        s.stale[s.count] = stale;
        s.kind[s.count++] = kind;
    }
    return s;
}

#define UNTOUCHED 0xEE
#define WAIT_TICKS 40 // GET_RANDOM's 200 ms

typedef struct {
    int result;
    uint16_t sequence; // the request's, if one was sent
    uint16_t advanced; // how far sequence_nr moved
    uint8_t buf[RANDOM_MAX_BYTES + 8];
} outcome_t;

static void get_random(uint16_t len, outcome_t *o)
{
    memset(o->buf, UNTOUCHED, sizeof(o->buf));
    o->sequence = sequence_nr;
    o->result = wifi_get_random(o->buf, len);
    o->advanced = (uint16_t)(sequence_nr - o->sequence);
}

static bool untouched(const outcome_t *o)
{
    for (size_t i = 0; i < sizeof(o->buf); i++) {
        if (o->buf[i] != UNTOUCHED) {
            return false;
        }
    }
    return true;
}

static bool channel_free(void)
{
    return !dedicated_waiting && (world_queued() == 0) && (world_suspended == 0) &&
           (world_trace.find("not suspended") == std::string::npos);
}

// One GET_RANDOM of 32 bytes in a calm world that answers with steps.
static void scripted(uint32_t seed, const world_step_t *steps, int n, outcome_t *o)
{
    made.clear();
    world_reset(seed, random_maker, true);
    world_script(steps, n);
    get_random(32, o);
}

static void test_answers(void)
{
    printf("GET_RANDOM: what each reply leads to\n");
    outcome_t o;
    const world_step_t right[] = { at(1, { RIGHT }) };
    scripted(1, right, 1, &o);
    check((o.result == 0) && from_right_answer(o.sequence, o.buf, 32) &&
          (o.buf[32] == UNTOUCHED) && (o.advanced == 1) && channel_free(),
          "the answer: 0, its 32 bytes and nothing past them, the sequence number moved on by one");

    struct {
        int kind;
        int result;
        const char *line;
        const char *what;
    } const single[] = {
        { LATE, WIFI_RANDOM_BUSY, "Get Random: no reply.", "an answer to the previous request is skipped: BUSY" },
        { OTHER_COMMAND, WIFI_RANDOM_BUSY, "Get Random: no reply.", "an answer to another command is skipped: BUSY" },
        { REFUSED, WIFI_RANDOM_REFUSED, "Get Random failed (2). Size: 12. Error: 259.", "a refusal: REFUSED" },
        { NO_SOURCE, WIFI_RANDOM_REFUSED, "Get Random failed (2). Size: 44. Error: 0.", "bytes without a source: REFUSED" },
        { LONGER, WIFI_RANDOM_INVALID, "Get Random failed (3). Size: 44. Error: 0.", "one byte too many: INVALID" },
        { SHORT, WIFI_RANDOM_INVALID, "Get Random failed (3). Size: 43. Error: 0.", "one byte missing: INVALID" },
    };
    for (const auto &c : single) {
        const world_step_t step[] = { at(1, { c.kind }) };
        scripted(2, step, 1, &o);
        check((o.result == c.result) && untouched(&o) && printed(c.line) && (o.advanced == 1) && channel_free(),
              "%s, no bytes, logged", c.what);
    }

    const world_step_t stale[] = { at(1, { RIGHT }, true) };
    scripted(3, stale, 1, &o);
    check((o.result == WIFI_RANDOM_BUSY) && untouched(&o) && (count(world_trace, "free b") == 0) && channel_free(),
          "the answer queued before a pool reset is dropped unread and not freed: BUSY");
    const world_step_t foreign_first[] = { at(1, { LATE, RIGHT }) };
    scripted(4, foreign_first, 1, &o);
    check((o.result == 0) && from_right_answer(o.sequence, o.buf, 32) && channel_free(),
          "a late answer, then the answer, in one wait: the answer is taken");
    const world_step_t two_waits[] = { at(5, { OTHER_COMMAND }), at(10, { RIGHT }) };
    scripted(5, two_waits, 2, &o);
    check((o.result == 0) && from_right_answer(o.sequence, o.buf, 32) && (count(world_trace, "receive ") == 3),
          "another command's answer, then the answer 10 ticks later: taken in the second wait");
    const world_step_t too_late[] = { at(WAIT_TICKS + 1, { RIGHT }) };
    scripted(6, too_late, 1, &o);
    check((o.result == WIFI_RANDOM_BUSY) && untouched(&o) && printed("Get Random: no reply.") && channel_free(),
          "an answer after 200 ms: BUSY");

    const world_step_t overshoot[] = { at(WAIT_TICKS - 1, { OTHER_COMMAND }, false, 5) };
    scripted(11, overshoot, 1, &o);
    check((o.result == WIFI_RANDOM_BUSY) && untouched(&o) && (world_wait_span <= WAIT_TICKS) &&
          (count(world_trace, "receive 0") == 1) && channel_free(),
          "a skipped reply just before the deadline, then preempted past it: BUSY, no wait beyond 200 ms "
          "(%llu ticks)", (unsigned long long)world_wait_span);
    const world_step_t skipped_early[] = { at(10, { OTHER_COMMAND }) };
    scripted(12, skipped_early, 1, &o);
    check((o.result == WIFI_RANDOM_BUSY) && (world_wait_span == WAIT_TICKS) && channel_free(),
          "a reply skipped 10 ticks in: the wait goes on to the deadline, not 200 ms past the skip (%llu ticks)",
          (unsigned long long)world_wait_span);

    // The wait ends on a skipped reply with the timeout spent; what is still
    // queued behind it decides the result.
    const world_step_t tail_right[] = { at(WAIT_TICKS, { OTHER_COMMAND, RIGHT }) };
    scripted(7, tail_right, 1, &o);
    check((o.result == 0) && from_right_answer(o.sequence, o.buf, 32) && channel_free(),
          "the answer queued behind a skipped reply as the time runs out is still taken");
    const world_step_t tail_foreign[] = { at(WAIT_TICKS, { OTHER_COMMAND, LATE }) };
    scripted(8, tail_foreign, 1, &o);
    check((o.result == WIFI_RANDOM_BUSY) && untouched(&o) && printed("Get Random: no reply.") && channel_free(),
          "only foreign replies queued as the time runs out: BUSY, not FOREIGN (%d)", o.result);
    const world_step_t tail_refused[] = { at(WAIT_TICKS, { OTHER_COMMAND, REFUSED }) };
    scripted(9, tail_refused, 1, &o);
    check((o.result == WIFI_RANDOM_REFUSED) && untouched(&o) && channel_free(),
          "a refusal queued behind a skipped reply as the time runs out: REFUSED");
    const world_step_t burst[] = { at(1, { RIGHT, RIGHT, RIGHT, RIGHT }) };
    scripted(10, burst, 1, &o);
    check((o.result == 0) && (count(world_trace, "free b") == 4) && channel_free(),
          "a burst of four: the first is taken, the other three released, all freed");
}

static void test_orderings(void)
{
    printf("GET_RANDOM: the deadline and the claim, around the queue\n");
    outcome_t o;
    const world_step_t nothing[] = { at(100000, {}) };
    made.clear();
    world_reset(13, random_maker, true);
    world_script(nothing, 1);
    world_transmit_ticks = 30;
    get_random(32, &o);
    check((o.result == WIFI_RANDOM_BUSY) && (world_wait_span == WAIT_TICKS) && channel_free(),
          "a request that takes 150 ms to queue still waits 200 ms from then (%llu ticks)",
          (unsigned long long)world_wait_span);
    made.clear();
    world_reset(14, random_maker, true);
    world_script(nothing, 1);
    world_late_after_poll = 1;
    get_random(32, &o);
    check((o.result == WIFI_RANDOM_BUSY) && channel_free(),
          "a reply arriving as the queue is drained finds the claim released: nothing is left queued");
}

static void test_sequence(void)
{
    printf("GET_RANDOM: the request and its sequence number\n");
    made.clear();
    world_reset(20, random_maker, true);
    uint16_t first = sequence_nr;
    outcome_t a, b;
    get_random(300, &a);
    std::string request = world_trace.substr(world_trace.find("transmit"));
    char want[64];
    snprintf(want, sizeof(want), "transmit 6: %02x %02x %02x %02x 2c 01\n", CMD_GET_RANDOM,
             WIFI_DEDICATED_THREAD, first & 0xFF, first >> 8);
    check(request.compare(0, strlen(want), want) == 0,
          "the request: CMD_GET_RANDOM, the dedicated thread, the sequence number, the length (%s)", want);
    world_trace.clear();
    const world_step_t late[] = { at(1, { LATE, RIGHT }) };
    world_script(late, 1);
    get_random(300, &b);
    check((a.result == 0) && (b.result == 0) && (b.sequence == (uint16_t)(first + 1)) &&
          (sequence_nr == (uint16_t)(first + 2)) && from_right_answer(b.sequence, b.buf, 300) &&
          !from_right_answer(a.sequence, b.buf, 300),
          "two calls: two sequence numbers, and the first's late answer is not taken for the second's");
}

static void test_unsent(void)
{
    printf("GET_RANDOM: when nothing is sent\n");
    outcome_t o;
    world_reset(30, random_maker, true);
    world_ready = false;
    get_random(32, &o);
    check((o.result == WIFI_RANDOM_NO_MODULE) && untouched(&o) && world_trace.empty() && (o.advanced == 0),
          "no module application: NO_MODULE, nothing tried");
    world_reset(31, random_maker, true);
    dedicated_replies = NULL;
    get_random(32, &o);
    check((o.result == WIFI_RANDOM_NO_MODULE) && world_trace.empty(), "no reply queue: NO_MODULE, nothing tried");
    world_reset(32, random_maker, true);
    dedicated_waiting = true;
    get_random(32, &o);
    check((o.result == WIFI_RANDOM_BUSY) && untouched(&o) && dedicated_waiting && (world_transmits == 0) &&
          (o.advanced == 0),
          "another request outstanding: BUSY at once, its claim left alone, nothing sent");
    world_reset(33, random_maker, true);
    world_no_buffer = true;
    get_random(32, &o);
    check((o.result == WIFI_RANDOM_BUSY) && untouched(&o) && printed("No buffer for WiFi command.") &&
          (world_transmits == 0) && channel_free(),
          "no buffer: BUSY, logged, the channel free again");
    world_reset(34, random_maker, true);
    world_no_transmit = true;
    get_random(32, &o);
    check((o.result == WIFI_RANDOM_BUSY) && untouched(&o) && printed("Get Random: no reply.") && channel_free(),
          "a failed transmit: BUSY, the channel free again");
}

// Seeds of a world where anything can go wrong: replies right, late, foreign,
// refused, short, stale, in bursts, after preemption, near the tick wrap.
static void test_random_world(void)
{
    printf("GET_RANDOM in a world where anything goes wrong, seed by seed\n");
    static const uint16_t lengths[] = { 1, 2, 3, 31, 32, 255, 256, 257, 300, 511, 512 };
    const uint32_t seeds = 100000;
    int wrong = 0;
    int results[5] = { 0 };
    int with[6] = { 0 };
    const char *features[6] = { "a stale reply dropped", "the queue overflowing", "a reply refused or invalid",
                                "no buffer", "a failed transmit", "a reply released after the wait" };
    static outcome_t o;
    for (uint32_t seed = 1; seed <= seeds; seed++) {
        made.clear();
        world_reset(seed, random_maker, false);
        bool held = dedicated_waiting;
        bool gated = held || !world_ready || !dedicated_replies;
        uint16_t len = (world_rand(4) == 0) ? (uint16_t)(1 + world_rand(RANDOM_MAX_BYTES))
                                            : lengths[world_rand(sizeof(lengths) / sizeof(lengths[0]))];
        get_random(len, &o);
        bool ok = (o.result >= 0) && (o.result <= WIFI_RANDOM_NO_MODULE) && (o.advanced == (gated ? 0 : 1)) &&
                  (dedicated_waiting == held) && (world_suspended == 0) &&
                  (world_trace.find("not suspended") == std::string::npos) && (gated || (world_queued() == 0)) &&
                  (world_wait_span <= WAIT_TICKS);
        if (o.result == 0) {
            ok = ok && from_right_answer(o.sequence, o.buf, len) &&
                 (o.buf[len] == UNTOUCHED);
        } else {
            ok = ok && untouched(&o);
        }
        if (!ok) {
            if (wrong++ < 3) {
                printf("  seed %u: result %d, sequence moved %u, waited up to %llu ticks\n", seed, o.result,
                       o.advanced, (unsigned long long)world_wait_span);
            }
            continue;
        }
        results[o.result]++;
        const std::string &t = world_trace;
        with[0] += t.find("suspend\nresume\n") != std::string::npos; // looked at, not freed
        with[1] += t.find("(full)") != std::string::npos;
        with[2] += t.find("Get Random failed") != std::string::npos;
        with[3] += t.find("getbuffer 4 -> none") != std::string::npos;
        with[4] += t.find("-> failed") != std::string::npos;
        with[5] += t.find("receive 0 -> b") != std::string::npos;
    }
    check(wrong == 0, "%u seeds: bytes only from this request's answer, no wait past the 200 ms deadline, the "
          "sequence number and the channel as they should be (%d not)", seeds, wrong);
    printf("        results: 0 OK %d, 1 BUSY %d, 2 REFUSED %d, 3 INVALID %d, 4 NO_MODULE %d\n",
           results[0], results[1], results[2], results[3], results[4]);
    for (int i = 0; i < 6; i++) {
        check(with[i] > 100, "the seeds include %s (%d)", features[i], with[i]);
    }
}

// A call of the channel's own, with its own timeout.
static int takes;
static uint8_t taken[32];

static int fill_test(uint8_t *request, void *context)
{
    ((rpc_get_random_req *)request)->length = sizeof(taken);
    return sizeof(rpc_get_random_req);
}

static int take_test(const uint8_t *reply, int size, uint16_t sequence, void *context)
{
    int retval = wifi_random_take_reply(reply, size, sequence, taken, sizeof(taken));
    takes += (retval != DEDICATED_FOREIGN);
    return retval;
}

static int call(uint32_t timeout_ms)
{
    takes = 0;
    const dedicated_call_t c = { "Test", CMD_GET_RANDOM, timeout_ms, fill_test, take_test, NULL };
    return wifi_dedicated_call(&c);
}

static void test_waits(void)
{
    printf("The channel's wait: the caller's timeout, in one go\n");
    const world_step_t nothing[] = { at(100000, {}) };
    world_reset(40, random_maker, true);
    world_script(nothing, 1);
    int r = call(5000);
    check((r == DEDICATED_BUSY) && (count(world_trace, "receive 1000") == 1) && (takes == 0) &&
          printed("Test: no reply.") && (world_wait_span == 1000) && channel_free(),
          "nothing arrives: one wait of the whole timeout (5 s = 1000 ticks), then BUSY");
    const world_step_t late[] = { at(999, { RIGHT }) };
    world_reset(41, random_maker, true);
    world_script(late, 1);
    r = call(5000);
    check((r == 0) && (takes == 1) && channel_free(), "the answer a tick before the timeout is taken");
}

int main(void)
{
    test_answers();
    test_sequence();
    test_orderings();
    test_unsent();
    test_random_world();
    test_waits();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
