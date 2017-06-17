/*
 * cia_regs.h
 *
 *  Created on: Jun 10, 2017
 *      Author: gideon
 */

#ifndef CIA_REGS_H_
#define CIA_REGS_H_

#include "u2p.h"

#define CIA_TEST_BASE (U2P_IO_BASE + 0x0300)

#define CIA_ADDR     (*(volatile uint8_t *)(CIA_TEST_BASE + 0x00))
#define CIA_DBUS     (*(volatile uint8_t *)(CIA_TEST_BASE + 0x01))
#define CIA_CTRL_SET (*(volatile uint8_t *)(CIA_TEST_BASE + 0x02))
#define CIA_CTRL_CLR (*(volatile uint8_t *)(CIA_TEST_BASE + 0x03))
#define CIA_PA       (*(volatile uint8_t *)(CIA_TEST_BASE + 0x04))
#define CIA_PA_DIR   (*(volatile uint8_t *)(CIA_TEST_BASE + 0x05))
#define CIA_PB       (*(volatile uint8_t *)(CIA_TEST_BASE + 0x06))
#define CIA_PB_DIR   (*(volatile uint8_t *)(CIA_TEST_BASE + 0x07))
#define CIA_HS       (*(volatile uint8_t *)(CIA_TEST_BASE + 0x08))
#define CIA_HS_SET   (*(volatile uint8_t *)(CIA_TEST_BASE + 0x08))
#define CIA_HS_CLR   (*(volatile uint8_t *)(CIA_TEST_BASE + 0x09))
#define CIA_HS_DIR   (*(volatile uint8_t *)(CIA_TEST_BASE + 0x0A))

#define CTRL_CSN  0x01
#define CTRL_CS2  0x02
#define CTRL_RSTN 0x04
#define CTRL_RWN  0x08
#define CTRL_PHI2 0x10

#define HS_PC    0x01
#define HS_TOD   0x02
#define HS_CNT   0x04
#define HS_SP    0x08
#define HS_FLAG  0x10
#define HS_IRQ	 0x20

#define M6526_PRA  0
#define M6526_PRB  1
#define M6526_DDRA 2
#define M6526_DDRB 3
#define M6526_TALO 4
#define M6526_TAHI 5
#define M6526_TBLO 6
#define M6526_TBHI 7
#define M6526_TODT 8
#define M6526_TODS 9
#define M6526_TODM 10
#define M6526_TODH 11
#define M6526_SDR  12
#define M6526_ICR  13
#define M6526_CRA  14
#define M6526_CRB  15

#define M6522_ORB  0
#define M6522_IRB  0
#define M6522_ORA  1
#define M6522_IRA  1
#define M6522_DDRB 2
#define M6522_DDRA 3
#define M6522_T1CL 4
#define M6522_T1CH 5
#define M6522_T1LL 6
#define M6522_T1LH 7
#define M6522_T2CL 8
#define M6522_T2CH 9
#define M6522_SR   10
#define M6522_ACR  11
#define M6522_PCR  12
#define M6522_IFR  13
#define M6522_IER  14

#endif /* CIA_REGS_H_ */
