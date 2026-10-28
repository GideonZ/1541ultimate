#include "owner_http_provider.h"
#include "provider_tls_backend.h"
#include "provider_data_client.h"
namespace bound_test {
using uart_test::Wire;
using namespace connected_test;
struct PlatformBinding : provider_data::TlsPlatform {
    Platform& platform;
    bool (*predicate)(void*) = 0; void *context = 0;
    explicit PlatformBinding(Platform& p) : platform(p) {}
    tls_stream_port callbacks() override { return {&platform,clock_read,clock_ready,::cancelled,entropy,trust,open_socket,read_socket,write_socket,close_socket}; }
    void bind_cancel(bool (*p)(void*),void *c) override { predicate=p; context=c; }
};
struct Provider : HttpOwnerEndpoint {
    Platform& platform;
    Remote remote;
    PlatformBinding binding;
    provider_data::TlsBackend backend;
    ManagerWire control_wire;
    ManagerDelivery control;
    ManagerRunner runner;
    Wire outbound,inbound;
    provider_data::Delivery replies,requests;
    provider_data::Client client;
    provider_data::Owner owner;
    CancellationSource lifetime_events;
    OperationCancellation command;
    const char *mode;
    uint32_t timeout;
    int64_t interrupt_at = 0;
    bool interrupted = false;
    unsigned created = 0,destroyed = 0;
    Provider(Platform& p, Identity boot,const char *m,uint32_t budget) : platform(p),
        remote(p,boot,!strncmp(m,"manager-",8) ? 350 : 1000),binding(p),backend(remote.worker,binding),
        control(control_wire,{&p,[](void *ctx,Identity& id) { return entropy(ctx,id.bytes,sizeof(id.bytes)) == 0; }},{100,remote.lease}),
        runner(control,remote.lease/3),replies(outbound,true),requests(inbound,false),
        client(outbound,replies,control,runner),owner(requests,remote.worker,&backend),mode(m),timeout(budget) {
        client.bind_lifetime(lifetime_events.token());
        expect(runner.start(boot,lifetime_events.token(),now_ms()),"BOUND manager starts");
        controls(); controls(); expect(runner.state() == ManagerRunner::Active,"BOUND grant initialized");
    }
    void ingress() {
        while (!control_wire.requests.empty()) {
            auto b = control_wire.requests.front(); control_wire.requests.pop_front();
            expect(remote.worker.submit(b.data(),b.size()),"BOUND control ingress");
        }
    }
    void controls() {
        ingress(); remote.worker.step(now_ms());
        while (!remote.wire.replies.empty()) {
            Frame f = remote.wire.replies.front(); remote.wire.replies.pop_front(); uint8_t b[FrameSize]; encode(f,b);
            control_wire.lock(); expect(control.receive_isr(b,sizeof(b)),"BOUND control reply"); control_wire.unlock();
        }
        runner.step(now_ms());
    }
    void service() {
        controls(); client.step(now_ms(),uint32_t(now_ms()));
        outbound.deliver(requests); owner.step(now_ms());
        inbound.deliver(replies); controls(); client.step(now_ms(),uint32_t(now_ms()));
    }
    bool cancelled_at(int64_t now) {
        if (!interrupted && interrupt_at && now >= interrupt_at) {
            interrupted = true; lifetime_events.signal();
            if (!strncmp(mode,"provider-",9)) { remote.worker.disconnect(); requests.disconnect(); replies.disconnect(); }
            else runner.stop();
        }
        // Owner queue cancellation produces a control close, independently of
        // the occupied provider data slot. No resource cleanup in this callback.
        if (command.cancelled()) { client.step(now,uint32_t(now)); ingress(); }
        return binding.predicate && binding.predicate(binding.context);
    }
    bool ready() const override { return runner.state() == ManagerRunner::Active; }
    OperationCancellation lifetime() const override { return lifetime_events.token(); }
    bool submit(const provider_data::Frame& f,OperationCancellation c) override { return client.submit(f,c,uint32_t(::now_ms())); }
    bool collect(provider_data::Frame& f) override { return client.collect(f); }
    uint32_t now_ms() const override { return uint32_t(::now_ms()); }
    void wait() override { service(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    void retire() override { client.stop(); remote.worker.disconnect(); remote.worker.step(::now_ms()); }
    std::mutex factory_mutex;
    void lock() override { factory_mutex.lock(); }
    void unlock() override { factory_mutex.unlock(); }

};
static bool cancelled(Platform *p,int64_t now) { return static_cast<Provider*>(p->worker_context)->cancelled_at(now); }
static void exchange(const char *mode,const char *ca,int port) {
    int allocations = live_allocations, fds = descriptors();
    {
        Platform platform(ca); Identity boot = {}; boot.bytes[0] = !strcmp(mode,"recovery") ? 2 : 1;
        bool timeout = strstr(mode,"timeout"), cancel = strstr(mode,"cancel");
        bool good = !strcmp(mode,"valid") || !strcmp(mode,"recovery");
        Provider provider(platform,boot,mode,timeout ? 650 : 3000);
        platform.worker_context = &provider; platform.worker_cancelled = cancelled;
        if (!strcmp(mode,"total-timeout")) platform.open_delay = 250;
        CancellationSource commands; provider.command = commands.token(); std::thread canceller;
        OwnerHttpProvider http_provider(provider,timeout ? 650 : 3000,1000);
        if (!strncmp(mode,"command-",8)) canceller = std::thread([&] { std::this_thread::sleep_for(std::chrono::milliseconds(150)); commands.signal(); });
        else if (cancel) provider.interrupt_at = now_ms()+150;
        int64_t begin = now_ms();
        {
            HttpTarget target(7,&http_provider); target.begin_command(commands.token());
            char url[128]; snprintf(url,sizeof(url),"https://%s:%d/probe",!strcmp(mode,"hostname") ? "wrong.lab.test" : "tls.lab.test",port);
            std::vector<uint8_t> b = {6,HTTP_CMD_HEADER_CREATE,1}; b.insert(b.end(),url,url+strlen(url));
            Message input = {int(b.size()),true,b.data()},*data,*status; target.parse_command(&input,&data,&status);
            expect(data->length == 1,"BOUND header handle"); uint8_t handle = data->length ? data->message[0] : 0;
            const char *header = "Connection: close";
            b = {6,HTTP_CMD_HEADER_ADD,handle}; b.insert(b.end(),header,header+strlen(header));
            input = {int(b.size()),true,b.data()}; target.parse_command(&input,&data,&status);
            uint8_t command[] = {6,HTTP_CMD_DO_EXCHANGE_RAW,handle,255};
            input = {sizeof(command),true,command}; target.parse_command(&input,&data,&status); target.prepare_reply(&data,&status);
            expect(data->last_part && data->length == (good ? 8 : 0),"BOUND complete or empty failure body");
            if (good) expect(!memcmp(data->message,"tls-ok!\n",8),"BOUND exact response");
            else expect(status->length == 23 && !memcmp(status->message,"503 SERVICE UNAVAILABLE",23),"BOUND safe failure");
            target.finish_reply(true);
        }
        if (canceller.joinable()) canceller.join();
        auto d = provider.backend.diagnostic(); int elapsed = int(now_ms()-begin);
        
        expect(!provider.backend.occupied() && !provider.remote.worker.occupied(),"bound resource released");
        expect(platform.opens == 1 && platform.closes == 1 && platform.fd == -1,"BOUND socket balanced");
        if (timeout) expect(d.status == TLS_TIMEOUT && elapsed >= 630 && elapsed <= 900,"BOUND total timeout");
        if (cancel) expect(d.status == TLS_CANCELLED && elapsed <= 750,"BOUND cancellation bounded");
        if (!good && !timeout && !cancel) expect(d.verify_flags,"BOUND auth sends no HTTP");
        printf("BOUND %s status=%d flags=%u elapsed_ms=%d opens=%d closes=%d releases=%u\n",mode,d.status,d.verify_flags,elapsed,platform.opens,platform.closes,unsigned(!provider.backend.occupied()));
        platform.worker_cancelled = NULL;
    }
    expect(live_allocations == allocations && descriptors() == fds,"BOUND scoped resources balanced");
}
}
