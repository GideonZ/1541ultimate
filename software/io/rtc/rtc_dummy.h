#ifndef RTC_DUMMY_H
#define RTC_DUMMY_H
/*
-------------------------------------------------------------------------------
--
--  (C) COPYRIGHT 2025, Gideon's Hardware and Programmable Logic Solutions
--
-------------------------------------------------------------------------------
-- Project    : Ultimate 1541
-- Title      : rtc.h
-------------------------------------------------------------------------------
*/
#include <stdint.h>
#include "config.h"
#include "iomap.h"

#define RTC_TIMER_SECONDS    *((volatile uint32_t *)RTC_TIMER_BASE)

class Rtc : public ConfigurableObject
{
public:
	Rtc();
	~Rtc();

	// DUMMYs
    int  get_correction(void);
	void set_time_in_chip(int, int y, int M, int D, int wd, int h, int m, int s);
    // END DUMMYs
	void set_time(int y, int M, int D, int wd, int h, int m, int s);
    void set_time_utc(int seconds); // this is a dummy for U2; since it won't support NTP

	void get_time(int &y, int &M, int &D, int &wd, int &h, int &m, int &s);
	const char* get_time_string(char *dest, int len);
	const char* get_date_string(char *dest, int len);
	const char* get_long_date(char *dest, int len);
	uint32_t get_fat_time(void);
};

extern "C" uint32_t get_fattime(void);

extern Rtc rtc;
#endif
