/* Minimal esp_log.h for host builds: the log lines are part of what the
   firmware prints, so keep them, but send them to stdout. */
#ifndef TEST_STUB_ESP_LOG_H
#define TEST_STUB_ESP_LOG_H

#include <stdio.h>

#define ESP_LOGI(tag, fmt, ...) printf("I (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("W (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("E (%s) " fmt "\n", tag, ##__VA_ARGS__)

#endif
