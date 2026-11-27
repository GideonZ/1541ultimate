// Shared by the tape tests: helpers for the scripted environment.
#ifndef TAPE_TEST_UTIL_H
#define TAPE_TEST_UTIL_H
#include <string.h>
#include "host_test/host_test.h"
#include "tape_env.h"
#include "tape_regs.h"
#include "userinterface.h"
#include "subsys.h"
#include "c64.h"

namespace tapetest {
using namespace tapeenv;

inline void reset_env()
{
    fs.reset();
    recorder.reset();
    rtos.reset();
    preempt.reset();
    sched.reset();
    executed_functions.clear();
}

// Runs one step of a scenario and turns a Fault of the fake into a failed assertion that
// names it, instead of an anonymous exception. Returns true if the step ran through.
template <class F>
inline bool guarded(const char *what, F f)
{
    try {
        f();
        return true;
    } catch (const Fault &e) {
        host_test::record_failure(std::string(what) + ": " + e.what(), false);
    }
    return false;
}

inline bool has_popup(UserInterface &ui, const char *text)
{
    for (size_t i = 0; i < ui.popups.size(); i++) if (ui.popups[i] == text) return true;
    return false;
}

}
#endif
