/*
 * u64_tester.h
 *
 *  Created on: Sep 8, 2018
 *      Author: gideon
 */

#ifndef U64_TESTER_H_
#define U64_TESTER_H_

#include <stdint.h>

#define U64TESTER_IO1_BASE   0xA0080000 // 10 000
#define U64TESTER_IO2_BASE   0xA0090000 // 10 010
#define U64TESTER_PLD_BASE   0xA00A0000 // 10 100
#define U64TESTER_SID1_BASE  0xA00A8000 // 10 101
#define U64TESTER_SID2_BASE  0xA00A8020 // 10 101
#define U64TESTER_REG_BASE   0xA00B0000 // 10 110
#define U64TESTER_AUDIO_BASE 0xA00C0000

#define REMOTE_CART_OUT   (*(volatile uint8_t *)(U64TESTER_IO1_BASE + 0))
#define REMOTE_CART_IN    (*(volatile uint8_t *)(U64TESTER_IO1_BASE + 1))
#define REMOTE_ADDR_HI    (*(volatile uint8_t *)(U64TESTER_IO1_BASE + 2))
#define REMOTE_ADDR_LO    (*(volatile uint8_t *)(U64TESTER_IO1_BASE + 3))
#define REMOTE_IEC        (*(volatile uint8_t *)(U64TESTER_IO1_BASE + 4))
#define REMOTE_CAS        (*(volatile uint8_t *)(U64TESTER_IO1_BASE + 5))
#define REMOTE_SIGNATURE  (*(volatile uint8_t *)(U64TESTER_IO1_BASE + 7))

#define LOCAL_IEC         (*(volatile uint8_t *)(U64TESTER_REG_BASE + 0))
#define LOCAL_CAS         (*(volatile uint8_t *)(U64TESTER_REG_BASE + 1))
#define LOCAL_CART        (*(volatile uint8_t *)(U64TESTER_REG_BASE + 2))
#define LOCAL_CIA1        (*(volatile uint8_t *)(U64TESTER_REG_BASE + 3))
#define LOCAL_CIA2        (*(volatile uint8_t *)(U64TESTER_REG_BASE + 4))
#define LOCAL_PADDLESEL   (*(volatile uint8_t *)(U64TESTER_REG_BASE + 7))
#define LOCAL_PADDLE_X    (*(volatile uint8_t *)(U64TESTER_REG_BASE + 8))
#define LOCAL_PADDLE_Y    (*(volatile uint8_t *)(U64TESTER_REG_BASE + 9))

#define PLD_CIA1_A        (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 0))
#define PLD_CIA1_B        (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 1))
#define PLD_CIA2_A        (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 0x100))
#define PLD_CIA2_B        (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 0x101))

// Addresses of the PLD are bits 4, bit 8, bit 1 and bit 0
// Joy swap = 1010 => 0x12
// Readback of Control = 1011 => 0x13
// Swap state = 1110 => 0x112
// Version = 1111 => 113
// Control write 1 => 1010 => 0x12
// Control write 2 => 1011 => 0x13
// Swap write => 1110 => 0x112

#define PLD_WR_CTRL1      (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 0x12))
#define PLD_WR_CTRL2      (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 0x13))
#define PLD_JOYSWAP       (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 0x112))
#define PLD_RD_CTRL       (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 0x13)) // not implemented in version < 16
#define PLD_RD_VERSION    (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 0x113))
#define PLD_RD_JOY        (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 0x12)) // not implemented in version < 16

typedef struct {
    uint8_t sda;
    uint8_t scl;
    uint8_t capsel;
    uint8_t capval;
    uint8_t id;
} socket_tester_t;

#define SOCKET1           ((volatile socket_tester_t *)U64TESTER_SID1_BASE)
#define SOCKET2           ((volatile socket_tester_t *)U64TESTER_SID2_BASE)

#define AUDIO_SAMPLER_START (*(volatile uint8_t *)(U64TESTER_AUDIO_BASE))
#define AUDIO_SAMPLES       ((volatile int *)(U64TESTER_AUDIO_BASE))
#define AUDIO_SAMPLE_COUNT  512

#ifdef __cplusplus
extern "C" {
#endif

int U64TestKeyboard();
int U64TestUserPort();
int U64TestCartridge();
int U64TestIEC();
int U64TestCassette();
int U64EliteTestJoystick();
int U64PaddleTest();
int U64AudioCodecTest();

#ifdef __cplusplus
}
#endif

#endif /* U64_TESTER_H_ */
