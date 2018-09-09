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

int U64TestKeyboard()
{
    int errors = 0;

    typedef struct _t_kbvector {
        char *pinName;
        uint8_t a_out, b_out;
        uint8_t a_in, b_in;
        uint8_t restore;
    } t_kbvector;

    const t_kbvector test[] = {
            { "All Ones", 0xFF, 0xFF, 0xFF, 0xFF, 0x00 },

            { "PA7->PB0", 0x7F, 0xFF, 0x7F, 0xFE, 0x00 },
            { "PA6->PB6", 0xBF, 0xFF, 0xBF, 0xBF, 0x00 },
            { "PA5->PB5", 0xDF, 0xFF, 0xDF, 0xDF, 0x00 },
            { "PA4->PB4", 0xEF, 0xFF, 0xEF, 0xEF, 0x00 },
            { "PA3->PB7", 0xF7, 0xFF, 0xF7, 0x7F, 0x00 },
            { "PA2->PB2", 0xFB, 0xFF, 0xFB, 0xFB, 0x00 },
            { "PA1->PB1", 0xFD, 0xFF, 0xFD, 0xFD, 0x00 },
            { "PA0->PB3", 0xFE, 0xFF, 0xFE, 0xF7, 0x02 },

            { "PB7->PA3", 0xFF, 0x7F, 0xF7, 0x7F, 0x00 },
            { "PB6->PA6", 0xFF, 0xBF, 0xBF, 0xBF, 0x00 },
            { "PB5->PA5", 0xFF, 0xDF, 0xDF, 0xDF, 0x00 },
            { "PB4->PA4", 0xFF, 0xEF, 0xEF, 0xEF, 0x00 },
            { "PB3->PA0", 0xFF, 0xF7, 0xFE, 0xF7, 0x02 },
            { "PB2->PA2", 0xFF, 0xFB, 0xFB, 0xFB, 0x00 },
            { "PB1->PA1", 0xFF, 0xFD, 0xFD, 0xFD, 0x00 },
            { "PB0->PA7", 0xFF, 0xFE, 0x7F, 0xFE, 0x00 },

            { "All Ones", 0xFF, 0xFF, 0xFF, 0xFF, 0x00 },
    };

    for(int i=0;i<18;i++) {
        PLD_CIA1_A = test[i].a_out;
        PLD_CIA1_B = test[i].b_out;
        vTaskDelay(1);
        if ((PLD_CIA1_A != test[i].a_in) || (PLD_CIA1_B != test[i].b_in)) {
            printf("\eOError %s: %b %b\n", PLD_CIA1_A, PLD_CIA1_B);
            errors++;
        }
        if (U64_RESTORE_REG != test[i].restore) {
            printf("\e0Error Restore key mismatch line %d\n", i);
            errors++;
        }
    }
    if (!errors) {
        printf("\e5Keyboard test passed.\n");
    } else {
        printf("\e2Keyboard test FAILED.\n");
    }
    return errors;
}

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

int U64TestUserPort()
{
    PLD_CIA1_B = 0xFF;

    const t_generic_vector vec[] = {
            // first test PB6 and PB7 to see if they connect correctly to the PLD

            { "Init",     &PLD_CIA1_B, 0xFF, NULL, 0, 0, 0 },
            { "Init",     &PLD_CIA2_B, 0xFF, NULL, 0, 0, 0 },
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
            { "PB0->PWM1",   &PLD_CIA2_B, 0xFE, &LOCAL_CIA2, 0x02, 0x02, 0x00 },
            { "CNT1->PB6",   &PLD_CIA2_B, 0xBF, &LOCAL_CIA1, 0x08, 0x08, 0x00 },
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
    int errors;
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
            { "RST#r", &REMOTE_CART_OUT, 0x20, &LOCAL_CART, 0xFF, 0x37, 0x1F },
            { "NMI#r", &REMOTE_CART_OUT, 0x02, &LOCAL_CART, 0xFF, 0x1F, 0x3D },
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
            { "Init",     &LOCAL_IEC,  0x00, NULL, 0, 0, 0 },
            { "Init",     &REMOTE_IEC, 0x00, NULL, 0, 0, 0 },
            { "IEC ATN",  &LOCAL_IEC,  0x01, &LOCAL_IEC,  0xFF, 0x0F, 0x0E },
            { "IEC CLK",  &LOCAL_IEC,  0x02, &LOCAL_IEC,  0xFF, 0x0E, 0x0D },
            { "IEC DAT",  &LOCAL_IEC,  0x04, &LOCAL_IEC,  0xFF, 0x0D, 0x0B },
            { "IEC SRQ",  &LOCAL_IEC,  0x08, &LOCAL_IEC,  0xFF, 0x0B, 0x07 },
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
