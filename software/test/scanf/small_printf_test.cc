// The firmware's own sscanf, checked against the rules its callers are written to.
//
// sscanf is declared here rather than by including <stdio.h>, because on a Linux
// host that header redirects the call to the C library's __isoc99_sscanf. Including
// it would test glibc and say nothing about the code that runs on the device. That
// redirection is also why the other host suites cannot see defects in this file.
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

extern "C" int sscanf(const char *buf, const char *fmt, ...);
extern "C" int printf(const char *fmt, ...);

extern "C" void outbyte(int c)
{
    char b = (char)c;
    ssize_t written = write(1, &b, 1);
    (void)written;
}

static int failures = 0;

static void expect(const char *what, bool ok)
{
    if (ok) {
        printf("  %s => OK!\n", what);
        return;
    }
    printf("  %s => FAILED\n", what);
    failures++;
}

static void expect_int(const char *what, int got, int expected)
{
    if (got == expected) {
        printf("  %s => OK!\n", what);
        return;
    }
    printf("  %s => got %d, expected %d\n", what, got, expected);
    failures++;
}

static void test_counts_only_what_it_converted(void)
{
    int a = -1, b = -1, c = -1, d = -1, e = -1, f = -1;

    expect_int("empty input", sscanf("", "%d", &a), 0);

    a = -1;
    expect_int("no digits at all", sscanf("abc", "%d", &a), 0);
    expect_int("and it stored nothing", a, -1);

    a = b = c = d = e = f = -1;
    expect_int("six conversions, none possible",
               sscanf("junk", "%d,%d,%d,%d,%d,%d", &a, &b, &c, &d, &e, &f), 0);

    a = b = -1;
    expect_int("second number missing", sscanf("7,", "%d,%d", &a, &b), 1);
    expect_int("the first was stored", a, 7);
    expect_int("the second was not", b, -1);
}

static void test_literals_have_to_match(void)
{
    int a = -1, b = -1;

    expect_int("the comma matches", sscanf("1,2", "%d,%d", &a, &b), 2);
    expect_int("first value", a, 1);
    expect_int("second value", b, 2);

    a = b = -1;
    expect_int("a semicolon is not a comma", sscanf("1;2", "%d,%d", &a, &b), 1);
    expect_int("and the second value was not stored", b, -1);

    a = b = -1;
    expect_int("slashes match", sscanf("01/02", "%d/%d", &a, &b), 2);
    expect_int("day", b, 2);
}

static void test_whitespace(void)
{
    int a = -1, b = -1;

    expect_int("leading space before a number", sscanf(" 42", "%d", &a), 1);
    expect_int("value", a, 42);

    a = b = -1;
    expect_int("a space in the format matches several",
               sscanf("1    2", "%d %d", &a, &b), 2);
    expect_int("second value", b, 2);

    a = b = -1;
    expect_int("a space in the format matches none",
               sscanf("1 2", "%d%d", &a, &b), 2);
    expect_int("second value", b, 2);
}

static void test_numbers(void)
{
    int a = -1;

    expect_int("a negative number", sscanf("-7", "%d", &a), 1);
    expect_int("value", a, -7);

    a = -1;
    expect_int("a signed positive number", sscanf("+7", "%d", &a), 1);
    expect_int("value", a, 7);

    a = -1;
    expect_int("hexadecimal, lower case", sscanf("ff", "%x", &a), 1);
    expect_int("value", a, 255);

    a = -1;
    expect_int("hexadecimal, upper case", sscanf("FF", "%x", &a), 1);
    expect_int("value", a, 255);

    a = -1;
    expect_int("a letter is not a decimal digit", sscanf("f", "%d", &a), 0);
}

static void test_characters(void)
{
    // %c used to fall through to a branch that stored a whole int, which wrote three
    // bytes past a char and left the caller's marker at zero.
    struct { char marker; char guard[3]; } probe;
    int a = -1;

    memset(&probe, '#', sizeof(probe));
    expect_int("a character after a number", sscanf("1 P", "%d %c", &a, &probe.marker), 2);
    expect_int("the number", a, 1);
    expect("the character is the one in the input", probe.marker == 'P');
    expect("and nothing was written past it",
           (probe.guard[0] == '#') && (probe.guard[1] == '#') && (probe.guard[2] == '#'));

    memset(&probe, '#', sizeof(probe));
    expect_int("no character left to read", sscanf("1", "%d %c", &a, &probe.marker), 1);
    expect("so nothing was stored", probe.marker == '#');
}

static void test_the_forms_the_firmware_sends(void)
{
    int M = -1, d = -1, y = -1, h12 = -1, m = -1;
    char ampm = '#';
    int ip[4] = { -1, -1, -1, -1 }, hi = -1, lo = -1;
    int addr = -1, value = -1;

    // A directory listing filtered by a time stamp, from cbmdos_parser.cc.
    expect_int("a time stamp filter",
               sscanf("01/02/25 03:04 PM", "%d/%d/%d %d:%d %c", &M, &d, &y, &h12, &m, &ampm), 6);
    expect("the afternoon marker survives", ampm == 'P');
    expect_int("the minutes", m, 4);

    ampm = '#';
    expect_int("a time stamp without its marker",
               sscanf("01/02/25 03:04", "%d/%d/%d %d:%d %c", &M, &d, &y, &h12, &m, &ampm), 5);

    // The FTP server's PORT argument, from ftpd.cc.
    expect_int("a well formed PORT",
               sscanf("192,168,1,10,4,1", "%d,%d,%d,%d,%d,%d",
                      &ip[0], &ip[1], &ip[2], &ip[3], &hi, &lo), 6);
    expect_int("the last octet", ip[3], 10);
    expect_int("a truncated PORT",
               sscanf("192,168,1", "%d,%d,%d,%d,%d,%d",
                      &ip[0], &ip[1], &ip[2], &ip[3], &hi, &lo), 3);

    // The U64 poke dialog, from u64_config.cc.
    expect_int("a poke", sscanf("d020,01", "%x,%x", &addr, &value), 2);
    expect_int("the address", addr, 0xd020);
    expect_int("the value", value, 1);
    addr = value = -1;
    expect_int("a poke with no value", sscanf("d020", "%x,%x", &addr, &value), 1);
}

int main(int argc, const char *argv[])
{
    printf("Counting only what was converted\n");
    test_counts_only_what_it_converted();
    printf("Literal characters in the format\n");
    test_literals_have_to_match();
    printf("Whitespace\n");
    test_whitespace();
    printf("Numbers\n");
    test_numbers();
    printf("Characters\n");
    test_characters();
    printf("The forms this firmware sends\n");
    test_the_forms_the_firmware_sends();

    if (failures) {
        printf("\n%d check(s) failed.\n", failures);
        return 1;
    }
    printf("\nAll checks passed.\n");
    return 0;
}
