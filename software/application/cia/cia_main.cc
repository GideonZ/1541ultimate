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

static void Tod(void)
{
	if ((cycle / 9852) & 1) {
		CIA_HS_SET = HS_TOD;
	} else {
		CIA_HS_CLR = HS_TOD;
	}
}

static uint8_t ReadRegister(int reg)
{
	CIA_CTRL_CLR = CTRL_PHI2;
	CIA_ADDR = reg;
	CIA_CTRL_CLR = CTRL_CSN;
	CIA_CTRL_SET = CTRL_RWN;
	CIA_CTRL_SET = CTRL_PHI2;
	uint8_t ret;
	int err = 0;
	printf("%3d: PHI2=1: %B (%b) [%B (%b)].. ", cycle++, CIA_HS, CIA_PB, MINE_HS, MINE_PB);
	if (MINE_HS != CIA_HS) {
		err ++;
	}
	if (MINE_PB != CIA_PB) {
		err ++;
	}
	ret = CIA_DBUS;
	uint8_t mine = MINE_DB;

	CIA_ADDR = 10;
	printf("%b:", CIA_DBUS);
	CIA_ADDR = 9;
	printf("%b:", CIA_DBUS);
	CIA_ADDR = 8;
	printf("%b ", CIA_DBUS);


	CIA_CTRL_CLR = CTRL_PHI2;
	CIA_CTRL_SET = CTRL_CSN | CTRL_RWN;
	printf("PHI2=0: %B (%b) [%B (%b)].. ", CIA_HS, CIA_PB, MINE_HS, MINE_PB);
	if (MINE_HS != CIA_HS) {
		err ++;
	}
	if (MINE_PB != CIA_PB) {
		err ++;
	}
	if (mine != ret) {
		err ++;
	}
	printf("Read  (%b|%b) from %s ", ret, mine, regnames[reg]);
	if(err) {
		printf("ERRR\n");
		errors ++;
	} else {
		printf("\n");
	}
	Tod();
	return ret;
}

static uint8_t ReadRegisterQuiet(int reg)
{
	CIA_CTRL_CLR = CTRL_PHI2;
	CIA_ADDR = reg;
	CIA_CTRL_CLR = CTRL_CSN;
	CIA_CTRL_SET = CTRL_RWN;
	CIA_CTRL_SET = CTRL_PHI2;
	cycle++;
	uint8_t ret;
	ret = CIA_DBUS;
	CIA_CTRL_CLR = CTRL_PHI2;
	CIA_CTRL_SET = CTRL_CSN | CTRL_RWN;
	Tod();
	return ret;
}

static void ReadAllRegisters(uint8_t *buf)
{
	uint16_t mask = 0xF0F3;
	CIA_ADDR = 0;
	CIA_CTRL_SET = CTRL_RWN | CTRL_CS2;
	CIA_CTRL_CLR = CTRL_CSN;
	CIA_CTRL_SET = CTRL_PHI2;

	printf("%3d|", cycle);
	cycle++;
	int err = 0;
	for (int i=0; i < 16; i++) {
		if (mask & (1 << i)) {
			CIA_ADDR = i;
			printf("%s:%b|%b, ", regnames[i], CIA_DBUS, MINE_DB);
			if (MINE_DB != CIA_DBUS) {
				err ++;
			}
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
	Tod();
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
	Tod();
}

static void PhiCycleS(void)
{
	CIA_CTRL_SET = CTRL_PHI2;
	CIA_CTRL_CLR = CTRL_PHI2;

	int err = 0;
	if (MINE_HS != CIA_HS) {
		err ++;
	}
	if(err) {
		printf("%3d: {%B %B} ERR\n", cycle, CIA_HS, MINE_HS);
		errors ++;
	}
	cycle++;
	Tod();
}

static void WriteRegister(int reg, uint8_t value)
{
	CIA_CTRL_CLR = CTRL_PHI2;

	CIA_CTRL_SET = CTRL_PHI2;
	CIA_CTRL_CLR = CTRL_CSN;
	int err = 0;
	printf("%3d: PHI2=1: %B (%b) [%B (%b)].. ", cycle++, CIA_HS, CIA_PB, MINE_HS, MINE_PB);
	if (CIA_HS != MINE_HS) err++;
	if (CIA_PB != MINE_PB) err++;

	CIA_ADDR = 10;
	printf("%b:", CIA_DBUS);
	CIA_ADDR = 9;
	printf("%b:", CIA_DBUS);
	CIA_ADDR = 8;
	printf("%b ", CIA_DBUS);

	CIA_ADDR = reg;
	CIA_DBUS = value;
	CIA_CTRL_CLR = CTRL_CSN | CTRL_RWN;
	CIA_CTRL_CLR = CTRL_CSN | CTRL_RWN;

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
	Tod();
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
	CIA_HS_SET = 0xFF;
	CIA_HS_DIR = 0x12;

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
	WriteRegister(M6526_PRB, 0x0A);
	WriteRegister(M6526_TALO, 0x55);
	WriteRegister(M6526_TAHI, 0x55);
	WriteRegister(M6526_CRA, 0x00); // reset timer a
	WriteRegister(M6526_CRB, 0x00); // reset timer b

	//WriteRegister(M6526_TODH, 0x01); // tod hours
	//WriteRegister(M6526_TODM, 0x23);
	//WriteRegister(M6526_TODS, 0x45);
	//WriteRegister(M6526_TODT, 0x06);
	//ReadAllRegisters(0);
	ReadRegister(M6526_TODT);

	errors = 0;

	CIA_PB_DIR = 0x00;
	CIA_PB     = 0x00;
}

static void CiaTodTest1(void)
{
	CiaInit();
	WriteRegister(M6526_CRA, 0x80); // 50 Hz TOD mode
	ReadAllRegisters(0);
	cycle = 0;

	PhiCycleS();
	uint8_t h = ReadRegisterQuiet(M6526_TODH);
	PhiCycleS();
	uint8_t m = ReadRegisterQuiet(M6526_TODM);
	PhiCycleS();
	uint8_t s = ReadRegisterQuiet(M6526_TODS);
	PhiCycleS();
	uint8_t t = ReadRegisterQuiet(M6526_TODT);
	printf("%b:%b:%b:%b <=\n", h, m, s, t);
	ReadRegister(M6526_TODT);

	CIA_HS_CLR = 0x02;
	PhiCycleS();
	PhiCycleS();
	PhiCycleS();
	PhiCycleS();
	PhiCycleS();

	// Check the effect of reading hour register on the TOD output
	ReadAllRegisters(0);
	for (int i=0;i<27;i++) {
		CIA_HS_SET = 0x02;
		for (int j=0;j<20;j++) {
			if (cycle == 788) {
				ReadAllRegisters(0);
			} else {
				PhiCycleS();
			}
		}
		CIA_HS_CLR = 0x02;
		for (int j=0;j<20;j++) {
			if (cycle == 788) {
				ReadAllRegisters(0);
			} else {
				PhiCycleS();
			}
		}
	}
	ReadAllRegisters(0);


/*

	CIA_HS_SET = 0x02;
	PhiCycleS();
	PhiCycleS();
	PhiCycleS();
	PhiCycleS();
	PhiCycleS();


	for (int i=0;i<20;i++) {
		for (int j=0;j<50;j++) {
			CIA_HS_SET = 0x02;
			PhiCycleS();
			PhiCycleS();
			PhiCycleS();
			PhiCycleS();
			PhiCycleS();
			CIA_HS_CLR = 0x02;
			PhiCycleS();
			PhiCycleS();
			PhiCycleS();
			PhiCycleS();
			PhiCycleS();
		}
		ReadAllRegisters(0);
*/		{
			uint8_t h = ReadRegisterQuiet(M6526_TODH);
			uint8_t m = ReadRegisterQuiet(M6526_TODM);
			uint8_t s = ReadRegisterQuiet(M6526_TODS);
			uint8_t t = ReadRegisterQuiet(M6526_TODT);
			printf("%b:%b:%b:%b  ", h, m, s, t);
		}
		ReadAllRegisters(0);
		{
			uint8_t h = ReadRegisterQuiet(M6526_TODH);
			uint8_t m = ReadRegisterQuiet(M6526_TODM);
			uint8_t s = ReadRegisterQuiet(M6526_TODS);
			uint8_t t = ReadRegisterQuiet(M6526_TODT);
			printf("%b:%b:%b:%b\n", h, m, s, t);
		}
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
		printf("Force loading timer when it is on %d\n", 3-j);
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

static void CiaTimerTest8(void)
{
	uint8_t outmode = 0x16;

	// This test loads the timer with 0 and observes the result.
	// Pulse mode output on PB6. Timer B counts events of Timer A.
	CiaInit();
	CIA_HS_SET = HS_CNT;
	CIA_HS_DIR = 0x12 | HS_CNT;

	cycle = 0;
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TALO, 0x0B);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TAHI, 0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TBLO, 0x40);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TBHI, 1);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRB, 0x43); // input from other timer
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TALO, 0x05);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRA, 0x02); // turn on PB6
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRA, 0x12); // Force load with.. 0005?
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TALO, 0x00);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRA, 0x12); // Force load with.. 0000?
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRA, 0x13); // now start! but not really, because we switch to CNT pulses
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	CIA_HS_CLR = HS_CNT;
	ReadAllRegisters(0);
	CIA_HS_SET = HS_CNT;
	WriteRegister(M6526_TALO, 0x03);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	CIA_HS_CLR = HS_CNT;
	ReadAllRegisters(0);
	CIA_HS_SET = HS_CNT;
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TALO, 0x00);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRA, 0x03);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TALO, 0x03);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
}

static void CiaTimerTest9(void)
{
	// This test loads the timer with 0 to cause a constant trigger
	// on the output. Then, the interrupts are enabled, acknowledged
	// and disabled.
	// Pulse mode output on PB6. Timer B counts events of Timer A.
	CiaInit();
	cycle = 0;
	WriteRegister(M6526_ICR, 0x7F);
	ReadAllRegisters(0);
	ReadRegister(M6526_ICR);
	ReadAllRegisters(0);
	WriteRegister(M6526_TALO, 0x00);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TAHI, 0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TBLO, 0x40);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TBHI, 1);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRB, 0x43); // input from other timer
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRA, 0x02); // turn on PB6
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRA, 0x13); // Start!
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_ICR, 0x81); // Enable interrupt
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadRegister(M6526_ICR); // ACKnowledge
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRA, 0x00); // stop timer
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadRegister(M6526_ICR); // ACKnowledge
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	WriteRegister(M6526_ICR, 0x01); // Disable interrupt
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadRegister(M6526_ICR); // ACKnowledge
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	ReadAllRegisters(0);
}

static void CiaTimerTestWilfred(void)
{
	// This test loads the timer with 10 and does an "INC DD0D,X" at
	// various offsets from the start of the timer.
	CiaInit();
	cycle = 0;
	WriteRegister(M6526_ICR, 0x7F);
	ReadAllRegisters(0);
	ReadRegister(M6526_ICR);
	ReadAllRegisters(0);
	WriteRegister(M6526_TALO, 0x0A);
	ReadAllRegisters(0);
	WriteRegister(M6526_TAHI, 0);
	ReadAllRegisters(0);
	WriteRegister(M6526_TBLO, 0x40);
	ReadAllRegisters(0);
	WriteRegister(M6526_TBHI, 1);
	ReadAllRegisters(0);
	WriteRegister(M6526_CRB, 0x43); // input from other timer
	ReadAllRegisters(0);
	WriteRegister(M6526_CRA, 0x02); // turn on PB6
	ReadAllRegisters(0);
	WriteRegister(M6526_ICR, 0x81); // Enable interrupt
	ReadAllRegisters(0);
	ReadAllRegisters(0);
	for (int i=5; i<15; i++) {
		printf("i = %d\n", i);
		WriteRegister(M6526_ICR, 0x81); // Enable interrupt
		ReadAllRegisters(0);
		WriteRegister(M6526_CRA, 0x13); // Start with force load
		for(int j=0;j<i;j++) {
			ReadAllRegisters(0);
		}
		ReadRegister(M6526_ICR); // ACKnowledge
		ReadRegister(M6526_ICR); // ACKnowledge
		WriteRegister(M6526_ICR, 0x00);
		WriteRegister(M6526_ICR, 0x01); // disable

		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		ReadAllRegisters(0);
		WriteRegister(M6526_CRA, 0x00); // Stop
		ReadRegister(M6526_ICR); // ACKnowledge
	}
}

static void CiaTimerTestGroupaz(void)
{
/*
# * 20263783200 ns:0 Writing CIA_2 Reg 0 to D7 in cycle 21108106
# * 20263947360 ns:0 Writing CIA_2 Reg D to 7F in cycle 21108277  0
# * 20263953120 ns:0 Writing CIA_2 Reg D to 81 in cycle 21108283  6
# * 20263977120 ns:0 Writing CIA_2 Reg E to 00 in cycle 21108308 31
# * 20263980960 ns:0 Writing CIA_2 Reg 5 to 00 in cycle 21108312 35
# * 20264011680 ns:0 Writing CIA_2 Reg 4 to 0A in cycle 21108344 67
# * 20264017440 ns:0 Writing CIA_2 Reg E to 10 in cycle 21108350 73
# * 20264025120 ns:0 Writing CIA_2 Reg E to 19 in cycle 21108358 81
# * 20264032800 ns:0 Writing CIA_2 Reg 4 to 14 in cycle 21108366 89
# * 20264036640 ns:0 Writing CIA_2 Reg E to 01 in cycle 21108370 93

 */
	CiaInit();
	cycle = 0;
	while(1) {
		switch(cycle) {
		case 0:
			WriteRegister(M6526_ICR, 0x7F);
			break;
		case 6:
			WriteRegister(M6526_ICR, 0x81);
			break;
		case 31:
			WriteRegister(M6526_CRA, 0x00);
			break;
		case 35:
			WriteRegister(M6526_TAHI, 0x00);
			break;
		case 67:
			WriteRegister(M6526_TALO, 0x0A);
			break;
		case 73:
			WriteRegister(M6526_CRA, 0x10);
			break;
		case 81:
			WriteRegister(M6526_CRA, 0x19);
			break;
		case 89:
			WriteRegister(M6526_TALO, 0x14);
			break;
		case 93:
			WriteRegister(M6526_CRA, 0x01);
			break;
		case 120:
			return;
		default:
			ReadAllRegisters(0);
		}
	}
}

static void CiaTimerTestGroupaz2(void)
{
/*
Reg	D	to	7F	in	cycle	21087738
Reg	D	to	81	in	cycle	21087744	6
Reg	E	to	0	in	cycle	21087769	31
Reg	5	to	0	in	cycle	21087773	35
Reg	4	to	0A	in	cycle	21087805	67
Reg	E	to	10	in	cycle	21087811	73
Reg	E	to	11	in	cycle	21087819	81
Reg	4	to	14	in	cycle	21087827	89
Reg	E	to	9	in	cycle	21087831	93
 */
	CiaInit();
	cycle = 0;
	while(1) {
		switch(cycle) {
		case 0:
			WriteRegister(M6526_ICR, 0x7F);
			break;
		case 6:
			WriteRegister(M6526_ICR, 0x81);
			break;
		case 31:
			WriteRegister(M6526_CRA, 0x00);
			break;
		case 35:
			WriteRegister(M6526_TAHI, 0x00);
			break;
		case 67:
			WriteRegister(M6526_TALO, 0x0A);
			break;
		case 73:
			WriteRegister(M6526_CRA, 0x10);
			break;
		case 81:
			WriteRegister(M6526_CRA, 0x11);
			break;
		case 89:
			WriteRegister(M6526_TALO, 0x14);
			break;
		case 93:
			WriteRegister(M6526_CRA, 0x09);
			break;
		case 120:
			return;
		default:
			ReadAllRegisters(0);
		}
	}
}

typedef struct {
	int cycle;
	bool read;
	uint8_t reg;
	uint8_t wdata;
} t_access;

static t_access icr_test[] = {
		{ 0, false, 0xE, 0x0 },
		{ 4, false, 0x4, 0x0 },
		{ 8, false, 0x5, 0x0 },
		{ 14, false, 0xD, 0x7F },
		{ 18, true, 0xD, 0x0 },
		{ 24, false, 0xE, 0x11 },
		{ 30, false, 0xD, 0x81 },
		{ 38, false, 0xD, 0x7F },
		{ 42, true, 0xD, 0x0 },
		{ 60, false, 0xE, 0x0 },
		{ 64, false, 0x4, 0x1 },
		{ 68, false, 0x5, 0x0 },
		{ 74, false, 0xD, 0x7F },
		{ 78, true, 0xD, 0x0 },
		{ 84, false, 0xE, 0x11 },
		{ 90, false, 0xD, 0x81 },
		{ 98, false, 0xD, 0x7F },
		{ 102, true, 0xD, 0x0 },
		{ 120, false, 0xE, 0x0 },
		{ 124, false, 0x4, 0x2 },
		{ 128, false, 0x5, 0x0 },
		{ 134, false, 0xD, 0x7F },
		{ 138, true, 0xD, 0x0 },
		{ 144, false, 0xE, 0x11 },
		{ 150, false, 0xD, 0x81 },
		{ 158, false, 0xD, 0x7F },
		{ 162, true, 0xD, 0x0 },
		{ 180, false, 0xE, 0x0 },
		{ 184, false, 0x4, 0x3 },
		{ 188, false, 0x5, 0x0 },
		{ 194, false, 0xD, 0x7F },
		{ 198, true, 0xD, 0x0 },
		{ 204, false, 0xE, 0x11 },
		{ 210, false, 0xD, 0x81 },
		{ 218, false, 0xD, 0x7F },
		{ 222, true, 0xD, 0x0 },
		{ 240, false, 0xE, 0x0 },
		{ 244, false, 0x4, 0x4 },
		{ 248, false, 0x5, 0x0 },
		{ 254, false, 0xD, 0x7F },
		{ 258, true, 0xD, 0x0 },
		{ 264, false, 0xE, 0x11 },
		{ 270, false, 0xD, 0x81 },
		{ 278, false, 0xD, 0x7F },
		{ 282, true, 0xD, 0x0 },
		{ 300, false, 0xE, 0x0 },
		{ 304, false, 0x4, 0x5 },
		{ 308, false, 0x5, 0x0 },
		{ 314, false, 0xD, 0x7F },
		{ 318, true, 0xD, 0x0 },
		{ 324, false, 0xE, 0x11 },
		{ 330, false, 0xD, 0x81 },
		{ 338, false, 0xD, 0x7F },
		{ 342, true, 0xD, 0x0 },
		{ 360, false, 0xE, 0x0 },
		{ 364, false, 0x4, 0x6 },
		{ 368, false, 0x5, 0x0 },
		{ 374, false, 0xD, 0x7F },
		{ 378, true, 0xD, 0x0 },
		{ 384, false, 0xE, 0x11 },
		{ 390, false, 0xD, 0x81 },
		{ 398, false, 0xD, 0x7F },
		{ 402, true, 0xD, 0x0 },
		{ 420, false, 0xE, 0x0 },
		{ 424, false, 0x4, 0x7 },
		{ 428, false, 0x5, 0x0 },
		{ 434, false, 0xD, 0x7F },
		{ 438, true, 0xD, 0x0 },
		{ 444, false, 0xE, 0x11 },
		{ 450, false, 0xD, 0x81 },
		{ 458, false, 0xD, 0x7F },
		{ 462, true, 0xD, 0x0 },
		{ 480, false, 0xE, 0x0 },
		{ 484, false, 0x4, 0x8 },
		{ 488, false, 0x5, 0x0 },
		{ 494, false, 0xD, 0x7F },
		{ 498, true, 0xD, 0x0 },
		{ 504, false, 0xE, 0x11 },
		{ 510, false, 0xD, 0x81 },
		{ 518, false, 0xD, 0x7F },
		{ 522, true, 0xD, 0x0 },
		{ 540, false, 0xE, 0x0 },
		{ 544, false, 0x4, 0x9 },
		{ 548, false, 0x5, 0x0 },
		{ 554, false, 0xD, 0x7F },
		{ 558, true, 0xD, 0x0 },
		{ 564, false, 0xE, 0x11 },
		{ 570, false, 0xD, 0x81 },
		{ 578, false, 0xD, 0x7F },
		{ 582, true, 0xD, 0x0 },
		{ 600, false, 0xE, 0x0 },
		{ 604, false, 0x4, 0x0A },
		{ 608, false, 0x5, 0x0 },
		{ 614, false, 0xD, 0x7F },
		{ 618, true, 0xD, 0x0 },
		{ 624, false, 0xE, 0x11 },
		{ 630, false, 0xD, 0x81 },
		{ 638, false, 0xD, 0x7F },
		{ 642, true, 0xD, 0x0 },
		{ 660, false, 0xE, 0x0 },
		{ 664, false, 0x4, 0x0B },
		{ 668, false, 0x5, 0x0 },
		{ 674, false, 0xD, 0x7F },
		{ 678, true, 0xD, 0x0 },
		{ 684, false, 0xE, 0x11 },
		{ 690, false, 0xD, 0x81 },
		{ 698, false, 0xD, 0x7F},
		{ 702, true, 0xD, 0x0 },
		{ 720, false, 0xE, 0x0 },
		{ -1 , false, 0xE, 0x0 }, // inserted
		{ 724, false, 0x4, 0x0C },
		{ 728, false, 0x5, 0x0 },
		{ 734, false, 0xD, 0x7F },
		{ 738, true, 0xD, 0x0 },
		{ 744, false, 0xE, 0x11 },
		{ 750, false, 0xD, 0x81 },
		{ 758, false, 0xD, 0x7F },
		{ 762, true, 0xD, 0x0 },
		{ 780, false, 0xE, 0x0 },
		{ 784, false, 0x4, 0x0D },
		{ 788, false, 0x5, 0x0 },
		{ 794, false, 0xD, 0x7F },
		{ 798, true, 0xD, 0x0 },
		{ 804, false, 0xE, 0x11 },
		{ 810, false, 0xD, 0x81 },
		{ 818, false, 0xD, 0x7F },
		{ 822, true, 0xD, 0x0 },
		{ 840, false, 0xE, 0x0 },
		{ 844, false, 0x4, 0x0E },
		{ 848, false, 0x5, 0x0 },
		{ 854, false, 0xD, 0x7F },
		{ 858, true, 0xD, 0x0 },
		{ 864, false, 0xE, 0x11 },
		{ 870, false, 0xD, 0x81 },
		{ 878, false, 0xD, 0x7F },
		{ 882, true, 0xD, 0x0 },
		{ 900, false, 0xE, 0x0 },
		{ 904, false, 0x4, 0x0F },
		{ 908, false, 0x5, 0x0 },
		{ 914, false, 0xD, 0x7F },
		{ 918, true, 0xD, 0x0 },
		{ 924, false, 0xE, 0x11 },
		{ 930, false, 0xD, 0x81 },
		{ 938, false, 0xD, 0x7F },
		{ 942, true, 0xD, 0x0 },
		{ 960, false, 0xE, 0x0 },
		{ 964, false, 0x4, 0x10 },
		{ 968, false, 0x5, 0x0 },
		{ 974, false, 0xD, 0x7F },
		{ 978, true, 0xD, 0x0 },
		{ 984, false, 0xE, 0x11 },
		{ 990, false, 0xD, 0x81 },
		{ 998, false, 0xD, 0x7F },
		{ 1002, true, 0xD, 0x0 },
		{ 1020, false, 0xE, 0x0 },
		{ 1024, false, 0x4, 0x11 },
		{ 1028, false, 0x5, 0x0 },
		{ 1034, false, 0xD, 0x7F },
		{ 1038, true, 0xD, 0x0 },
		{ 1044, false, 0xE, 0x11 },
		{ 1050, false, 0xD, 0x81 },
		{ 1058, false, 0xD, 0x7F },
		{ 1062, true, 0xD, 0x0 },
		{ 1080, false, 0xE, 0x0 },
		{ 1084, false, 0x4, 0x12 },
		{ 1088, false, 0x5, 0x0 },
		{ 1094, false, 0xD, 0x7F },
		{ 1098, true, 0xD, 0x0 },
		{ 1104, false, 0xE, 0x11 },
		{ 1110, false, 0xD, 0x81 },
		{ 1118, false, 0xD, 0x7F },
		{ 1122, true, 0xD, 0x0 },
		{ 1140, false, 0xE, 0x0 },
		{ 1144, false, 0x4, 0x13 },
		{ 1148, false, 0x5, 0x0 },
		{ 1154, false, 0xD, 0x7F },
		{ 1158, true, 0xD, 0x0 },
		{ 1164, false, 0xE, 0x11 },
		{ 1170, false, 0xD, 0x81 },
		{ 1178, false, 0xD, 0x7F },
		{ 1182, true, 0xD, 0x0 },
		{ 1200, false, 0xE, 0x0 },
		{ 1204, false, 0x4, 0x14 },
		{ 1208, false, 0x5, 0x0 },
		{ 1214, false, 0xD, 0x7F },
		{ 1218, true, 0xD, 0x0 },
		{ 1224, false, 0xE, 0x11 },
		{ 1230, false, 0xD, 0x81 },
		{ 1238, false, 0xD, 0x7F },
		{ 1242, true, 0xD, 0x0 },
		{ 1260, false, 0xE, 0x0 },
		{ 1264, false, 0x4, 0x15 },
		{ 1268, false, 0x5, 0x0 },
		{ 1274, false, 0xD, 0x7F },
		{ 1278, true, 0xD, 0x0 },
		{ 1284, false, 0xE, 0x11 },
		{ 1290, false, 0xD, 0x81 },
		{ 1298, false, 0xD, 0x7F },
		{ 1302, true, 0xD, 0x0 },
		{ 1320, false, 0xE, 0x0 },
		{ 1324, false, 0x4, 0x16 },
		{ 1328, false, 0x5, 0x0 },
		{ 1334, false, 0xD, 0x7F },
		{ 1338, true, 0xD, 0x0 },
		{ 1344, false, 0xE, 0x11 },
		{ 1350, false, 0xD, 0x81 },
		{ 1358, false, 0xD, 0x7F },
		{ 1362, true, 0xD, 0x0 },
		{ 1380, false, 0xE, 0x0 },
		{ 1384, false, 0x4, 0x17 },
		{ 1388, false, 0x5, 0x0 },
		{ 1394, false, 0xD, 0x7F },
		{ 1398, true, 0xD, 0x0 },
		{ 1404, false, 0xE, 0x11 },
		{ 1410, false, 0xD, 0x81 },
		{ 1418, false, 0xD, 0x7F },
		{ 1422, true, 0xD, 0x0 },
		{ 1440, false, 0xE, 0x0 },
		{ 1444, false, 0x4, 0x18 },
		{ 1448, false, 0x5, 0x0 },
		{ 1454, false, 0xD, 0x7F },
		{ 1458, true, 0xD, 0x0 },
		{ 1464, false, 0xE, 0x11 },
		{ 1470, false, 0xD, 0x81 },
		{ 1478, false, 0xD, 0x7F },
		{ 1482, true, 0xD, 0x0 },
		{ 1500, false, 0xE, 0x0 },
		{ 1504, false, 0x4, 0x19 },
		{ 1508, false, 0x5, 0x0 },
		{ 1514, false, 0xD, 0x7F },
		{ 1518, true, 0xD, 0x0 },
		{ 1524, false, 0xE, 0x11 },
		{ 1530, false, 0xD, 0x81 },
		{ 1538, false, 0xD, 0x7F },
		{ 1542, true, 0xD, 0x0 },
		{ 1560, false, 0xE, 0x0 },
		{ 1564, false, 0x4, 0x1A },
		{ 1568, false, 0x5, 0x0 },
		{ 1574, false, 0xD, 0x7F },
		{ 1578, true, 0xD, 0x0 },
		{ 1584, false, 0xE, 0x11 },
		{ 1590, false, 0xD, 0x81 },
		{ 1598, false, 0xD, 0x7F },
		{ 1602, true, 0xD, 0x0 },
		{ 1620, false, 0xE, 0x0 },
		{ 1624, false, 0x4, 0x1B },
		{ 1628, false, 0x5, 0x0 },
		{ 1634, false, 0xD, 0x7F },
		{ 1638, true, 0xD, 0x0 },
		{ 1644, false, 0xE, 0x11 },
		{ 1650, false, 0xD, 0x81 },
		{ 1658, false, 0xD, 0x7F },
		{ 1662, true, 0xD, 0x0 },
		{ 1680, false, 0xE, 0x0 },
		{ 1684, false, 0x4, 0x1C },
		{ 1688, false, 0x5, 0x0 },
		{ 1694, false, 0xD, 0x7F },
		{ 1698, true, 0xD, 0x0 },
		{ 1704, false, 0xE, 0x11 },
		{ 1710, false, 0xD, 0x81 },
		{ 1718, false, 0xD, 0x7F },
		{ 1722, true, 0xD, 0x0 },
		{ 1740, false, 0xE, 0x0 },
		{ 1744, false, 0x4, 0x1D },
		{ 1748, false, 0x5, 0x0 },
		{ 1754, false, 0xD, 0x7F },
		{ 1758, true, 0xD, 0x0 },
		{ 1764, false, 0xE, 0x11 },
		{ 1770, false, 0xD, 0x81 },
		{ 1778, false, 0xD, 0x7F },
		{ 1782, true, 0xD, 0x0 },
		{ 1800, false, 0xE, 0x0 },
		{ 1804, false, 0x4, 0x1E },
		{ 1808, false, 0x5, 0x0 },
		{ 1814, false, 0xD, 0x7F },
		{ 1818, true, 0xD, 0x0 },
		{ 1824, false, 0xE, 0x11 },
		{ 1830, false, 0xD, 0x81 },
		{ 1838, false, 0xD, 0x7F },
		{ 1842, true, 0xD, 0x0 },
		{ 1860, false, 0xE, 0x0 },
		{ 1864, false, 0x4, 0x1F },
		{ 1868, false, 0x5, 0x0 },
		{ 1874, false, 0xD, 0x7F },
		{ 1878, true, 0xD, 0x0 },
		{ 1884, false, 0xE, 0x11 },
		{ 1890, false, 0xD, 0x81 },
		{ 1898, false, 0xD, 0x7F },
		{ 1902, true, 0xD, 0x0 },
		{ 1920, false, 0xE, 0x0 },
		{ 1924, false, 0x4, 0x20 },
		{ 1928, false, 0x5, 0x0 },
		{ 1934, false, 0xD, 0x7F },
		{ 1938, true, 0xD, 0x0 },
		{ 1944, false, 0xE, 0x11 },
		{ 1950, false, 0xD, 0x81 },
		{ 1958, false, 0xD, 0x7F },
		{ 1962, true, 0xD, 0x0 },
		{ 1980, false, 0xE, 0x0 },
		{ 1984, false, 0x4, 0x21 },
		{ 1988, false, 0x5, 0x0 },
		{ 1994, false, 0xD, 0x7F },
		{ 1998, true, 0xD, 0x0 },
		{ 2004, false, 0xE, 0x11 },
		{ 2010, false, 0xD, 0x81 },
		{ 2018, false, 0xD, 0x7F },
		{ 2022, true, 0xD, 0x0 },
		{ 2040, false, 0xE, 0x0 },
		{ 2044, false, 0x4, 0x22 },
		{ 2048, false, 0x5, 0x0 },
		{ 2054, false, 0xD, 0x7F },
		{ 2058, true, 0xD, 0x0 },
		{ 2064, false, 0xE, 0x11 },
		{ 2070, false, 0xD, 0x81 },
		{ 2078, false, 0xD, 0x7F },
		{ 2082, true, 0xD, 0x0 },
		{ 2100, false, 0xE, 0x0 },
		{ 2104, false, 0x4, 0x23 },
		{ 2108, false, 0x5, 0x0 },
		{ 2114, false, 0xD, 0x7F },
		{ 2118, true, 0xD, 0x0 },
		{ 2124, false, 0xE, 0x11 },
		{ 2130, false, 0xD, 0x81 },
		{ 2138, false, 0xD, 0x7F },
		{ 2142, true, 0xD, 0x0 },
		{ 2160, false, 0xE, 0x0 },
		{ 2164, false, 0x4, 0x24 },
		{ 2168, false, 0x5, 0x0 },
		{ 2174, false, 0xD, 0x7F },
		{ 2178, true, 0xD, 0x0 },
		{ 2184, false, 0xE, 0x11 },
		{ 2190, false, 0xD, 0x81 },
		{ 2198, false, 0xD, 0x7F },
		{ 2202, true, 0xD, 0x0 },
		{ 2220, false, 0xE, 0x0 },
		{ 2224, false, 0x4, 0x25 },
		{ 2228, false, 0x5, 0x0 },
		{ 2234, false, 0xD, 0x7F },
		{ 2238, true, 0xD, 0x0 },
		{ 2244, false, 0xE, 0x11 },
		{ 2250, false, 0xD, 0x81 },
		{ 2258, false, 0xD, 0x7F },
		{ 2262, true, 0xD, 0x0 },
		{ 2280, false, 0xE, 0x0 },
		{ 2284, false, 0x4, 0x26 },
		{ 2288, false, 0x5, 0x0 },
		{ 2294, false, 0xD, 0x7F },
		{ 2298, true, 0xD, 0x0 },
		{ 2304, false, 0xE, 0x11 },
		{ 2310, false, 0xD, 0x81 },
		{ 2318, false, 0xD, 0x7F },
		{ 2322, true, 0xD, 0x0 },
		{ 2340, false, 0xE, 0x0 },
		{ 2344, false, 0x4, 0x27 },
		{ 2348, false, 0x5, 0x0 },
		{ 2354, false, 0xD, 0x7F },
		{ 2358, true, 0xD, 0x0 },
		{ 2364, false, 0xE, 0x11 },
		{ 2370, false, 0xD, 0x81 },
		{ 2378, false, 0xD, 0x7F },
		{ 2382, true, 0xD, 0x0 },
		{ -1, true, 0xD, 0x0 },
};

t_access icr_test2[] = {
		{ 0, false, 0xE, 0x0 },
		{ 6, false, 0xD, 0x7F },
		{ 10, true, 0xD, 0x81 },
		{ 16, false, 0xD, 0x81 },
		{ 24, false, 0x5, 0x0 },
		{ 30, false, 0x4, 0x1 },
		{ 42, false, 0xE, 0xD9 },
		{ 48, false, 0xE, 0xD9 },
		{ 52, true, 0xD, 0x89 },
		{ 65, true, 0xD, 0x0 },
		{ 81, false, 0xE, 0xD9 },
		{ 85, true, 0xD, 0x1 },
		{ 98, true, 0xD, 0x0 },
		{ 114, false, 0xE, 0xD9 },
		{ 118, true, 0xD, 0x1 },
		{ 131, true, 0xD, 0x0 },
		{ 147, false, 0xE, 0xD9 },
		{ 151, true, 0xD, 0x1 },
		{ 164, true, 0xD, 0x0 },
		{ 180, false, 0xE, 0xD9 },
		{ 184, true, 0xD, 0x1 },
		{ 197, true, 0xD, 0x0 },
		{ 213, false, 0xE, 0xD9 },
		{ 217, true, 0xD, 0x1 },
		{ 230, true, 0xD, 0x0 },
		{ 246, false, 0xE, 0xD9 },
		{ 250, true, 0xD, 0x1 },
		{ 263, true, 0xD, 0x0 },
		{ -1, true, 0xD, 0x0 },
};

const t_access cia_timer_oldcias[] = {
  {      93, false, 0xE, 0x00 },
  {      97, false, 0xF, 0x00 },
  {     105, true,  0xD, 0x00 },
  {     319, false, 0xD, 0x7F },
  {     339, true,  0xD, 0x00 },
  {   19737, true,  0xD, 0x00 },
  {   19751, false, 0x4, 0x00 },
  {   19755, false, 0x6, 0x00 },
  {   19769, false, 0x5, 0x01 },
  {   19773, false, 0x7, 0x01 },
  {   19777, true,  0x4, 0x00 },
  {   19778, false, 0x4, 0x00 },
  {   19779, false, 0x4, 0x01 },
  {   19789, false, 0xE, 0x11 },
  {   19940, true,  0x4, 0x6D },
  {   20198, true,  0x4, 0x6D },
  {   20456, true,  0x4, 0x6D },
  {   20714, true,  0x4, 0x6D },
  {   20972, true,  0x4, 0x6D },
  {   21230, true,  0x4, 0x6D },
  {   21488, true,  0x4, 0x6D },
  {   21746, true,  0x4, 0x6D },
  {   22004, true,  0x4, 0x6D },
  {   22262, true,  0x4, 0x6D },
  {   22520, true,  0x4, 0x6D },
  {   22778, true,  0x4, 0x6D },
  {   23036, true,  0x4, 0x6D },
  {   23294, true,  0x4, 0x6D },
  {   23552, true,  0x4, 0x6D },
  {   23810, true,  0x4, 0x6D },
  {   23953, false, 0xE, 0x00 },
  {   23957, false, 0xF, 0x00 },
  {   23965, true,  0xD, 0x01 },
  {   24179, false, 0xD, 0x7F },
  {   24199, true,  0xD, 0x00 },
  {   39397, true,  0xD, 0x00 },
  {   39411, false, 0x4, 0x00 },
  {   39415, false, 0x6, 0x00 },
  {   39429, false, 0x5, 0x01 },
  {   39433, false, 0x7, 0x01 },
  {   39437, true,  0x4, 0x00 },
  {   39438, false, 0x4, 0x00 },
  {   39439, false, 0x4, 0x01 },
  {   39449, false, 0xE, 0x11 },
  {   39600, true,  0x4, 0x6D },
  {   39730, true,  0x4, 0xED },
  {   39900, true,  0x4, 0x43 },
  {   39988, true,  0x4, 0xED },
  {   40158, true,  0x4, 0x43 },
  {   40243, true,  0x4, 0xF0 },
  {   40416, true,  0x4, 0x43 },
  {   40501, true,  0x4, 0xF0 },
  {   40674, true,  0x4, 0x43 },
  {   40757, true,  0x4, 0xF2 },
  {   40932, true,  0x4, 0x43 },
  {   41015, true,  0x4, 0xF2 },
  {   41190, true,  0x4, 0x43 },
  {   41273, true,  0x4, 0xF2 },
  {   41448, true,  0x4, 0x43 },
  {   41664, true,  0x4, 0x6D },
  {   41922, true,  0x4, 0x6D },
  {   42042, true,  0x4, 0xF7 },
  {   42222, true,  0x4, 0x43 },
  {   42300, true,  0x4, 0xF7 },
  {   42480, true,  0x4, 0x43 },
  {   42558, true,  0x4, 0xF7 },
  {   42738, true,  0x4, 0x43 },
  {   42816, true,  0x4, 0xF7 },
  {   42996, true,  0x4, 0x43 },
  {   43070, true,  0x4, 0xFB },
  {   43254, true,  0x4, 0x43 },
  {   43328, true,  0x4, 0xFB },
  {   43512, true,  0x4, 0x43 },
  {   43586, true,  0x4, 0xFB },
  {   43655, false, 0xE, 0x00 },
  {   43659, false, 0xF, 0x00 },
  {   43667, true,  0xD, 0x01 },
  {   43882, false, 0xD, 0x7F },
  {   43902, true,  0xD, 0x00 },
  {  118018, true,  0xD, 0x00 },
  {  118032, false, 0x4, 0x00 },
  {  118036, false, 0x6, 0x00 },
  {  118050, false, 0x5, 0x01 },
  {  118054, false, 0x7, 0x01 },
  {  118058, true,  0x4, 0x00 },
  {  118059, false, 0x4, 0x00 },
  {  118060, false, 0x4, 0x01 },
  {  118070, false, 0xE, 0x11 },
  {  118221, true,  0x4, 0x6D },
  {  118479, true,  0x4, 0x6D },
  {  118737, true,  0x4, 0x6D },
  {  118995, true,  0x4, 0x6D },
  {  119253, true,  0x4, 0x6D },
  {  119511, true,  0x4, 0x6D },
  {  119769, true,  0x4, 0x6D },
  {  120027, true,  0x4, 0x6D },
  {  120285, true,  0x4, 0x6D },
  {  120543, true,  0x4, 0x6D },
  {  120801, true,  0x4, 0x6D },
  {  121059, true,  0x4, 0x6D },
  {  121317, true,  0x4, 0x6D },
  {  121575, true,  0x4, 0x6D },
  {  121833, true,  0x4, 0x6D },
  {  122091, true,  0x4, 0x6D },
  {  122234, false, 0xE, 0x00 },
  {  122238, false, 0xF, 0x00 },
  {  122246, true,  0xD, 0x01 },
  {  122460, false, 0xD, 0x7F },
  {  122480, true,  0xD, 0x00 },
  {  255609, true,  0xD, 0x00 },
  {  255623, false, 0x4, 0x00 },
  {  255627, false, 0x6, 0x00 },
  {  255641, false, 0x5, 0x01 },
  {  255645, false, 0x7, 0x01 },
  {  255649, true,  0x4, 0x00 },
  {  255650, false, 0x4, 0x00 },
  {  255651, false, 0x4, 0x01 },
  {  255661, false, 0xE, 0x11 },
  {  255812, true,  0x4, 0x6D },
  {  255942, true,  0x4, 0xED },
  {  256112, true,  0x4, 0x43 },
  {  256200, true,  0x4, 0xED },
  {  256370, true,  0x4, 0x43 },
  {  256455, true,  0x4, 0xF0 },
  {  256628, true,  0x4, 0x43 },
  {  256713, true,  0x4, 0xF0 },
  {  256886, true,  0x4, 0x43 },
  {  256969, true,  0x4, 0xF2 },
  {  257144, true,  0x4, 0x43 },
  {  257227, true,  0x4, 0xF2 },
  {  257402, true,  0x4, 0x43 },
  {  257485, true,  0x4, 0xF2 },
  {  257660, true,  0x4, 0x43 },
  {  257876, true,  0x4, 0x6D },
  {  258134, true,  0x4, 0x6D },
  {  258254, true,  0x4, 0xF7 },
  {  258434, true,  0x4, 0x43 },
  {  258512, true,  0x4, 0xF7 },
  {  258692, true,  0x4, 0x43 },
  {  258770, true,  0x4, 0xF7 },
  {  258950, true,  0x4, 0x43 },
  {  259028, true,  0x4, 0xF7 },
  {  259208, true,  0x4, 0x43 },
  {  259282, true,  0x4, 0xFB },
  {  259466, true,  0x4, 0x43 },
  {  259540, true,  0x4, 0xFB },
  {  259724, true,  0x4, 0x43 },
  {  259798, true,  0x4, 0xFB },
  {  259867, false, 0xE, 0x00 },
  {  259871, false, 0xF, 0x00 },
  {  259879, true,  0xD, 0x01 },
  {  260093, false, 0xD, 0x7F },
  {  260099, false, 0xD, 0x80 },
  {  260113, true,  0xD, 0x00 },
  {  294921, true,  0xD, 0x00 },
  {  294935, false, 0x4, 0x00 },
  {  294939, false, 0x6, 0x00 },
  {  294953, false, 0x5, 0x01 },
  {  294957, false, 0x7, 0x01 },
  {  294969, false, 0xE, 0x11 },
  {  295199, true,  0x4, 0x1D },
  {  295212, true,  0xD, 0x00 },
  {  295221, true,  0xD, 0x00 },
  {  295457, true,  0x4, 0x1C },
  {  295470, true,  0xD, 0x01 },
  {  295479, true,  0xD, 0x00 },
  {  295715, true,  0x4, 0x1B },
  {  295728, true,  0xD, 0x01 },
  {  295737, true,  0xD, 0x00 },
  {  295973, true,  0x4, 0x1A },
  {  295986, true,  0xD, 0x01 },
  {  295995, true,  0xD, 0x00 },
  {  296231, true,  0x4, 0x19 },
  {  296244, true,  0xD, 0x01 },
  {  296253, true,  0xD, 0x00 },
  {  296489, true,  0x4, 0x18 },
  {  296502, true,  0xD, 0x01 },
  {  296511, true,  0xD, 0x00 },
  {  296747, true,  0x4, 0x17 },
  {  296760, true,  0xD, 0x01 },
  {  296769, true,  0xD, 0x00 },
  {  297005, true,  0x4, 0x16 },
  {  297018, true,  0xD, 0x01 },
  {  297027, true,  0xD, 0x01 },
  {  297263, true,  0x4, 0x15 },
  {  297276, true,  0xD, 0x00 },
  {  297285, true,  0xD, 0x01 },
  {  297521, true,  0x4, 0x14 },
  {  297534, true,  0xD, 0x00 },
  {  297543, true,  0xD, 0x01 },
  {  297779, true,  0x4, 0x13 },
  {  297792, true,  0xD, 0x00 },
  {  297801, true,  0xD, 0x01 },
  {  298037, true,  0x4, 0x12 },
  {  298050, true,  0xD, 0x00 },
  {  298059, true,  0xD, 0x01 },
  {  298295, true,  0x4, 0x11 },
  {  298308, true,  0xD, 0x00 },
  {  298317, true,  0xD, 0x01 },
  {  298553, true,  0x4, 0x10 },
  {  298566, true,  0xD, 0x00 },
  {  298575, true,  0xD, 0x01 },
  {  298811, true,  0x4, 0x0F },
  {  298824, true,  0xD, 0x00 },
  {  298833, true,  0xD, 0x01 },
  {  299069, true,  0x4, 0x0E },
  {  299082, true,  0xD, 0x00 },
  {  299091, true,  0xD, 0x01 },
  {  299137, false, 0xE, 0x00 },
  {  299141, false, 0xF, 0x00 },
  {  299149, true,  0xD, 0x00 },
  {  299364, false, 0xD, 0x7F },
  {  299370, false, 0xD, 0x81 },
  {  299384, true,  0xD, 0x00 },
  {  432513, true,  0xD, 0x00 },
  {  432527, false, 0x4, 0x00 },
  {  432531, false, 0x6, 0x00 },
  {  432545, false, 0x5, 0x01 },
  {  432549, false, 0x7, 0x01 },
  {  432561, false, 0xE, 0x11 },
  {  432791, true,  0x4, 0x1D },
  {  432804, true,  0xD, 0x00 },
  {  432813, true,  0xD, 0x00 },
  {  432837, true,  0xD, 0x81 },
  {  433049, true,  0x4, 0x1C },
  {  433062, true,  0xD, 0x00 },
  {  433071, true,  0xD, 0x00 },
  {  433095, true,  0xD, 0x81 },
  {  433307, true,  0x4, 0x1B },
  {  433320, true,  0xD, 0x00 },
  {  433329, true,  0xD, 0x00 },
  {  433350, true,  0xD, 0x81 },
  {  433565, true,  0x4, 0x1A },
  {  433578, true,  0xD, 0x00 },
  {  433587, true,  0xD, 0x00 },
  {  433608, true,  0xD, 0x81 },
  {  433823, true,  0x4, 0x19 },
  {  433836, true,  0xD, 0x00 },
  {  433845, true,  0xD, 0x00 },
  {  433864, true,  0xD, 0x81 },
  {  434081, true,  0x4, 0x18 },
  {  434094, true,  0xD, 0x00 },
  {  434103, true,  0xD, 0x00 },
  {  434122, true,  0xD, 0x81 },
  {  434339, true,  0x4, 0x17 },
  {  434352, true,  0xD, 0x00 },
  {  434361, true,  0xD, 0x00 },
  {  434380, true,  0xD, 0x81 },
  {  434597, true,  0x4, 0x16 },
  {  434610, true,  0xD, 0x00 },
  {  434619, true,  0xD, 0x01 },
  {  434855, true,  0x4, 0x15 },
  {  434868, true,  0xD, 0x00 },
  {  434877, true,  0xD, 0x81 },
  {  434896, true,  0xD, 0x00 },
  {  435113, true,  0x4, 0x14 },
  {  435126, true,  0xD, 0x00 },
  {  435135, true,  0xD, 0x81 },
  {  435149, true,  0xD, 0x00 },
  {  435371, true,  0x4, 0x13 },
  {  435384, true,  0xD, 0x00 },
  {  435393, true,  0xD, 0x81 },
  {  435407, true,  0xD, 0x00 },
  {  435629, true,  0x4, 0x12 },
  {  435642, true,  0xD, 0x00 },
  {  435651, true,  0xD, 0x81 },
  {  435665, true,  0xD, 0x00 },
  {  435887, true,  0x4, 0x11 },
  {  435900, true,  0xD, 0x00 },
  {  435909, true,  0xD, 0x81 },
  {  435923, true,  0xD, 0x00 },
  {  436145, true,  0x4, 0x10 },
  {  436158, true,  0xD, 0x00 },
  {  436177, true,  0xD, 0x81 },
  {  436209, true,  0xD, 0x00 },
  {  436403, true,  0x4, 0x0F },
  {  436416, true,  0xD, 0x00 },
  {  436435, true,  0xD, 0x81 },
  {  436467, true,  0xD, 0x00 },
  {  436661, true,  0x4, 0x0E },
  {  436674, true,  0xD, 0x00 },
  {  436693, true,  0xD, 0x81 },
  {  436725, true,  0xD, 0x00 },
  {  436771, false, 0xE, 0x00 },
  {  436775, false, 0xF, 0x00 },
  {  436783, true,  0xD, 0x00 },
  {  436997, false, 0xD, 0x7F },
  {  437003, false, 0xD, 0x80 },
  {  437017, true,  0xD, 0x00 },
  {  471825, true,  0xD, 0x00 },
  {  471839, false, 0x4, 0x00 },
  {  471843, false, 0x6, 0x00 },
  {  471857, false, 0x5, 0x01 },
  {  471861, false, 0x7, 0x01 },
  {  471873, false, 0xF, 0x11 },
  {  472103, true,  0x6, 0x1D },
  {  472116, true,  0xD, 0x00 },
  {  472125, true,  0xD, 0x00 },
  {  472361, true,  0x6, 0x1C },
  {  472374, true,  0xD, 0x02 },
  {  472383, true,  0xD, 0x00 },
  {  472619, true,  0x6, 0x1B },
  {  472632, true,  0xD, 0x02 },
  {  472641, true,  0xD, 0x00 },
  {  472877, true,  0x6, 0x1A },
  {  472890, true,  0xD, 0x02 },
  {  472899, true,  0xD, 0x00 },
  {  473135, true,  0x6, 0x19 },
  {  473148, true,  0xD, 0x02 },
  {  473157, true,  0xD, 0x00 },
  {  473393, true,  0x6, 0x18 },
  {  473406, true,  0xD, 0x02 },
  {  473415, true,  0xD, 0x00 },
  {  473651, true,  0x6, 0x17 },
  {  473664, true,  0xD, 0x02 },
  {  473673, true,  0xD, 0x00 },
  {  473909, true,  0x6, 0x16 },
  {  473922, true,  0xD, 0x02 },
  {  473931, true,  0xD, 0x02 },
  {  474167, true,  0x6, 0x15 },
  {  474180, true,  0xD, 0x00 },
  {  474189, true,  0xD, 0x02 },
  {  474425, true,  0x6, 0x14 },
  {  474438, true,  0xD, 0x00 },
  {  474447, true,  0xD, 0x02 },
  {  474683, true,  0x6, 0x13 },
  {  474696, true,  0xD, 0x00 },
  {  474705, true,  0xD, 0x02 },
  {  474941, true,  0x6, 0x12 },
  {  474954, true,  0xD, 0x00 },
  {  474963, true,  0xD, 0x02 },
  {  475199, true,  0x6, 0x11 },
  {  475212, true,  0xD, 0x00 },
  {  475221, true,  0xD, 0x02 },
  {  475457, true,  0x6, 0x10 },
  {  475470, true,  0xD, 0x00 },
  {  475479, true,  0xD, 0x02 },
  {  475715, true,  0x6, 0x0F },
  {  475728, true,  0xD, 0x00 },
  {  475737, true,  0xD, 0x02 },
  {  475973, true,  0x6, 0x0E },
  {  475986, true,  0xD, 0x00 },
  {  475995, true,  0xD, 0x02 },
  {  476041, false, 0xE, 0x00 },
  {  476045, false, 0xF, 0x00 },
  {  476053, true,  0xD, 0x00 },
  {  476268, false, 0xD, 0x7F },
  {  476274, false, 0xD, 0x82 },
  {  476288, true,  0xD, 0x00 },
  {  609417, true,  0xD, 0x00 },
  {  609431, false, 0x4, 0x00 },
  {  609435, false, 0x6, 0x00 },
  {  609449, false, 0x5, 0x01 },
  {  609453, false, 0x7, 0x01 },
  {  609465, false, 0xF, 0x11 },
  {  609695, true,  0x6, 0x1D },
  {  609708, true,  0xD, 0x00 },
  {  609717, true,  0xD, 0x00 },
  {  609741, true,  0xD, 0x82 },
  {  609953, true,  0x6, 0x1C },
  {  609966, true,  0xD, 0x00 },
  {  609975, true,  0xD, 0x00 },
  {  609999, true,  0xD, 0x82 },
  {  610211, true,  0x6, 0x1B },
  {  610224, true,  0xD, 0x00 },
  {  610233, true,  0xD, 0x00 },
  {  610254, true,  0xD, 0x82 },
  {  610469, true,  0x6, 0x1A },
  {  610482, true,  0xD, 0x00 },
  {  610491, true,  0xD, 0x00 },
  {  610512, true,  0xD, 0x82 },
  {  610727, true,  0x6, 0x19 },
  {  610740, true,  0xD, 0x00 },
  {  610749, true,  0xD, 0x00 },
  {  610768, true,  0xD, 0x82 },
  {  610985, true,  0x6, 0x18 },
  {  610998, true,  0xD, 0x00 },
  {  611007, true,  0xD, 0x00 },
  {  611026, true,  0xD, 0x82 },
  {  611243, true,  0x6, 0x17 },
  {  611256, true,  0xD, 0x00 },
  {  611265, true,  0xD, 0x00 },
  {  611284, true,  0xD, 0x82 },
  {  611501, true,  0x6, 0x16 },
  {  611514, true,  0xD, 0x00 },
  {  611523, true,  0xD, 0x02 },
  {  611759, true,  0x6, 0x15 },
  {  611772, true,  0xD, 0x00 },
  {  611781, true,  0xD, 0x82 },
  {  611800, true,  0xD, 0x00 },
  {  612017, true,  0x6, 0x14 },
  {  612030, true,  0xD, 0x00 },
  {  612039, true,  0xD, 0x82 },
  {  612053, true,  0xD, 0x00 },
  {  612275, true,  0x6, 0x13 },
  {  612288, true,  0xD, 0x00 },
  {  612297, true,  0xD, 0x82 },
  {  612311, true,  0xD, 0x00 },
  {  612533, true,  0x6, 0x12 },
  {  612546, true,  0xD, 0x00 },
  {  612555, true,  0xD, 0x82 },
  {  612569, true,  0xD, 0x00 },
  {  612791, true,  0x6, 0x11 },
  {  612804, true,  0xD, 0x00 },
  {  612813, true,  0xD, 0x82 },
  {  612827, true,  0xD, 0x00 },
  {  613049, true,  0x6, 0x10 },
  {  613062, true,  0xD, 0x00 },
  {  613081, true,  0xD, 0x82 },
  {  613113, true,  0xD, 0x00 },
  {  613307, true,  0x6, 0x0F },
  {  613320, true,  0xD, 0x00 },
  {  613339, true,  0xD, 0x82 },
  {  613371, true,  0xD, 0x00 },
  {  613565, true,  0x6, 0x0E },
  {  613578, true,  0xD, 0x00 },
  {  613597, true,  0xD, 0x82 },
  {  613629, true,  0xD, 0x00 },
  {  613670, false, 0xE, 0x00 },
  {  613674, false, 0xF, 0x00 },
  {  613682, true,  0xD, 0x00 },
  { -1, false, 0x0, 0x00 }
};

const t_access cia_sp_test_continues_old[] = {
  {      54, false, 0xD, 0x7F },
  {      62, false, 0x0, 0x7F },
  {      68, false, 0xE, 0x08 },
  {      76, false, 0xF, 0x08 },
  {      86, false, 0x3, 0x00 },
  {     100, false, 0x2, 0xFF },
  {     134, false, 0x4, 0x25 },
  {     143, false, 0x5, 0x40 },
  {     152, false, 0xD, 0x81 },
  {     156, true,  0xE, 0x08 },
  {     164, false, 0xE, 0x11 },
  {   55392, false, 0x4, 0x25 },
  {   55401, false, 0x5, 0x40 },
  {   55410, false, 0xD, 0x81 },
  {   55414, true,  0xE, 0x01 },
  {   55422, false, 0xE, 0x11 },
  {   55527, true,  0x1, 0xFF },
  {   55531, true,  0x1, 0xFF },
  {   55597, false, 0x0, 0x00 },
  {   55601, true,  0x1, 0xFF },
  {   55635, false, 0x0, 0x7F },
  {   55645, true,  0xD, 0x81 },
  {   71930, true,  0x1, 0xFF },
  {   71934, true,  0x1, 0xFF },
  {   72000, false, 0x0, 0x00 },
  {   72004, true,  0x1, 0xFF },
  {   72038, false, 0x0, 0x7F },
  {   72048, true,  0xD, 0x81 },
  {   88393, true,  0x1, 0xFF },
  {   88397, true,  0x1, 0xFF },
  {   88463, false, 0x0, 0x00 },
  {   88467, true,  0x1, 0xFF },
  {   88501, false, 0x0, 0x7F },
  {   88511, true,  0xD, 0x81 },
  {  104775, true,  0x1, 0xFF },
  {  104779, true,  0x1, 0xFF },
  {  104845, false, 0x0, 0x00 },
  {  104849, true,  0x1, 0xFF },
  {  104883, false, 0x0, 0x7F },
  {  104893, true,  0xD, 0x81 },
  {  121206, true,  0x1, 0xFF },
  {  121210, true,  0x1, 0xFF },
  {  121276, false, 0x0, 0x00 },
  {  121280, true,  0x1, 0xFF },
  {  121314, false, 0x0, 0x7F },
  {  121324, true,  0xD, 0x81 },
  {  137616, true,  0x1, 0xFF },
  {  137620, true,  0x1, 0xFF },
  {  137693, false, 0x0, 0x00 },
  {  137697, true,  0x1, 0xFF },
  {  137731, false, 0x0, 0x7F },
  {  137741, true,  0xD, 0x81 },
  {  154038, true,  0x1, 0xFF },
  {  154042, true,  0x1, 0xFF },
  {  154108, false, 0x0, 0x00 },
  {  154112, true,  0x1, 0xFF },
  {  154146, false, 0x0, 0x7F },
  {  154156, true,  0xD, 0x81 },
  {  170464, true,  0x1, 0xFF },
  {  170468, true,  0x1, 0xFF },
  {  170577, false, 0x0, 0x00 },
  {  170581, true,  0x1, 0xFF },
  {  170615, false, 0x0, 0x7F },
  {  170625, true,  0xD, 0x81 },
  {  186884, true,  0x1, 0xFF },
  {  186888, true,  0x1, 0xFF },
  {  186954, false, 0x0, 0x00 },
  {  186958, true,  0x1, 0xFF },
  {  186992, false, 0x0, 0x7F },
  {  187002, true,  0xD, 0x81 },
  {  202156, false, 0xD, 0x8F },
  {  202164, false, 0xE, 0x00 },
  {  202168, false, 0x5, 0x00 },
  {  202174, false, 0x4, 0x01 },
  {  202356, false, 0xE, 0xD1 },
  {  202362, false, 0xC, 0xA8 },
  {  202370, true,  0xD, 0x89 },
  {  202409, false, 0xD, 0x7F },
  {  202413, true,  0xD, 0x89 },
  { -1, false, 0x0, 0x00 }
};

const t_access hammerfist0[] = {
  {      54, false, 0xD, 0x7F },
  {      62, false, 0x0, 0x7F },
  {      68, false, 0xE, 0x08 },
  {      76, false, 0xF, 0x08 },
  {      86, false, 0x3, 0x00 },
  {     100, false, 0x2, 0xFF },
  {     134, false, 0x4, 0x25 },
  {     143, false, 0x5, 0x40 },
  {     152, false, 0xD, 0x81 },
  {     156, true,  0xE, 0x08 },
  {     164, false, 0xE, 0x11 },
  {   55392, false, 0x4, 0x25 },
  {   55401, false, 0x5, 0x40 },
  {   55410, false, 0xD, 0x81 },
  {   55414, true,  0xE, 0x01 },
  {   55422, false, 0xE, 0x11 },
  {   55527, true,  0x1, 0xFF },
  {   55531, true,  0x1, 0xFF },
  {   55597, false, 0x0, 0x00 },
  {   55601, true,  0x1, 0xFF },
  {   55635, false, 0x0, 0x7F },
  {   55645, true,  0xD, 0x80 },
  {   71930, true,  0x1, 0xFF },
  {   71934, true,  0x1, 0xFF },
  {   72000, false, 0x0, 0x00 },
  {   72004, true,  0x1, 0xFF },
  {   72038, false, 0x0, 0x7F },
  {   72048, true,  0xD, 0x81 },
  {   88393, true,  0x1, 0xFF },
  {   88397, true,  0x1, 0xFF },
  {   88463, false, 0x0, 0x00 },
  {   88467, true,  0x1, 0xFF },
  {   88501, false, 0x0, 0x7F },
  {   88511, true,  0xD, 0x81 },
  {  104775, true,  0x1, 0xFF },
  {  104779, true,  0x1, 0xFF },
  {  104845, false, 0x0, 0x00 },
  {  104849, true,  0x1, 0xFF },
  {  104883, false, 0x0, 0x7F },
  {  104893, true,  0xD, 0x81 },
  {  121206, true,  0x1, 0xFF },
  {  121210, true,  0x1, 0xFF },
  {  121276, false, 0x0, 0x00 },
  {  121280, true,  0x1, 0xFF },
  {  121314, false, 0x0, 0x7F },
  {  121324, true,  0xD, 0x81 },
  {  137616, true,  0x1, 0xFF },
  {  137620, true,  0x1, 0xFF },
  {  137693, false, 0x0, 0x00 },
  {  137697, true,  0x1, 0xFF },
  {  137731, false, 0x0, 0x7F },
  {  137741, true,  0xD, 0x81 },
  {  154038, true,  0x1, 0xFF },
  {  154042, true,  0x1, 0xFF },
  {  154108, false, 0x0, 0x00 },
  {  154112, true,  0x1, 0xFF },
  {  154146, false, 0x0, 0x7F },
  {  154156, true,  0xD, 0x81 },
  {  161232, true,  0xB, 0x91 },
  {  161233, false, 0xB, 0x09 },
  {  161246, true,  0xA, 0x00 },
  {  161247, false, 0xA, 0x30 },
  {  161260, true,  0x9, 0x00 },
  {  161261, false, 0x9, 0x12 },
  {  161274, true,  0x8, 0x02 },
  {  161275, false, 0x8, 0x03 },
  {  161299, true,  0x8, 0x03 },
  {  161300, false, 0x8, 0x00 },
  {  161311, true,  0x9, 0x12 },
  {  161312, false, 0x9, 0x00 },

  {  161319, true,  0xD, 0x00 },

  {  161323, true,  0xA, 0x30 },
  {  161324, false, 0xA, 0x00 },
  {  161335, true,  0xB, 0x09 },
  {  161340, false, 0xB, 0x00 }, //36
  {  161352, true,  0xD, 0x00 },
  {  170461, true,  0x1, 0xFF },
  {  170465, true,  0x1, 0xFF },
  {  170581, false, 0x0, 0x00 },
  {  170585, true,  0x1, 0xFF },
  {  170619, false, 0x0, 0x7F },
  {  170629, true,  0xD, 0x81 },
  {  186881, true,  0x1, 0xFF },
  {  186885, true,  0x1, 0xFF },
  {  187064, false, 0x0, 0x00 },
  {  187068, true,  0x1, 0xFF },
  {  187102, false, 0x0, 0x7F },
  {  187155, true,  0xD, 0x81 },
  { -1, false, 0x0, 0x00 }
};

const t_access tod4[] = {
  {      58, false, 0xD, 0x7F },
  {      72, false, 0xE, 0x08 },
  {      80, false, 0xF, 0x08 },
  {      90, false, 0x3, 0x00 },
  {     106, false, 0x0, 0x07 },
  {     112, false, 0x2, 0x3F },
  {     171, true,  0x0, 0xC7 },
  {     177, false, 0x0, 0xD7 },
  {   55429, true,  0x0, 0xD7 },
  {   55435, false, 0x0, 0xD7 },
  {  201615, false, 0xD, 0x7F },
  {  243551, true,  0xD, 0x00 },
  {  243552, false, 0xD, 0x84 },
  {  243603, true,  0xF, 0x08 },
  {  243609, false, 0xF, 0x08 },
  {  243617, true,  0xB, 0x91 },
  {  243618, false, 0xB, 0x00 },
  {  243630, true,  0xA, 0x00 },
  {  243631, false, 0xA, 0x00 },
  {  243643, true,  0x9, 0x00 },
  {  243644, false, 0x9, 0x00 },
  {  243656, true,  0x8, 0x03 },
  {  243657, false, 0x8, 0x00 },
  {  251945, true,  0xB, 0x00 },
  {  251958, true,  0xA, 0x00 },
  {  251971, true,  0x9, 0x00 },
  {  251984, true,  0x8, 0x00 },
  {  271601, true,  0xB, 0x00 },
  {  271614, true,  0xA, 0x00 },
  {  271627, true,  0x9, 0x00 },
  {  271640, true,  0x8, 0x00 },
  {  291257, true,  0xB, 0x00 },
  {  291270, true,  0xA, 0x00 },
  {  291283, true,  0x9, 0x00 },
  {  291296, true,  0x8, 0x00 },
  {  310913, true,  0xB, 0x00 },
  {  310926, true,  0xA, 0x00 },
  {  310939, true,  0x9, 0x00 },
  {  310952, true,  0x8, 0x00 },
  {  330569, true,  0xB, 0x00 },
  {  330582, true,  0xA, 0x00 },
  {  330595, true,  0x9, 0x00 },
  {  330608, true,  0x8, 0x00 },
  {  350225, true,  0xB, 0x00 },
  {  350238, true,  0xA, 0x00 },
  {  350251, true,  0x9, 0x00 },
  {  350264, true,  0x8, 0x00 },
  {  354710, true,  0xD, 0x84 },
  {  354726, true,  0xB, 0x00 },
  {  354739, true,  0xA, 0x00 },
  {  354752, true,  0x9, 0x00 },
  {  354765, true,  0x8, 0x01 },
  { -1, false, 0x0, 0x00 }
};

static void CiaTimerICRTest(void)
{
	CiaInit();
	cycle = 0;
	const t_access *currentAccess = tod4; //icr_test;
	while(currentAccess->cycle >= 0) {
		if (cycle == currentAccess->cycle) {
			if (currentAccess->read) {
				ReadRegister(currentAccess->reg);
			} else {
				WriteRegister(currentAccess->reg, currentAccess->wdata);
			}
			currentAccess++;
		} else { // if (cycle < (currentAccess->cycle - 10)) {
			PhiCycleS();
/*
		} else {
			ReadAllRegisters(0);
*/
		}
	}
	printf("Total errors: %d\n", errors);
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

	//CiaTodTest1();
	CiaTimerICRTest();
	//CiaTimerTestWilfred();
	//CiaTimerTestGroupaz2();
	//CiaTimerTest8();
	//CiaSerialTest1(250+17);
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
