#ifndef PROVIDER_DATA_CLIENT_H
#define PROVIDER_DATA_CLIENT_H
#include "provider_data.h"
#include "provider_manager_runner.h"
namespace provider_data {
// Fixed one-request owner mailbox. submit/collect are task-context APIs; they
// copy all data under Port's lock and never call the control manager. Only step
// calls the runner. Cancellation sources and this object must outlive requests.
class Client {
public:
    enum { Close = 4 };
    Client(Port&, Delivery&, provider_control::ManagerDelivery&, provider_control::ManagerRunner&);
    bool submit(const Frame&, OperationCancellation, uint32_t now_ms);
    bool collect(Frame&);
    void bind_lifetime(OperationCancellation token) { boot_ = token; } // Owner, before admission enabled.
    void step(uint64_t control_now, uint32_t now_ms);
    void stop(); // Owner task, including every terminal task exit.
private:
    enum Phase { Idle, Queued, Waiting, Closing, Complete };
    Port& port_;
    Delivery& data_;
    provider_control::ManagerDelivery& control_;
    provider_control::ManagerRunner& runner_;
    Phase phase_;
    Frame frame_;
    OperationCancellation cancel_, link_, boot_;
    provider_session::Ticket ticket_;
    uint64_t sequence_;
    uint32_t queued_at_, opened_at_, budget_;
    bool active_, closing_sent_, failed_, stopped_;
    void finish(bool);
    void begin_close();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
};
}
#endif
