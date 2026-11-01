#include "provider_control_test_port.h"
#include "provider_manager.h"
struct WorkerTlsContext {
    provider_control::Worker& worker;
    provider_session::Ticket ticket;
    provider_control::Frame interrupt;
    provider_control::Manager *manager;
};
static bool worker_cancelled(Platform *p, int64_t now)
{
    WorkerTlsContext *context = static_cast<WorkerTlsContext *>(p->worker_context);
    if (p->restart_at && now >= p->restart_at) {
        p->restart_at = 0;
        uint8_t bytes[provider_control::FrameSize];
        provider_session::Identity boot = {}; boot.bytes[0] = 1;
        bool made = context->interrupt.opcode == provider_control::Close ?
            context->manager->close(context->ticket, now, bytes) : context->manager->offer(boot, {}, now, bytes);
        expect(made && provider_control::decode(bytes, sizeof(bytes), context->interrupt), "manager builds cancellation control");
        expect(context->worker.submit(bytes, sizeof(bytes)), "control ingress while TLS blocked");
    }
    return !context->manager->current(context->ticket, now) || !context->worker.current(context->ticket, now);
}
static provider_control::Frame worker_reply(ControlTestPort& port)
{
    expect(port.replies.size() == 1, "one complete correlated reply");
    if (port.replies.empty()) return {};
    auto result = port.replies.front(); port.replies.pop_front();
    expect(result.status == provider_control::Ok, "control operation succeeded");
    // The firmware formatter supports 32-bit conversions only.
    printf("CONTROL opcode=%u request_hi=%u request_lo=%u status=%u epoch_hi=%u epoch_lo=%u\n",
        result.opcode, unsigned(result.request >> 32), unsigned(result.request), result.status,
        unsigned(result.grant.challenge.epoch >> 32), unsigned(result.grant.challenge.epoch));
    return result;
}
static void worker_exchange(const char *mode, const char *ca, int primary, int recovery)
{
    using namespace provider_control;
    using namespace provider_session;
    int initial_allocations = live_allocations, initial_descriptors = descriptors();
    {
        bool loss = strcmp(mode, "worker-loss-read-cancel") == 0;
        bool close = strcmp(mode, "worker-close-read-cancel") == 0;
        bool handshake = strcmp(mode, "worker-handshake-cancel") == 0;
        Identity boot = {}; boot.bytes[0] = 1;
        ControlTestPort port; Worker worker(port, boot, Required, {1000, loss ? 350u : 2000u, 250});
        Platform platform(ca);
        Manager manager({&platform, [](void *p, Identity& value) { return entropy(p, value.bytes, sizeof(value.bytes)) == 0; }},
                        {500, loss ? 350u : 2000u});
        WorkerTlsContext context = {worker, {}, {}, &manager};
        platform.worker_context = &context; platform.worker_cancelled = worker_cancelled;
        tls_stream_port callbacks = { &platform, clock_read, clock_ready, cancelled,
            entropy, trust, open_socket, read_socket, write_socket, close_socket };
        int cancellations = 0, releases = 0;
        for (unsigned phase = 0; phase < 2; ++phase) {
            uint8_t bytes[FrameSize];
            expect(manager.offer(boot, {}, now_ms(), bytes) && worker.submit(bytes, sizeof(bytes)), "wire offer submitted");
            worker.step(now_ms()); Frame offered = worker_reply(port); encode(offered, bytes);
            expect(manager.receive(bytes, sizeof(bytes), now_ms()), "manager validates offer");
            expect(manager.initialize(now_ms(), bytes) && worker.submit(bytes, sizeof(bytes)), "wire initialization submitted");
            worker.step(now_ms()); Frame initialized = worker_reply(port); encode(initialized, bytes);
            expect(manager.receive(bytes, sizeof(bytes), now_ms()) && manager.reserve(now_ms(), context.ticket), "manager validates grant and reserves ticket");
            Grant grant = context.ticket.grant;
            OwnerResource *resource = new OwnerResource(callbacks, platform, cancellations, releases);
            expect(worker.admit(grant, 1, resource, now_ms(), context.ticket), "worker adopts real TLS resource");
            int64_t begin = now_ms();
            if (!phase && !loss) {
                context.interrupt.opcode = close ? Close : Offer;
                platform.restart_at = begin + 150;
            }
            int opened = resource->connection.open("tls.lab.test", phase ? recovery : primary);
            expect((opened >= 0) == (phase || !handshake), "worker authenticated open");
            if (opened >= 0) {
                const char request[] = "GET /probe HTTP/1.1\r\nHost: tls.lab.test\r\nConnection: close\r\n\r\n";
                size_t sent = 0;
                while (sent < sizeof(request) - 1) {
                    int n = resource->connection.write((const uint8_t *)request + sent, sizeof(request) - 1 - sent);
                    if (n <= 0) break;
                    sent += n;
                }
                expect(sent == sizeof(request) - 1, "worker exact request sent");
                uint8_t bytes[256] = {}; size_t received = 0;
                while (received < sizeof(bytes) - 1) {
                    int n = resource->connection.read(bytes + received, sizeof(bytes) - 1 - received);
                    if (n <= 0) break;
                    received += n;
                    if (phase && strstr((const char *)bytes, "tls-ok!\n")) break;
                }
                expect(phase ? strstr((const char *)bytes, "tls-ok!\n") != NULL : received == 0, "worker recovery or no stale data");
            }
            auto diagnostic = resource->connection.diagnostic();
            int64_t elapsed = now_ms() - begin;
            expect(diagnostic.status == (phase ? TLS_OK : TLS_CANCELLED) && elapsed <= 750, "worker bounded TLS result");
            expect(releases == int(phase), "ingress never frees live TLS object");
            if (phase) { manager.disconnect(); worker.disconnect(); }
            worker.step(now_ms());
            if (!phase && !loss) {
                Frame acknowledged = worker_reply(port);
                encode(acknowledged, bytes);
                expect(acknowledged.request == context.interrupt.request && acknowledged.opcode == (close ? Close : Offer) &&
                       manager.receive(bytes, sizeof(bytes), now_ms()), "interrupt reply correlated by manager");
            }
            expect(!worker.occupied() && releases == int(phase + 1) && cancellations == int(phase + 1), "worker cleanup exactly once");
            expect(platform.opens == platform.closes && platform.fd == -1, "transport closed before next owner");
            printf("WORKER %s status=%d elapsed_ms=%d opens=%d closes=%d releases=%d\n",
                phase ? "recovery" : mode, diagnostic.status, int(elapsed), platform.opens, platform.closes, releases);
        }
        platform.worker_cancelled = NULL;
    }
    expect(descriptors() == initial_descriptors, "worker descriptor balance");
    expect(live_allocations == initial_allocations, "worker scoped allocation balance");
}
