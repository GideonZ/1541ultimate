#include <stdio.h>
#include "rtc_only.h"
#include "i2c_drv.h"
#include "itu.h"

const char *month_strings_short[]={ "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
										"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

const char *month_strings_long[] = { "", "January", "February", "March", "April", "May", "June",
									"July", "August", "September", "October", "November", "December" };

const char *weekday_strings[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };


#define RTC_ADDR_CTRL1      0
#define RTC_ADDR_CTRL2      1
#define RTC_ADDR_OFFSET     2
#define RTC_ADDR_RAM		3
#define RTC_ADDR_SECONDS    4
#define RTC_ADDR_MINUTES    5
#define RTC_ADDR_HOURS      6
#define RTC_ADDR_DAYS       7
#define RTC_ADDR_WEEKDAYS   8
#define RTC_ADDR_MONTHS     9
#define RTC_ADDR_YEARS      10

static uint8_t bcd2bin(uint8_t bcd)
{
	return (((bcd & 0xF0) >> 4) * 10) + (bcd & 0x0F);
}

static uint8_t bin2bcd(uint8_t bin)
{
	uint8_t bcd = 0;
	while(bin >= 10) { // division is probably slower
		bin -= 10;
		bcd += 0x10;
	}
	bcd += bin;
	return bcd;
}

Rtc :: Rtc()
{
	capable = true;
	get_time_from_chip();
}

Rtc :: ~Rtc()
{
}

void Rtc :: write_byte(int addr, uint8_t val)
{
    I2C_Driver i2c;
    ENTER_SAFE_SECTION
	i2c.i2c_write_byte(0xA2, addr, val);
	LEAVE_SAFE_SECTION
	rtc_regs[addr] = val; // update internal structure as well.
}


void Rtc :: read_all(void)
{
    I2C_Driver i2c;
	ENTER_SAFE_SECTION
	int dummy;
	for(int i=0;i<11;i++) {
        rtc_regs[i] = i2c.i2c_read_byte(0xA2, i, &dummy);

    }
	LEAVE_SAFE_SECTION
}

void Rtc :: get_time_from_chip(void)
{
	read_all();
}

void Rtc :: set_time_in_chip(int corr_ppm, int y, int M, int D, int wd, int h, int m, int s)
{
	// calculate correction register
	uint8_t corr_byte = 0;
	int div;
	int val = (29 * corr_ppm) / 59;
	if(val > 0)
		if(val & 1) val++;
	else
		if(val & 1) val--;
	val = (val >> 1) & 0x7F;
	corr_byte = 0x80 | uint8_t(val);
	printf("Correction byte for %d ppm: %b\n", corr_ppm, corr_byte);
	
	write_byte(RTC_ADDR_CTRL1,    0x21); // stop
	write_byte(RTC_ADDR_CTRL2,    0x07); // no clock out
	write_byte(RTC_ADDR_SECONDS,  bin2bcd(s));
	write_byte(RTC_ADDR_MINUTES,  bin2bcd(m));
	write_byte(RTC_ADDR_HOURS,    bin2bcd(h));
	write_byte(RTC_ADDR_WEEKDAYS, bin2bcd(wd));
	write_byte(RTC_ADDR_DAYS,     bin2bcd(D));
	write_byte(RTC_ADDR_MONTHS,   bin2bcd(M));
	write_byte(RTC_ADDR_YEARS,    bin2bcd(y));
	write_byte(RTC_ADDR_OFFSET,   corr_byte);
	write_byte(RTC_ADDR_CTRL1,    0x01);
    
}

int  Rtc :: get_correction(void)
{
	uint8_t corr_byte = rtc_regs[RTC_ADDR_OFFSET] & 0x7F;
	int val = (corr_byte & 0x40) ? (int(corr_byte & 0x3F) -64) : int(corr_byte & 0x3F);

	int corr = (236 * val) / 29;
	if(corr > 0)
		if(corr & 1) corr++;
	else
		if(corr & 1) corr--;
	return (corr >> 1);
}

void Rtc :: get_time(int &y, int &M, int &D, int &wd, int &h, int &m, int &s)
{
	read_all(); // read directly from chip

	s  = (int)bcd2bin(rtc_regs[RTC_ADDR_SECONDS] & 0x7F);
	m  = (int)bcd2bin(rtc_regs[RTC_ADDR_MINUTES]);
	h  = (int)bcd2bin(rtc_regs[RTC_ADDR_HOURS]);
	wd = (int)bcd2bin(rtc_regs[RTC_ADDR_WEEKDAYS]);
	D  = (int)bcd2bin(rtc_regs[RTC_ADDR_DAYS]);
	M  = (int)bcd2bin(rtc_regs[RTC_ADDR_MONTHS]);
	y  = (int)bcd2bin(rtc_regs[RTC_ADDR_YEARS]);

    if (M < 1) M = 1;
    if (M > 12) M = 12;
    if (D < 1) D = 1;
    if (D > 31) D = 31;
    if (wd < 0) wd = 0;
    if (wd > 6) wd = 6;
}

void Rtc :: set_time(int y, int M, int D, int wd, int h, int m, int s)
{
}

const char * Rtc :: get_time_string(char *dest, int len)
{
	if(len < 9)
		return "";
    int y, M, D, wd, h, m, s;
    get_time(y, M, D, wd, h, m, s);
	sprintf(dest, "%02d:%02d:%02d", h, m, s);
	return dest;
}


const char * Rtc :: get_date_string(char *dest, int len)
{
	if(len < 14)
		return "";
    int y, M, D, wd, h, m, s;
    get_time(y, M, D, wd, h, m, s);
	sprintf(dest, "%s %2d, %4d", month_strings_short[M], D, 1980+y);
	return dest;
}

const char * Rtc :: get_long_date(char *dest, int len)
{
	// Wednesday September 30, 2009
	if(len < 29)
		return "";
    int y, M, D, wd, h, m, s;
    get_time(y, M, D, wd, h, m, s);
	sprintf(dest, "%s %s %2d, %4d", weekday_strings[wd], month_strings_long[M], D, 1980+y);
	return dest;
}

char * Rtc :: get_datetime_compact(char *dest, int len)
{
	// 20161119-141733
	if (len < 16) {
		dest[0] = 0;
		return dest;
	}
    int y, M, D, wd, h, m, s;
    get_time(y, M, D, wd, h, m, s);
	sprintf(dest, "%4d%02d%02d-%02d%02d%02d", 1980+y, M, D, h, m, s);
	return dest;
}

uint32_t Rtc :: get_fat_time(void)
{
    if(!capable)
        return 0x42FF9877;
/*
    33 << 25 = 0x42000000
     7 << 21 = 0x00E00000
    31 << 16 = 0x001F0000
    19 << 11 = 0x00009800
    03 <<  5 = 0x00000060
    23 <<  0 = 0x00000017
*/

    int y, M, D, wd, h, m, s;
    get_time(y, M, D, wd, h, m, s);

    uint32_t result = 0;    
    result |= (y << 25);
    result |= (M << 21);
    result |= (D << 16);
    result |= (h << 11);
    result |= (m <<  5);
    result |= (s >>  1);

    return result;
}

extern "C"
uint32_t get_fattime (void)	/* 31-25: Year(0-127 org.1980), 24-21: Month(1-12), 20-16: Day(1-31) */
						    /* 15-11: Hour(0-23), 10-5: Minute(0-59), 4-0: Second(0-29 *2) */
{
	return rtc.get_fat_time();
}

extern "C" void get_current_time(int& wd, int& year, int& month, int& day, int& hour, int& min, int& sec)
{
    rtc.get_time(year, month, day, wd, hour, min, sec);
}


Rtc rtc; // global
