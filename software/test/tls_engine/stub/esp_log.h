#ifndef STUB_ESP_LOG_H
#define STUB_ESP_LOG_H
#include <stdio.h>
extern int tlsguard_verbose;
#define ESP_LOGE(tag, ...) do { if (tlsguard_verbose) { fprintf(stderr, "E %s: ", tag); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
#define ESP_LOGW(tag, ...) do { if (tlsguard_verbose) { fprintf(stderr, "W %s: ", tag); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
#endif
