#include "rtc_i2c_sntp.h"
#include <stdio.h>
#include "i2c_drv.h"
#include "u2p.h"

#include <stdlib.h>
#include <time.h>
#include "sntp_time.h"
#include "config.h"

static int use_ntp = -1;

const char *month_strings_short[]={ "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

const char *month_strings_long[] = { "", "January", "February", "March", "April", "May", "June",
                                    "July", "August", "September", "October", "November", "December" };

const char *weekday_strings[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

typedef struct {
    const char *timezone;
    const char *utc;
    const char *location;
    const char *posix;
} timezone_entry_t;

const timezone_entry_t zones[] = {
    { "AoE",   "UTC -12",   "US Baker Island", "AOE12" },
    { "NUT",   "UTC -11",   "America Samoa",   "NUT11" },
    { "HST",   "UTC -10",   "Hawaii", "HST11HDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "MART",  "UTC -9:30", "French Polynesia", "MART9:30,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "AKST",  "UTC -9",    "Alaska", "ASKT9AKDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "PDT",   "UTC -8",    "Los Angeles", "PST8PDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "MST",   "UTC -7",    "Denver, Colorado", "MST7MDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "MST",   "UTC -7",    "Phoenix, Arizona", "MST7" },
    { "CST",   "UTC -6",    "Chicago, Illinois", "CST6CDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "EST",   "UTC -5",    "New York", "EST5EDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "ART",   "UTC -3",    "Argentina", "ART3" },
    { "NDT",   "UTC -2:30", "Newfoundland", "NDT3:30NST,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "WGST",  "UTC -2",    "Greenland", "WGST3WGT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "CVT",   "UTC -1",    "Cabo Verde", "CVT1" },
    { "GMT",   "UTC 0",     "Iceland", "GMT" },
    { "BST",   "UTC +1",    "UK", "BST0GMT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "CEST",  "UTC +2",    "Western Europe", "CEST-1CET,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "MSK",   "UTC +3",    "Greece", "MSK-3" },
    { "GST",   "UTC +4",    "Azerbaijan", "GST-4" },
    { "IRDT",  "UTC +4:30", "Iran", "IRDT-3:30IRST,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "UZT",   "UTC +5",    "Pakistan", "UZT-5" },
    { "IST",   "UTC +5:30", "India", "IST-5:30" },
    { "NPT",   "UTC +5:45", "Nepal", "NPT-5:45" },
    { "BST",   "UTC +6",    "Bangladesh", "BST-6" },
    { "MMT",   "UTC +6:30", "Myanmar", "MMT-6:30" },
    { "WIB",   "UTC +7",    "Indonesia", "WIB-7" },
    { "CST",   "UTC +8",    "China", "CST-8" },
    { "ACWST", "UTC +8:45", "Western Australia", "ACWST-8:45" },
    { "JST",   "UTC +9",    "Japan", "JST-9" },
    { "ACST",  "UTC +9:30", "Central Australia", "ACST-8:30ACDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "AEST",  "UTC +10",   "Eastern Australia", "AEST-9AEDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "LHST",  "UTC +10:30","Lord Howe Island", "LHST-9:30LHDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "SBT",   "UTC +11",   "Solomon Islands", "SBT-11" },
    { "ANAT",  "UTC +12",   "New Zealand", "ANAT-12" },
    { "CHAST", "UTC +12:45","Chatham Islands", "CHAST-11:45CHADT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "TOT",   "UTC +13",   "Tonga", "TOT-12TOST,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "LINT",  "UTC +14",   "Christmas Island", "LINT-14" },
};

const char *zone_names[] = {
    "US Baker Island",
    "America Samoa",
    "Hawaii",
    "French Polynesia",
    "Alaska",
    "Los Angeles",
    "Denver, Colorado",
    "Phoenix, Arizona",
    "Chicago, Illinois",
    "New York",
    "Argentina",
    "Newfoundland",
    "Greenland",
    "Cabo Verde",
    "Iceland",
    "UK",
    "Western Europe",
    "Greece",
    "Azerbaijan",
    "Iran",
    "Pakistan",
    "India",
    "Nepal",
    "Bangladesh",
    "Myanmar",
    "Indonesia",
    "China",
    "Western Australia",
    "Japan",
    "Central Australia",
    "Eastern Australia",
    "Lord Howe Island",
    "Solomon Islands",
    "New Zealand",
    "Chatham Islands",
    "Tonga",
    "Christmas Island",
};

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
#define CFG_RTC_ZONE    0x19
#define CFG_RTC_NTP     0x1A

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

struct t_cfg_definition time_config[] = {
    { CFG_RTC_ZONE,     CFG_TYPE_ENUM,   "TimeZone", "%s", zone_names, 0, (sizeof(zone_names)/sizeof(const char *))-1, 16 },
    { CFG_RTC_NTP,      CFG_TYPE_ENUM,   "NTP Enable", "%s", en_dis,     0,  1, 0 },
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
    register_store(0x54696d65, "Time Settings", time_config);
    cfg2 = new RtcConfigStore("Clock Settings", rtc_config);
    ConfigManager::getConfigManager()->add_custom_store(cfg2);
    // This is a fix for the case where the I2C is not yet initialized
    // This only works for systems where I2C is operated in PIO mode
    // Fortunately, the only system where this is not the case is the U64-II,
    // which does not have an RTC.
    if (!i2c) {
        i2c = new I2C_Driver();
    }
    get_time_from_chip();
}

Rtc::~Rtc()
{
    if (capable) {
        ConfigManager::getConfigManager()->remove_store(cfg);
        delete cfg;
    }
}

void Rtc::effectuate_settings(void)
{
    use_ntp = cfg->get_value(CFG_RTC_NTP);
    printf("Set use_ntp=%i\n", use_ntp);
}

void Rtc::write_byte(int addr, uint8_t val)
{
    ENTER_SAFE_SECTION
    i2c->i2c_write_byte(0xA2, addr, val);
    LEAVE_SAFE_SECTION
    rtc_regs[addr] = val; // update internal structure as well.
}

void Rtc::read_all(void)
{
    ENTER_SAFE_SECTION
    int dummy;
    for (int i = 0; i < 11; i++) {
        rtc_regs[i] = i2c->i2c_read_byte(0xA2, i, &dummy);

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
   if (use_ntp < 0)
   {
    use_ntp = cfg->get_value(CFG_RTC_NTP);
    printf("Set use_ntp=%i\n", use_ntp);
   }

   if (use_ntp)
   {
    uint32_t sec = RTC_TIMER_SECONDS;

    time_t now;
    struct tm timeinfo;
    int zone_index = cfg->get_value(CFG_RTC_ZONE);

    setenv("TZ", zones[zone_index].posix, 1);
    tzset();
    now = sec;
    localtime_r(&now, &timeinfo);

    y = timeinfo.tm_year - 80;
    M = timeinfo.tm_mon + 1;
    D = timeinfo.tm_mday;
    wd = timeinfo.tm_wday;
    h = timeinfo.tm_hour;
    m = timeinfo.tm_min;
    s = timeinfo.tm_sec;
   }
   else
   {
    read_all(); // read directly from chip

    s = (int) bcd2bin(rtc_regs[RTC_ADDR_SECONDS] & 0x7F);
    m = (int) bcd2bin(rtc_regs[RTC_ADDR_MINUTES]);
    h = (int) bcd2bin(rtc_regs[RTC_ADDR_HOURS]);
    wd = (int) bcd2bin(rtc_regs[RTC_ADDR_WEEKDAYS]);
    D = (int) bcd2bin(rtc_regs[RTC_ADDR_DAYS]);
    M = (int) bcd2bin(rtc_regs[RTC_ADDR_MONTHS]);
    y = (int) bcd2bin(rtc_regs[RTC_ADDR_YEARS]);

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
}

void Rtc::set_time(int y, int M, int D, int wd, int h, int m, int s)
{
   if (use_ntp < 0)
   {
    use_ntp = cfg->get_value(CFG_RTC_NTP);
    printf("Set use_ntp=%i\n", use_ntp);
   }
    if (!use_ntp) return;
    struct tm timeinfo;
    timeinfo.tm_year = y + 80;
    timeinfo.tm_mon = M - 1;
    timeinfo.tm_mday = D;
    timeinfo.tm_wday = wd;
    timeinfo.tm_hour = h;
    timeinfo.tm_min = m;
    timeinfo.tm_sec = s;
    time_t seconds = mktime(&timeinfo);
    RTC_TIMER_SECONDS = seconds;
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
        int value = i->getValue();
        switch (i->definition->id) {
        case CFG_RTC_YEAR:
            y = value - 1980;
            break;
        case CFG_RTC_MONTH:
            M = value;
            break;
        case CFG_RTC_DATE:
            D = value;
            break;
            /*
             case CFG_RTC_WEEKDAY:
             wd = i->value;
             break;
             */
        case CFG_RTC_HOUR:
            h = value;
            break;
        case CFG_RTC_MINUTE:
            m = value;
            break;
        case CFG_RTC_SECOND:
            s = value;
            break;
        case CFG_RTC_CORR:
            corr = value;
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

    set_effectuated();
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
