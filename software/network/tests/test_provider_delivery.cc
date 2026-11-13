#include "dma_uart.h"
#include "provider_manager_uart.h"
#include "provider_control_test_port.h"
#include "provider_lifecycle.h"
#include <stdio.h>
using namespace provider_control;
using namespace provider_session;
unsigned manager_critical_depth;
static unsigned checks, failures;
static void expect(bool b, const char *s) { ++checks; if (!b) { ++failures; printf("FAIL %s\n", s); } }
static Identity id(unsigned n) { Identity result = {}; result.bytes[0] = n; return result; }
static bool fresh(void *p, Identity& value) { value = id(*static_cast<unsigned*>(p)); return value.valid(); }
struct Fixture {
    unsigned identity = 2;
    DmaUART uart;
    ProviderManagerUart link;
    ControlTestPort port;
    Worker worker;
    CancellationSource boot;
    Fixture() : link({&identity,fresh}, {100,200}), worker(port,id(1),Required,{100,200,50}) {
        expect(link.bind_uart(&uart), "qualified UART bind");
    }
    Frame remote(uint64_t now) {
        expect(uart.sent.size() == 1, "one transmitted request");
        if (uart.sent.empty()) return {};
        auto bytes = uart.sent.front(); uart.sent.erase(uart.sent.begin());
        expect(worker.submit(bytes.data(), bytes.size()), "worker receives encoded request"); worker.step(now);
        expect(port.replies.size() == 1, "one remote reply");
        if (port.replies.empty()) return {};
        Frame reply = port.replies.front(); port.replies.pop_front(); return reply;
    }
    void ingress(Frame reply) {
        uint8_t bytes[FrameSize]; encode(reply, bytes);
        portENTER_CRITICAL();
        expect(link.receive_isr(bytes, sizeof(bytes)), "reserved reply consumed");
        portEXIT_CRITICAL();
        memset(bytes, 0xff, sizeof(bytes)); // The DMA buffer is free/reusable after ingress.
    }
    Ticket initialize(uint64_t now) {
        expect(link.delivery.offer(id(1), boot.token(), now), "offer transmitted"); ingress(remote(now));
        expect(!link.delivery.initialize(now), "ISR never advances manager");
        link.delivery.step(now);
        expect(link.delivery.initialize(now), "owner consumes copied offer"); ingress(remote(now));
        link.delivery.step(now);
        Ticket ticket = {};
        expect(link.delivery.reserve(now,ticket) && link.delivery.current(ticket,now), "manager grants session");
        return ticket;
    }
};
static void qualification_and_framing()
{
    DmaUART uart; ProviderManagerUart disabled({NULL,NULL},{100,200});
    expect(!disabled.bind_uart(&uart) && !disabled.delivery.offer(id(1),{},0) && uart.gets == 0, "no identity means no UART access");
    Fixture f;
    expect(!f.link.bind_uart(&uart) && !f.link.bind_uart(NULL), "cannot rebind or bind null");
    uint8_t bytes[FrameSize + 1] = {}; bytes[0] = 2; bytes[1] = 0xff;
    portENTER_CRITICAL();
    expect(!f.link.receive_isr(bytes,FrameSize), "legacy boot probe stays on its route");
    bytes[1] = 0xfe;
    for (size_t size = 0; size <= sizeof(bytes); ++size)
        expect(f.link.receive_isr(bytes,size) == (size >= 2), "reserved malformed frame consumed safely");
    portEXIT_CRITICAL();
    f.initialize(0);
}
static void overflow_and_loss()
{
    Fixture f; Ticket ticket = f.initialize(0); auto token = f.link.delivery.link_lifetime();
    expect(f.link.delivery.renew(10), "renew before overflow"); Frame reply = f.remote(10);
    for (unsigned i = 0; i <= QueueDepth; ++i) f.ingress(reply);
    expect(token.cancelled() && !f.link.delivery.current(ticket,11), "overflow immediately fences publication");
    f.link.delivery.step(11);
    expect(!f.link.delivery.reserve(11,ticket), "overflow cannot grant a session");
    ticket = f.initialize(12); token = f.link.delivery.link_lifetime();
    portENTER_CRITICAL(); f.link.delivery.invalidate_isr(); portEXIT_CRITICAL();
    expect(token.cancelled() && !f.link.delivery.current(ticket,13), "link loss retires before poll");
    f.ingress(reply); f.link.delivery.step(13);
    expect(!f.link.delivery.current(ticket,13), "queued pre-loss reply cannot revive grant");
    ticket = f.initialize(14);
    f.boot.signal(); expect(!f.link.delivery.current(ticket,15), "provider restart invalidates captured boot token");
}
static void send_failures_and_deadlines()
{
    Fixture f; f.uart.available = false;
    expect(!f.link.delivery.offer(id(1),f.boot.token(),0) && f.uart.consumed == 0, "buffer shortage retires pending offer");
    f.uart.available = true; f.uart.transmit_ok = false;
    expect(!f.link.delivery.offer(id(1),f.boot.token(),1) && f.uart.consumed == 1, "enqueue failure consumes buffer once");
    f.uart.transmit_ok = true; Ticket ticket = f.initialize(2);
    expect(f.link.delivery.renew(3), "renew queued"); f.ingress(f.remote(3));
    f.link.delivery.step(103);
    expect(!f.link.delivery.current(ticket,103), "queued timely packet processed at deadline is rejected");
    ticket = f.initialize(104);
    f.uart.transmit_ok = false;
    expect(!f.link.delivery.close(ticket,105) && !f.link.delivery.current(ticket,105), "failed close send never permits reuse");
    f.uart.transmit_ok = true; ticket = f.initialize(106);
    f.link.delivery.disconnect(); expect(!f.link.delivery.current(ticket,106), "explicit disconnect retires grant");
}
static void bounded_work()
{
    Fixture f;
    f.link.delivery.offer(id(1),f.boot.token(),0); Frame reply = f.remote(0), wrong = reply;
    ++wrong.request; f.ingress(wrong); f.ingress(reply);
    f.link.delivery.step(1);
    expect(!f.link.delivery.initialize(1), "one reply maximum per owner step");
    f.link.delivery.step(2); expect(f.link.delivery.initialize(2), "later valid reply preserved");
    f.ingress(f.remote(2)); f.link.delivery.step(2);
    Ticket ticket = {}; expect(f.link.delivery.reserve(2,ticket), "bounded queue recovers");
    f.link.delivery.offer(id(1),f.boot.token(),3);
    expect(!f.link.delivery.current(ticket,3), "reprobe fences preceding session");
    f.ingress(reply); f.link.delivery.step(3);
    expect(!f.link.delivery.initialize(3), "old reply cannot satisfy new request");
}
static void data_packets()
{
    Fixture f; Ticket ticket = f.initialize(0);
    provider_data::Frame packet = {}; packet.opcode = provider_data::Read; packet.request = 1;
    packet.ticket = ticket; packet.budget_ms = 100; packet.count = 256;
    expect(f.link.data.send(packet,f.link.data.lifetime()),"bounded data packet transmitted");
    expect(f.uart.sent.size() == 1 && f.uart.sent[0].size() == provider_data::FrameSize,"DMA owns whole data frame");
    f.uart.sent.clear(); packet.reply = true; packet.status = provider_data::Ok; packet.budget_ms = 0; packet.length = 256;
    memset(packet.payload,0x81,256); uint8_t bytes[provider_data::FrameSize]; provider_data::encode(packet,bytes);
    portENTER_CRITICAL(); expect(f.link.receive_isr(bytes,sizeof(bytes)),"reserved data reply consumed"); portEXIT_CRITICAL();
    memset(bytes,0,sizeof(bytes)); provider_data::Event event;
    expect(f.link.data.take(event) && event.frame.payload[255] == 0x81,"DMA reuse cannot corrupt queued reply");
    f.link.data.finish(event.generation);
    auto generation = f.link.data.lifetime(), control = f.link.delivery.link_lifetime();
    bytes[0] = 2; bytes[1] = 0xfd;
    portENTER_CRITICAL(); f.link.receive_isr(bytes,sizeof(bytes)); portEXIT_CRITICAL();
    expect(generation.cancelled() && control.cancelled(),"malformed data retires both channels");
}
int main()
{
    qualification_and_framing(); overflow_and_loss(); send_failures_and_deadlines(); bounded_work(); data_packets();
    expect(manager_critical_depth == 0, "balanced critical sections");
    printf("Provider delivery: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
