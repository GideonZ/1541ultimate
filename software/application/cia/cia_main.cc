/*
 * cia_main.c
 *
 *  Created on: Nov 14, 2016
 *      Author: gideon
 */

#include "FreeRTOS.h"
#include "task.h"
#include "altera_avalon_pio_regs.h"
#include "altera_msgdma.h"
#include "system.h"

#include "stdio.h"
#include "stdlib.h"
#include "u2p.h"
#include "cia_regs.h"
#include "dump_hex.h"

static const char *mos6526_regs[] = {
	"PRA",
	"PRB",
	"DRA",
	"DRB",
	"TAL",
	"TAH",
	"TBL",
	"TBH",
	"TDT",
	"TDS",
	"TDM",
	"TDH",
	"SDR",
	"ICR",
	"CRA",
	"CRB",
};

static const char *mos6522_regs[] = {
	"IORB",
	"IORA",
	"DDRB",
	"DDRA",
	"T1CL",
	"T1CH",
	"T1LL",
	"T1LH",
	"T2CL",
	"T2CH",
	"SR",
	"ACR",
	"PCR",
	"IFR",
	"IER",
	"ORA*",
};

static const char **regnames = mos6526_regs;
static int cycle = 0;
static int errors = 0;

static uint8_t ReadRegister(int reg)
{
	CIA_CTRL_CLR = CTRL_PHI2;
	CIA_ADDR = reg;
	CIA_CTRL_CLR = CTRL_CSN;
	CIA_CTRL_SET = CTRL_RWN;
	CIA_CTRL_SET = CTRL_PHI2;
	uint8_t ret;
	printf("%3d: PHI2=1: %B (%b) [%B (%b)].. ", cycle++, CIA_HS, CIA_PB, MINE_HS, MINE_PB);
	ret = CIA_DBUS;
	CIA_CTRL_CLR = CTRL_PHI2;
	CIA_CTRL_SET = CTRL_CSN | CTRL_RWN;
	printf("PHI2=0: %B (%b) [%B (%b)].. ", CIA_HS, CIA_PB, MINE_HS, MINE_PB);
	printf("Read  %b from %s\n", ret, regnames[reg]);
	return ret;
}

static void ReadAllRegisters(uint8_t *buf)
{
	CIA_ADDR = 0;
	CIA_CTRL_SET = CTRL_RWN | CTRL_CS2;
	CIA_CTRL_CLR = CTRL_CSN;
	CIA_CTRL_SET = CTRL_PHI2;

	printf("%3d|", cycle);
	cycle++;
	int err = 0;
	for (int i=0; i < 16; i++) {
		CIA_ADDR = i;
		printf("%s:%b|%b, ", regnames[i], CIA_DBUS, MINE_DB);
		if ((MINE_DB != CIA_DBUS) && ((i & 12) != 8)) {
			err ++;
		}
		if (buf) {
			buf[i] = CIA_DBUS;
		}
	}
	printf("RPA: %b. RPB: %b F:%b ---", CIA_PA, CIA_PB, CIA_HS);
	if (buf) {
		buf[16] = CIA_PA;
		buf[17] = CIA_PB;
		buf[18] = CIA_HS;
	}
	CIA_ADDR = 3;
	CIA_CTRL_SET = CTRL_CSN;
	CIA_CTRL_CLR = CTRL_PHI2;
	printf("PHI2=0: %B (%b)..", CIA_HS, CIA_PB);
	printf("[%B|%b]", MINE_HS, MINE_PB);
	if (MINE_HS != CIA_HS) {
		err ++;
	}
	if (MINE_PB != CIA_PB) {
		err ++;
	}
	if(err) {
		printf("ERR\n");
		errors ++;
	} else {
		printf("\n");
	}
}

static void PhiCycle(void)
{
	int err = 0;
	CIA_CTRL_SET = CTRL_PHI2;
	printf("%3d: PHI2=1: %B (%b).. ", cycle++, CIA_HS, CIA_PB);
	printf("[%B|%b]", MINE_HS, MINE_PB);
	CIA_CTRL_CLR = CTRL_PHI2;
	printf("PHI2=0: %B (%b)..", CIA_HS, CIA_PB);
	printf("[%B|%b]", MINE_HS, MINE_PB);
	if (MINE_HS != CIA_HS) {
		err ++;
	}
	if (MINE_PB != CIA_PB) {
		err ++;
	}
	if(err) {
		printf("ERR\n");
		errors ++;
	} else {
		printf("\n");
	}
}

static void PhiCycleS(void)
{
	CIA_CTRL_SET = CTRL_PHI2;
	CIA_CTRL_CLR = CTRL_PHI2;
	cycle++;
}

static void WriteRegister(int reg, uint8_t value)
{
	CIA_CTRL_CLR = CTRL_PHI2;
	CIA_CTRL_CLR = CTRL_CSN | CTRL_RWN;
	CIA_ADDR = reg;
	CIA_DBUS = value;
	CIA_CTRL_CLR = CTRL_CSN | CTRL_RWN;
	CIA_CTRL_SET = CTRL_PHI2;
	int err = 0;
	printf("%3d: PHI2=1: %B (%b) [%B (%b)].. ", cycle++, CIA_HS, CIA_PB, MINE_HS, MINE_PB);
	if (CIA_HS != MINE_HS) err++;
	if (CIA_PB != MINE_PB) err++;
	CIA_CTRL_CLR = CTRL_PHI2;
	CIA_CTRL_SET = CTRL_CSN;
	CIA_CTRL_SET = CTRL_RWN;
	printf("PHI2=0: %B (%b) [%B (%b)].. ", CIA_HS, CIA_PB, MINE_HS, MINE_PB);
	if (CIA_HS != MINE_HS) err++;
	if (CIA_PB != MINE_PB) err++;
	printf("Wrote %b to %s. %s\n", value, regnames[reg], (err)?"ERR!":"");
	if (err) {
		errors++;
	}
}

static void TogglePHI2(void)
{
	for(int i=0;i<100;i++) {
		CIA_CTRL_SET = CTRL_PHI2;
		CIA_CTRL_CLR = CTRL_PHI2;
		CIA_CTRL_SET = CTRL_PHI2;
		CIA_CTRL_CLR = CTRL_PHI2;
		CIA_CTRL_SET = CTRL_PHI2;
		CIA_CTRL_CLR = CTRL_PHI2;
		CIA_CTRL_SET = CTRL_PHI2;
		CIA_CTRL_CLR = CTRL_PHI2;
		CIA_CTRL_SET = CTRL_PHI2;
		CIA_CTRL_CLR = CTRL_PHI2;
	}
}

static void Reset()
{
	CIA_CTRL_SET = CTRL_CSN | CTRL_RWN | CTRL_PHI2 | CTRL_CS2;
	CIA_CTRL_CLR = CTRL_PHI2;
	CIA_CTRL_CLR = CTRL_RSTN;

	for(int i=0; i < 5; i++) {
		CIA_CTRL_SET = CTRL_PHI2;
		CIA_CTRL_CLR = CTRL_PHI2;
	}

	CIA_CTRL_SET = CTRL_RSTN;
	cycle = 0;
	errors = 0;
}

/*
static void ViaTimer2OneShot(void)
{
	Reset();
	for (int i=0; i<10; i++) {
		ReadRegister(M6522_T2CL);
	}

	WriteRegister(M6522_T2CL, 0x05);
	PhiCycleS();
	WriteRegister(M6522_ACR, 0);

	PhiCycleS();
	for (int i=0; i<10; i++) {
		ReadRegister(M6522_T2CL);
		PhiCycleS();
		ReadRegister(M6522_T2CH);
		PhiCycleS();
	}

	WriteRegister(M6522_DDRB, 0xFF);
	for(int i=0; i<255; i+=21)
		WriteRegister(M6522_ORB, i);

	ReadRegister(M6522_IFR);
	PhiCycleS();
	WriteRegister(M6522_IER, 0x7F);
	PhiCycleS();
	WriteRegister(M6522_IER, 0xA0);
	PhiCycleS();

	for (int i=0; i<10; i++) {
		ReadRegister(M6522_T2CL);
		PhiCycleS();
		ReadRegister(M6522_T2CH);
		PhiCycleS();
	}

	WriteRegister(M6522_T2CL, 0x10);
	PhiCycle();
	WriteRegister(M6522_T2CL, 0x0F);
	PhiCycle();
	WriteRegister(M6522_T2CH, 0x00);
	PhiCycle();

	for (int i=0; i<30; i++) {
		 // PhiCycle();
		 ReadRegister(M6522_T2CL);
	}
}
*/

static void ViaTimer2OneShot(void)
{
	Reset();
	ReadRegister(M6522_T2CL);
	ReadRegister(M6522_T2CH);
	WriteRegister(M6522_T2CL, 0x10);
	WriteRegister(M6522_T2CH, 0x02);
	WriteRegister(M6522_IER, 0x7F);
	WriteRegister(M6522_IER, 0xA0);
	PhiCycle();

	for (int i=0; i<550; i++) {
		ReadRegister(M6522_T2CL);
//		PhiCycle();
	}
}

static void ViaTimer2OneShotSerial(void)
{
	Reset();
	ReadRegister(M6522_T2CL);
	ReadRegister(M6522_T2CH);
	ReadRegister(M6522_SR);
	ReadRegister(M6522_T1CL);
	ReadRegister(M6522_T1CH);
	ReadRegister(M6522_T1LL);
	ReadRegister(M6522_T1LH);
	WriteRegister(M6522_DDRB, 0xFF);
	WriteRegister(M6522_ORB, 0x39);
	WriteRegister(M6522_T2CL, 0x06);
	WriteRegister(M6522_T2CH, 0x02);
	WriteRegister(M6522_IER, 0x7F);
	WriteRegister(M6522_IER, 0x84);
	WriteRegister(M6522_T1LL, 0x34);
	WriteRegister(M6522_T1LH, 0x12);
	PhiCycle();

	for (int i=0; i<150; i++) {
		ReadRegister(M6522_T2CL);
		//		PhiCycle();
	}
	WriteRegister(M6522_ACR, 0x14); // select timer T2 as shift register operation (out!)
	WriteRegister(M6522_SR, 0x01);
	PhiCycle();

	for (int i=0; i<450; i++) {
		uint8_t r = ReadRegister(M6522_IFR);
		if (cycle == 394) {
			WriteRegister(M6522_SR, 0x55);
			PhiCycle();
		}
		//		PhiCycle();
		// WriteRegister(M6522_SR, i);
	}

}

static void ViaTimer2Ext(void)
{
	Reset();

	CIA_PB_DIR = 0x40;
	CIA_PB     = 0x00;

	WriteRegister(M6522_DDRB, 0x00);
	PhiCycle();
	WriteRegister(M6522_ACR, 0x20);
	PhiCycle();
	ReadRegister(M6522_IFR);
	PhiCycle();
	WriteRegister(M6522_IER, 0x7F);
	PhiCycle();
	WriteRegister(M6522_IER, 0xA0);
	PhiCycle();
	WriteRegister(M6522_PCR, 0xC0); // CB2 is low output.
	PhiCycle();
	WriteRegister(M6522_T2CL, 0x26);
	PhiCycle();
	WriteRegister(M6522_T2CH, 0x02);
	PhiCycle();
	ReadRegister(M6522_T2CH);
	ReadRegister(M6522_T2CL);
	PhiCycle();
	for(int i=0;i<10;i++) {
		CIA_PB = 0x00;
		PhiCycle();
		//CIA_CTRL_SET = CTRL_PHI2;
		CIA_PB = 0x40;
		PhiCycle();
		//CIA_CTRL_CLR = CTRL_PHI2;
	}
	ReadRegister(M6522_T2CH);
	ReadRegister(M6522_T2CL);
}

static void ViaTimerSerial(void)
{
	Reset();
	for(int i=0; i < 0x5501; i++) {
		CIA_CTRL_SET = CTRL_PHI2;
		CIA_CTRL_CLR = CTRL_PHI2;
	}
	PhiCycle();
	PhiCycle();

	WriteRegister(M6522_ACR, 0);
	PhiCycle();

	ReadRegister(M6522_IFR);
	PhiCycle();
	WriteRegister(M6522_IER, 0x7F);
	PhiCycle();
	WriteRegister(M6522_IER, 0x84);
	PhiCycle();
//	WriteRegister(M6522_PCR, 0xC0); // CB2 is low output.
	PhiCycle();
	WriteRegister(M6522_T2CL, 0x26);
	PhiCycle();
	WriteRegister(M6522_T2CH, 0x02);
	PhiCycle();
	// Let's test when the first bit is coming out, when T2CH is not zero.
	PhiCycle();

	WriteRegister(M6522_ACR, 4 * 1);
	PhiCycleS();
	ReadRegister(M6522_T2CL);
	PhiCycleS();

	WriteRegister(M6522_T2CL, 0x03);
	PhiCycleS();
    WriteRegister(M6522_SR, 0x81);
    // ReadRegister(M6522_SR);
	PhiCycleS();

	for (int i=0; i<121; i++) {
		//PhiCycle();
		ReadRegister(M6522_SR);

/*
		if (i == 105) { // 103 = ignore, 104 = IRQ glitch, 105 = retrigger. T2 unaffected.
			//PhiCycleS();
			WriteRegister(M6522_SR, 0xFE);
			PhiCycle();
			ReadRegister(M6522_T2CL);
		}
*/
	}
	ReadRegister(M6522_IFR);
	PhiCycle();
	WriteRegister(M6522_ACR, 0 * 5);
	PhiCycle();
	PhiCycle();
	ReadRegister(M6522_IFR);
	PhiCycle();
	PhiCycle();
}

/*
ACR = 0
T1L = a5
ACR = 1
T1H = 45
ACR = 2
T2L = 05
ACR = 3
T2H = 65
ACR = 4
SR  = E5

 */
static void CiaReset(void)
{
	CIA_CTRL_SET = CTRL_CSN | CTRL_RWN | CTRL_PHI2 | CTRL_CS2;
	CIA_CTRL_CLR = CTRL_PHI2;
	CIA_CTRL_CLR = CTRL_RSTN;
	for(int i=0; i < 2; i++) {
		CIA_CTRL_SET = CTRL_PHI2;
		CIA_CTRL_CLR = CTRL_PHI2;
	}
	CIA_CTRL_SET = CTRL_RSTN;
	//CIA_CTRL_SET = CTRL_RSTN;
	printf("Out of reset!\n");
}


static void CiaInit(void)
{
	CiaReset();
	ReadAllRegisters(0);
	ReadRegister(M6526_ICR);
	ReadAllRegisters(0);
	WriteRegister(M6526_ICR, 0x83);
	ReadAllRegisters(0);
	WriteRegister(M6526_DDRA, 0x5A);
	WriteRegister(M6526_DDRB, 0xFF);
	WriteRegister(M6526_PRB, 0xA6);
	WriteRegister(M6526_DDRB, 0x00);
	WriteRegister(M6526_DDRB, 0xFF);
	WriteRegister(M6526_PRB, 0x6A);
	WriteRegister(M6526_TALO, 0x55);
	WriteRegister(M6526_TAHI, 0x55);
	WriteRegister(M6526_CRA, 0x00); // reset timer a
	WriteRegister(M6526_CRB, 0x00); // reset timer b

	CIA_HS_SET = 0xFF;
	CIA_HS_DIR = 0x12;

	CIA_PB_DIR = 0x00;
	CIA_PB     = 0x00;
}


static void CiaTimerTest1(void)
{
	// This test enables and disables the timer at certain tricky times.
	// Pulse mode output on PB6. Timer B counts events of Timer A.
	CiaInit();
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRA, 0x02); // just output its state
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRA, 0x06); // just output its state in toggle mode
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TALO, 0x0B);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TAHI, 0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TBHI, 1);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TBLO, 0x40);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRB, 0x43); // input from other timer
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRA, 0x06); // 0B = single shot, 03 = Continuous
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRA, 0x07); // 0B = single shot, 03 = Continuous
	int enabled = cycle;
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);

	uint8_t regs[20];

	for(int i=0;i<60;i++) {
		if (cycle == (enabled + 17)) {
			ReadRegister(M6526_ICR); // clear interrupt
		}
		if (cycle == (enabled + 22)) {
			WriteRegister(M6526_CRA, 0x02); // 0B = single shot, 03 = Continuous
		}
		if (cycle == (enabled + 33)) {
			WriteRegister(M6526_CRA, 0x03); // 0B = single shot, 03 = Continuous
		}
		ReadAllRegisters(regs);
		vTaskDelay(1);
	}
	ReadRegister(M6526_ICR);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRB, 0x0B); // count PHI2 pulses
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRB, 0x4B); // count timer A pulses
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
}

static void CiaTimerTest2(void)
{
	uint8_t outmode = 0x06;

	// This test enables and disables the timer at certain tricky times.
	// Pulse mode output on PB6. Timer B counts events of Timer A.
	for(int j=0;j<4;j++) {
		printf("\nStopping timer when it is on %d\n", 3-j);
		CiaInit();
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TALO, 0x0B);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TAHI, 0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TBHI, 1);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TBLO, 0x40);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRB, 0x43); // input from other timer
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRA, outmode | 1); // 0B = single shot, 03 = Continuous
		cycle = 0;
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);

		uint8_t regs[20];

		for(int i=0;i<60;i++) {
			if (cycle == 17) {
				ReadRegister(M6526_ICR); // clear interrupt
			}
			if (cycle == (21 + j)) {
				WriteRegister(M6526_CRA, outmode); // stop
			}
			if (cycle == 33) {
				WriteRegister(M6526_CRA, outmode | 1); // reenable
			}
			ReadAllRegisters(regs);
			vTaskDelay(1);
		}
		ReadRegister(M6526_ICR);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRA, 0x00); // Turn off Timer A
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
	}
}

static void CiaTimerTest3(void)
{
	uint8_t outmode = 0x12;

	// This test acknowledges the timer interrupts at certain tricky times.
	// Pulse mode output on PB6. Timer B counts events of Timer A.
	for(int j=0;j<4;j++) {
		printf("\nAcking when timer is on %d\n", 3-j);
		CiaInit();
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TALO, 0x0B);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TAHI, 0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TBHI, 1);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TBLO, 0x40);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRB, 0x43); // input from other timer
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRA, outmode | 1); // 0B = single shot, 03 = Continuous
		cycle = 0;
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);

		uint8_t regs[20];

		for(int i=0;i<50;i++) {
			if (cycle == (11 + j)) {
				ReadRegister(M6526_ICR); // clear interrupt
			}
			ReadAllRegisters(regs);
			vTaskDelay(1);
		}
		ReadRegister(M6526_ICR);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRA, 0x00); // Turn off Timer A
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
	}
}

static void CiaTimerTest4(void)
{
	uint8_t outmode = 0x02;

	// This test cycles through irq acknowledge times when timer value = 1
	// Pulse mode output on PB6. Timer B counts events of Timer A.
	for(int j=0;j<4;j++) {
		CiaInit();
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TALO, 0x01);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TAHI, 0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TBHI, 1);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TBLO, 0x40);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRB, 0x43); // input from other timer
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRA, outmode | 1); // 0B = single shot, 03 = Continuous
		cycle = 0;
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);

		uint8_t regs[20];

		for(int i=0;i<50;i++) {
			if (cycle == (11 + j)) {
				ReadRegister(M6526_ICR); // clear interrupt
			}
			ReadAllRegisters(regs);
			vTaskDelay(1);
		}
		ReadRegister(M6526_ICR);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRA, 0x00); // Turn off Timer A
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
	}
}

static void CiaTimerTest5(void)
{
	uint8_t outmode = 0x06;

	// This test enables and disables the timer at certain tricky times, while
	// Timer value is 1, which is tricky as well.
	for(int j=0;j<4;j++) {
		CiaInit();
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TALO, 0x01);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TAHI, 0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TBHI, 1);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TBLO, 0x40);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRB, 0x43); // input from other timer
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRA, outmode | 1); // 0B = single shot, 03 = Continuous
		cycle = 0;
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);

		uint8_t regs[20];

		for(int i=0;i<60;i++) {
			if (cycle == 17) {
				ReadRegister(M6526_ICR); // clear interrupt
			}
			if (cycle == (21 + j)) {
				WriteRegister(M6526_CRA, outmode); // stop
			}
			if (cycle == 33) {
				WriteRegister(M6526_CRA, outmode | 1); // reenable
			}
			ReadAllRegisters(regs);
			vTaskDelay(1);
		}
		ReadRegister(M6526_ICR);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRA, 0x00); // Turn off Timer A
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
	}
}

static void CiaTimerTest6(void)
{
	uint8_t outmode = 0x06;

	// This test switches between PHI2 and CNT pulses
	for(int j=0;j<4;j++) {
		CiaInit();
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TALO, 0x0B);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TAHI, 0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TBHI, 1);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TBLO, 0x40);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRB, 0x43); // input from other timer
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRA, outmode | 1); // 0B = single shot, 03 = Continuous
		cycle = 0;
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);

		uint8_t regs[20];

		for(int i=0;i<60;i++) {
			if (cycle == 17) {
				ReadRegister(M6526_ICR); // clear interrupt
			}
			if (cycle == (21 + j)) {
				WriteRegister(M6526_CRA, outmode | 1 | 0x20); // CNT pulses
			}
			if (cycle == 33) {
				WriteRegister(M6526_CRA, outmode | 1); // PHI2 pulses
			}
			ReadAllRegisters(regs);
			vTaskDelay(1);
		}
		ReadRegister(M6526_ICR);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRA, 0x00); // Turn off Timer A
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
	}
}

static void CiaTimerTest7(void)
{
	uint8_t outmode = 0x16;

	// This test enables and disables the timer at certain tricky times.
	// Pulse mode output on PB6. Timer B counts events of Timer A.
	for(int j=0;j<4;j++) {
		printf("\Force loading timer when it is on %d\n", 3-j);
		CiaInit();
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TALO, 0x0B);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TAHI, 0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TBHI, 1);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_TBLO, 0x40);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRB, 0x43); // input from other timer
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRA, outmode | 1);
		cycle = 0;
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);

		uint8_t regs[20];

		for(int i=0;i<60;i++) {
			if (cycle == 17) {
				ReadRegister(M6526_ICR); // clear interrupt
			}
			if (cycle == (21 + j)) {
				WriteRegister(M6526_CRA, outmode | 1); // reload
			}
			ReadAllRegisters(regs);
			vTaskDelay(1);
		}
		ReadRegister(M6526_ICR);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRA, 0x00); // Turn off Timer A
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
	}
}

static void CiaSerialTest1(int waitTime)
{
	uint8_t outmode = 0x46; // toggle PB6, SP = output

	// This test transmits a byte on the serial port
	CiaInit();
	WriteRegister(M6526_ICR, 0x7F);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_ICR, 0x88);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadRegister(M6526_ICR);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TALO, 0x07);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TAHI, 0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TBHI, 1);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TBLO, 0x40);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRB, 0x23); // input cnt pulses
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRA, outmode | 1);
	cycle = 0;
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadRegister(M6526_ICR);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);

	uint8_t regs[20];

	while(cycle < waitTime) {
		if (cycle == 17) {
			WriteRegister(M6526_SDR, 0x33); // transmit a byte
		}
		if (cycle == 60) {
			WriteRegister(M6526_SDR, 0x44); // transmit a byte
		}
		if (cycle == 150) {
			ReadRegister(M6526_ICR);
		}
		ReadAllRegisters(regs);
		vTaskDelay(1);
	}
	ReadRegister(M6526_ICR);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRA, 0x07); // Switch to receive mode
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);

	CIA_HS_DIR = 0x12 | HS_CNT | HS_SP;
	PhiCycle();
	PhiCycle();

	uint8_t byte = 0xC9;
	for (int i=0; i < 8; i++) {
		CIA_HS_CLR = HS_CNT;
		if (byte & 0x80)
			CIA_HS_SET = HS_SP;
		else
			CIA_HS_CLR = HS_SP;
		byte <<= 1;
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		CIA_HS_SET = HS_CNT;
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
	}
	ReadAllRegisters(0);
	ReadAllRegisters(0);


	WriteRegister(M6526_CRA, 0x47); // Switch back to transmit mode
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
}


extern "C" {
void main_task(void *context)
{
	CIA_HS_DIR = 0x12; // Set flag, which is connected to CS1 on 6522.
	CIA_HS_SET = 0xFF; // Set flag to high.

	CIA_PB_DIR = 0x00;
	CIA_PB     = 0x00;

	CIA_PA_DIR = 0x00;
	CIA_PA     = 0x00;

	printf("\ec"); // clear

	CiaTimerTest6();
	while(1);
	CiaSerialTest1(250+17);
	while(1);
	int errs[40];
	for(int i=0;i<40;i++) {
		errors = 0;
		CiaSerialTest1(250+i);
		errs[i] = errors;
	}
	for(int i=0;i<40;i++) {
		printf("%d:%d\n", i, errs[i]);
	}
	//ViaTimer2OneShot();
	// ViaTimer2OneShotSerial();
	// ViaTimer2Ext();
	//ViaTimerSerial();
}
}
