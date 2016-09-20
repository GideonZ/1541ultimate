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
#include "integer.h"
#include "config.h"
#include "iomap.h"


#define RTC_TIMER_HUNDREDTHS *((volatile uint8_t *)(RTC_TIMER_BASE + 0x07))
#define RTC_TIMER_SECONDS    *((volatile uint8_t *)(RTC_TIMER_BASE + 0x06))
#define RTC_TIMER_MINUTES    *((volatile uint8_t *)(RTC_TIMER_BASE + 0x05))
#define RTC_TIMER_HOURS      *((volatile uint8_t *)(RTC_TIMER_BASE + 0x04))
#define RTC_TIMER_WEEKDAYS   *((volatile uint8_t *)(RTC_TIMER_BASE + 0x03))
#define RTC_TIMER_DAYS       *((volatile uint8_t *)(RTC_TIMER_BASE + 0x02))
#define RTC_TIMER_MONTHS     *((volatile uint8_t *)(RTC_TIMER_BASE + 0x01))
#define RTC_TIMER_YEARS      *((volatile uint8_t *)(RTC_TIMER_BASE + 0x00))
#define RTC_TIMER_LOCK       *((volatile uint8_t *)(RTC_TIMER_BASE + 0x0C))
#define RTC_TIMER_FAT_TIME   *((volatile uint32_t *)(RTC_TIMER_BASE + 0x08))

class RtcConfigStore : public ConfigStore
{
public:
	RtcConfigStore(const char *name, t_cfg_definition *defs) : ConfigStore(0, name, -1, 0, defs, NULL) { }
	~RtcConfigStore() { if(dirty) write(); }

	void read(void);
	void write(void);
};

class Rtc
{
private:
	void write_byte(int addr, uint8_t val);
	void read_all();
    bool capable;
	uint8_t rtc_regs[16];
	RtcConfigStore *cfg;
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
	uint32_t get_fat_time(void);
};

extern "C" uint32_t get_fattime(void);

extern Rtc rtc;
#endif
