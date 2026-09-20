#ifndef CURRENT_TIME_H
#define CURRENT_TIME_H

// The system clock as local calendar time, for code that has no business knowing which
// clock chip is fitted. Each real time clock driver defines both functions; a build
// without one keeps the clock in memory.
//
// The year is a calendar year and the day of week is 0 for Sunday. set_current_time()
// stores the day of week it is given rather than deriving it, because the DOS clock
// commands and the control interface both carry one, and it answers false when the
// clock could not be set.

extern "C" {
    void get_current_time(int& wd, int& year, int& month, int& day, int& hour, int& min, int& sec);
    bool set_current_time(int wd, int year, int month, int day, int hour, int min, int sec);
}

#endif /* CURRENT_TIME_H */
