#ifndef PROVIDER_CONTROL_TEST_PORT_H
#define PROVIDER_CONTROL_TEST_PORT_H
#include "provider_control.h"
#include <deque>
#include <mutex>
#include <vector>
struct ControlTestPort : provider_control::Port {
    std::mutex mutex;
    std::deque<provider_control::Event> queue;
    std::deque<provider_control::Frame> replies;
    bool transmit = true;
    void lock() override { mutex.lock(); }
    void unlock() override { mutex.unlock(); }
    bool enqueue(const provider_control::Event& event) override {
        if (queue.size() == provider_control::QueueDepth) return false;
        queue.push_back(event); return true;
    }
    bool dequeue(provider_control::Event& event) override {
        if (queue.empty()) return false;
        event = queue.front(); queue.pop_front(); return true;
    }
    bool send(const uint8_t *bytes, size_t size) override {
        provider_control::Frame frame;
        if (!transmit || !provider_control::decode(bytes, size, frame)) return false;
        replies.push_back(frame); return true;
    }
};
inline bool control_submit(provider_control::Worker& worker, const provider_control::Frame& frame)
{
    uint8_t bytes[provider_control::FrameSize]; provider_control::encode(frame, bytes);
    return worker.submit(bytes, sizeof(bytes));
}
#endif
