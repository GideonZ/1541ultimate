/*
 * u64_tester.c
 *
 *  Created on: Sep 8, 2018
 *      Author: gideon
 */

#include "u64.h"
#include "u64ii_tester.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dump_hex.h"
#include "i2c_drv.h"
#include "i2c_drv_sockettest.h"
#include "nau8822.h"
#include "esp32.h"
#include "pattern.h"
#include "wifi.h"
#include "rpc_calls.h"
#include "screen_logger.h"
#include "usb_base.h"
#include "usb_device.h"
#include "wifi_cmd.h"
#include "network_test.h"
#include "flash.h"
#include "screen.h"
#include "screen_logger.h"

// dirty to define it twice, but alas
#define VOLTAGES         ((volatile voltages_t *)(0x00A0))
#define SERIAL_NUMBER    ((volatile char *)(0x00B0))

extern "C" {
    #include "audio_dma.h"
    #include "audio_test.h"
}

typedef struct _t_genericvector {
    char *pinName;
    volatile uint8_t *outreg;
    uint8_t outval;
    volatile uint8_t *inreg;
    uint8_t mask;
    uint8_t before;
    uint8_t expect;
    const char **pinnames;
} t_generic_vector;

static void PrintPinNames(const char **names, uint8_t bits)
{
    for(int i=0; i<8; i++) {
        if (bits & 1) {
            printf(names[i]);
            bits >>= 1;
            if (bits) {
                printf(", ");
            } else {
                break;
            }
        } else {
            bits >>= 1;
        }
    }
}

static int PerformTest(const t_generic_vector *vec)
{
    int errors = 0;
    for(int i=0; ; i++) {
        uint8_t before = 0;
        uint8_t after = 0;
        if (!vec[i].pinName) {
            break;
        }
        // printf("%-10s CIA current: %b %b %b\n", vec[i].pinName, LOCAL_CIA1, LOCAL_CIA2, LOCAL_CIA3);
        if (vec[i].inreg) {
            before = *(vec[i].inreg);
        }
        if (vec[i].outreg) {
            *(vec[i].outreg) = vec[i].outval;
            vTaskDelay(2);
        }
        if (vec[i].inreg) {
            after = *(vec[i].inreg);
            uint8_t bdiff = (before & vec[i].mask) ^ vec[i].before;
            uint8_t adiff = (after & vec[i].mask) ^ vec[i].expect;
            if (bdiff) {
                printf("Before %12s Read: %b&%b, exp: %b (diff: %b [", vec[i].pinName, before, vec[i].mask, vec[i].before, bdiff);
                if (vec[i].pinnames) {
                    PrintPinNames(vec[i].pinnames, bdiff);
                }
                printf("])\n");
                errors ++;
            }
            if (adiff) {
                printf(" After %12s Read: %b&%b, exp: %b (diff: %b [", vec[i].pinName, after, vec[i].mask, vec[i].expect, adiff);
                if (vec[i].pinnames) {
                    PrintPinNames(vec[i].pinnames, adiff);
                }
                printf("])\n");
                errors ++;
            }
        }
    }
    return errors;
}
const char *cia1_pb_pins[] = { "PB0", "PB1", "PB2", "PB3", "PB4", "PB5", "PB6", "PB7" };
const char *cia1_pa_pins[] = { "PA0", "PA1", "PA2", "PA3", "PA4", "PA5", "PA6", "PA7" };
const char *cia2_pb_pins[] = { "PB0", "PB1", "PB2", "PB3", "PB4", "PB5", "PB6 (CIA2)", "PB7 (CIA2)" };
// const char *cia2_pa_pins[] = { "PA0", "PA1", "PA2", "PA3", "PA4", "PA5", "PA6", "PA7" };

static const char *separator = "----------------------------------------";
#define TEST_START(x) const char *testname = x; printf("%s\nTest: %s\n%s\n", separator, testname, separator);
#define TEST_RESULT(x) result_message(x, testname); return x;

int U64TestKeyboard()
{
    TEST_START("Keyboard test");
    int res, errors = 0;
    i2c->i2c_lock("keyboard");
    i2c->set_channel(1);

    // Column is A  (drive), Row is B  (read)
    // Port 0,               Port 1
    res = i2c->i2c_write_byte(0x42, 0x06, 0x00); // All pins output on port 0
    if (res < 0) { errors++; printf("Write byte failed to keyboard I2C chip\n"); }
    res = i2c->i2c_write_byte(0x42, 0x07, 0xFF); // All pins input on port 1
    if (res < 0) { errors++; printf("Write byte failed to keyboard I2C chip\n"); }


    uint8_t rd[8] = { 0 };
    if (!errors) {
        uint8_t wr = 0x01;
        for(int i=0; i<8; i++) {
            i2c->i2c_write_byte(0x42, 0x02, wr ^ 0xFF);
            rd[i] = i2c->i2c_read_byte(0x42, 0x01, &res);
            wr <<= 1;
        }
        printf("Keyboard Read: %b %b %b %b %b %b %b %b\n", rd[0], rd[1], rd[2], rd[3], rd[4], rd[5], rd[6], rd[7]);
    }
    i2c->i2c_unlock();

    const uint8_t correct[] = { 0xF7, 0xFD, 0xFB, 0x7F, 0xEF, 0xDF, 0xBF, 0xFE };
    for (int i=0; i<8; i++) {
        if (rd[i] != correct[i]) {
            errors++;
        }
    }
    TEST_RESULT(errors);
}

int U64TestUserPort()
{
    TEST_START("Userport test");

    // Make sure the buffer is enabled
    U64_USERPORT_EN = 1;

    const t_generic_vector vec[] = {
            // Test the connections of the loopback cable: 10 connections
            // Initially, the pins read: 0x3F, 0x84, 0xBE, which corresponds to:
            // 0x3F: 00111111: CIA2(7..6), CIA1_CNT, CIA1_SP, CIA2_CNT, CIA2_SP all high
            // 0x84: 10000100: CIA_2_PA and CIA2_FLAGn high, CIA_PWM = "00"
            // 0xBE: 10111110: RSTn=1, CIA2_PB(5..0)="111110"; PB0=low, others are 1.
            // Why is PB0 low? Because it is connected to PWM1, which has a pull-down

            { "Init Out",    &LOCAL_CIA1, 0x00, NULL, 0, 0, 0, NULL },
            { "Init Out",    &LOCAL_CIA2, 0x00, NULL, 0, 0, 0, NULL },
            { "Init Out",    &LOCAL_CIA3, 0x00, NULL, 0, 0, 0, NULL },
            { "Init Out",    &LOCAL_IEC,  0x00, NULL, 0, 0, 0, NULL },
            { "PB0->PWM1",   &LOCAL_CIA3, 0x81, &LOCAL_CIA2, 0x02, 0x02, 0x00, NULL },
            { "PB1->ATN",    &LOCAL_CIA3, 0x82, &LOCAL_IEC,  0x01, 0x00, 0x01, NULL },
            { "Init Out",    &LOCAL_CIA3, 0x00, NULL, 0, 0, 0, NULL }, // tristate 
            { "/PC2->PB2",   &LOCAL_CIA1, 0x08, &LOCAL_CIA3, 0x04, 0x04, 0x00, NULL },
            { "PB3->SP2",    &LOCAL_CIA3, 0x88, &LOCAL_CIA1, 0x01, 0x01, 0x00, NULL },
            { "PB4->CNT2",   &LOCAL_CIA3, 0x90, &LOCAL_CIA1, 0x02, 0x02, 0x00, NULL },
            { "PB5->SP1",    &LOCAL_CIA3, 0xA0, &LOCAL_CIA1, 0x04, 0x04, 0x00, NULL },
            { "PB6->CNT1",   &LOCAL_CIA1, 0x10, &LOCAL_CIA1, 0x08, 0x08, 0x00, NULL },
            { "PB7->/RESET", &LOCAL_CIA1, 0x20, &LOCAL_CIA3, 0x80, 0x80, 0x00, NULL },
            { "PA2->/FLAG2", &LOCAL_CIA3, 0x40, &LOCAL_CIA2, 0x80, 0x80, 0x00, NULL },
            { "PWM0->PWM1",  &LOCAL_CIA2, 0x04, &LOCAL_CIA2, 0x02, 0x02, 0x00, NULL },
            { "Init Out",    &LOCAL_CIA1, 0x00, NULL, 0, 0, 0, NULL },
            { "Init Out",    &LOCAL_CIA2, 0x00, NULL, 0, 0, 0, NULL },
            { "Init Out",    &LOCAL_CIA3, 0x00, NULL, 0, 0, 0, NULL },
            { "Init Out",    &LOCAL_IEC,  0x00, NULL, 0, 0, 0, NULL },
            { NULL, NULL, 0, NULL, 0, 0, 0, NULL },
    };

    int errors = PerformTest(vec);
    TEST_RESULT(errors);
}

int U64TestCartridge()
{
    TEST_START("Cartridge test");

    if (REMOTE_SIGNATURE != 0x99) {
        fail_message("Cartridge Test: Remote tester not found");
        dump_hex((uint8_t *)U64TESTER_IO1_BASE, 8);
//        return 1;
    }
    int errors = 0;

    volatile uint8_t *dest = (volatile uint8_t *)U64TESTER_IO2_BASE;
    // let's fill the RAM. We only fill 10 locations, but such that all address/data lines 0..7 are checked
    const uint8_t addr[] = { 0x00, 0x55, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
    const uint8_t data[] = { 0x55, 0xAA, 0x80, 0x20, 0x08, 0x02, 0x40, 0x04, 0x10, 0x01 };

    for (int i=0; i<10; i++) {
        dest[addr[i]] = data[i];
    }
    // and check it again, remember, we need an extra clock to read memory
    uint8_t dummy, readback;
    for (int i=0; i<10; i++) {
        dummy = dest[addr[i]];
        readback = dest[addr[i]];
        if (readback != data[i]) {
            errors ++;
            printf("%b/%b ", readback, data[i]);
        }
    }
    if (errors) {
        printf("FAIL\n");
        dump_hex_relative((void *)dest, 144);
    }

    // Now check the remaining address lines 8 .. 15
    volatile uint8_t *remoteRegs = (volatile uint8_t *)U64TESTER_IO1_BASE;

    for(int i=8; i<16; i++) {
        readback = remoteRegs[(1 << i) + 2];
        if (readback != (1 << (i-8))) {
            errors ++;
            printf("ADDR %d:%b ", i, readback);
        }
    }
    if (errors) {
        printf("FAIL\n");
    }

    // Now check each of the other cartridge I/O lines (PHI2 was already used, and R/Wn and IO1 and IO2 as well)
    // SO there are 38-16-8-4 = 10 left?  DMA, BA, ROML, ROMH, GAME, EXROM, DOTCLK, IRQ, NMI, RESET
    const char *remote_cart_pins[] = { "IRQ", "NMI", "ROML", "ROMH", "DOTCK", "RST#", "BA", "??" };
    const char *local_cart_pins[] = { "IRQ", "NMI", "EXROM", "GAME", "DMA", "??", "??", "??" };

    const t_generic_vector vec[] = {
            { "Init",  &LOCAL_CART, 0x00, NULL, 0, 0, 0, NULL },
            { "Init",  &REMOTE_CART_OUT, 0x00, NULL, 0, 0, 0, NULL },
            { "IRQ#",  &LOCAL_CART, 0x01, &REMOTE_CART_IN, 0x7F, 0x23, 0x22, remote_cart_pins },
            { "NMI#",  &LOCAL_CART, 0x02, &REMOTE_CART_IN, 0x7F, 0x22, 0x21, remote_cart_pins },
            { "ROML#", &LOCAL_CART, 0x04, &REMOTE_CART_IN, 0x7F, 0x21, 0x27, remote_cart_pins },
            { "ROMH#", &LOCAL_CART, 0x08, &REMOTE_CART_IN, 0x7F, 0x27, 0x2B, remote_cart_pins },
            { "DOTCK", &LOCAL_CART, 0x10, &REMOTE_CART_IN, 0x7F, 0x2B, 0x33, remote_cart_pins },
            { "RST#",  &LOCAL_CART, 0x20, &REMOTE_CART_IN, 0x7F, 0x33, 0x03, remote_cart_pins },
            { "BA",    &LOCAL_CART, 0x40, &REMOTE_CART_IN, 0x7F, 0x03, 0x63, remote_cart_pins },
            { "End",   &LOCAL_CART, 0x00, NULL, 0, 0, 0, NULL },
            { "DMA#",  &REMOTE_CART_OUT, 0x10, &LOCAL_CART, 0xFF, 0x3F, 0x2F, local_cart_pins },
            { "GAME#", &REMOTE_CART_OUT, 0x04, &LOCAL_CART, 0xFF, 0x2F, 0x3B, local_cart_pins },
            { "EXROM#",&REMOTE_CART_OUT, 0x08, &LOCAL_CART, 0xFF, 0x3B, 0x37, local_cart_pins },
            { "NMI#r", &REMOTE_CART_OUT, 0x02, &LOCAL_CART, 0xFF, 0x37, 0x3D, local_cart_pins },
            { "IRQ#r", &REMOTE_CART_OUT, 0x01, &LOCAL_CART, 0xFF, 0x3D, 0x3E, local_cart_pins },
            { NULL, NULL, 0, NULL, 0, 0, 0, NULL },
    };

    errors += PerformTest(vec);
    TEST_RESULT(errors);
}

int U64TestIEC()
{
    TEST_START("Local IEC test");

    const char *iec_pins[] = { "ATN", "CLK", "DATA", "SRQ", "RESET", "??", "??", "??" };
    const t_generic_vector vec[] = {
            { "Init",     &LOCAL_IEC,  0x00, NULL, 0, 0, 0, NULL },
            { "Init",     &REMOTE_IEC, 0x00, NULL, 0, 0, 0, NULL },
            { "IEC ATN",  &LOCAL_IEC,  0x01, &LOCAL_IEC,  0xFF, 0x00, 0x01, iec_pins },
            { "IEC CLK",  &LOCAL_IEC,  0x02, &LOCAL_IEC,  0xFF, 0x01, 0x02, iec_pins },
            { "IEC DAT",  &LOCAL_IEC,  0x04, &LOCAL_IEC,  0xFF, 0x02, 0x04, iec_pins },
            { "IEC SRQ",  &LOCAL_IEC,  0x08, &LOCAL_IEC,  0xFF, 0x04, 0x08, iec_pins },
            { "IEC RST",  &LOCAL_IEC,  0x10, &LOCAL_IEC,  0xFF, 0x08, 0x10, iec_pins },
            { NULL, NULL, 0, NULL, 0, 0, 0 },
    };
    int errors = PerformTest(vec);
    TEST_RESULT(errors);
}

int U64TestCassette()
{
    TEST_START("Cassette test");

    if (REMOTE_SIGNATURE != 0x99) {
        fail_message("Cassette test: Remote tester not found");
        return 1;
    }
    // Make sure the userport buffer is turned on
    U64_USERPORT_EN = 1;

    const char *cas_pins[] = { "MOTOR", "READ", "WRITE", "SENSE", "?", "?", "?", "?" };
    const t_generic_vector vec[] = {
            { "Init",      &LOCAL_CAS,  0x00, NULL, 0, 0, 0, NULL },
            { "Cas Motor", &LOCAL_CAS,  0x01, &REMOTE_CAS, 0xFF, 0x00, 0x01, cas_pins },
            { "Cas Read",  &LOCAL_CAS,  0x02, &REMOTE_CAS, 0xFF, 0x01, 0x02, cas_pins },
            { "Cas Write", &LOCAL_CAS,  0x04, &REMOTE_CAS, 0xFF, 0x02, 0x04, cas_pins },
            { "Cas Sense", &LOCAL_CAS,  0x08, &REMOTE_CAS, 0xFF, 0x04, 0x08, cas_pins },
            { NULL, NULL, 0, NULL, 0, 0, 0 },
    };
    int errors = PerformTest(vec);
    TEST_RESULT(errors);
}

int U64TestJoystick()
{
    TEST_START("Joystick test");
    // with this test, the fire button is used as output to clock the counter.
    // then the counter reaches 15 for the second time, bit 0 to bit 3 have been tested.
    // In order to make sure that the keyboard test does not see the counter, the isolate
    // mode is turned on here.

    LOCAL_JOY1 = 0x0F;
    LOCAL_JOY2 = 0x0F;

    int ok1 = -1;
    int ok2 = -1;
    int errors = 0;
    const char correct[20] = "@ABCDEFGHIJKLMNO";
    uint8_t buffer[32];
    memset(buffer, 0xAA, 32);

    for (int i=0; i<32; i++) {
        LOCAL_JOY1 = 0x0F;
        vTaskDelay(1);
        LOCAL_JOY1 = 0x1F;
        vTaskDelay(1);
        buffer[i] = (LOCAL_JOY1 & 0x0F) | 0x40;
        if ((buffer[i] == 0x4F) && (i >= 16)) {
            ok1 = memcmp(&buffer[i-15], correct, 16);
            break;
        }
    }
    if (ok1) {
        printf("Joystick Port A:\n");
        dump_hex_relative(buffer, 32);
        errors ++;
    }

    memset(buffer, 0xAA, 32);
    for (int i=0; i<32; i++) {
        LOCAL_JOY2 = 0x0F;
        vTaskDelay(1);
        LOCAL_JOY2 = 0x1F;
        vTaskDelay(1);
        buffer[i] = (LOCAL_JOY2 & 0x0F) | 0x40;
        if ((buffer[i] == 0x4F) && (i >= 16)) {
            ok2 = memcmp(&buffer[i-15], correct, 16);
            break;
        }
    }
    if (ok2) {
        printf("Joystick Port B:\n");
        dump_hex_relative(buffer, 32);
        errors ++;
    }
    TEST_RESULT(errors);
}

int U64TestPaddle(void)
{
    TEST_START("Paddle test");

    LOCAL_PADDLESEL = 1;
    vTaskDelay(2);
    uint8_t x1 = LOCAL_PADDLE_X;
    uint8_t y1 = LOCAL_PADDLE_Y;

    LOCAL_PADDLESEL = 2;
    vTaskDelay(2);
    uint8_t x2 = LOCAL_PADDLE_X;
    uint8_t y2 = LOCAL_PADDLE_Y;

    // Expected values:
    // X1: 52, Y1: 6, X2: 106, Y2: 31
    // 10% tolerance:
    // X1: 5, Y1: 2, X2: 10, Y2: 3

    int errors = 0;
    if ((x1 < 2) || (x1 > 6)) {
        printf("Paddle X on Port 1 FAIL. Expected ~3, got %d\n", x1);
        errors++;
    }
    if ((y1 < 7) || (y1 > 15)) {
        printf("Paddle Y on Port 1 FAIL. Expected ~10, got: %d\n", y1);
        errors++;
    }
    if ((x2 < 40) || (x2 > 54)) {
        printf("Paddle X on Port 2 FAIL. Expected ~47, got: %d\n", x2);
        errors++;
    }
    if ((y2 < 17) || (y2 > 28)) {
        printf("Paddle Y on Port 2 FAIL. Expected ~22, got: %d\n", y2);
        errors++;
    }
    printf("Paddle test %d, %d, %d, %d\n", x1, y1, x2, y2);
    TEST_RESULT(errors);
}

static void config_socket(int socketnr, uint8_t ctrlbyte)
{
    uint8_t mask = 0x0F << (4 * socketnr);
    static uint8_t lastctrl = 0;
    ctrlbyte &= 0x0F;
    lastctrl &= ~mask;
    lastctrl |= ctrlbyte << (4 * socketnr);

    i2c->i2c_lock("sid socket");
    i2c->set_channel(1);
    i2c->i2c_write_byte(0x40, 0x01, lastctrl);
    i2c->i2c_write_byte(0x40, 0x03, 0x00); // all pins output
    i2c->i2c_unlock();
}

static void read_socket_analog(I2C_Driver_SocketTest& skti2c, int& vdd, int& vcc, int& mid)
{
    uint8_t buf[4];
    skti2c.read_results(0xC8, buf);
    vdd = (301 * (int)buf[0]) >> 2;
    vcc = 32 * (int)buf[1];
    mid = 32 * (int)buf[2];
}

static void read_caps(volatile socket_tester_t *test, int &cap1, int &cap2)
{
    test->capsel = 0;
    vTaskDelay(2);
    cap1 = (1666 * (int)test->capval) / 11 - 616;
    test->capsel = 1;
    vTaskDelay(2);
    cap2 = (1666 * (int)test->capval) / 11 - 616;
}

static int test_socket_voltages(I2C_Driver_SocketTest& skti2c, int socket_nr, uint8_t ctrlbyte, int low, int high, bool shunt)
{
    config_socket(socket_nr, ctrlbyte);

    vTaskDelay(40);
    int vdd, vcc, mid, error = 0;
    read_socket_analog(skti2c, vdd, vcc, mid);

    // 4% range for VCC: 0.96*5000 < vcc < 1.04*5000
    if ((vcc < 4800) || (vcc > 5200)) {
        printf("VCC Out Of Range: %d mV  (should be 5V)\n", vcc);
        error |= (1 << 0);
    }

    if ((vdd < low) || (vdd > high)) {
        printf("VDD Out Of Range: %d mV  (should be between %d and %d mV)\n", vdd, low, high);
        error |= (1 << 1);
    }

    if (shunt) {
        vdd *= 68;
        vdd /= 100;
    }
    if (abs(mid * 2 - vdd) > 250) {
        printf("Mid level Out Of Range: %d (Expected %d mV)\n", mid, vdd / 2);
        error |= (1 << 2);
    }
    return error;
}

static int test_socket_caps(volatile socket_tester_t *test, int socket_nr, uint8_t ctrlbyte, int low, int high, int expected)
{
    int cap1, cap2;
    int error = 0;
    config_socket(socket_nr, ctrlbyte);
    vTaskDelay(10);
    // should be in 22 nF mode now. 5-7% caps
    read_caps(test, cap1, cap2);
    if ((cap1 < low) || (cap1 > high)) {
        printf("CAP1 out of range: %d pF, expected (%d-%d-%d) pF\n", cap1, low, expected, high);
        error |= (1 << 0);
    }
    if ((cap2 < low) || (cap2 > high)) {
        printf("CAP2 out of range: %d pF, expected (%d-%d-%d) pF\n", cap2, low, expected, high);
        error |= (1 << 1);
    }
    return error;
}

static int socket_test(volatile socket_tester_t *test, int socket_nr)
{
    int error = 0;

    if (test->id != 0x34) {
        printf("Socket Tester not found (0x%b != 0x34)\n", test->id);
        fail_message("SID Socket Tester not found");
        return -1;
    }

    // bit 3: Capsel (1 = 470 pF)
    // bit 2: 1K shunt
    // bit 1: Regulator enable
    // bit 0: regulator select (1 = 12V, 0 = 9V)
    I2C_Driver_SocketTest skti2c(test);

    // Setup byte: 1.101.0000 (unipolar, internal clock, reference always on)
    // Configuration byte: 0.00.0011.1
    if (skti2c.i2c_write_byte(0xC8, 0xD0, 0x07) < 0) {
        printf("\e2Unable to access I2C ADC on tester\n");
        return -2;
    }
    error |= test_socket_voltages(skti2c, socket_nr, 0,  4500,  5200, false);
    error |= test_socket_voltages(skti2c, socket_nr, 2,  8640,  9560, false) << 3;
    error |= test_socket_voltages(skti2c, socket_nr, 3, 11600, 13000, false) << 6;
    error |= test_socket_voltages(skti2c, socket_nr, 7, 11600, 13000, true) << 9;
    error |= test_socket_caps(test, socket_nr, 7 , 19000, 30000, 22470) << 12;
    error |= test_socket_caps(test, socket_nr, 15, 200, 1200, 470) << 14;

    if (!error) {
        return 0;
    }
    return error;

    // One LSB = 4.096 / 256 = 16 mV
    // Input 0: VDD Sense, 2.7k / 10k, thus 0,212598425 times input voltage. Thus one LSB = 75,259259329 mV  (Sample read: 7A => 122 * 75.26 = 9.181V)
    // Input 1: VCC Sense, 10k / 10k, thus 0.5 times input voltage. Thus one LSB = 32 mV (Sample read: 9E => 158 * 32 = 5.056V)
    // Input 2: AOUTDC: 100k / 100k, thus 0.5 times input voltage. Thus one LSB = 32 mV (Sample read: 8E => 142 * 32 = 4.544V)
    // Input 3: unused.
}

int U64TestSidSockets()
{
    LOCAL_CART = 0x20; // Reset PLD in socket!
    vTaskDelay(10);
    LOCAL_CART = 0x00; // release reset for PLD. This should enable the oscillator

    int error1 = 0;
    int error2 = 0;

    {
        TEST_START("SID Socket 1 test");
        error1 = socket_test(SOCKET1, 0);
        result_message(error1, testname);
    }
    {
        TEST_START("SID Socket 2 test");
        error2 = socket_test(SOCKET2, 1);
        result_message(error2, testname);
    }

    return error1 | error2;
}

void nau8822_set_input_level(uint16_t val);

int U64TestAudioCodecSilence(void)
{
    TEST_START("Audio codec silence test");
    audio_dma_t *audio_dma = (audio_dma_t *)U64TESTER_AUDIO_BASE;
    audio_dma->out_ctrl = 0x00; // disable output

    int *samples = new int[AUDIO_SAMPLE_COUNT*2];
    memset(samples, 0, AUDIO_SAMPLE_COUNT*2*4);
    record_audio(audio_dma, samples, AUDIO_SAMPLE_COUNT*2, e_32bit_stereo);
    vTaskDelay(5);

    // differentiate
    for (int i=2; i<(AUDIO_SAMPLE_COUNT*2); i+=2) {
        samples[i-2] = (samples[i]   >> 8) - (samples[i-2] >> 8);
        samples[i-1] = (samples[i+1] >> 8) - (samples[i-1] >> 8);
    }
    samples[AUDIO_SAMPLE_COUNT*2-2] = 0;
    samples[AUDIO_SAMPLE_COUNT*2-1] = 0;

    // integrate (DC is now gone)
    for (int i=0, left=0, right=0; i<(AUDIO_SAMPLE_COUNT*2); i+=2) {
        left += samples[i+0];
        right += samples[i+1];
        samples[i+0] = left;
        samples[i+1] = right;
    }

    int maxL = 0, maxR = 0;
    for (int i=0; i<(AUDIO_SAMPLE_COUNT*2); i+=2) {
        // printf("%9d %9d %08x %08x\n", samples[i], samples[i+1], samples[i], samples[i+1]);
        if(abs(samples[i]) > maxL) {
            maxL = abs(samples[i]);
        }
        if(abs(samples[i+1]) > maxR) {
            maxR = abs(samples[i+1]);
        }
    }
    int errors = 0;
    if (maxL > 10000) errors ++;
    if (maxR > 10000) errors ++;
    if (errors) {
        printf("Audio codec silence level too high: %d %d\n", maxL, maxR);
    }
    delete samples;
    TEST_RESULT(errors);
}

int U64TestAudioCodecPurity(void)
{
    TEST_START("Audio codec loopback test");

    nau8822_enable_hpout(1, 1);
    int errors = TestAudio(11, 7, 0);
    nau8822_enable_hpout(1, 0);

    TEST_RESULT(errors);
}

int U64TestSpeaker(void)
{
    extern int _speaker_sample[];
    extern uint32_t _speaker_sample_size;
    audio_dma_t *audio_dma = (audio_dma_t *)U64TESTER_AUDIO_BASE;
    play_audio_speaker(audio_dma, _speaker_sample, _speaker_sample_size, 0, e_16bit_mono);
    return 0;
}

int U64TestWiFiComm(void)
{
    TEST_START("Wifi Module test");

    static char *detectBuffer[512];
    int n = esp32.DetectModule(detectBuffer, 511);
    if (n <= 0) {
        fail_message("WiFi module not detected");
        return 1;
    }
    detectBuffer[n] = 0;
    //printf("\e\025WiFi module detected: %s\n", detectBuffer);
    bool found = pattern_match("*esp32*", (const char *)detectBuffer, true);
    if (!found) {
        fail_message("WiFi module not detected (esp32 string not in reply)");
        return 1;
    }
    found = pattern_match("*DOWNLOAD*", (const char *)detectBuffer);
    if (!found) {
        fail_message("WiFi module not in boot mode (DOWNLOAD string not in reply)");
        return 1;
    }
    esp32.EnableRunMode(); // cannot catch reply
    vTaskDelay(100); // Half a second delay
    wifi_command_init();
    char versionString[32];
    uint16_t major=0, minor=0;
    BaseType_t f = wifi_detect(&major, &minor, versionString, 32);
    if (((major | minor) == 0) || (f != pdTRUE)) {
        fail_message("WiFi module not responding to identify command");
        return 1;
    }
    printf("%s passed. %s\n", testname, versionString);

    TEST_RESULT(0);
    return 0;
}

int U64TestOff(void)
{
    if (!esp32.isRunning()) {
        esp32.EnableRunMode(); // cannot catch reply
        vTaskDelay(100); // Half a second delay
        wifi_command_init();
    }
    wifi_machine_off();
    return 0;
}

int U64TestReboot(void)
{
    if (!esp32.isRunning()) {
        esp32.EnableRunMode(); // cannot catch reply
        vTaskDelay(100); // Half a second delay
        wifi_command_init();
    }
    printf("Sending reboot command\n");
    wifi_machine_reboot();
    return 0;
}

// Precondition: WiFi module has been initialized
int U64TestVoltages(void)
{
    TEST_START("Board Voltages test");
    voltages_t voltages;

    if (!esp32.isRunning()) {
        esp32.EnableRunMode(); // cannot catch reply
        vTaskDelay(100); // Half a second delay
        wifi_command_init();
    }

    int res = wifi_get_voltages(&voltages);
    if (res != 0) {
        printf("WiFi module not responding to voltage command\n");
        fail_message(testname);
        return 1;
    }
    printf("Voltages: VBUS: %d mV, VAUX: %d mV, V50: %d mV, V33: %d mV, V18: %d mV, V10: %d mV, VUSB: %d mV\n",
            voltages.vbus, voltages.vaux, voltages.v50, voltages.v33, voltages.v18, voltages.v10, voltages.vusb);
    *VOLTAGES = voltages; // memcpy

    int errors = 0;
    if ((voltages.vbus < 8500) || (voltages.vbus > 15500)) {
        warn_message(testname, "VBUS out of range: %d mV", voltages.vbus);
        errors++;
    }
    if ((voltages.vaux < 3150) || (voltages.vaux > 3500)) {
        warn_message(testname, "VAUX out of range: %d mV", voltages.vaux);
        errors++;
    }
    if ((voltages.v50 < 4700) || (voltages.v50 > 5200)) {
        warn_message(testname, "+5.0V out of range: %d mV", voltages.v50);
        errors++;
    }
    if ((voltages.v33 < 3150) || (voltages.v33 > 3450)) {
        warn_message(testname, "+3.3V out of range: %d mV", voltages.v33);
        errors++;
    }
    if ((voltages.v18 < 1710) || (voltages.v18 > 1890)) {
        warn_message(testname, "+1.8V out of range: %d mV", voltages.v18);
        errors++;
    }
    if ((voltages.v10 < 950) || (voltages.v10 > 1050)) {
        warn_message(testname, "+1.0V out of range: %d mV", voltages.v10);
        errors++;
    }
    if ((voltages.vusb < 4750) || (voltages.vusb > 5250)) {
        warn_message(testname, "V_USB out of range: %d mV", voltages.vusb);
        errors++;
    }
    TEST_RESULT(errors);
}

int U64TestUsbHub(void)
{
    TEST_START("USB Hub test");

    UsbDevice *device = usb2.first_device();
	if (!device) {
		warn_message(testname, "No USB device was found on the USB PHY.");
        fail_message(testname);
		return 1;
	}
	if (device->vendorID != 0x0424) {
		warn_message(testname, "Primary device is not from Microchip %04x.", device->vendorID);
        fail_message(testname);
		return 1;
	}
	if ((device->productID != 0x2513) && (device->productID != 0x2514)) {
		warn_message(testname, "Primary device does not have the right product ID %04x.", device->productID);
        fail_message(testname);
		return 1;
	}
    TEST_RESULT(0);
}

int U64TestEthernet(void)
{
    TEST_START("Ethernet Test");

    const int tx_len = 1000;
    static uint8_t tx_packet[1000];

    NetworkInterface *intf = NetworkInterface :: getInterface(0);
    if (!intf) {
        warn_message(testname, "No Network Interface available.");
        fail_message(testname);
        return 1;
    }
    if (!(intf->is_link_up())) {
        warn_message(testname, "Network Interface is not up.");
        fail_message(testname);
        return 2;
    }
    for(int i=0;i<6;i++) {
        tx_packet[i] = 0xFF;
    }
    for(int i=6;i<tx_len;i++) {
        tx_packet[i] = (uint8_t)i;
    }
    intf->output(tx_packet, tx_len);

    vTaskDelay(3);

	uint8_t *rx_packet;
	int rx_len;
	if (getNetworkPacket(&rx_packet, &rx_len) == 0) {
		warn_message(testname, "No network packet received.");
        fail_message(testname);
		return 3;
	} else {
		if (rx_len != tx_len) {
            warn_message(testname, "Received packet length %d, expected %d.", rx_len, tx_len);
            fail_message(testname);
			return 2;
		}
	}
    TEST_RESULT(0);
}

int U64TestSetSerial()
{
    TEST_START("Set Serial");
    char serial[16];
    memset(serial, 0, 16);
    memcpy(serial, (char *)SERIAL_NUMBER, 15);
    int res = wifi_set_serial(serial);
    if (res != 0) {
        printf("WiFi module not responding to set_serial command\n");
        fail_message(testname);
        return 1;
    }
    TEST_RESULT(0);
}

int U64TestGetSerial()
{
    TEST_START("Get Serial");
    char serial[16];
    int res = wifi_get_serial(serial);
    if (res != 0) {
        printf("WiFi module not responding to get_serial command\n");
        fail_message(testname);
        return 1;
    }
    info_message("Serial: %s\n", serial);
    extern Screen *screen2;
    console_print(screen2, "                %s", serial);
    TEST_RESULT(0);
}

int U64TestClearConfig()
{
    TEST_START("Clear Config");
    Flash *fl = get_flash();
    int num = fl->get_number_of_config_pages();
    for (int i=0; i < num; i++) {
        fl->clear_config_page(i);
    }
    info_message("Configuration cleared, %d pages\n", num);
    TEST_RESULT(0);
}

int U64TestClearFlash()
{
    TEST_START("Clear Flash (First sector only)");
    Flash *fl = get_flash();
    fl->protect_disable();
    bool res = fl->erase_sector(0);
    TEST_RESULT(res ? 0 : 1);
}
