#include "screen.h"
#include "small_printf.h"
#include <stdarg.h>

extern Screen *screen;
extern Screen *screen2;

void pass_message(const char *msg)
{
    console_print(screen, "\e5%50s Passed\n\e\037", msg);
    printf("PASS: %s\n", msg);
}

void fail_message(const char *msg)
{
    console_print(screen, "\e2%50s FAILED\n\e\037", msg);
    printf("FAIL: %s\n", msg);
}

void _diag_write_char(char c, void **param);

void warn_message(const char *test, const char *fmt, ...)
{
    va_list ap;
    console_print(screen, "\e7%s: ", test);
    va_start(ap, fmt);
    _my_vprintf(Screen :: _put, (void **)screen, fmt, ap);
    va_end(ap);
    screen->output('\n');
    screen->set_color(15);

    printf("WARNING (%s): ", test);
    va_start(ap, fmt);
    _my_vprintf(_diag_write_char, NULL, fmt, ap);
    va_end(ap);
    printf("\n");
}

void info_message(const char *fmt, ...)
{
    va_list ap;
    screen->set_color(4);
    va_start(ap, fmt);
    _my_vprintf(Screen :: _put, (void **)screen, fmt, ap);
    va_end(ap);
    screen->set_color(15);

    printf("INFO: ");
    va_start(ap, fmt);
    _my_vprintf(_diag_write_char, NULL, fmt, ap);
    va_end(ap);
}

void result_message(int errors, const char *msg)
{
    if (errors) {
        fail_message(msg);
    } else {
        pass_message(msg);
    }
}