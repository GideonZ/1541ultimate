/*
 * u64_tester.h
 *
 *  Created on: Sep 8, 2018
 *      Author: gideon
 */

#ifndef U64_TESTER_H_
#define U64_TESTER_H_

#include <stdint.h>

#define U64TESTER_IO1_BASE 0xA0080000
#define U64TESTER_IO2_BASE 0xA0090000
#define U64TESTER_PLD_BASE 0xA00A0000
#define U64TESTER_REG_BASE 0xA00B0000

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

#define PLD_CIA1_A        (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 0))
#define PLD_CIA1_B        (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 1))
#define PLD_CIA2_A        (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 0x100))
#define PLD_CIA2_B        (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 0x101))


#ifdef __cplusplus
extern "C" {
#endif

int U64TestKeyboard();
int U64TestUserPort();
int U64TestCartridge();
int U64TestIEC();
int U64TestCassette();

#ifdef __cplusplus
}
#endif

#endif /* U64_TESTER_H_ */
