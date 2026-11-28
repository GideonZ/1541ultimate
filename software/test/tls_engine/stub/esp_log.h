#ifndef STUB_ESP_LOG_H
#define STUB_ESP_LOG_H
#include <stdio.h>
extern int tlsguard_verbose;
extern char tlsguard_last_error[160]; // the last ESP_LOGE text, for tests that assert on which check refused
#define ESP_LOGE(tag, ...) do { snprintf(tlsguard_last_error, sizeof(tlsguard_last_error), __VA_ARGS__); if (tlsguard_verbose) { fprintf(stderr, "E %s: ", tag); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
#define ESP_LOGW(tag, ...) do { if (tlsguard_verbose) { fprintf(stderr, "W %s: ", tag); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
#endif
