#ifndef RTC_H
#define RTC_H
/*
-------------------------------------------------------------------------------
--
--  (C) COPYRIGHT 2009, Gideon's Hardware and Programmable Logic Solutions
--
-------------------------------------------------------------------------------
-- Project    : Ultimate 1541
-- Title      : rtc.h
-------------------------------------------------------------------------------
*/
#include <stdint.h>

class Rtc
{
private:
	void write_byte(int addr, uint8_t val);
	void read_all();
    bool capable;
	uint8_t rtc_regs[16];
public:
	Rtc();
	~Rtc();

	int  get_correction(void);
	void get_time_from_chip(void);
	void set_time_in_chip(int, int y, int M, int D, int wd, int h, int m, int s);

	void get_time(int &y, int &M, int &D, int &wd, int &h, int &m, int &s);
	void set_time(int y, int M, int D, int wd, int h, int m, int s);
	const char* get_time_string(char *dest, int len);
	const char* get_date_string(char *dest, int len);
	const char* get_long_date(char *dest, int len);
	char* get_datetime_compact(char *dest, int len);
	uint32_t get_fat_time(void);
};

extern "C" uint32_t get_fattime(void);

extern Rtc rtc;
#endif
