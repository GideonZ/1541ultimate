#include "rtc.h"
#include <stdio.h>
#include "i2c.h"

const char *month_strings_short[]={ "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

const char *month_strings_long[] = { "", "January", "February", "March", "April", "May", "June",
                                    "July", "August", "September", "October", "November", "December" };

const char *weekday_strings[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

// =================
//   Configuration
// =================

#define CFG_RTC_YEAR    0x11
#define CFG_RTC_MONTH   0x12
#define CFG_RTC_DATE    0x13
// #define CFG_RTC_WEEKDAY 0x14
#define CFG_RTC_HOUR    0x15
#define CFG_RTC_MINUTE  0x16
#define CFG_RTC_SECOND  0x17
#define CFG_RTC_CORR    0x18

struct t_cfg_definition rtc_config[] = {
    { CFG_RTC_YEAR,     CFG_TYPE_VALUE,  "Year",     "%d", NULL,  1980, 2079, 2015 },
    { CFG_RTC_MONTH,    CFG_TYPE_ENUM,   "Month",    "%s", month_strings_long,  1, 12,  10 },
    { CFG_RTC_DATE,     CFG_TYPE_VALUE,  "Day",      "%d", NULL,     1, 31,  13 },
//  { CFG_RTC_WEEKDAY,  CFG_TYPE_ENUM,   "Weekday",  "%s", weekday_strings, 0, 6, 2 },
    { CFG_RTC_HOUR,     CFG_TYPE_VALUE,  "Hours",   "%02d", NULL,     0, 23, 16 },
    { CFG_RTC_MINUTE,   CFG_TYPE_VALUE,  "Minutes", "%02d", NULL,     0, 59, 52 },
    { CFG_RTC_SECOND,   CFG_TYPE_VALUE,  "Seconds", "%02d", NULL,     0, 59, 55 },
    { CFG_RTC_CORR,     CFG_TYPE_VALUE,  "Correction", "%d ppm", NULL, -200, 200, 0 },
    { CFG_TYPE_END,     CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};

#define RTC_ADDR_CTRL1      0
#define RTC_ADDR_CTRL2      1
#define RTC_ADDR_OFFSET     2
#define RTC_ADDR_RAM        3
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
    while (bin >= 10) { // division is probably slower
        bin -= 10;
        bcd += 0x10;
    }
    bcd += bin;
    return bcd;
}

Rtc::Rtc()
{
    capable = true;
    cfg = new RtcConfigStore("Clock Settings", rtc_config);
    ConfigManager::getConfigManager()->add_custom_store(cfg);
    get_time_from_chip();
}

Rtc::~Rtc()
{
    if (capable) {
        ConfigManager::getConfigManager()->remove_store(cfg);
        delete cfg;
    }
}

void Rtc::write_byte(int addr, uint8_t val)
{
    ENTER_SAFE_SECTION
    i2c_write_byte(0xA2, addr, val);
    LEAVE_SAFE_SECTION
    rtc_regs[addr] = val; // update internal structure as well.
}

void Rtc::read_all(void)
{
    ENTER_SAFE_SECTION
    int dummy;
    for (int i = 0; i < 11; i++) {
        rtc_regs[i] = i2c_read_byte(0xA2, i, &dummy);

    }
    LEAVE_SAFE_SECTION
}

bool Rtc::is_valid(void)
{
    return (rtc_regs[RTC_ADDR_WEEKDAYS] != 0xFF);
}

void Rtc::get_time_from_chip(void)
{
    read_all();

    if (getFpgaCapabilities() & CAPAB_RTC_TIMER) {
        RTC_TIMER_LOCK = 1;
        RTC_TIMER_SECONDS = bcd2bin(rtc_regs[RTC_ADDR_SECONDS]);
        RTC_TIMER_MINUTES = bcd2bin(rtc_regs[RTC_ADDR_MINUTES]);
        RTC_TIMER_HOURS = bcd2bin(rtc_regs[RTC_ADDR_HOURS]);
        RTC_TIMER_WEEKDAYS = bcd2bin(rtc_regs[RTC_ADDR_WEEKDAYS]);
        RTC_TIMER_DAYS = bcd2bin(rtc_regs[RTC_ADDR_DAYS]);
        RTC_TIMER_MONTHS = bcd2bin(rtc_regs[RTC_ADDR_MONTHS]);
        RTC_TIMER_YEARS = bcd2bin(rtc_regs[RTC_ADDR_YEARS]);
        RTC_TIMER_LOCK = 0;
    }
}

void Rtc::set_time_in_chip(int corr_ppm, int y, int M, int D, int wd, int h, int m, int s)
{
    // calculate correction register
    uint8_t corr_byte = 0;
    int div;
    int val = (29 * corr_ppm) / 59;
    if (val > 0)
        if (val & 1)
            val++;
        else if (val & 1)
            val--;
    val = (val >> 1) & 0x7F;
    corr_byte = 0x80 | uint8_t(val);
    printf("Correction byte for %d ppm: %b\n", corr_ppm, corr_byte);

    write_byte(RTC_ADDR_CTRL1, 0x21); // stop
    write_byte(RTC_ADDR_CTRL2, 0x07); // no clock out
    write_byte(RTC_ADDR_SECONDS, bin2bcd(s));
    write_byte(RTC_ADDR_MINUTES, bin2bcd(m));
    write_byte(RTC_ADDR_HOURS, bin2bcd(h));
    write_byte(RTC_ADDR_WEEKDAYS, bin2bcd(wd));
    write_byte(RTC_ADDR_DAYS, bin2bcd(D));
    write_byte(RTC_ADDR_MONTHS, bin2bcd(M));
    write_byte(RTC_ADDR_YEARS, bin2bcd(y));
    write_byte(RTC_ADDR_OFFSET, corr_byte);
    write_byte(RTC_ADDR_CTRL1, 0x01);

}

int Rtc::get_correction(void)
{
    uint8_t corr_byte = rtc_regs[RTC_ADDR_OFFSET] & 0x7F;
    int val = (corr_byte & 0x40) ? (int(corr_byte & 0x3F) - 64) : int(corr_byte & 0x3F);

    int corr = (236 * val) / 29;
    if (corr > 0)
        if (corr & 1)
            corr++;
        else if (corr & 1)
            corr--;
    return (corr >> 1);
}

void Rtc::get_time(int &y, int &M, int &D, int &wd, int &h, int &m, int &s)
{
    if (getFpgaCapabilities() & CAPAB_RTC_TIMER) {
        RTC_TIMER_LOCK = 1;
        y = (int) RTC_TIMER_YEARS;
        M = (int) RTC_TIMER_MONTHS;
        D = (int) RTC_TIMER_DAYS;
        wd = (int) RTC_TIMER_WEEKDAYS;
        h = (int) RTC_TIMER_HOURS;
        m = (int) RTC_TIMER_MINUTES;
        s = (int) RTC_TIMER_SECONDS;
        RTC_TIMER_LOCK = 0;
    } else {
        read_all(); // read directly from chip

        s = (int) bcd2bin(rtc_regs[RTC_ADDR_SECONDS] & 0x7F);
        m = (int) bcd2bin(rtc_regs[RTC_ADDR_MINUTES]);
        h = (int) bcd2bin(rtc_regs[RTC_ADDR_HOURS]);
        wd = (int) bcd2bin(rtc_regs[RTC_ADDR_WEEKDAYS]);
        D = (int) bcd2bin(rtc_regs[RTC_ADDR_DAYS]);
        M = (int) bcd2bin(rtc_regs[RTC_ADDR_MONTHS]);
        y = (int) bcd2bin(rtc_regs[RTC_ADDR_YEARS]);
    }
    if (M < 1)
        M = 1;
    if (M > 12)
        M = 12;
    if (D < 1)
        D = 1;
    if (D > 31)
        D = 31;
    if (wd < 0)
        wd = 0;
    if (wd > 6)
        wd = 6;
}

void Rtc::set_time(int y, int M, int D, int wd, int h, int m, int s)
{
    if (getFpgaCapabilities() & CAPAB_RTC_TIMER) {
        RTC_TIMER_LOCK = 1;
        RTC_TIMER_YEARS = (uint8_t) y;
        RTC_TIMER_MONTHS = (uint8_t) M;
        RTC_TIMER_DAYS = (uint8_t) D;
        RTC_TIMER_WEEKDAYS = (uint8_t) wd;
        RTC_TIMER_HOURS = (uint8_t) h;
        RTC_TIMER_MINUTES = (uint8_t) m;
        RTC_TIMER_SECONDS = (uint8_t) s;
        RTC_TIMER_LOCK = 0;
    }
}

const char * Rtc::get_time_string(char *dest, int len)
{
    if (len < 9)
        return "";
    int y, M, D, wd, h, m, s;
    get_time(y, M, D, wd, h, m, s);
    sprintf(dest, "%02d:%02d:%02d", h, m, s);
    return dest;
}

const char * Rtc::get_date_string(char *dest, int len)
{
    if (len < 14)
        return "";
    int y, M, D, wd, h, m, s;
    get_time(y, M, D, wd, h, m, s);
    sprintf(dest, "%s %2d, %4d", month_strings_short[M], D, 1980 + y);
    return dest;
}

const char * Rtc::get_long_date(char *dest, int len)
{
    // Wednesday September 30, 2009
    if (len < 29)
        return "";
    int y, M, D, wd, h, m, s;
    get_time(y, M, D, wd, h, m, s);
    sprintf(dest, "%s %s %2d, %4d", weekday_strings[wd], month_strings_long[M], D, 1980 + y);
    return dest;
}

uint32_t Rtc::get_fat_time(void)
{
    if (!capable)
        return 0x42FF9877;
    /*
     33 << 25 = 0x42000000
     7 << 21 = 0x00E00000
     31 << 16 = 0x001F0000
     19 << 11 = 0x00009800
     03 <<  5 = 0x00000060
     23 <<  0 = 0x00000017
     */

    if (getFpgaCapabilities() & CAPAB_RTC_TIMER)
        return RTC_TIMER_FAT_TIME;

    int y, M, D, wd, h, m, s;
    get_time(y, M, D, wd, h, m, s);

    uint32_t result = 0;
    result |= (y << 25);
    result |= (M << 21);
    result |= (D << 16);
    result |= (h << 11);
    result |= (m << 5);
    result |= (s >> 1);

    return result;
}

Rtc rtc; // global

// ============================================================
// == Functions that link the RTC to the configuration manager
// ============================================================
void RtcConfigStore::at_open_config(void)
{
    printf("** Cfg RTC Read **\n");
    int y, M, D, wd, h, m, s;
    rtc.get_time(y, M, D, wd, h, m, s);
    int corr = rtc.get_correction();
    ConfigItem *i;

    for (int n = 0; n < items.get_elements(); n++) {
        i = items[n];
        switch (i->definition->id) {
        case CFG_RTC_YEAR:
            i->value = 1980 + y;
            break;
        case CFG_RTC_MONTH:
            i->value = M;
            break;
        case CFG_RTC_DATE:
            i->value = D;
            break;
            /*
             case CFG_RTC_WEEKDAY:
             i->value = wd;
             break;
             */
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

void RtcConfigStore::at_close_config(void)
{
    printf("** Cfg RTC Write **\n");

    if (!need_effectuate())
        return;

    int y, M, D, wd, h, m, s, corr;

    ConfigItem *i;

    for (int n = 0; n < items.get_elements(); n++) {
        i = items[n];
        switch (i->definition->id) {
        case CFG_RTC_YEAR:
            y = i->value - 1980;
            break;
        case CFG_RTC_MONTH:
            M = i->value;
            break;
        case CFG_RTC_DATE:
            D = i->value;
            break;
            /*
             case CFG_RTC_WEEKDAY:
             wd = i->value;
             break;
             */
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
    int yz, mz, cz;
    mz = M;
    yz = y + 1980;
    if (mz < 3) {
        yz--;
        mz += 12;
    }
    cz = yz / 100;
    yz = yz % 100;
    wd = D + (mz + 1) * 13 / 5 + yz + yz / 4 + cz / 4 - 2 * cz + 6;
    wd = wd % 7;
    if (wd < 0)
        wd += 7;
    printf("Writing time: %d-%d-%d (%d) %02d:%02d:%02d\n", y, M, D, wd, h, m, s);
    rtc.set_time(y, M, D, wd, h, m, s);
    rtc.set_time_in_chip(corr, y, M, D, wd, h, m, s);

    set_effectuated();;
}

extern "C" uint32_t get_fattime(void) /* 31-25: Year(0-127 org.1980), 24-21: Month(1-12), 20-16: Day(1-31) */
/* 15-11: Hour(0-23), 10-5: Minute(0-59), 4-0: Second(0-29 *2) */
{
    return rtc.get_fat_time();

    /*
     29 << 25 = 0x3A000000
     4 << 21 = 0x00800000
     4 << 16 = 0x00040000
     9 << 11 = 0x00004800
     36 <<  5 = 0x00000480
     23 <<  0 = 0x00000017
     return 0x3A844C97;
     */
}
