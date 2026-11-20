#include "command_target.h"

CommandTarget *command_targets[CMD_IF_MAX_TARGET + 1];
Message c_message_no_target = { 9, true, (uint8_t *)"NO TARGET" };
Message c_status_ok = { 5, true, (uint8_t *)"00,OK" };
Message c_status_unknown_command = { 18, true, (uint8_t *)"21,UNKNOWN COMMAND" };
Message c_message_empty = { 0, true, (uint8_t *)"" };

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <cstdlib>
#include "http_target_test_support.h"
static std::mutex lock;
static std::condition_variable finished;
static uint32_t issued, completed;
uint32_t command_defer() { return ++issued; }
void command_deferred_done(uint32_t ticket) {
    std::lock_guard<std::mutex> guard(lock);
    completed = ticket;
    finished.notify_one();
}
void run_target_command(HttpTarget& target, Message *cmd, Message **data, Message **status) {
    uint32_t before = issued;
    target.parse_command(cmd, data, status);
    if (before == issued) return;
    std::unique_lock<std::mutex> guard(lock);
    if (!finished.wait_for(guard, std::chrono::seconds(5), [] { return completed == issued; })) std::abort();
    guard.unlock();
    target.deferred_reply(data, status);
}
