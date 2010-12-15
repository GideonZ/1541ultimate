#include "rtc.h"
extern "C" {
    #include "small_printf.h"
}

char *month_strings_short[]={ "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
										"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

char *month_strings_long[] = { "", "January", "February", "March", "April", "May", "June",
									"July", "August", "September", "October", "November", "December" };

char *weekday_strings[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

// =================
//   Configuration
// =================

#define CFG_RTC_YEAR    0x11
#define CFG_RTC_MONTH   0x12
#define CFG_RTC_DATE    0x13
#define CFG_RTC_WEEKDAY 0x14
#define CFG_RTC_HOUR    0x15
#define CFG_RTC_MINUTE  0x16
#define CFG_RTC_SECOND  0x17
#define CFG_RTC_CORR    0x18

struct t_cfg_definition rtc_config[] = {
	{ CFG_RTC_YEAR,     CFG_TYPE_VALUE,  "Year",     "%d", NULL,  1980, 2079, 2010 },
	{ CFG_RTC_MONTH,    CFG_TYPE_ENUM,   "Month",    "%s", month_strings_long,  1, 12,  5 },
	{ CFG_RTC_DATE,     CFG_TYPE_VALUE,  "Day",      "%d", NULL,     1, 31,  15 },
	{ CFG_RTC_WEEKDAY,  CFG_TYPE_ENUM,   "Weekday",  "%s", weekday_strings, 0, 6, 6 },
	{ CFG_RTC_HOUR,     CFG_TYPE_VALUE,  "Hours",   "%02d", NULL,     0, 23, 11 },
	{ CFG_RTC_MINUTE,   CFG_TYPE_VALUE,  "Minutes", "%02d", NULL,     0, 59, 26 },
	{ CFG_RTC_SECOND,   CFG_TYPE_VALUE,  "Seconds", "%02d", NULL,     0, 59, 10 },
	{ CFG_RTC_CORR,     CFG_TYPE_VALUE,  "Correction", "%d ppm", NULL, -200, 200, 0 },
	{ CFG_TYPE_END,     CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};

static BYTE bcd2bin(BYTE bcd)
{
	return (((bcd & 0xF0) >> 4) * 10) + (bcd & 0x0F);
}

static BYTE bin2bcd(BYTE bin)
{
	BYTE bcd = 0;
	while(bin >= 10) { // division is probably slower
		bin -= 10;
		bcd += 0x10;
	}
	bcd += bin;
	return bcd;
}

Rtc :: Rtc()
{
    if(CAPABILITIES & CAPAB_RTC_CHIP) {
        capable = true;
    	cfg = new RtcConfigStore("Clock Settings", rtc_config);
    	config_manager.add_custom_store(cfg);
    	get_time_from_chip();
    
    	// Check and correct clock out setting
    	if((rtc_regs[RTC_ADDR_CLOCKOUT] & 0x70) != 0x70)
    		write_byte(RTC_ADDR_CLOCKOUT, 0x72);
    } else {
        capable = false;
    }
}

Rtc :: ~Rtc()
{
    //printf("Destructor RTC\n");
    //config_manager.dump();
    config_manager.remove_store(cfg);
	delete cfg;
	//printf("RTC configuration store now gone..\n");
}

void Rtc :: write_byte(int addr, BYTE val)
{
	RTC_CHIP_CTRL = SPI_CS_ON;
    RTC_CHIP_DATA = (BYTE)(0x10 + addr);
    RTC_CHIP_DATA = val;
    RTC_CHIP_CTRL = SPI_CS_OFF;
	rtc_regs[addr] = val; // update internal structure as well.
}


void Rtc :: read_all(BYTE *buf)
{
    RTC_CHIP_CTRL = SPI_CS_ON;
    RTC_CHIP_DATA = 0x90; // read + startbit

    for(int i=0;i<16;i++) {
        buf[i] = RTC_CHIP_DATA;
    }
	RTC_CHIP_CTRL = SPI_CS_OFF;
}

void Rtc :: get_time_from_chip(void)
{
	read_all(rtc_regs);
	RTC_TIMER_LOCK = 1;

    RTC_TIMER_SECONDS    = bcd2bin(rtc_regs[RTC_ADDR_SECONDS]);
    RTC_TIMER_MINUTES    = bcd2bin(rtc_regs[RTC_ADDR_MINUTES]);
    RTC_TIMER_HOURS      = bcd2bin(rtc_regs[RTC_ADDR_HOURS]);
    RTC_TIMER_WEEKDAYS   = bcd2bin(rtc_regs[RTC_ADDR_WEEKDAYS]);
    RTC_TIMER_DAYS       = bcd2bin(rtc_regs[RTC_ADDR_DAYS]);
    RTC_TIMER_MONTHS     = bcd2bin(rtc_regs[RTC_ADDR_MONTHS]);
    RTC_TIMER_YEARS      = bcd2bin(rtc_regs[RTC_ADDR_YEARS]);

	RTC_TIMER_LOCK = 0;
}

void Rtc :: set_time_in_chip(int corr_ppm)
{
	// calculate correction register
	BYTE corr_byte = 0;
	int div;
	if((corr_ppm > 136)||(corr_ppm < -136)) {
		div = 434;
		corr_byte = 0x80;
	} else {
		div = 217;
	}
	int val = (200 * corr_ppm) / div;
	if(val > 0)
		if(val & 1) val++;
	else
		if(val & 1) val--;
	val = (val >> 1) & 0x7F;
	corr_byte |= BYTE(val);
//	printf("Correction byte for %d ppm: %b\n", corr_ppm, corr_byte);
	
	RTC_TIMER_LOCK = 1;
	write_byte(RTC_ADDR_CTRL1,    0x80);
	write_byte(RTC_ADDR_SECONDS,  bin2bcd(RTC_TIMER_SECONDS));
	write_byte(RTC_ADDR_MINUTES,  bin2bcd(RTC_TIMER_MINUTES));
	write_byte(RTC_ADDR_HOURS,    bin2bcd(RTC_TIMER_HOURS));
	write_byte(RTC_ADDR_WEEKDAYS, bin2bcd(RTC_TIMER_WEEKDAYS));
	write_byte(RTC_ADDR_DAYS,     bin2bcd(RTC_TIMER_DAYS));
	write_byte(RTC_ADDR_MONTHS,   bin2bcd(RTC_TIMER_MONTHS));
	write_byte(RTC_ADDR_YEARS,    bin2bcd(RTC_TIMER_YEARS));
	write_byte(RTC_ADDR_OFFSET,   corr_byte);
	write_byte(RTC_ADDR_CTRL1,    0x00);
	RTC_TIMER_LOCK = 0;
}

int  Rtc :: get_correction(void)
{
	BYTE corr_byte = rtc_regs[RTC_ADDR_OFFSET];
	int mul = (corr_byte & 0x80)?434:217;
	int val = (corr_byte & 0x40)?(int(corr_byte) | -128):int(corr_byte & 0x3F);
//	printf("Mul = %d. Val = $%b (%d)\n", mul, val, val);
	int corr = (mul * val) / 50;
	if(corr > 0)
		if(corr & 1) corr++;
	else
		if(corr & 1) corr--;
	return (corr >> 1);
}

void Rtc :: get_time(int &y, int &M, int &D, int &wd, int &h, int &m, int &s)
{
	RTC_TIMER_LOCK = 1;
	y = (int)RTC_TIMER_YEARS;
	M = (int)RTC_TIMER_MONTHS;
	D = (int)RTC_TIMER_DAYS;
	wd = (int)RTC_TIMER_WEEKDAYS;
	h = (int)RTC_TIMER_HOURS;
	m = (int)RTC_TIMER_MINUTES;
	s = (int)RTC_TIMER_SECONDS;
	RTC_TIMER_LOCK = 0;
}

void Rtc :: set_time(int y, int M, int D, int wd, int h, int m, int s)
{
	RTC_TIMER_LOCK = 1;
	RTC_TIMER_YEARS = (BYTE)y;
	RTC_TIMER_MONTHS = (BYTE)M;
	RTC_TIMER_DAYS = (BYTE)D;
	RTC_TIMER_WEEKDAYS = (BYTE)wd;
	RTC_TIMER_HOURS = (BYTE)h;
	RTC_TIMER_MINUTES = (BYTE)m;
	RTC_TIMER_SECONDS = (BYTE)s;
	RTC_TIMER_LOCK = 0;

}

char * Rtc :: get_time_string(char *dest, int len)
{
	if(len < 9)
		return "";
	sprintf(dest, "%02d:%02d:%02d", RTC_TIMER_HOURS, RTC_TIMER_MINUTES, RTC_TIMER_SECONDS);
	return dest;
}


char * Rtc :: get_date_string(char *dest, int len)
{
	if(len < 14)
		return "";
	sprintf(dest, "%s %2d, %4d", month_strings_short[RTC_TIMER_MONTHS], RTC_TIMER_DAYS, 1980+RTC_TIMER_YEARS);
	return dest;
}

char * Rtc :: get_long_date(char *dest, int len)
{
	// Wednesday September 30, 2009
	if(len < 29)
		return "";
	sprintf(dest, "%s %s %2d, %4d", weekday_strings[RTC_TIMER_WEEKDAYS], month_strings_long[RTC_TIMER_MONTHS], RTC_TIMER_DAYS, 1980+RTC_TIMER_YEARS);
	return dest;
}

DWORD Rtc :: get_fat_time(void)
{
    if(capable)
    	return RTC_TIMER_FAT_TIME;
/*
    30 << 25 = 0x3C000000
    11 << 21 = 0x01600000
     7 << 16 = 0x00070000
    16 << 11 = 0x00008000
    36 <<  5 = 0x00000480
    23 <<  0 = 0x00000017
*/
    return 0x3D678497;
}

Rtc rtc; // global

// ============================================================
// == Functions that link the RTC to the configuration manager
// ============================================================
int RtcConfigStore :: fetch_children(void)
{
	ConfigStore :: fetch_children();
	read();
}

void RtcConfigStore :: read(void)
{
	printf("** Cfg RTC Read **\n");
	int y, M, D, wd, h, m, s;
	rtc.get_time(y, M, D, wd, h, m, s);
	int corr = rtc.get_correction();
    ConfigItem *i;

    for(int n = 0;n < children.get_elements(); n++) {
    	i = (ConfigItem *)children[n];
		switch(i->definition->id) {
		case CFG_RTC_YEAR:
			i->value = 1980 + y;
			break;
		case CFG_RTC_MONTH:  
			i->value = M;
			break;
		case CFG_RTC_DATE:
			i->value = D;
			break;
		case CFG_RTC_WEEKDAY:
			i->value = wd;
			break;
		case CFG_RTC_HOUR:   
			i->value = h;
			break;
		case CFG_RTC_MINUTE: 
			i->value = m;
			break;
		case CFG_RTC_SECOND: 
			i->value = s;
			break;
		case CFG_RTC_CORR:
			i->value = corr;
			break;
		default:
			break;
		}
	}
	check_bounds();
}

void RtcConfigStore :: write(void)
{
	printf("** Cfg RTC Write **\n");

	if(!dirty)
		return;

	int y, M, D, wd, h, m, s, corr;

    ConfigItem *i;

    for(int n = 0;n < children.get_elements(); n++) {
    	i = (ConfigItem *)children[n];
		switch(i->definition->id) {
		case CFG_RTC_YEAR:
			y = i->value - 1980;
			break;
		case CFG_RTC_MONTH:
			M = i->value;
			break;
		case CFG_RTC_DATE:
			D = i->value;
			break;
		case CFG_RTC_WEEKDAY:
			wd = i->value;
			break;
		case CFG_RTC_HOUR:
			h = i->value;
			break;
		case CFG_RTC_MINUTE:
			m = i->value;
			break;
		case CFG_RTC_SECOND:
			s = i->value;
			break;
		case CFG_RTC_CORR:
			corr = i->value;
			break;
		default:
			break;
		}
	}
    printf("Writing time: %d-%d-%d (%d) %02d:%02d:%02d\n", y, M, D, wd, h, m, s);
    rtc.set_time(y, M, D, wd, h, m, s);
	rtc.set_time_in_chip(corr);
	
	dirty = false;
}
