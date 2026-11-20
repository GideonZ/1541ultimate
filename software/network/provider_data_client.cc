#include "provider_data_client.h"
#include <limits.h>
namespace provider_data {
Client::Client(Port& port, Delivery& data, provider_control::ManagerDelivery& control, provider_control::ManagerRunner& runner) :
    port_(port), data_(data), control_(control), runner_(runner), phase_(Idle), frame_{}, ticket_{}, sequence_(0),
    queued_at_(0), opened_at_(0), budget_(0), active_(false), closing_sent_(false), failed_(false), stopped_(false) {}
bool Client::submit(const Frame& f, OperationCancellation cancel, uint32_t now) {
    if (f.opcode < Open || f.opcode > Close || f.reply || f.length > PayloadSize || f.count > PayloadSize ||
        !f.budget_ms || f.budget_ms > INT32_MAX || cancel.cancelled()) return false;
    port_.lock();
    bool accepted = !stopped_ && phase_ == Idle;
    if (accepted) { frame_ = f; cancel_ = cancel; queued_at_ = now; link_ = data_.lifetime(); phase_ = Queued; }
    port_.unlock(); return accepted;
}
bool Client::collect(Frame& output) {
    port_.lock();
    bool ready = phase_ == Complete;
    if (ready) {
        if (cancel_.cancelled() || link_.cancelled() || boot_.cancelled()) {
            frame_.status = Failed; frame_.count = frame_.length = 0; memset(frame_.payload,0,sizeof(frame_.payload));
        }
        output = frame_; phase_ = Idle;
    }
    port_.unlock(); return ready;
}
void Client::finish(bool success) {
    port_.lock();
    frame_.reply = true; frame_.port = 0; frame_.budget_ms = 0;
    if (!success || cancel_.cancelled() || link_.cancelled() || boot_.cancelled()) {
        frame_.status = Failed; frame_.count = frame_.length = 0; memset(frame_.payload,0,sizeof(frame_.payload));
    }
    else frame_.status = Ok;
    phase_ = Complete; port_.unlock();
}
void Client::stop() {
    runner_.stop(); data_.disconnect(); active_ = false;
    port_.lock(); stopped_ = true; bool pending = phase_ != Idle && phase_ != Complete; port_.unlock();
    if (pending) finish(false);
}
void Client::begin_close() {
    failed_ = frame_.opcode != Close; closing_sent_ = false;
    port_.lock(); phase_ = Closing; port_.unlock();
}
void Client::step(uint64_t control_now, uint32_t now) {
    // submit only writes Idle->Queued; collect only Complete->Idle. All active
    // frame mutation belongs to this owner, and completion publishes under lock.
    port_.lock(); Phase phase = phase_; port_.unlock();
    if (phase == Idle || phase == Complete) return;
    if (link_.cancelled() || boot_.cancelled() || runner_.state() != provider_control::ManagerRunner::Active) { stop(); return; }
    if (phase == Queued) {
        if (frame_.opcode == Close) {
            if (!active_) { frame_.count = frame_.length = 0; finish(true); return; }
            begin_close();
        } else if (cancel_.cancelled()) { if (active_) begin_close(); else { finish(false); return; } }
        else {
            if (frame_.opcode == Open) {
                if (active_ || !runner_.reserve(control_now,ticket_)) { finish(false); return; }
                active_ = true; sequence_ = 0; opened_at_ = queued_at_; budget_ = frame_.budget_ms;
            } else if (!active_) { finish(false); return; }
            uint32_t elapsed = now - opened_at_;
            if (elapsed >= budget_ || sequence_ == UINT64_MAX) { begin_close(); }
            else {
                frame_.ticket = ticket_; frame_.request = ++sequence_; frame_.budget_ms = budget_-elapsed;
                if (!runner_.current(ticket_,control_now)) { stop(); return; }
                if (!data_.send(frame_,link_,cancel_,boot_)) {
                    if (cancel_.cancelled() && !link_.cancelled() && !boot_.cancelled()) begin_close();
                    else { stop(); return; }
                } else { port_.lock(); phase_ = Waiting; port_.unlock(); }
            }
        }
    }
    if (phase_ == Waiting) {
        if (cancel_.cancelled() || now-opened_at_ >= budget_) begin_close();
        else {
            Event reply;
            if (!data_.take(reply)) return;
            bool match = !reply.generation.cancelled() && same_ticket(reply.frame.ticket,ticket_) &&
                reply.frame.request == frame_.request && reply.frame.opcode == frame_.opcode &&
                reply.frame.count <= frame_.count && runner_.current(ticket_,control_now);
            data_.finish(reply.generation);
            if (!match) { stop(); return; }
            bool ok = reply.frame.status == Ok;
            frame_ = reply.frame; finish(ok); return;
        }
    }
    if (phase_ == Closing) {
        Event abandoned;
        if (data_.take(abandoned)) data_.finish(abandoned.generation);
        auto status = control_.status(control_now);
        if (!closing_sent_) {
            if (status.pending) return; // Drain a renewal before terminal close.
            if (!runner_.close(ticket_,control_now)) { stop(); return; }
            closing_sent_ = true;
        } else if (status.state == provider_control::Manager::Ready && !status.pending) {
            active_ = false; frame_.count = frame_.length = 0; finish(!failed_);
        }
    }
}
}
