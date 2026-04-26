#ifndef SCREEN_LOGGER_H
#define SCREEN_LOGGER_H

void pass_message(const char *msg);
void fail_message(const char *msg);
void warn_message(const char *test, const char *fmt, ...);
void info_message(const char *fmt, ...);
void result_message(int errors, const char *msg);

#endif
