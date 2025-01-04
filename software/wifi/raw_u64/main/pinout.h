#ifndef PINOUT_H
#define PINOUT_H

#define GPIO_LED0 4
#define GPIO_LED1 16
#define GPIO_LED2 19
#define GPIO_WIFI_MISO 34 // Input Only
#define GPIO_WIFI_MOSI 35 // Input Only
#define GPIO_WIFI_CLK  32 // I/O, can be set to output
#define GPIO_WIFI_CSn  33 // I/O, but shared with IO0, should be input on ESP32

#define PIN_DEVKIT_LED 2

#define IO_UART_RXD 3
#define IO_UART_TXD 1
#define IO_UART_RTS GPIO_WIFI_CLK
#define IO_UART_CTS GPIO_WIFI_MOSI
#define IO_ESP_LED GPIO_LED0

#endif
