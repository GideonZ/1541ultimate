/*
 * u64_tester.c
 *
 *  Created on: Sep 8, 2018
 *      Author: gideon
 */

#include "u64.h"
#include "u64_tester.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dump_hex.h"

typedef struct _t_genericvector {
    char *pinName;
    volatile uint8_t *outreg;
    uint8_t outval;
    volatile uint8_t *inreg;
    uint8_t mask;
    uint8_t before;
    uint8_t expect;
} t_generic_vector;

static int PerformTest(const t_generic_vector *vec)
{
    int errors = 0;
    printf("\eO");
    for(int i=0; ; i++) {
        uint8_t before = 0;
        uint8_t after = 0;
        if (!vec[i].pinName) {
            break;
        }
        if (vec[i].inreg) {
            before = *(vec[i].inreg);
        }
        if (vec[i].outreg) {
            *(vec[i].outreg) = vec[i].outval;
            vTaskDelay(1);
        }
        if (vec[i].inreg) {
            after = *(vec[i].inreg);
            if ((before & vec[i].mask) != vec[i].before) {
                printf("Before %s: Read: %b&%b, expected: %b\n", vec[i].pinName, before, vec[i].mask, vec[i].before);
                errors ++;
            }
            if ((after & vec[i].mask) != vec[i].expect) {
                printf("After %s: Read: %b&%b, expected: %b\n", vec[i].pinName, after, vec[i].mask, vec[i].expect);
                errors ++;
            }
        }
    }
    return errors;
}

int U64TestKeyboard()
{

    LOCAL_CART = 0;
    REMOTE_CART_OUT = 0;

    const t_generic_vector vec[] = {
            // first test PB6 and PB7 to see if they connect correctly to the PLD

            { "Init",     &PLD_CIA1_A, 0xFF, NULL, 0, 0, 0 },
            { "Init",     &PLD_CIA1_B, 0xFF, NULL, 0, 0, 0 },

            { "PA7->PB0", &PLD_CIA1_A, 0x7F, &PLD_CIA1_B, 0xFF, 0xFF, 0xFE },
            { "PA6->PB6", &PLD_CIA1_A, 0xBF, &PLD_CIA1_B, 0xFF, 0xFE, 0xBF },
            { "PA5->PB5", &PLD_CIA1_A, 0xDF, &PLD_CIA1_B, 0xFF, 0xBF, 0xDF },
            { "PA4->PB4", &PLD_CIA1_A, 0xEF, &PLD_CIA1_B, 0xFF, 0xDF, 0xEF },
            { "PA3->PB7", &PLD_CIA1_A, 0xF7, &PLD_CIA1_B, 0xFF, 0xEF, 0x7F },
            { "PA2->PB2", &PLD_CIA1_A, 0xFB, &PLD_CIA1_B, 0xFF, 0x7F, 0xFB },
            { "PA1->PB1", &PLD_CIA1_A, 0xFD, &PLD_CIA1_B, 0xFF, 0xFB, 0xFD },
            { "PA0->PB3", &PLD_CIA1_A, 0xFE, &PLD_CIA1_B, 0xFF, 0xFD, 0xF7 },
            { "Arelease", &PLD_CIA1_A, 0xFF, &PLD_CIA1_B, 0xFF, 0xF7, 0xFF },

            { "PB7->PA3", &LOCAL_CIA1, 0x80, &PLD_CIA1_A, 0xFF, 0xFF, 0xF7 },
            { "PB6->PA6", &LOCAL_CIA1, 0x40, &PLD_CIA1_A, 0xFF, 0xF7, 0xBF },
            { "CiaRel",   &LOCAL_CIA1, 0x00, NULL, 0, 0, 0 },
            { "PB5->PA5", &PLD_CIA1_B, 0xDF, &PLD_CIA1_A, 0xFF, 0xFF, 0xDF },
            { "PB4->PA4", &PLD_CIA1_B, 0xEF, &PLD_CIA1_A, 0xFF, 0xDF, 0xEF },
            { "PB3->PA0", &PLD_CIA1_B, 0xF7, &PLD_CIA1_A, 0xFF, 0xEF, 0xFE },
            { "PB2->PA2", &PLD_CIA1_B, 0xFB, &PLD_CIA1_A, 0xFF, 0xFE, 0xFB },
            { "PB1->PA1", &PLD_CIA1_B, 0xFD, &PLD_CIA1_A, 0xFF, 0xFB, 0xFD },
            { "PB0->PA7", &PLD_CIA1_B, 0xFE, &PLD_CIA1_A, 0xFF, 0xFD, 0x7F },
            { "Brelease", &PLD_CIA1_B, 0xFF, &PLD_CIA1_A, 0xFF, 0x7F, 0xFF },
            { NULL, NULL, 0, NULL, 0, 0, 0 },
    };
    int errors = PerformTest(vec);
    if (!errors) {
        printf("\e5Keyboard test passed.\n");
    } else {
        printf("\e2Keyboard test FAILED.\n");
    }
    return errors;
}


int U64TestUserPort()
{
    PLD_CIA1_B = 0xFF;

    const t_generic_vector vec[] = {
            // first test PB6 and PB7 to see if they connect correctly to the PLD

            { "Init",     &PLD_CIA1_B, 0xFF, NULL, 0, 0, 0 },
            { "Init",     &PLD_CIA2_B, 0xFF, NULL, 0, 0, 0 },
            { "Init",     &LOCAL_IEC,  0xFF, NULL, 0, 0, 0 },
            { "Release1", &LOCAL_CIA1, 0x00, NULL, 0, 0, 0 },
            { "Release2", &LOCAL_CIA2, 0x00, NULL, 0, 0, 0 },
            { "CIA1_PB7", &LOCAL_CIA1, 0x80, &PLD_CIA1_B, 0xC0, 0xC0, 0x40 },
            { "CIA1_PB6", &LOCAL_CIA1, 0x40, &PLD_CIA1_B, 0xC0, 0x40, 0x80 },
            { "CIA2_PB7", &LOCAL_CIA1, 0x20, &PLD_CIA2_B, 0xC0, 0xC0, 0x40 },
            { "CIA2_PB6", &LOCAL_CIA1, 0x10, &PLD_CIA2_B, 0xC0, 0x40, 0x80 },
            { "Release2", &LOCAL_CIA1, 0x00, NULL, 0, 0, 0 },

            // then test the connections of the loopback cable: 10 connections
            { "Release PLD", &PLD_CIA2_A, 0xFF, NULL, 0, 0, 0 },
            { "/FLAG2->PA2", &PLD_CIA2_A, 0xFB, &LOCAL_CIA2, 0x80, 0x80, 0x00 },
            { "/RESET->PB7", &LOCAL_CART, 0x20, &PLD_CIA2_B, 0x80, 0x80, 0x00 },
            { "ReleaseRST",  &LOCAL_CART, 0x00, NULL, 0, 0, 0 },
            { "PB0->PWM1",   &PLD_CIA2_B, 0xFE, &LOCAL_CIA2, 0x02, 0x02, 0x00 },
            { "CNT1->PB6",   &LOCAL_CIA1, 0x10, &LOCAL_CIA1, 0x08, 0x08, 0x00 },
            { "CiaRel",      &LOCAL_CIA1, 0x00, NULL, 0, 0, 0 },
            { "PB1->ATN",    &PLD_CIA2_B, 0xFD, &LOCAL_IEC,  0x01, 0x01, 0x00 },
            { "SP1->PB5",    &PLD_CIA2_B, 0xDF, &LOCAL_CIA1, 0x04, 0x04, 0x00 },
            { "PB2->/PC2",   &LOCAL_CIA1, 0x08, &PLD_CIA2_B, 0xFF, 0xDF, 0xDB },
            { "CNT2->PB4",   &PLD_CIA2_B, 0xEF, &LOCAL_CIA1, 0x02, 0x02, 0x00 },
            { "PB3->SP2",    &PLD_CIA2_B, 0xF7, &LOCAL_CIA1, 0x01, 0x01, 0x00 },
            { "PWM0->PWM1",  &LOCAL_CIA2, 0x04, &LOCAL_CIA2, 0x02, 0x02, 0x00 },
            { NULL, NULL, 0, NULL, 0, 0, 0 },
    };

    int errors = PerformTest(vec);
    if (!errors) {
        printf("\e5Userport test passed.\n");
    } else {
        printf("\e2Userport test FAILED.\n");
    }
    return errors;
}

int U64TestCartridge()
{
    if (REMOTE_SIGNATURE != 0x99) {
        printf("\e2Cartridge Test: Remote tester not found\n\eO");
        return 1;
    }
    int errors = 0;
    // let's fill the RAM. We only fill 10 locations, but such that all address/data lines 0..7 are checked
    const uint8_t addr[] = { 0x00, 0x55, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
    const uint8_t data[] = { 0x55, 0xAA, 0x80, 0x20, 0x08, 0x02, 0x40, 0x04, 0x10, 0x01 };
    volatile uint8_t *dest = (volatile uint8_t *)U64TESTER_IO2_BASE;
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
    }

    // Now check the remaining address lines 8 .. 15
    volatile uint8_t *remoteRegs = (volatile uint8_t *)U64TESTER_IO1_BASE;

    for(int i=8; i<16; i++) {
        readback = remoteRegs[(1 << i) + 2];
        if (readback != (1 << (i-8))) {
            errors ++;
            printf("A%d:%b ", i, readback);
        }
    }
    if (errors) {
        printf("FAIL\n");
    }

    // Now check each of the other cartridge I/O lines (PHI2 was already used, and R/Wn and IO1 and IO2 as well)
    // SO there are 38-16-8-4 = 10 left?  DMA, BA, ROML, ROMH, GAME, EXROM, DOTCLK, IRQ, NMI, RESET
    const t_generic_vector vec[] = {
            { "Init",  &LOCAL_CART, 0x00, NULL, 0, 0, 0 },
            { "Init",  &REMOTE_CART_OUT, 0x00, NULL, 0, 0, 0 },
            { "IRQ#",  &LOCAL_CART, 0x01, &REMOTE_CART_IN, 0xFF, 0x23, 0x22 },
            { "NMI#",  &LOCAL_CART, 0x02, &REMOTE_CART_IN, 0xFF, 0x22, 0x21 },
            { "ROML#", &LOCAL_CART, 0x04, &REMOTE_CART_IN, 0xFF, 0x21, 0x27 },
            { "ROMH#", &LOCAL_CART, 0x08, &REMOTE_CART_IN, 0xFF, 0x27, 0x2B },
            { "DOTCK", &LOCAL_CART, 0x10, &REMOTE_CART_IN, 0xFF, 0x2B, 0x33 },
            { "RST#",  &LOCAL_CART, 0x20, &REMOTE_CART_IN, 0xFF, 0x33, 0x03 },
            { "BA",    &LOCAL_CART, 0x40, &REMOTE_CART_IN, 0xFF, 0x03, 0x63 },
            { "End",   &LOCAL_CART, 0x00, NULL, 0, 0, 0 },
            { "DMA#",  &REMOTE_CART_OUT, 0x10, &LOCAL_CART, 0xFF, 0x3F, 0x2F },
            { "GAME#", &REMOTE_CART_OUT, 0x04, &LOCAL_CART, 0xFF, 0x2F, 0x3B },
            { "EXROM#",&REMOTE_CART_OUT, 0x08, &LOCAL_CART, 0xFF, 0x3B, 0x37 },
            { "NMI#r", &REMOTE_CART_OUT, 0x02, &LOCAL_CART, 0xFF, 0x37, 0x3D },
            { "IRQ#r", &REMOTE_CART_OUT, 0x01, &LOCAL_CART, 0xFF, 0x3D, 0x3E },
            { NULL, NULL, 0, NULL, 0, 0, 0 },
    };

    errors += PerformTest(vec);
    if (!errors) {
        printf("\e5Cartridge test passed.\n");
    } else {
        printf("\e2Cartridge test FAILED.\n");
    }
    return errors;
}

int U64TestIEC()
{
    const t_generic_vector vec[] = {
            { "Init",     &PLD_CIA2_A, 0xFF, NULL, 0, 0, 0 },
            { "Init",     &PLD_CIA2_B, 0xFF, NULL, 0, 0, 0 },
            { "Init",     &LOCAL_IEC,  0xFF, NULL, 0, 0, 0 },
            { "Init",     &REMOTE_IEC, 0x00, NULL, 0, 0, 0 },
            { "IEC ATN",  &LOCAL_IEC,  0xFE, &LOCAL_IEC,  0xFF, 0x0F, 0x0E },
            { "IEC CLK",  &LOCAL_IEC,  0xFD, &LOCAL_IEC,  0xFF, 0x0E, 0x0D },
            { "IEC DAT",  &LOCAL_IEC,  0xFB, &LOCAL_IEC,  0xFF, 0x0D, 0x0B },
            { "IEC SRQ",  &LOCAL_IEC,  0xF7, &LOCAL_IEC,  0xFF, 0x0B, 0x07 },
            { NULL, NULL, 0, NULL, 0, 0, 0 },
    };
    int errors = PerformTest(vec);
    if (!errors) {
        printf("\e5Local IEC test passed.\n");
    } else {
        printf("\e2Local IEC test FAILED.\n");
    }
    return errors;
}

int U64TestCassette()
{
    if (REMOTE_SIGNATURE != 0x99) {
        printf("\e2Cassette test: Remote tester not found\n\eO");
        return 1;
    }

    const t_generic_vector vec[] = {
            { "Init",      &LOCAL_CAS,  0x00, NULL, 0, 0, 0 },
            { "Cas Motor", &LOCAL_CAS,  0x01, &REMOTE_CAS, 0xFF, 0x00, 0x01 },
            { "Cas Read",  &LOCAL_CAS,  0x02, &REMOTE_CAS, 0xFF, 0x01, 0x02 },
            { "Cas Write", &LOCAL_CAS,  0x04, &REMOTE_CAS, 0xFF, 0x02, 0x04 },
            { "Cas Sense", &LOCAL_CAS,  0x08, &REMOTE_CAS, 0xFF, 0x04, 0x08 },
            { NULL, NULL, 0, NULL, 0, 0, 0 },
    };
    int errors = PerformTest(vec);
    if (!errors) {
        printf("\e5Cassette test passed.\n");
    } else {
        printf("\e2Cassette IEC test FAILED.\n");
    }
    return errors;
}

int U64EliteTestJoystick()
{
    // with this test, the fire button is used as output to clock the counter.
    // then the counter reaches 15 for the second time, bit 0 to bit 3 have been tested.
    // In order to make sure that the keyboard test does not see the counter, the isolate
    // mode is turned on here.

    PLD_CIA1_A = 0xFF;
    PLD_CIA1_B = 0xFF;

    if (PLD_RD_VERSION != 0x16) {
        printf("\e\022PLD version should be V1.6 for this test to work.\n");
        return -1;
    }
    PLD_JOYSWAP = 0x02; // Isolate mode

    int ok1 = -1;
    int ok2 = -1;
    const char correct[16] = "@ABCDEFGHIJKLMNO";
    uint8_t buffer[32];
    memset(buffer, 0xAA, 32);

    for (int i=0; i<32; i++) {
        PLD_CIA1_A = 0xEF;
        vTaskDelay(1);
        PLD_CIA1_A = 0xFF;
        vTaskDelay(1);
        buffer[i] = (PLD_RD_JOY & 0x0F) | 0x40;
        if ((buffer[i] == 0x4F) && (i >= 16)) {
            ok1 = memcmp(&buffer[i-15], correct, 16);
            break;
        }
    }
    if (ok1) {
        printf("\e\022Joystick Port A:\n");
        dump_hex_relative(buffer, 32);
        printf("\e\037");
    }

    PLD_JOYSWAP = 0x03; // Isolate mode, swap ports

    memset(buffer, 0xAA, 32);
    for (int i=0; i<32; i++) {
        PLD_CIA1_A = 0xEF;
        vTaskDelay(1);
        PLD_CIA1_A = 0xFF;
        vTaskDelay(1);
        buffer[i] = (PLD_RD_JOY & 0x0F) | 0x40;
        if ((buffer[i] == 0x4F) && (i >= 16)) {
            ok2 = memcmp(&buffer[i-15], correct, 16);
            break;
        }
    }
    if (ok2) {
        printf("\e\022Joystick Port B:\e\022\n");
        dump_hex_relative(buffer, 32);
        printf("\e\037");
    }

    PLD_JOYSWAP = 0x02; // Isolate mode

    if (ok1) {
        return -1;
    }
    if (ok2) {
        return -2;
    }
    printf("\e\025Joystick Ports passed.\n");
    return 0;
}

int U64PaddleTest(void)
{
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
    if ((x1 < 47) || (x1 > 57)) {
        printf("\e\022Paddle X on Port 1 FAIL. Expected ~52, got %d\n", x1);
        errors++;
    }
    if ((y1 < 4) || (y1 > 8)) {
        printf("\e\022Paddle Y on Port 1 FAIL. Expected ~7, got: %d\n", y1);
        errors++;
    }
    if ((x2 < 96) || (x2 > 116)) {
        printf("\e\022Paddle X on Port 2 FAIL. Expected ~106, got: %d\n", x2);
        errors++;
    }
    if ((y2 < 12) || (y2 > 34)) {
        printf("\e\022Paddle Y on Port 2 FAIL. Expected ~31, got: %d\n", y2);
        errors++;
    }
    if (!errors) {
        printf("\e\025Paddle test passed.\n");
    }
    return errors;
}

void codec_loopback(int enable);

int U64AudioCodecTest(void)
{
    codec_loopback(0); // we should now sample "silence"
    AUDIO_SAMPLER_START = 0; // trigger a sample
    vTaskDelay(5);

    int maxL = 0, maxR = 0;
    for (int i=0; i<(AUDIO_SAMPLE_COUNT*2); i+=2) {
        if(abs(AUDIO_SAMPLES[i]) > maxL) {
            maxL = abs(AUDIO_SAMPLES[i]);
        }
        if(abs(AUDIO_SAMPLES[i+1]) > maxR) {
            maxR = abs(AUDIO_SAMPLES[i+1]);
        }
    }
    int errors = 0;
    if (maxL > 10000) errors ++;
    if (maxR > 10000) errors ++;
    if (errors) {
        printf("\e\022Audio codec silence level too high: %d %d\n", maxL, maxR);
    }

    codec_loopback(1); // we should now sample what we send
    AUDIO_SAMPLER_START = 0; // trigger a sample
    vTaskDelay(5);

    maxL = 0;
    maxR = 0;
    uint8_t start = AUDIO_SAMPLER_START; // reading this byte will give the initial value of the counter
    int errors2 = 0;

    for (int i=0; i<(AUDIO_SAMPLE_COUNT*2); i+=2) {
        uint32_t expected = start;
        expected *= 0x010101; // triplicate
        uint32_t read = (uint32_t)AUDIO_SAMPLES[i+1]; // read left sample
        if ((read & 0xFFFFFF) != expected) {
            printf("L Expected: %08x, got: %08x\n", expected, read & 0xFFFFFF);
            errors2 ++;
        }
        read = (uint32_t)AUDIO_SAMPLES[i]; // read right sample
        expected ^= 0xFF0000;
        if ((read & 0xFFFFFF) != expected) {
            printf("R Expected: %08x, got: %08x\n", expected, read & 0xFFFFFF);
            errors2 ++;
        }
        start++;
    }
    if (errors2) {
        printf("\e\022Codec loopback gave wrong results.\n");
    }
    errors += errors2;
    if (!errors) {
        printf("\e\025Audio codec passed.\n");
    }

    return errors;
}
