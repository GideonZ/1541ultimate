#include "controller_probe.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

static std::vector<uint8_t> reply(unsigned major = 1, unsigned minor = 20)
{
    std::vector<uint8_t> p(8);
    p[0] = 2; p[1] = 0xfe; p[2] = 0x37; p[3] = 0xd1;
    p[4] = major; p[5] = major >> 8; p[6] = minor; p[7] = minor >> 8;
    char name[64]; snprintf(name, sizeof(name), "ESP32 WiFi Bridge V%u.%u", major, minor);
    p.insert(p.end(), name, name + strlen(name) + 1);
    return p;
}
struct FakeIO {
    uint8_t flow = 5, divisor = 19;
    unsigned ticks = 0, sends = 0, resets = 0, pops = 0;
    bool freeze = false, overflow = false, no_reply = false, bad_reply = false, queued = false;
    uint8_t request[4] = {};
    std::vector<uint8_t> packet = reply();
    std::vector<unsigned> writes;
    uint16_t milliseconds() { return freeze ? 0 : ticks++ / 10; }
    uint8_t read(unsigned r) {
        if (r == 0) return divisor;
        if (r == 1) return 0;
        if (r == 3) return flow;
        if (r == 2) return 0xc0 | (queued ? 32 : 0) | (overflow ? 8 : 0);
        if (r == 8) return packet.size();
        if (r == 9) return packet.size() >> 8;
        assert(false); return 0;
    }
    void write(unsigned r, uint8_t v) {
        assert(r == 2 || r == 3 || r == 11);
        writes.push_back(r);
        if (r == 3) { assert(v == (flow | 128)); ++resets; queued = false; }
        if (r == 11) { ++pops; queued = false; }
    }
    void receive() {}
    void submit(unsigned n) {
        assert(n == 4 && request[0] == 2 && request[1] == 0xfe);
        assert(++sends == 1);
        packet[2] = request[2]; packet[3] = request[3];
        if (bad_reply) packet[0] = 3;
        queued = !no_reply;
    }
    uint8_t *tx() { return request; }
    const uint8_t *rx() { return packet.data(); }
};

int main()
{
    unsigned checks = 0;
    ControllerIdentity id;
    auto p = reply();
    auto parse = [&](const std::vector<uint8_t> &v) {
        return controller_identity_parse(v.data(), v.size(), 0xfe, 0xd137, id);
    };
    assert(parse(p) && id.major == 1 && id.minor == 20); ++checks;
    auto padded = p; padded.insert(padded.end(), 3, 0xa5);
    assert(parse(padded)); ++checks;
    for (size_t i = 0; i < p.size(); ++i) {
        auto short_reply = p; short_reply.resize(i);
        assert(!parse(short_reply) && id.major == 0 && id.name[0] == 0); ++checks;
    }
    for (size_t offset : {size_t(0),size_t(1),size_t(2),size_t(3),size_t(4),size_t(6),size_t(8),p.size()-1}) {
        auto changed = p; changed[offset] ^= 1;
        assert(!parse(changed)); ++checks;
    }
    for (unsigned n : {1u,2u,4u,60u}) {
        auto extended = p; extended.insert(extended.end(), n, 0);
        assert(!parse(extended)); ++checks;
    }
    auto control = p; control[10] = 27; assert(!parse(control)); ++checks;
    assert(!controller_identity_parse(nullptr, 12, 0xfe, 0xd137, id)); ++checks;
    for (unsigned minor : {0u,20u,21u,22u,65534u}) {
        auto version = reply(1, minor); assert(parse(version));
        assert(controller_install_decision(true,id,1,21) == (minor < 21 ? ControllerInstallDecision::Older : minor == 21 ? ControllerInstallDecision::EqualUnverified : ControllerInstallDecision::Newer));
        ++checks;
    }
    assert(controller_install_decision(false,id,1,21) == ControllerInstallDecision::Unknown); ++checks;
    for (int scenario = 0; scenario < 7; ++scenario) {
        FakeIO io;
        if (scenario == 1) io.flow = 0x35;
        if (scenario == 2) io.divisor = 86;
        if (scenario == 3) io.no_reply = true;
        if (scenario == 4) io.bad_reply = true;
        if (scenario == 5) io.overflow = true;
        if (scenario == 6) { io.freeze = true; io.no_reply = true; }
        auto result = controller_probe(io,id);
        auto expected = scenario == 0 ? ControllerProbeResult::Identified :
            scenario < 3 ? ControllerProbeResult::UnsupportedUart :
            scenario == 4 ? ControllerProbeResult::Malformed :
            scenario == 5 ? ControllerProbeResult::Overflow : ControllerProbeResult::Timeout;
        assert(result == expected);
        if (scenario == 1 || scenario == 2) assert(io.writes.empty() && io.sends == 0);
        else assert(io.resets == 2 && io.sends <= 1);
        ++checks;
    }
    printf("identity and probe: %u checks passed\n", checks);
}
