// Included by the TLS fixture to reuse its instrumented loopback port.
struct OwnerResource : provider_session::Resource {
    TlsHttpConnection connection;
    Platform& platform;
    int& cancellations;
    int& releases;
    OwnerResource(const tls_stream_port& port, Platform& p, int& c, int& r) :
        connection(port, 3000), platform(p), cancellations(c), releases(r) {}
    void cancel() override { ++cancellations; }
    bool cleanup() override { connection.close_session(); return platform.fd == -1; }
    ~OwnerResource() { ++releases; }
};

static void owner_exchange(const char *mode, const char *ca, int primary, int recovery)
{
    using namespace provider_session;
    int initial_allocations = live_allocations, initial_descriptors = descriptors();
    {
        bool lost = strcmp(mode, "owner-loss-read-cancel") == 0;
        bool handshake = strcmp(mode, "owner-handshake-cancel") == 0;
        Identity boot = {}, manager = {}; boot.bytes[0] = 1; manager.bytes[0] = 2;
        Owner owner(boot, Required, {1000, lost ? 350u : 2000u, 250});
        Platform platform(ca);
        platform.session_owner = &owner;
        tls_stream_port callbacks = { &platform, clock_read, clock_ready, cancelled,
            entropy, trust, open_socket, read_socket, write_socket, close_socket };
        Challenge challenge = owner.offer(now_ms());
        Grant grant = {};
        expect(owner.initialize(challenge, manager, 1, Required, now_ms(), grant), "initial owner initialized");
        int cancellations = 0, releases = 0;
        for (unsigned phase = 0; phase < 2; ++phase) {
            OwnerResource *resource = new OwnerResource(callbacks, platform, cancellations, releases);
            expect(owner.admit(grant, 1, resource, now_ms(), platform.session_ticket), "real TLS resource admitted");
            Ticket retired = platform.session_ticket;
            int64_t begin = now_ms();
            if (!phase && !lost) platform.restart_at = begin + 150;
            int opened = resource->connection.open("tls.lab.test", phase ? recovery : primary);
            expect((opened >= 0) == (phase || !handshake), "owner authenticated open result");
            size_t received = 0;
            if (opened >= 0) {
                const char request[] = "GET /probe HTTP/1.1\r\nHost: tls.lab.test\r\nConnection: close\r\n\r\n";
                size_t sent = 0;
                while (sent < sizeof(request) - 1) {
                    int n = resource->connection.write((const uint8_t *)request + sent, sizeof(request) - 1 - sent);
                    if (n <= 0) break;
                    sent += n;
                }
                expect(sent == sizeof(request) - 1, "owner exact request sent");
                uint8_t response[256] = {};
                while (received < sizeof(response) - 1) {
                    int n = resource->connection.read(response + received, sizeof(response) - 1 - received);
                    if (n <= 0) break;
                    received += n;
                    if (phase && strstr((const char *)response, "tls-ok!\n")) break;
                }
                expect(phase ? strstr((const char *)response, "tls-ok!\n") != NULL : received == 0,
                       "new owner recovers and retired owner exposes no partial response");
            }
            tls_stream_diagnostics diagnostic = resource->connection.diagnostic();
            int64_t elapsed = now_ms() - begin;
            expect(diagnostic.status == (phase ? TLS_OK : TLS_CANCELLED), "owner causal TLS diagnostic");
            expect(elapsed <= 750, "owner cancellation or recovery within fixture bound");
            if (!phase) {
                expect(!owner.current(retired, now_ms()), "retired owner cannot publish");
                if (lost) {
                    owner.poll(now_ms());
                    challenge = owner.offer(now_ms());
                } else {
                    challenge = platform.next_challenge;
                    Grant early = {};
                    expect(!owner.initialize(challenge, manager, 1, Required, now_ms(), early), "no early remote cleanup acknowledgement");
                    owner.poll(now_ms());
                }
                expect(!owner.occupied() && releases == 1 && cancellations == 1, "real TLS resources reclaimed once");
                expect(platform.fd == -1 && platform.opens == platform.closes, "remote transport closed before new initialization");
                manager.bytes[0] = 3;
                expect(owner.initialize(challenge, manager, 1, Required, now_ms(), grant), "new owner initialized after real cleanup");
                expect(!owner.close(retired, now_ms()), "old close cannot touch new owner");
            } else {
                owner.stop(now_ms());
                owner.poll(now_ms());
                expect(releases == 2 && cancellations == 2 && !owner.occupied(), "recovery resource reclaimed");
            }
            printf("OWNER %s status=%d elapsed_ms=%d opens=%d closes=%d releases=%d\n",
                   phase ? "recovery" : mode, diagnostic.status, int(elapsed), platform.opens, platform.closes, releases);
        }
        platform.session_owner = NULL;
    }
    expect(descriptors() == initial_descriptors, "owner descriptor balance");
    expect(live_allocations == initial_allocations, "owner scoped allocation balance");
}
