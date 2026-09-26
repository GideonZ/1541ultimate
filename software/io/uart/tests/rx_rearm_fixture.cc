#include <cassert>
#include <cstdint>
#include <cstdio>
#include <deque>

using BaseType_t=int;
constexpr int pdTRUE=1, pdFALSE=0, UART_DATA=0;
struct command_buf_t { uint8_t data[16]; int size; };
struct Queue { std::deque<command_buf_t *> entries; };
struct command_buf_context_t { Queue free, delivered, tx; };
static int release_count;
static BaseType_t xQueueSendFromISR(Queue *q,command_buf_t **b,BaseType_t *)
{ q->entries.push_back(*b); return pdTRUE; }
static BaseType_t xQueueReceiveFromISR(Queue *q,command_buf_t **b,BaseType_t *)
{ if(q->entries.empty())return pdFALSE; *b=q->entries.front();q->entries.pop_front();return pdTRUE; }
static BaseType_t cmd_buffer_get_free_isr(command_buf_context_t *c,command_buf_t **b,BaseType_t *w)
{ return xQueueReceiveFromISR(&c->free,b,w); }
static BaseType_t cmd_buffer_free_isr(command_buf_context_t *c,command_buf_t *b,BaseType_t *w)
{ ++release_count;return xQueueSendFromISR(&c->free,&b,w); }
static BaseType_t cmd_buffer_get_tx_isr(command_buf_context_t *c,command_buf_t **b,BaseType_t *w)
{ return xQueueReceiveFromISR(&c->tx,b,w); }
static void ioWrite8(int,int) {}
struct Registers;
struct Register {
    Registers *owner; int kind;
    operator uint8_t() const;
    void operator=(uint8_t value);
};
struct Address {
    Registers *owner;
    void operator=(uint8_t *);
};
struct Registers {
    bool rx_pending=true, need_address=true;
    uint8_t enabled=5;
    int reads=0, assigned=0;
    Register status{this,0}, ictrl{this,1}, rx_pop{this,2};
    Address rx_addr{this};
    uint8_t *tx_addr=nullptr;
    uint16_t length=12; uint8_t tx_push=0;
};
Register::operator uint8_t() const
{
    assert(++owner->reads<20); // Regression must terminate, not spin in an ISR.
    return ((owner->rx_pending?1:0)|(owner->need_address?4:0))&owner->enabled;
}
void Register::operator=(uint8_t value)
{
    if(kind==2) {assert(value==1);owner->rx_pending=false;return;}
    assert(kind==1);
    if(value&0x80)owner->enabled|=value&7;
    else owner->enabled&=~(value&7);
}
void Address::operator=(uint8_t *) {owner->need_address=false;++owner->assigned;}
struct DmaUART {
    Registers *uart;
    command_buf_context_t *packets;
    command_buf_t *current_tx_buf=nullptr;
    Queue *rx_bufs;
    BaseType_t (*isr_rx_callback)(command_buf_context_t *,command_buf_t *,BaseType_t *);
    static uint8_t DmaUartInterrupt(void *);
};

// INSERT_PRODUCTION_ISR

static BaseType_t consume(command_buf_context_t *c,command_buf_t *b,BaseType_t *w)
{ cmd_buffer_free_isr(c,b,w);return pdTRUE; }
static BaseType_t reject(command_buf_context_t *,command_buf_t *,BaseType_t *) {return pdFALSE;}
static BaseType_t retain(command_buf_context_t *c,command_buf_t *b,BaseType_t *w)
{ return xQueueSendFromISR(&c->delivered,&b,w); }
int main()
{
    for(auto callback:{consume,reject,retain}) {
        Registers regs;
        command_buf_context_t context;
        Queue received; command_buf_t packet={}; received.entries.push_back(&packet);
        DmaUART driver; driver.uart=&regs;driver.packets=&context;
        driver.rx_bufs=&received;driver.isr_rx_callback=callback;release_count=0;
        DmaUART::DmaUartInterrupt(&driver);
        assert(!regs.rx_pending && packet.size==12);
        if(callback==retain) {
            assert(regs.assigned==0 && release_count==0);
            assert(!(regs.enabled&4)); // No free buffer: interrupts must stay bounded.
            BaseType_t w=0;command_buf_t *held;
            assert(xQueueReceiveFromISR(&context.delivered,&held,&w)==pdTRUE);
            cmd_buffer_free_isr(&context,held,&w);
            regs.ictrl=0x84; // The ordinary task-side FreeBuffer wakeup.
            regs.reads=0;DmaUART::DmaUartInterrupt(&driver);
        }
        assert(regs.assigned==1 && release_count==1 && context.free.entries.empty());
        assert(received.entries.size()==1 && received.entries.front()==&packet);
    }
    puts("DMA UART: simultaneous empty-pool/RX interrupts recover for ISR consume, rejection and delayed task release.");
}
