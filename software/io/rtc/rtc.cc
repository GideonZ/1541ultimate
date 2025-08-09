#include "rtc.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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
#define CFG_RTC_HOUR    0x15
#define CFG_RTC_MINUTE  0x16
#define CFG_RTC_SECOND  0x17
#define CFG_RTC_CORR    0x18

struct t_cfg_definition rtc_config[] = {
    { CFG_RTC_YEAR,     CFG_TYPE_VALUE,  "Year",     "%d", NULL,  1980, 2079, 2015 },
    { CFG_RTC_MONTH,    CFG_TYPE_ENUM,   "Month",    "%s", month_strings_long,  1, 12,  10 },
    { CFG_RTC_DATE,     CFG_TYPE_VALUE,  "Day",      "%d", NULL,     1, 31,  13 },
    { CFG_RTC_HOUR,     CFG_TYPE_VALUE,  "Hours",   "%02d", NULL,     0, 23, 16 },
    { CFG_RTC_MINUTE,   CFG_TYPE_VALUE,  "Minutes", "%02d", NULL,     0, 59, 52 },
    { CFG_RTC_SECOND,   CFG_TYPE_VALUE,  "Seconds", "%02d", NULL,     0, 59, 55 },
    { CFG_RTC_CORR,     CFG_TYPE_VALUE,  "Correction", "%d ppm", NULL, -200, 200, 0 },
    { CFG_TYPE_END,     CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};

// ========================
//   Register definitions
// ========================
#define RTC_CHIP_DATA  *((volatile uint8_t *)(RTC_BASE + 0x00))
#define RTC_CHIP_SPEED *((volatile uint8_t *)(RTC_BASE + 0x04))
#define RTC_CHIP_CTRL  *((volatile uint8_t *)(RTC_BASE + 0x08))

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
    capable = false;
}

Rtc::~Rtc()
{
    //printf("Destructor RTC\n");
    if (capable) {
        //config_manager.dump();
        ConfigManager::getConfigManager()->remove_store(cfg);
        delete cfg;
        //printf("RTC configuration store now gone..\n");
    }
}

void Rtc::init()
{
    if (getFpgaCapabilities() & CAPAB_RTC_CHIP) {
        capable = true;
        cfg = new RtcConfigStore("Clock Settings", rtc_config);
        ConfigManager::getConfigManager()->add_custom_store(cfg);
        get_time_from_chip();

        // Check and correct clock out setting
        if ((rtc_regs[RTC_ADDR_CLOCKOUT] & 0x70) != 0x70)
            write_byte(RTC_ADDR_CLOCKOUT, 0x72);
    }
}

void Rtc::write_byte(int addr, uint8_t val)
{
    ENTER_SAFE_SECTION
    RTC_CHIP_CTRL = SPI_CS_ON;
    RTC_CHIP_DATA = (uint8_t)(0x10 + addr);
    RTC_CHIP_DATA = val;
    RTC_CHIP_CTRL = SPI_CS_OFF;
    LEAVE_SAFE_SECTION
    rtc_regs[addr] = val; // update internal structure as well.
}

void Rtc::read_all(void)
{
    ENTER_SAFE_SECTION
    RTC_CHIP_CTRL = SPI_CS_ON;
    RTC_CHIP_DATA = 0x90; // read + startbit

    for (int i = 0; i < 16; i++) {
        rtc_regs[i] = RTC_CHIP_DATA;
    }
    RTC_CHIP_CTRL = SPI_CS_OFF;
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
    if ((corr_ppm > 136) || (corr_ppm < -136)) {
        div = 434;
        corr_byte = 0x80;
    } else {
        div = 217;
    }
    int val = (200 * corr_ppm) / div;
    if (val > 0)
        if (val & 1)
            val++;
        else if (val & 1)
            val--;
    val = (val >> 1) & 0x7F;
    corr_byte |= uint8_t(val);
//	printf("Correction byte for %d ppm: %b\n", corr_ppm, corr_byte);

    write_byte(RTC_ADDR_CTRL1, 0x80);
    write_byte(RTC_ADDR_SECONDS, bin2bcd(s));
    write_byte(RTC_ADDR_MINUTES, bin2bcd(m));
    write_byte(RTC_ADDR_HOURS, bin2bcd(h));
    write_byte(RTC_ADDR_WEEKDAYS, bin2bcd(wd));
    write_byte(RTC_ADDR_DAYS, bin2bcd(D));
    write_byte(RTC_ADDR_MONTHS, bin2bcd(M));
    write_byte(RTC_ADDR_YEARS, bin2bcd(y));
    write_byte(RTC_ADDR_OFFSET, corr_byte);
    write_byte(RTC_ADDR_CTRL1, 0x00);

}

int Rtc::get_correction(void)
{
    uint8_t corr_byte = rtc_regs[RTC_ADDR_OFFSET];
    int mul = (corr_byte & 0x80) ? 434 : 217;
    int val = (corr_byte & 0x40) ? (int(corr_byte) | -128) : int(corr_byte & 0x3F);
//	printf("Mul = %d. Val = $%b (%d)\n", mul, val, val);
    int corr = (mul * val) / 50;
    if (corr > 0)
        if (corr & 1)
            corr++;
        else if (corr & 1)
            corr--;
    return (corr >> 1);
}

void Rtc::get_time(int &y, int &M, int &D, int &wd, int &h, int &m, int &s)
{
    read_all(); // read directly from chip

    s = (int) bcd2bin(rtc_regs[RTC_ADDR_SECONDS]);
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

void Rtc::set_time(int y, int M, int D, int wd, int h, int m, int s)
{
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

void Rtc::set_time_utc(int seconds)
{
    // UTC time coming in, so convert it to local time
    time_t now;
    struct tm timeinfo;

    now = seconds;
    localtime_r(&now, &timeinfo);

    int y = timeinfo.tm_year - 80;
    int M = timeinfo.tm_mon + 1;
    int D = timeinfo.tm_mday;
    int wd = timeinfo.tm_wday;
    int h = timeinfo.tm_hour;
    int m = timeinfo.tm_min;
    int s = timeinfo.tm_sec;

    set_time_in_chip(get_correction(), y, M, D, wd, h, m, s);
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
}

#include "init_function.h"
InitFunction init_rtc("RTC", [](void *_obj, void *_param) { rtc.init(); }, NULL, NULL, 2);
