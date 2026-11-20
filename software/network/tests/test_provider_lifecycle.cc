#include <stdio.h>
#include <string.h>
#include "provider_lifecycle.h"

static int checks, failures;
static void expect(bool value, const char *label)
{
    ++checks;
    if (!value) { ++failures; printf("FAIL %s\n", label); }
}
static size_t reply(uint8_t *packet, const uint8_t *request, uint8_t id)
{
    memset(packet, 0, 128); memcpy(packet, request, 4);
    packet[4] = 1; packet[6] = 15;
    memcpy(packet + 8, "ESP32", 6);
    uint8_t boot[PROVIDER_ID_SIZE] = {}; boot[0] = id;
    return provider_identity_append(packet, 128, 14, request + 8, boot);
}
static void wire_validation()
{
    ProviderLifecycle lifecycle;
    uint8_t request[32] = {}, packet[128], decoded[16], cookie[8];
    expect(lifecycle.begin(0, request), "first probe");
    for (unsigned size = 0; size <= 32; ++size)
        expect(bool(provider_probe_cookie(request, size, cookie)) == (size == 16), "exact request size");
    size_t size = reply(packet, request, 1);
    for (unsigned n = 0; n <= 128; ++n)
        expect(bool(provider_identity_read(packet, n, request + 8, decoded)) == (n == size), "bounded exact reply size");
    for (unsigned i = 0; i < size; ++i) {
        // Version/name and nonzero identity bytes are data, not framing fields.
        if ((i >= 4 && i < 13) || i >= 30) continue;
        uint8_t damaged[128]; memcpy(damaged, packet, sizeof(packet)); damaged[i] ^= 0x80;
        expect(!provider_identity_read(damaged, size, request + 8, decoded), "damaged header framing version or cookie rejected");
    }
    reply(packet, request, 0);
    expect(!provider_identity_read(packet, size, request + 8, decoded), "zero boot identity unavailable");
    uint8_t boot[16] = {1}; memset(packet, 0xa5, sizeof(packet));
    expect(provider_identity_append(packet, 45, 14, request + 8, boot) == 0 && packet[14] == 0xa5,
           "insufficient output capacity leaves buffer unchanged");
    expect(!provider_probe_cookie(request, 4, cookie), "legacy identify request not extended");
}
static void lifecycle_cases()
{
    ProviderLifecycle life;
    uint8_t request[16], previous[128], packet[128];
    auto before = life.token();
    expect(!life.identified() && life.begin(0, request), "starts unavailable with pending probe");
    expect(!life.begin(1, request), "one outstanding probe");
    size_t size = reply(previous, request, 1);
    expect(life.receive(previous, size, 10) && life.identified() && before.cancelled(), "first identity creates fresh lifetime");
    auto first = life.token();
    uint8_t copied[PROVIDER_ID_SIZE] = {}; OperationCancellation captured;
    expect(life.snapshot(copied, captured) && copied[0] == 1 && !captured.cancelled(), "coherent boot and lifetime snapshot");
    expect(!life.receive(previous, size, 11) && !first.cancelled(), "duplicate reply cannot mutate lifetime");
    expect(life.begin(5000, request), "scheduled liveness probe");
    expect(!life.receive(previous, size, 5001) && !first.cancelled(), "old probe reply rejected without rolling boot backward");
    size = reply(packet, request, 1);
    expect(life.receive(packet, size, 5002) && !first.cancelled(), "same boot keeps active session valid");
    expect(life.begin(10000, request), "restart probe");
    size = reply(packet, request, 2);
    expect(life.receive(packet, size, 10001) && first.cancelled() && life.identified(), "changed ESP boot invalidates old session");
    expect(captured.cancelled(), "snapshot lifetime retires with boot change");
    auto second = life.token();
    expect(!second.cancelled() && !life.receive(previous, size, 10002), "late old boot cannot revoke new session");
    expect(life.begin(15000, request), "timeout probe");
    expect(!life.expire(16499) && !second.cancelled(), "deadline not yet reached");
    expect(life.expire(16500) && second.cancelled() && !life.identified(), "silent ESP reset or lost link fails closed at deadline");
    size = reply(packet, request, 2);
    expect(!life.receive(packet, size, 16501), "late reply cannot revive expired lifetime");
    expect(life.begin(20000, request), "explicit fresh probe after failure");
    size = reply(packet, request, 2);
    expect(life.receive(packet, size, 20001), "same boot recovery still requires new response");
    auto recovered = life.token();
    life.invalidate(); life.invalidate();
    memset(copied, 0xa5, sizeof(copied));
    expect(!life.snapshot(copied, captured) && copied[0] == 0xa5, "unavailable snapshot preserves output");
    expect(recovered.cancelled() && !life.identified(), "stop and disconnect retire lifetime");
    ProviderLifecycle legacy;
    expect(legacy.begin(0, request), "old firmware probe");
    reply(packet, request, 1);
    expect(!legacy.receive(packet, 17, 10) && !legacy.identified(), "legacy version string cannot authorize provider");
    expect(legacy.expire(1500), "legacy firmware times out without hanging");
    ProviderLifecycle wrapped;
    uint32_t start = UINT32_MAX - 500;
    expect(wrapped.begin(start, request), "probe before timer wrap");
    auto pending = wrapped.token();
    expect(!wrapped.expire(start + 1499) && wrapped.expire(start + 1500) && pending.cancelled(), "timeout across unsigned clock wrap");
    ProviderLifecycle exhausted(UINT64_MAX);
    expect(!exhausted.begin(5000, request) && !exhausted.identified(), "probe counter never wraps");
}
int main()
{
    wire_validation(); lifecycle_cases();
    printf("Provider lifecycle: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
