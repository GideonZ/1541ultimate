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

#define RTC_CHIP_DATA  *((volatile uint8_t *)(RTC_BASE + 0x00))
#define RTC_CHIP_SPEED *((volatile uint8_t *)(RTC_BASE + 0x04))
#define RTC_CHIP_CTRL  *((volatile uint8_t *)(RTC_BASE + 0x08))

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

#define SPI_BUSY        0x80
#define SPI_FORCE_SS    0x01
#define SPI_LEVEL_SS    0x02
#define SPI_CS_ON       0x01
#define SPI_CS_OFF      0x03

#define RTC_ADDR_CTRL1      0
#define RTC_ADDR_CTRL2      1
#define RTC_ADDR_SECONDS    2
#define RTC_ADDR_MINUTES    3
#define RTC_ADDR_HOURS      4
#define RTC_ADDR_DAYS       5
#define RTC_ADDR_WEEKDAYS   6
#define RTC_ADDR_MONTHS     7
#define RTC_ADDR_YEARS      8
#define RTC_ADDR_MIN_ALM    9
#define RTC_ADDR_HOUR_ALM  10
#define RTC_ADDR_DAY_ALM   11
#define RTC_ADDR_WKDAY_ALM 12
#define RTC_ADDR_OFFSET    13
#define RTC_ADDR_TIMER     14
#define RTC_ADDR_CLOCKOUT  14
#define RTC_ADDR_COUNTDOWN 15

class RtcConfigStore : public ConfigStore
{
public:
	RtcConfigStore(char *name, t_cfg_definition *defs) : ConfigStore(0, name, -1, 0, defs, NULL) { }
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
	char* get_time_string(char *dest, int len);
	char* get_date_string(char *dest, int len);
	char* get_long_date(char *dest, int len);
	uint32_t get_fat_time(void);
};


extern Rtc rtc;
#endif
