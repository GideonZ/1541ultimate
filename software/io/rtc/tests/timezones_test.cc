// Host test of the time zone table: every POSIX rule must be one that newlib
// parses, and must give the UTC offsets of its IANA tzdb zone in 2026-2027.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "../timezones.cc"

#define ZONE_COUNT (sizeof(zone_names)/sizeof(const char *))

typedef struct {
    const char *location;
    const char *tzdb;
    int offset;          // minutes east of UTC on 2026-01-01 00:00 UTC
    int offset_alt;      // minutes east of UTC after the first clock change
    const char *changes[4]; // clock changes in 2026 and 2027, UTC
} expected_zone_t;

// From IANA tzdb 2026e. The order is the stored config value.
static const expected_zone_t expected[] = {
    { "US Baker Island",      "Etc/GMT+12",                     -720, -720, {  } },
    { "America Samoa",        "Pacific/Pago_Pago",              -660, -660, {  } },
    { "Hawaii",               "Pacific/Honolulu",               -600, -600, {  } },
    { "French Polynesia",     "Pacific/Tahiti",                 -600, -600, {  } },
    { "Alaska",               "America/Anchorage",              -540, -480, { "2026-03-08 11:00", "2026-11-01 10:00", "2027-03-14 11:00", "2027-11-07 10:00" } },
    { "Los Angeles",          "America/Los_Angeles",            -480, -420, { "2026-03-08 10:00", "2026-11-01 09:00", "2027-03-14 10:00", "2027-11-07 09:00" } },
    { "Denver, Colorado",     "America/Denver",                 -420, -360, { "2026-03-08 09:00", "2026-11-01 08:00", "2027-03-14 09:00", "2027-11-07 08:00" } },
    { "Phoenix, Arizona",     "America/Phoenix",                -420, -420, {  } },
    { "Chicago, Illinois",    "America/Chicago",                -360, -300, { "2026-03-08 08:00", "2026-11-01 07:00", "2027-03-14 08:00", "2027-11-07 07:00" } },
    { "New York",             "America/New_York",               -300, -240, { "2026-03-08 07:00", "2026-11-01 06:00", "2027-03-14 07:00", "2027-11-07 06:00" } },
    { "Argentina",            "America/Argentina/Buenos_Aires", -180, -180, {  } },
    { "Newfoundland",         "America/St_Johns",               -210, -150, { "2026-03-08 05:30", "2026-11-01 04:30", "2027-03-14 05:30", "2027-11-07 04:30" } },
    { "Greenland",            "America/Nuuk",                   -120,  -60, { "2026-03-29 01:00", "2026-10-25 01:00", "2027-03-28 01:00", "2027-10-31 01:00" } },
    { "Cabo Verde",           "Atlantic/Cape_Verde",             -60,  -60, {  } },
    { "Iceland",              "Atlantic/Reykjavik",                0,    0, {  } },
    { "UK",                   "Europe/London",                     0,   60, { "2026-03-29 01:00", "2026-10-25 01:00", "2027-03-28 01:00", "2027-10-31 01:00" } },
    { "Western Europe",       "Europe/Paris",                     60,  120, { "2026-03-29 01:00", "2026-10-25 01:00", "2027-03-28 01:00", "2027-10-31 01:00" } },
    { "Greece",               "Europe/Athens",                   120,  180, { "2026-03-29 01:00", "2026-10-25 01:00", "2027-03-28 01:00", "2027-10-31 01:00" } },
    { "Azerbaijan",           "Asia/Baku",                       240,  240, {  } },
    { "Iran",                 "Asia/Tehran",                     210,  210, {  } },
    { "Pakistan",             "Asia/Karachi",                    300,  300, {  } },
    { "India",                "Asia/Kolkata",                    330,  330, {  } },
    { "Nepal",                "Asia/Kathmandu",                  345,  345, {  } },
    { "Bangladesh",           "Asia/Dhaka",                      360,  360, {  } },
    { "Myanmar",              "Asia/Yangon",                     390,  390, {  } },
    { "Indonesia",            "Asia/Jakarta",                    420,  420, {  } },
    { "China",                "Asia/Shanghai",                   480,  480, {  } },
    { "Western Australia",    "Australia/Perth",                 480,  480, {  } },
    { "Japan",                "Asia/Tokyo",                      540,  540, {  } },
    { "Central Australia",    "Australia/Adelaide",              630,  570, { "2026-04-04 16:30", "2026-10-03 16:30", "2027-04-03 16:30", "2027-10-02 16:30" } },
    { "Eastern Australia",    "Australia/Sydney",                660,  600, { "2026-04-04 16:00", "2026-10-03 16:00", "2027-04-03 16:00", "2027-10-02 16:00" } },
    { "Lord Howe Island",     "Australia/Lord_Howe",             660,  630, { "2026-04-04 15:00", "2026-10-03 15:30", "2027-04-03 15:00", "2027-10-02 15:30" } },
    { "Solomon Islands",      "Pacific/Guadalcanal",             660,  660, {  } },
    { "New Zealand",          "Pacific/Auckland",                780,  720, { "2026-04-04 14:00", "2026-09-26 14:00", "2027-04-03 14:00", "2027-09-25 14:00" } },
    { "Chatham Islands",      "Pacific/Chatham",                 825,  765, { "2026-04-04 14:00", "2026-09-26 14:00", "2027-04-03 14:00", "2027-09-25 14:00" } },
    { "Tonga",                "Pacific/Tongatapu",               780,  780, {  } },
    { "Christmas Island",     "Pacific/Kiritimati",              840,  840, {  } },
    { "Aleutian Islands",     "America/Adak",                   -600, -540, { "2026-03-08 12:00", "2026-11-01 11:00", "2027-03-14 12:00", "2027-11-07 11:00" } },
    { "Marquesas Islands",    "Pacific/Marquesas",              -570, -570, {  } },
    { "Gambier Islands",      "Pacific/Gambier",                -540, -540, {  } },
    { "Pitcairn Islands",     "Pacific/Pitcairn",               -480, -480, {  } },
    { "Mexico, C. America",   "America/Mexico_City",            -360, -360, {  } },
    { "Easter Island",        "Pacific/Easter",                 -300, -360, { "2026-04-05 03:00", "2026-09-06 04:00", "2027-04-04 03:00", "2027-09-05 04:00" } },
    { "Colombia, Peru",       "America/Bogota",                 -300, -300, {  } },
    { "Cuba",                 "America/Havana",                 -300, -240, { "2026-03-08 05:00", "2026-11-01 05:00", "2027-03-14 05:00", "2027-11-07 05:00" } },
    { "Venezuela, Caribbean", "America/Puerto_Rico",            -240, -240, {  } },
    { "Atlantic Canada",      "America/Halifax",                -240, -180, { "2026-03-08 06:00", "2026-11-01 05:00", "2027-03-14 06:00", "2027-11-07 05:00" } },
    { "Chile",                "America/Santiago",               -180, -240, { "2026-04-05 03:00", "2026-09-06 04:00", "2027-04-04 03:00", "2027-09-05 04:00" } },
    { "St Pierre, Miquelon",  "America/Miquelon",               -180, -120, { "2026-03-08 05:00", "2026-11-01 04:00", "2027-03-14 05:00", "2027-11-07 04:00" } },
    { "Fernando de Noronha",  "America/Noronha",                -120, -120, {  } },
    { "Azores",               "Atlantic/Azores",                 -60,    0, { "2026-03-29 01:00", "2026-10-25 01:00", "2027-03-28 01:00", "2027-10-31 01:00" } },
    { "West Africa",          "Africa/Lagos",                     60,   60, {  } },
    { "South Africa",         "Africa/Johannesburg",             120,  120, {  } },
    { "Egypt",                "Africa/Cairo",                    120,  180, { "2026-04-23 22:00", "2026-10-29 21:00", "2027-04-29 22:00", "2027-10-28 21:00" } },
    { "Israel",               "Asia/Jerusalem",                  120,  180, { "2026-03-27 00:00", "2026-10-24 23:00", "2027-03-26 00:00", "2027-10-30 23:00" } },
    { "Lebanon",              "Asia/Beirut",                     120,  180, { "2026-03-28 22:00", "2026-10-24 21:00", "2027-03-27 22:00", "2027-10-30 21:00" } },
    { "Palestine",            "Asia/Gaza",                       120,  180, { "2026-03-28 00:00", "2026-10-23 23:00", "2027-03-27 00:00", "2027-10-29 23:00" } },
    { "Moscow, Istanbul",     "Europe/Moscow",                   180,  180, {  } },
    { "Afghanistan",          "Asia/Kabul",                      270,  270, {  } },
    { "Eucla, Australia",     "Australia/Eucla",                 525,  525, {  } },
    { "Northern Territory",   "Australia/Darwin",                570,  570, {  } },
    { "Queensland",           "Australia/Brisbane",              600,  600, {  } },
    { "Norfolk Island",       "Pacific/Norfolk",                 720,  660, { "2026-04-04 15:00", "2026-10-03 15:00", "2027-04-03 15:00", "2027-10-02 15:00" } },
    { "Fiji",                 "Pacific/Fiji",                    720,  720, {  } },
};

#define EXPECTED_COUNT (sizeof(expected)/sizeof(expected[0]))

// Seconds by which the table may change the clock after tzdb does.
static int tolerance(const char *location)
{
    // Nuuk springs forward at 01:00 UTC (tzdb "M3.5.0/-1"); newlib cannot
    // parse a negative time, so the table changes an hour later.
    return strcmp(location, "Greenland") == 0 ? 3600 : 0;
}

static const char *parse_name(const char *p)
{
    const char *s = p;
    while (isalpha((unsigned char)*p))
        p++;
    return (p - s >= 3 && p - s <= 10) ? p : NULL;
}

static const char *parse_number(const char *p, int max_digits)
{
    const char *s = p;
    while (isdigit((unsigned char)*p))
        p++;
    if (p == s || p - s > max_digits)
        return NULL;
    for (int i = 0; i < 2 && *p == ':'; i++) {
        if (!isdigit((unsigned char)p[1]) || !isdigit((unsigned char)p[2]))
            return NULL;
        p += 3;
    }
    return p;
}

static const char *parse_offset(const char *p)
{
    if (*p == '+' || *p == '-')
        p++;
    return parse_number(p, 2);
}

static const char *parse_rule(const char *p)
{
    int m, w, d, n = 0;
    if (sscanf(p, "M%d.%d.%d%n", &m, &w, &d, &n) != 3 || m < 1 || m > 12 || w < 1 || w > 5 || d < 0 || d > 6)
        return NULL;
    p += n;
    // newlib reads the time as unsigned, so a sign is not allowed here
    if (*p == '/')
        p = parse_number(p + 1, 3);
    return p;
}

// The subset of POSIX TZ that newlib's tzset parses: unquoted alphabetic
// names, an explicit standard offset, and both DST rules spelled out.
static bool newlib_parses(const char *tz)
{
    const char *p = parse_name(tz);
    if (p)
        p = parse_offset(p);
    if (!p)
        return false;
    if (!*p)
        return true;
    p = parse_name(p);
    if (!p)
        return false;
    if (*p != ',') {
        p = parse_offset(p);
        if (!p)
            return false;
    }
    for (int i = 0; i < 2; i++) {
        if (!p || *p != ',')
            return false;
        p = parse_rule(p + 1);
    }
    return p && !*p;
}

static time_t utc(const char *stamp)
{
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    sscanf(stamp, "%d-%d-%d %d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return timegm(&tm);
}

static int offset_at(time_t t)
{
    struct tm tm;
    localtime_r(&t, &tm);
    return (int)(tm.tm_gmtoff / 60);
}

static int failures = 0;

static void check(bool ok, int i, const char *what)
{
    if (!ok) {
        printf("FAIL zone %d (%s, %s): %s\n", i, zone_names[i], zones[i].posix, what);
        failures++;
    }
}

int main(void)
{
    unsigned count = ZONE_COUNT < EXPECTED_COUNT ? ZONE_COUNT : EXPECTED_COUNT;
    if (ZONE_COUNT != EXPECTED_COUNT || sizeof(zones)/sizeof(zones[0]) != ZONE_COUNT) {
        printf("FAIL %u zones, %u names, %u expected\n", (unsigned)(sizeof(zones)/sizeof(zones[0])),
               (unsigned)ZONE_COUNT, (unsigned)EXPECTED_COUNT);
        failures++;
    }
    for (unsigned i = 0; i < count; i++) {
        const expected_zone_t *e = &expected[i];
        char what[96];

        check(strcmp(zone_names[i], e->location) == 0, i, "name moved; the index is the stored setting");
        check(strcmp(zones[i].location, zone_names[i]) == 0, i, "zones[] and zone_names[] disagree");
        // .cfg files and REST select the zone by label, first match wins
        for (unsigned j = 0; j < i; j++)
            check(strcmp(zone_names[j], zone_names[i]) != 0, i, "label used twice");
        // the longest label in use, checked whole on the U64 settings menu
        check(strlen(zone_names[i]) <= 20, i, "label longer than 20 characters");
        check(newlib_parses(zones[i].posix), i, "not in the TZ subset newlib parses");

        setenv("TZ", zones[i].posix, 1);
        tzset();

        int now = e->offset;
        time_t start = utc("2026-01-01 00:00");
        sprintf(what, "offset %d on 2026-01-01, expected %d", offset_at(start), now);
        check(offset_at(start) == now, i, what);

        int n = 0;
        for (; n < 4 && e->changes[n]; n++) {
            time_t t = utc(e->changes[n]);
            int next = (now == e->offset) ? e->offset_alt : e->offset;
            int slack = tolerance(e->location);
            sprintf(what, "offset %d before %s, expected %d", offset_at(t - 1), e->changes[n], now);
            check(offset_at(t - 1) == now, i, what);
            sprintf(what, "offset %d after %s, expected %d", offset_at(t + slack), e->changes[n], next);
            check(offset_at(t + slack) == next, i, what);
            now = next;
        }
        if (n == 0) {
            const char *probes[] = { "2026-07-01 00:00", "2027-01-01 00:00", "2027-07-01 00:00" };
            for (int k = 0; k < 3; k++) {
                sprintf(what, "offset %d on %s, expected %d", offset_at(utc(probes[k])), probes[k], now);
                check(offset_at(utc(probes[k])) == now, i, what);
            }
        }
    }
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("All %u time zones match tzdb\n", (unsigned)ZONE_COUNT);
    return 0;
}
