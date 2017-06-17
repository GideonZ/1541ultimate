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

static const char **regnames = mos6522_regs;
static int cycle = 0;

static uint8_t ReadRegister(int reg)
{
	CIA_CTRL_CLR = CTRL_PHI2;
	CIA_ADDR = reg;
	CIA_CTRL_CLR = CTRL_CSN;
	CIA_CTRL_SET = CTRL_RWN;
	CIA_CTRL_SET = CTRL_PHI2;
	uint8_t ret;
	printf("%3d: PHI2=1: %B (%b).. ", cycle++, CIA_HS, CIA_PB);
	ret = CIA_DBUS;
	CIA_CTRL_CLR = CTRL_PHI2;
	CIA_CTRL_SET = CTRL_CSN | CTRL_RWN;
	printf("PHI2=0: %B (%b).. ", CIA_HS, CIA_PB);
	printf("Read  %b from %s\n", ret, regnames[reg]);
	return ret;
}

static void ReadAllRegisters(uint8_t *buf)
{
	CIA_ADDR = 0;
	CIA_CTRL_SET = CTRL_RWN | CTRL_CS2;
	CIA_CTRL_CLR = CTRL_CSN;
	CIA_CTRL_SET = CTRL_PHI2;

	for (int i=0; i < 16; i++) {
		CIA_ADDR = i;
		printf("%s: %b, ", regnames[i], CIA_DBUS);
		if (buf) {
			buf[i] = CIA_DBUS;
		}
	}
	printf("RPA: %b. RPB: %b F:%b\n", CIA_PA, CIA_PB, CIA_HS);
	if (buf) {
		buf[16] = CIA_PA;
		buf[17] = CIA_PB;
		buf[18] = CIA_HS;
	}
	CIA_ADDR = 3;
	CIA_CTRL_SET = CTRL_CSN;
}

static void PhiCycle(void)
{
	CIA_CTRL_SET = CTRL_PHI2;
	printf("%3d: PHI2=1: %B (%b).. ", cycle++, CIA_HS, CIA_PB);
	CIA_CTRL_CLR = CTRL_PHI2;
	printf("PHI2=0: %B (%b)..\n", CIA_HS, CIA_PB);
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
	printf("%3d: PHI2=1: %B (%b).. ", cycle++, CIA_HS, CIA_PB);
	CIA_CTRL_CLR = CTRL_PHI2;
	CIA_CTRL_SET = CTRL_CSN;
	CIA_CTRL_SET = CTRL_RWN;
	printf("PHI2=0: %B (%b).. ", CIA_HS, CIA_PB);
	printf("Wrote %b to %s.\n", value, regnames[reg]);
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
static void CiaTimerTest(void)
{
	ReadAllRegisters(0);
	ReadRegister(M6526_ICR);
	ReadAllRegisters(0);
	WriteRegister(M6526_ICR, 0x83);
	ReadAllRegisters(0);
	WriteRegister(M6526_DDRA, 0x5A);
	WriteRegister(M6526_DDRB, 0x00);
	ReadAllRegisters(0);
	WriteRegister(M6526_TALO, 0x30);
	ReadAllRegisters(0);
	WriteRegister(M6526_TAHI, 0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TBHI, 1);
	ReadAllRegisters(0);
	WriteRegister(M6526_TBLO, 0x40);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRA, 0x0B);
	ReadAllRegisters(0);
	// WriteRegister(M6526_CRB, 0x13);
	// ReadAllRegisters(0);

	CIA_PB_DIR = 0x00;
	CIA_PB     = 0x00;

	uint8_t regs[20];

	for(int i=0;i<300;i++) {
		switch(i) {
		case 16:
			WriteRegister(M6526_CRA, 0x02 + 0x08);
			break;
		case 26:
			WriteRegister(M6526_CRA, 0x03 + 0x08);
			break;
		case 36:
			WriteRegister(M6526_CRA, 0x13 + 0x08);
			break;
		default:
			ReadRegister(M6526_DDRA);
		}
/*
		// if (!(regs[18] & HS_IRQ)) {
		if ((regs[M6526_TALO] == 1) && (regs[M6526_TAHI] == 0)) {
			PhiCycle();
			ReadAllRegisters(regs);
			WriteRegister(M6526_CRA, 0x13);
		} else {
			PhiCycle();
		}
		// ReadRegister(M6526_TALO);
		i ++;
*/
		ReadAllRegisters(regs);
		vTaskDelay(20);
	}
}


extern "C" {
void main_task(void *context)
{
	CIA_HS_DIR = 0x10; // Set flag, which is connected to CS1 on 6522.
	CIA_HS_SET = 0x10; // Set flag to high.

	CIA_PB_DIR = 0x00;
	CIA_PB     = 0x00;

	CIA_PA_DIR = 0x00;
	CIA_PA     = 0x00;

	CIA_CTRL_SET = CTRL_CSN | CTRL_RWN | CTRL_PHI2 | CTRL_CS2;
	CIA_CTRL_CLR = CTRL_PHI2;
	CIA_CTRL_CLR = CTRL_RSTN;
	printf("\ecReset!\n");
	for(int i=0; i < 2; i++) {
		CIA_CTRL_SET = CTRL_PHI2;
		CIA_CTRL_CLR = CTRL_PHI2;
	}
	CIA_CTRL_SET = CTRL_RSTN;
	printf("Reset!\n");
	//CIA_CTRL_SET = CTRL_RSTN;
	printf("Out of reset!\n");

	// CiaTimerTest();
	//ViaTimer2OneShot();
	ViaTimer2OneShotSerial();
	// ViaTimer2Ext();
	//ViaTimerSerial();
}
}
