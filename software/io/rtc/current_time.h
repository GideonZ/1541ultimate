#ifndef CURRENT_TIME_H
#define CURRENT_TIME_H

// The system clock as local calendar time, for code that has no business knowing which
// clock chip is fitted. Each real time clock driver defines both functions; a host build
// without one reads the fixed clock in cbmdos_parser.cc.
//
// The year is a calendar year and the day of week is 0 for Sunday. set_current_time()
// stores the day of week it is given rather than deriving it, because the control
// interface's DOS_CMD_SET_TIME carries one. The drivers write the chip without learning
// whether it took the bytes, so they answer true; a caller that has to know reads the
// clock back.

extern "C" {
    void get_current_time(int& wd, int& year, int& month, int& day, int& hour, int& min, int& sec);
    bool set_current_time(int wd, int year, int month, int day, int hour, int min, int sec);
}

#endif /* CURRENT_TIME_H */
