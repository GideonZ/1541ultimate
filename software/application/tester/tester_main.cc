#include <stdio.h>
#include "system.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "altera_avalon_spi.h"
#include "altera_avalon_pio_regs.h"
#include "altera_msgdma.h"

#include "FreeRTOS.h"
#include "task.h"
#include "i2c.h"
#include "mdio.h"
#include "alt_types.h"
#include "dump_hex.h"
#include "iomap.h"
#include "itu.h"
#include "profiler.h"
#include "usb_nano.h"
#include "u2p.h"
#include "usb_base.h"
#include "filemanager.h"
#include "stream_textlog.h"
#include "itu.h"

extern "C" {
	#include "jtag.h"
	#include "audio_test.h"
	#include "analog.h"
	#include "digital_io.h"
	#include "flash_switch.h"
	#include "ethernet_test.h"
	void codec_init(void);
}

int getNetworkPacket(uint8_t **payload, int *length);

#include "usb_base.h"

extern unsigned char _dut_b_start;
extern unsigned char _dut_b_size;
extern unsigned char _dut_application_start;

typedef struct {
	const char *fileName;
	const char *romName;
	const uint32_t flashAddress;
	uint32_t *buffer;
	uint32_t  size;
} BinaryImage_t;

BinaryImage_t toBeFlashed[] = {
		{ "/Usb?/flash/ultimate_recovery.swp", "Recovery FPGA Image",  0x00000000, 0, 0 },
		{ "/Usb?/flash/recovery.app",          "Recovery Application", 0x00080000, 0, 0 },
		{ "/Usb?/flash/ultimate_run.swp",      "Runtime FPGA Image",   0x80000000, 0, 0 },
		{ "/Usb?/flash/ultimate.app",          "Runtime Application",  0x800C0000, 0, 0 },
		{ "/Usb?/flash/rompack.bin",           "ROM Pack",             0x80200000, 0, 0 }
};

BinaryImage_t toBeFlashedExec[] = {
		{ "/Usb?/flash/testexec.swp",      "TestExec FPGA Image",      0x00000000, 0, 0 },
		{ "/Usb?/flash/test_loader.app",   "Test Application Loader",  0x00080000, 0, 0 },
};

BinaryImage_t dutFpga   = { "/Usb?/tester/dut.fpga","DUT FPGA Image", 0, 0, 0 };
BinaryImage_t dutAppl   = { "/Usb?/tester/dut.app", "DUT Application Image", 0, 0, 0 };
BinaryImage_t jigReport = { "", "JIG Report", 0xEE000, 0, 0 };
BinaryImage_t slotReport = { "", "Slot Report", 0xEC000, 0, 0 };

#define APPLICATION_RUN  (0x20000780)
#define BUFFER_LOCATION  (0x20000784)
#define BUFFER_SIZE      (0x20000788)
#define BUFFER_HEAD		 (0x2000078C)
#define BUFFER_TAIL		 (0x20000790)
#define DUT_TO_TESTER    (0x20000794)
#define TESTER_TO_DUT	 (0x20000798)
#define TEST_STATUS		 (0x2000079C)
#define PROGRAM_DATALOC  (0x200007A0)
#define PROGRAM_DATALEN  (0x200007A4)
#define PROGRAM_ADDR     (0x200007A8)
#define TIMELOC          (0x200007B0)

static void outbyte_local(int c)
{
	while (ioRead8(UART_FLAGS) & UART_TxFifoFull);
	ioWrite8(UART_DATA, c);
}

int run_application_on_dut(JTAG_Access_t *target, uint32_t *app)
{
	int retval = 0;
	uint8_t reset = 0x80;
	uint8_t download = 0x40; // select PIO leds
	vji_write(target, 2, &reset, 1);
	vji_write(target, 2, &download, 1);
	vTaskDelay(100);
	int length = (int)(app[1] + 3) & -4;
	uint32_t dest = app[0];
	uint32_t runaddr = app[2];
	uint32_t *src = &app[3];
	printf("Copying %d words to %08x.. ", length >> 2, dest);
	vji_write_memory(target, dest, length >> 2, src);
	uint32_t *verifyBuffer = (uint32_t *)malloc(length);
	vji_read_memory(target, dest, length >> 2, verifyBuffer);
	if(memcmp(verifyBuffer, src, length) != 0) {
		printf("Verify failure. RAM error.\n");
		dump_hex_verify(src, verifyBuffer, length);
		retval = -3;
	} else {
		printf("Setting run address to %08x..", runaddr);
		vji_write_memory(target, APPLICATION_RUN, 1, &runaddr);
		printf("Done!\n");
	}
	free(verifyBuffer);
	return retval;
}

void read_bytes(JTAG_Access_t *target, uint32_t address, int bytes, uint8_t *dest)
{
	//("ReadBytes %x %d => ", address, bytes);
	uint32_t first_word = address >> 2;
	uint32_t last_word = (address + bytes -1) >> 2;
	int words = last_word - first_word + 1;
	int offset = address & 3;
	address &= -4;

	uint32_t buffer[256]; // 1K transfer per time
	while(bytes > 0) {
		int words_now = (words > 256) ? 256 : words;
		//printf("Read VJI: %08x %d ", address, words_now);
		vji_read_memory(target, address, words_now, buffer);
		int bytes_now = (words_now * 4) - offset;
		if (bytes_now > bytes)
			bytes_now = bytes;
		memcpy(dest, ((uint8_t *)buffer) + offset, bytes_now);
		bytes -= bytes_now;
		dest += bytes_now;
		address += bytes_now;
		offset = 0;
	}
}

int getchar_from_dut(JTAG_Access_t *target)
{
	static uint8_t message_buffer[1024];
	static int available = 0;
	static int read_pos = 0;

	struct {
		uint32_t location;
		uint32_t size;
		uint32_t head;
		uint32_t tail;
	} message_desc;

	if (available == 0) {
		vji_read_memory(target, BUFFER_LOCATION, 4, (uint32_t *)&message_desc);

		uint32_t addr = message_desc.location + message_desc.tail;
		int message_length = (int)message_desc.head - (int)message_desc.tail;

		if (message_length < 0) {
			message_length = (int)message_desc.size - (int)message_desc.tail;
		}
		if (message_length > 1024) {
			message_length = 1024;
		}
		if (message_length > 0) {
			//printf("Buffer is at %p. Size of buffer = %d. Head = %d. Tail = %d.\n", message_desc.location,
			//		message_desc.size, message_desc.head, message_desc.tail );
			read_bytes(target, addr, message_length, message_buffer);
			available = message_length;
			read_pos = 0;

			message_desc.tail += message_length;
			if (message_desc.tail >= message_desc.size) {
				message_desc.tail -= message_desc.size;
			}
			vji_write_memory(target, BUFFER_TAIL, 1, &(message_desc.tail));
		}
	}
	if (available > 0) {
		available--;
		return (int)message_buffer[read_pos++];
	}
	return -1;
}

int executeDutCommand(JTAG_Access_t *target, uint32_t commandCode, int timeout, char **log)
{
	uint32_t pre = 0;
	TickType_t tickStart = xTaskGetTickCount();
	vji_read_memory(target, DUT_TO_TESTER, 1, &pre);
	vji_write_memory(target, TESTER_TO_DUT, 1, &commandCode);
	uint32_t post;
	while(1) {
		vTaskDelay(1);
		if (!log) {
			while(1) {
				int ch = getchar_from_dut(target);
				if (ch == -1) {
					break;
				}				outbyte_local(ch);
			}
		}
		vji_read_memory(target, DUT_TO_TESTER, 1, &post);
		if (post != pre) {
			break;
		}
		TickType_t now = xTaskGetTickCount();
		if((now - tickStart) > (TickType_t)timeout) {
			return -99; // timeout
		}
	}
	uint32_t retval;
	vji_read_memory(target, TEST_STATUS, 1, &retval);

	char logBuffer[4096];
	int len = 0;
	while(len < 4095) {
		int ch = getchar_from_dut(target);
		if (ch == -1) {
			break;
		}
		logBuffer[len++] = (char)ch;
		// putchar(ch);
	}
	logBuffer[len] = 0;
	printf(logBuffer); // just dump, we cannot store it

	if (log) {
		if (len) {
			*log = new char[len+2];
			strcpy(*log, logBuffer);
		} else {
			*log = NULL;
		}
	}
	return (int)retval;
}

int LedTest(JTAG_Access_t *target)
{
	int errors = 0;
	uint8_t byte = 0xA0; // reset and select JTAG controlled LEDs
	vji_write(target, 2, &byte, 1);
	vTaskDelay(50);
	int cur = adc_read_current();
	printf("All LEDs off: %d mA\n", cur);
	uint8_t led = 0x01;
	for(int i=0;i<4;i++) {
		byte = led | 0xA0;
		vji_write(target, 2, &byte, 1);
		vTaskDelay(10);
		int ledCurrent = adc_read_current() - cur;
		printf("LED = %02x: %d mA\n", byte, ledCurrent);
		led <<= 1;
		if ((ledCurrent < 3) || (ledCurrent > 7)) {
			errors ++;
		}
	}
	byte = 0x40; // switch LED to PIO mode and take system out of reset
	vji_write(target, 2, &byte, 1);
	vTaskDelay(10);

	return errors;
}

int checkFpgaIdCode(volatile uint32_t *jtag)
{
	uint8_t idcode[4] = {0, 0, 0, 0};
	jtag_reset_to_idle(jtag);
	jtag_instruction(jtag, 6);
	jtag_senddata(jtag, idcode, 32, idcode);
	printf("ID code found: %02x %02x %02x %02x\n", idcode[3], idcode[2], idcode[1], idcode[0]);
	if ((idcode[3] != 0x02) || (idcode[2] != 0x0F) || (idcode[1] != 0x30) || (idcode[0] != 0xdd)) {
		return 1;
	}
	return 0;
}


int program_flash(JTAG_Access_t *target, BinaryImage_t *flashFile)
{
	uint32_t dut_addr = 0xA00000; // 10 MB from start - arbitrary! Should use malloc
	uint32_t destination = flashFile->flashAddress;
	uint32_t length = flashFile->size;
	int words = (length + 3) >> 2;
	vji_write_memory(target, dut_addr, words, flashFile->buffer);
	vji_write_memory(target, PROGRAM_ADDR, 1, &destination);
	vji_write_memory(target, PROGRAM_DATALEN, 1, &length);
	vji_write_memory(target, PROGRAM_DATALOC, 1, &dut_addr);

	printf("Programming %s\n", flashFile->romName);
	return executeDutCommand(target, 12, 60*200, NULL);
}




int load_file(BinaryImage_t *flashFile)
{
	FRESULT fres;
	File *file;
	uint32_t transferred;

	FileManager *fm = FileManager :: getFileManager();
	fres = fm->fopen(flashFile->fileName, FA_READ, &file);
	if (fres == FR_OK) {
		flashFile->size = file->get_size();
		flashFile->buffer = (uint32_t *)malloc(flashFile->size + 8);
		fres = file->read(flashFile->buffer, flashFile->size, &transferred);
		if (transferred != flashFile->size) {
			printf("Expected to read %d bytes, but got %d bytes.\n", flashFile->size, transferred);
			return -1;
		}
	} else {
		printf("Warning: Could not open file '%s'! %s\n", flashFile->fileName, FileSystem :: get_error_string(fres));
		return -2;
	}
	printf("Successfully read %-35s. Size = %8d. Stored at %p.\n", flashFile->fileName, flashFile->size, flashFile->buffer);
	return 0;
}

#include "report.h"
uint16_t random[4096];
void generateRandom(void)
{
	uint16_t seed = 0x1B7F;
	for(int i=0; i < 4096; i++) {
		if (seed & 0x8000) {
			seed = (seed << 1) ^ 0x8004;
		} else {
			seed = (seed << 1) | 1;
		}
		random[i] = seed;
	}
}

int checkMemory(JTAG_Access_t *target, int timeout, char **log)
{
	int retval = 0;

	uint8_t reset = 0x80;
	uint8_t download = 0x00;
	vji_write(target, 2, &reset, 1);
	vji_write(target, 2, &download, 1);
	vTaskDelay(100);

	uint32_t dest = 0;
	uint16_t *src = random;

	while (dest < (64*1024*1024)) {
		vji_write_memory(target, dest, 64, (uint32_t *)src);
		printf(">");
		src += 264;
		if (!dest) {
			dest = 0x100;
		} else {
			dest <<= 1;
		}
	}

	printf("\n");
	dest = 0;
	src = random;
	uint32_t verifyBuffer[64];

	while (dest < (64*1024*1024)) {
		vji_read_memory(target, dest, 64, verifyBuffer);
		printf("<");

		if(memcmp(verifyBuffer, src, 256) != 0) {
			printf("Verify failure. RAM error at address %p.\n", dest);
			dump_hex_verify(src, verifyBuffer, 256);
			retval = -3;
			break;
		}

		src += 264;
		if (!dest) {
			dest = 0x100;
		} else {
			dest <<= 1;
		}
	}
	return retval;
}

int jigPowerSwitchOverTest(JTAG_Access_t *target, int timeout, char **log)
{
	IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, 0xF0); // turn off DUT
	vTaskDelay(200);

	// 1 = vcc
	// 2 = v50

	int errors = 0;
	// first turn power on through VCC.. This should immediately result in power on V50.
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, (1 << 6));
	vTaskDelay(5);
	{
		uint32_t v50 = adc_read_channel_corrected(2);
		uint32_t vcc = adc_read_channel_corrected(1);
		printf("C64VCC: V50=%d, VCC=%d.\n", v50, vcc);
		if (vcc < 4900) {
			printf("Supply voltage of tester too low (%d mV), please increase to 5V.\n", vcc);
		}
		if (v50 < 4300) {
			printf("Supply voltage on internal VCC node (V50) too low (%d mV).\n", v50);
			errors |= 1;
		}
	}
	IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, (1 << 6)); // turn off power on VCC
	vTaskDelay(200);

	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, (1 << 7)); // turn on ext vcc
	vTaskDelay(5);
	{
		uint32_t v50 = adc_read_channel_corrected(2);
		uint32_t vcc = adc_read_channel_corrected(1);
		printf("ExtVCC: V50=%d, VCC=%d.\n", v50, vcc);
		if (vcc > 100) {
			printf("Should not read VCC on the slot pin.\n", vcc);
			errors |= 4;
		}
		if (v50 < 4300) {
			printf("Supply voltage on internal VCC node (V50) too low (%d mV).\n", v50);
			errors |= 2;
		}
	}
	IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, (1 << 7));
	vTaskDelay(100);

	return errors;
}

int jigVoltageRegulatorTest(JTAG_Access_t *target, int timeout, char **log)
{
	IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, (7 << 13)); // clear loads
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0xE0); // bypass diodes
	vTaskDelay(200);
	jtag_clear_fpga(target->host);
	vTaskDelay(100);
	report_analog();
	int errors = validate_analog(1);
	if (errors) {
		printf("Please verify power supplies before continuing.\n");
		return -1;
	}
	return 0;
}

int jigVoltageRegulatorLoad(JTAG_Access_t *target, int timeout, char **log)
{
	uint32_t v50, mA, uW, uWdiff, Vr, Ir, Pr, eff;
	int errors = 0;

	//IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, (1 << 7));
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0xE0); // bypass diodes
	vTaskDelay(150);
	report_analog();

	v50 = adc_read_channel_corrected(2);
	mA = adc_read_channel_corrected(0) / 1000;
	uW = v50 * mA;
	uint32_t uWStart = uW;
	printf("Starting with: %d mW\n", uWStart / 1000);

	// 1.2V
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, (1 << 13)); // load 1.2V
	vTaskDelay(80);
	v50 = adc_read_channel_corrected(2);
	mA = adc_read_channel_corrected(0) / 1000;
	Vr = adc_read_channel_corrected(7);
	Ir = ((10 * Vr) + 23) / 47;
	Pr = (Ir * Vr);
	uW = v50 * mA;
	uWdiff = uW - uWStart;
	eff = Pr / (uWdiff / 100);
	printf("%d mW (+%d mW), Vr = %d mV, Ir = %d mA thru 4.7 Ohm = %d mW. Eff = %d\%\n", uW / 1000, uWdiff / 1000, Vr, Ir, Pr / 1000, eff);

/*
	if (Vr < 1050) {
		errors |= 1;
	}
	if (eff < 70) {
		errors |= 2;
	}
*/

	IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, (1 << 13)); // load 1.2V (measured: 1.111)

	// 1.8V
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, (1 << 14)); // load 1.8V
	vTaskDelay(80);
	v50 = adc_read_channel_corrected(2);
	mA = adc_read_channel_corrected(0) / 1000;
	Vr = adc_read_channel_corrected(6);
	Ir = ((10 * Vr) + 34) / 68;
	Pr = (Ir * Vr);
	uW = v50 * mA;
	uWdiff = uW - uWStart;
	eff = Pr / (uWdiff / 100);
	printf("%d mW (+%d mW), Vr = %d mV, Ir = %d mA thru 6.8 Ohm = %d mW. Eff = %d\%\n", uW / 1000, uWdiff / 1000, Vr, Ir, Pr / 1000, eff);
	IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, (1 << 14)); // load 1.8V (measured: 1.708)

/*
	if (Vr < 1650) {
		errors |= 4;
	}
	if (eff < 82) {
		errors |= 8;
	}
*/

	// 3.3V
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, (1 << 15)); // load 3.3V
	vTaskDelay(80);
	v50 = adc_read_channel_corrected(2);
	mA = adc_read_channel_corrected(0) / 1000;
	Vr = adc_read_channel_corrected(4);
	Ir = (Vr + 8) / 15;
	Pr = (Ir * Vr);
	uW = v50 * mA;
	uWdiff = uW - uWStart;
	eff = Pr / (uWdiff / 100);
	printf("%d mW (+%d mW), Vr = %d mV, Ir = %d mA thru 15 Ohm = %d mW. Eff = %d\%\n", uW / 1000, uWdiff / 1000, Vr, Ir, Pr / 1000, eff);
	IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, (1 << 15)); // load 3.3V (measured: 3.24)

/*
	if (Vr < 3100) {
		errors |= 16;
	}
	if (eff < 82) {
		errors |= 32;
	}
*/
	return errors;
}


int bringupIdCode(JTAG_Access_t *target, int timeout, char **log)
{
	if (checkFpgaIdCode(target->host)) {
		printf("No FPGA / Wrong FPGA type.\n");
		return -2;
	}
	return 0;
}

int bringupConfigure(JTAG_Access_t *target, int timeout, char **log)
{
	// Configure FPGA
	if (!dutFpga.buffer) {
		printf("No FPGA image loaded from USB.\n");
		return -1;
	}

	portDISABLE_INTERRUPTS();
	jtag_configure_fpga(target->host, (uint8_t *)dutFpga.buffer, (int)dutFpga.size);
	portENABLE_INTERRUPTS();
	vTaskDelay(100);
	report_analog();

	if (FindJtagAccess(target->host, target) < 0) {
		return -2;
	}
	printf("FPGA seems to be correctly configured.\n");
	return 0;
}

int checkReferenceClock(JTAG_Access_t *target, int timeout, char **log)
{
	uint8_t clock1[2] = { 0, 0 };
	int retries = 5;
	do {
		vji_read(target, 8, &clock1[0], 1);
		vji_read(target, 8, &clock1[1], 1);
		retries --;
	} while((clock1[1] == clock1[0]) && (retries));

	printf("50 MHz clock detection: %02x %02x\n", clock1[0], clock1[1]);
	if (clock1[0] == clock1[1]) {
		printf("Failed to detect 50 MHz reference clock from Ethernet PHY.\n");
		return -4;
	}
	return 0;
}

int checkApplicationRun(JTAG_Access_t *target, int timeout, char **log)
{
	int download = run_application_on_dut(target, dutAppl.buffer);
	if (download) {
		return download;
	}

	int started = executeDutCommand(target, 99, timeout, log);
	if (started) {
		printf("Seems that the DUT did not start test software.\n");
	} else {
		target->dutRunning = 1;
	}
	return started;
}

int checkLEDs(JTAG_Access_t *target, int timeout, char **log)
{
	// Functional tests start here
	int leds = LedTest(target);
	if (leds) {
		printf("One or more of the LEDs failed.\n");
	}
	return leds;
}

int checkSpeaker(JTAG_Access_t *target, int timeout, char **log)
{
	int mono = executeDutCommand(target, 1, timeout, log);
	if (mono) {
		printf("Request for mono sound failed.\n");
		return -1;
	} else {
		int speaker = testDutSpeaker();
		if (speaker) {
			printf("Speaker output failed.");
			return -2;
		}
	}
	return 0;
}

int checkUsbClock(JTAG_Access_t *target, int timeout, char **log)
{
	int retries = 5;
	uint8_t clock2[2] = { 0, 0 };
	vji_read(target, 9, &clock2[0], 1);
	do {
		vji_read(target, 9, &clock2[0], 1);
		vji_read(target, 9, &clock2[1], 1);
		retries --;
	} while((clock2[1] == clock2[0]) && (retries));

	printf("60 MHz clock detection: %02x %02x\n", clock2[0], clock2[1]);
	if (clock2[0] == clock2[1]) {
		printf("Failed to detect 60 MHz Clock from PHY.\n");
		return -1;
	}
	return 0;
}

int checkUsbPhy(JTAG_Access_t *target, int timeout, char **log)
{
	int phy = executeDutCommand(target, 7, timeout, log);
	if (phy) {
		printf("USB PHY detection failed.\n");
	}
	return phy;
}

int checkRtcAccess(JTAG_Access_t *target, int timeout, char **log)
{
	int rtc = executeDutCommand(target, 13, timeout, log); // RTCAccessTst
	if (rtc) {
		printf("Failed to access RTC chip.\n");
	}
	return rtc;
}

int dutPowerOn(JTAG_Access_t *target, int timeout, char **log)
{
	codec_init();
	IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, 0xF0); // disable JIG
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x10); // enable DUT

	vTaskDelay(200);
	jtag_clear_fpga(target->host);
	vTaskDelay(100);

	uint32_t uA = adc_read_channel_corrected(0);
	printf("Measured current: %d mA\n", uA / 1000);

	if (uA < 25000) {
		printf("Current low. No device detected in slot.\n");
		return -1;
	}
	if (uA > 175000) {
		printf("Current high! There might be a problem with the device in the slot.\n");
		return -2;
	}
	return 0;
}

int slotAudioInput(JTAG_Access_t *target, int timeout, char **log)
{
	// Audio test
	// Request stereo output from DUT
	IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, (1 << 31));
	int aud;

	// Now test the stereo input of the DUT
	aud = startAudioOutput(128);
	if (aud) {
		printf("Request to play audio locally failed.\n");
		return -1;
	}
	vTaskDelay(200);
	aud = executeDutCommand(target, 3, timeout, NULL);
	if (aud) {
		printf("DUT Audio input failed.\n");
	}
	return aud;
}

int slotAudioOutput(JTAG_Access_t *target, int timeout, char **log)
{
	// Now test the stereo output of the DUT
	int aud = executeDutCommand(target, 2, timeout, log);
	if (aud) {
		printf("Request to play audio on DUT failed.\n");
		return aud;
	}
	aud = TestAudio(11, 7, 1);
	if (aud) {
		printf("Audio signal check failed.\n");
		return aud;
	}
	return 0;
}

int checkRtcAdvance(JTAG_Access_t *target, int timeout, char **log)
{
	int errors = 0;

	// RTC Read
	uint32_t timebuf1[3];
	int rtc = executeDutCommand(target, 14, timeout, log); // readRTC
	if (rtc) {
		printf("Failed to read RTC for the first time.\n");
		errors ++;
	} else {
		vji_read_memory(target, TIMELOC, 3, timebuf1);
	}

	vTaskDelay(300);

	// Read RTC and verify with previous result
	rtc = executeDutCommand(target, 14, timeout, log); // readRTC
	if (rtc) {
		printf("Failed to read RTC for the second time.\n");
		errors ++;
	} else {
		uint32_t timebuf2[3];
		vji_read_memory(target, TIMELOC, 3, timebuf2);
		uint8_t *t1, *t2;
		t1 = (uint8_t *)timebuf1;
		t2 = (uint8_t *)timebuf2;
		int same = 0;
		for(int i=0; i<11; i++) {
			if (t1[i] == t2[i]) {
				same++;
			}
		}
		if (same == 0) {
			printf("RTC did not advance.\n");
			errors ++;
		} else if (same < 6) {
			printf("Many bytes differ, RTC faulty.\n");
			errors ++;
		}
	}
	return errors;
}

int checkNetworkTx(JTAG_Access_t *target, int timeout, char **log)
{
	int eth;
	eth = executeDutCommand(target, 5, timeout, log);
	if (eth) {
		printf("Request to send network packet failed.\n");
		return -1;
	}
	vTaskDelay(3);
	uint8_t *packet;
	int length;
	if (getNetworkPacket(&packet, &length) == 0) {
		printf("No network packet received from DUT.\n");
		return -2;
	} else {
		printf("Network packet received with length = %d ", length);
		if (length != 900) {
			printf("OH no! \n");
			return -3;
		} else {
			printf("YES! \n");
		}
	}
	return 0;
}

int checkNetworkRx(JTAG_Access_t *target, int timeout, char **log)
{
	// Now the other way around! We do the sending.
	int retval = sendEthernetPacket(1000);
	if (retval) {
		printf("Tester failed to send network packet.\n");
		return -1;
	}
	vTaskDelay(10);
	int eth = executeDutCommand(target, 6, timeout, log);
	if (eth) {
		printf("DUT failed to receive network packet.\n");
	}
	return eth;
}

int checkUsbHub(JTAG_Access_t *target, int timeout, char **log)
{
	uint32_t powerBits = (IORD_ALTERA_AVALON_PIO_DATA(PIO_1_BASE) & 0x700) >> 8;
	printf("Before turning on USB power: Power bits: %2x\n", powerBits);
	if (powerBits != 0x04) {
		printf("-> Check U5: There seems to be power on the USB sticks, while they have not yet been turned on.\n");
	}
	// Now, let's start the USB stack on the DUT
	int usb = executeDutCommand(target, 11, timeout, log);
	if (usb) {
		printf("Failed to start USB on DUT.\n");
		return usb;
	}

	// Check for USB Hub
	int retries = 20;
	usb = -1;
	while(retries && usb) {
		vTaskDelay(50);
		usb = executeDutCommand(target, 8, timeout, log);
		retries--;
	}
	if (usb) {
		printf("USB HUB check failed.\n");
	}
	return usb;
}

int checkUsbSticks(JTAG_Access_t *target, int timeout, char **log)
{
	// Check for USB sticks
	int retries = 20;
	int usb = -1;
	while(retries && usb) {
		vTaskDelay(50);
		usb = executeDutCommand(target, 9, timeout, log);
		retries--;
	}
	uint32_t powerBits = (IORD_ALTERA_AVALON_PIO_DATA(PIO_1_BASE) & 0x700) >> 8;
	printf("After USB test: Power bits: %2x\n", powerBits);
	if (powerBits != 0x07) {
		printf("-> Check U5: There seems to be no power on the USB sticks.\n");
	}

	if (usb) {
		printf("*** USB check for 3 devices failed.\n");
	}
	executeDutCommand(target, 17, timeout, log);
	return usb;
}

int flashRoms(JTAG_Access_t *target, int timeout, char **log)
{
	int errors = 0;

	// Flash Programming
	int flash = 0;
	for (int i=0; i<5; i++) {
		if (!toBeFlashed[i].buffer) {
			printf("No Valid data for '%s' => Cannot flash.\n", toBeFlashed[i].romName);
			errors ++;
		} else {
			flash = program_flash(target, &toBeFlashed[i]);
		}
		if (flash) {
			printf("Flashing failed.\n");
			errors ++;
			break;
		}
	}
	if (!errors) {
		executeDutCommand(target, 16, timeout, log);
	}
	return errors;
}

int flashRomsExec(JTAG_Access_t *target, int timeout, char **log)
{
	int errors = 0;

	// Flash Programming
	int flash = 0;
	for (int i=0; i<2; i++) {
		if (!toBeFlashedExec[i].buffer) {
			printf("No Valid data for '%s' => Cannot flash.\n", toBeFlashed[i].romName);
			errors ++;
		} else {
			flash = program_flash(target, &toBeFlashedExec[i]);
		}
		if (flash) {
			printf("Flashing failed.\n");
			errors ++;
			break;
		}
	}
	if (!errors) {
		executeDutCommand(target, 16, timeout, log);
	}
	return errors;
}

int checkButtons(JTAG_Access_t *target, int timeout, char **log)
{
	printf(" ** PRESS EACH OF THE BUTTONS ON THE DUT **\n");
	int retval = executeDutCommand(target, 10, timeout, log);
	printf(" ** THANK YOU **\n");
	return retval;
}

int checkFlashSwitch(JTAG_Access_t *target, int timeout, char **log)
{
	int retval = executeDutCommand(target, 4, timeout, log);
	return retval;
}

int keepCheckingFlash(JTAG_Access_t *target, int timeout, char **log)
{
	int retval;
	int ch;
	do {
		retval += executeDutCommand(target, 4, timeout, log);
		ch = uart_get_byte(0);
	} while(ch < 0);

	return retval;
}

int checkDigitalIO(JTAG_Access_t *target, int timeout, char **log)
{
	return TestIOPins(target);
}

int copyRtc(JTAG_Access_t *target, int timeout, char **log)
{
	ENTER_SAFE_SECTION
	uint32_t timebuf[3];
	uint8_t *pb = (uint8_t *)timebuf;
    int res;
	for(int i=0;i<11;i++) {
        *(pb++) = i2c_read_byte(0xA2, i, &res);
        if (res) {
        	break;
        }
	}
	LEAVE_SAFE_SECTION
	int retval = -9;
	if (!res) {
		vji_write_memory(target, TIMELOC, 3, timebuf);
		retval = executeDutCommand(target, 15, timeout, log);
	}
	return retval;
}

int readReports(JTAG_Access_t *target, int timeout, char **log)
{
	static uint32_t buffer[2048];

	uint32_t dut_addr = 0xC00000; // 12 MB from start - arbitrary! Should use malloc
	uint32_t destination = jigReport.flashAddress;
	uint32_t length = 8192;
	int words = (length + 3) >> 2;
	vji_write_memory(target, PROGRAM_ADDR, 1, &destination);
	vji_write_memory(target, PROGRAM_DATALEN, 1, &length);
	vji_write_memory(target, PROGRAM_DATALOC, 1, &dut_addr);
	int retval = executeDutCommand(target, 18, timeout, log);
	if (!retval) {
		vji_read_memory(target, dut_addr, words, buffer);
		uint8_t *c = (uint8_t *)buffer;
		for(int i=0;i<8192;i++) {
			if (c[i] == 0xFF) {
				c[i] = 0;
				break;
			}
		}
		printf("\e[33m%s\n\e[0m", buffer);
	}
	if(retval) {
		return retval;
	}
	destination = slotReport.flashAddress;
	vji_write_memory(target, PROGRAM_ADDR, 1, &destination);
	vji_write_memory(target, PROGRAM_DATALEN, 1, &length);
	vji_write_memory(target, PROGRAM_DATALOC, 1, &dut_addr);
	retval = executeDutCommand(target, 18, timeout, log);
	if (!retval) {
		vji_read_memory(target, dut_addr, words, buffer);
		uint8_t *c = (uint8_t *)buffer;
		for(int i=0;i<8192;i++) {
			if (c[i] == 0xFF) {
				c[i] = 0;
				break;
			}
		}
		printf("\e[34m%s\n\e[0m", buffer);
	}
	return retval;
}

/*
bool breakOnFail;
bool skipWhenErrors;
bool logInSummary;
*/

TestDefinition_t jig_tests[] = {
		{ "Power SwitchOver Test",  jigPowerSwitchOverTest,  1, false, false, false },
		{ "Voltage Regulator Test", jigVoltageRegulatorTest, 1,  true, false, false },
		{ "VREG Load Test",         jigVoltageRegulatorLoad, 1, false, false, false },
		{ "Check FPGA ID Code",     bringupIdCode,           1,  true, false, false },
		{ "Configure the FPGA",     bringupConfigure,        1,  true, false, false },
		{ "Verify reference clock", checkReferenceClock,     1,  true, false, false },
		{ "Verify USB Phy clock",   checkUsbClock,           1, false, false,  true },
		{ "Verify LED presence",    checkLEDs,               2, false, false, false },
		{ "Memory Test", 			checkMemory,             1,  true, false, false },
		{ "Run DUT Application",    checkApplicationRun,   150,  true, false,  true },
		{ "Check Flash Types",      checkFlashSwitch,      150,  true, false,  true },
		{ "Audio amplifier test",   checkSpeaker,          150, false, false,  true },
		{ "Verify USB Phy type",    checkUsbPhy,           150, false, false,  true },
		{ "RTC access test",        checkRtcAccess,        150, false, false,  true },
		{ "", NULL, 0, false, false, false }
};

TestDefinition_t slot_tests[] = {
		{ "Power up DUT in slot",   dutPowerOn,              1,  true, false, false },
		{ "Check FPGA ID Code",     bringupIdCode,           1,  true, false,  true },
		{ "Configure the FPGA",     bringupConfigure,        1,  true, false,  true },
		{ "Digital I/O test",       checkDigitalIO,          1, false, false,  true },
		{ "Verify reference clock", checkReferenceClock,     1,  true, false,  true },
		{ "Memory Test", 			checkMemory,             1,  true, false, false },
		{ "Run DUT Application",    checkApplicationRun,   150,  true, false,  true },
		{ "Button Test", 			checkButtons,		  3000, false,  true,  true },
		{ "Check Flash Types",      checkFlashSwitch,      150, false, false,  true },
		{ "Audio input test",       slotAudioInput,        600, false, false,  true },
		{ "Audio output test",      slotAudioOutput,       600, false, false,  true },
		{ "Copy time to DUT RTC",   copyRtc,			   150, false, false,  true },
		{ "RTC advance test",       checkRtcAdvance,       400, false, false,  true },
		{ "Ethernet Tx test",       checkNetworkTx,        120, false, false,  false },
		{ "Ethernet Rx test",       checkNetworkRx,        120, false, false,  false },
		{ "Check USB Hub type",     checkUsbHub,           600, false, false,  true },
		{ "Check USB Ports (3x)",   checkUsbSticks,        600, false, false,  true },
		{ "Program Flashes",        flashRoms,             200, false,  true,  true },
		{ "", NULL, 0, false, false, false }
};

TestDefinition_t prepare_testexec[] = {
		{ "Power up DUT in slot",   dutPowerOn,              1,  true, false, false },
		{ "Check FPGA ID Code",     bringupIdCode,           1,  true, false,  true },
		{ "Configure the FPGA",     bringupConfigure,        1,  true, false,  true },
		{ "Digital I/O test",       checkDigitalIO,          1, false, false,  true },
		{ "Verify reference clock", checkReferenceClock,     1,  true, false,  true },
		{ "Memory Test", 			checkMemory,             1,  true, false, false },
		{ "Run DUT Application",    checkApplicationRun,   150,  true, false,  true },
		{ "Button Test", 			checkButtons,		  3000, false,  true,  true },
		{ "Check Flash Types",      checkFlashSwitch,      150, false, false,  true },
		{ "Audio input test",       slotAudioInput,        600, false, false,  true },
		{ "Audio output test",      slotAudioOutput,       600, false, false,  true },
		{ "Copy time to DUT RTC",   copyRtc,			   150, false, false,  true },
		{ "RTC advance test",       checkRtcAdvance,       400, false, false,  true },
		{ "Ethernet Tx test",       checkNetworkTx,        120, false, false,  false },
		{ "Ethernet Rx test",       checkNetworkRx,        120, false, false,  false },
		{ "Check USB Hub type",     checkUsbHub,           600, false, false,  true },
		{ "Check USB Ports (3x)",   checkUsbSticks,        600, false, false,  true },
		{ "Program Flashes",        flashRomsExec,         200, false,  true,  true },
		{ "", NULL, 0, false, false, false }
};

TestDefinition_t checks[] = {
		{ "Voltage Regulator Test", jigVoltageRegulatorTest, 1,  true, false, false },
		{ "Check FPGA ID Code",     bringupIdCode,           1,  true, false, false },
		{ "Configure the FPGA",     bringupConfigure,        1,  true, false, false },
		{ "Verify reference clock", checkReferenceClock,     1,  true, false, false },
		{ "Verify DUT appl running",checkApplicationRun,   150,  true, false, false },
		{ "Read reports", 			readReports,		   100,  true, false, false },
		{ "", NULL, 0, false, false, false }
};

TestDefinition_t usb_only[] = {
		{ "Power up DUT in slot",   dutPowerOn,              1,  true, false, false },
		{ "Check FPGA ID Code",     bringupIdCode,           1,  true, false,  true },
		{ "Configure the FPGA",     bringupConfigure,        1,  true, false,  true },
		{ "Verify DUT appl running",checkApplicationRun,   150,  true, false,  true },
		{ "Check USB Hub type",     checkUsbHub,           600, false, false,  true },
		{ "Check USB Ports (3x)",   checkUsbSticks,        600, false, false,  true },
		{ "", NULL, 0, false, false, false }
};

TestDefinition_t flash_only[] = {
		//{ "Power up DUT in slot",   dutPowerOn,              1,  true, false, false },
		{ "Check FPGA ID Code",     bringupIdCode,           1,  true, false,  true },
		{ "Configure the FPGA",     bringupConfigure,        1,  true, false,  true },
		{ "Verify DUT appl running",checkApplicationRun,   150,  true, false,  true },
		{ "Check Flash Types Cont.",keepCheckingFlash,      600, false, false,  true },
		{ "", NULL, 0, false, false, false }
};

/*
		const char *nameOfTest;
		TestFunction_t function;
		int  timeout; // in seconds
		bool breakOnFail;
		bool skipWhenErrors;
		bool logInSummary;
*/

StreamTextLog logger(8192);

void outbyte_log(int c)
{
	logger.charout(c);
	// Wait for space in FIFO
	outbyte_local(c);
}

int writeLog(StreamTextLog *log, const char *prefix, const char *name)
{
	char total_name[100];
	strcpy(total_name, prefix);
	strcat(total_name, name);
	strcat(total_name, ".log");

	FileManager *fm = FileManager :: getFileManager();
	File *file;
	FRESULT fres = fm->fopen(total_name, FA_WRITE | FA_CREATE_NEW, &file);
	if (fres == FR_OK) {
		uint32_t transferred = 0;
		file->write(log->getText(), log->getLength(), &transferred);
		if (transferred != log->getLength()) {
			printf("Warning! Log file not (completely) written.\n");
		}
		fm->fclose(file);
	} else {
		printf("WARNING! Log file could not be written!\n");
	}
	return fres;
}

void setTime(void)
{
	char buffer[32];
	printf("The current time is set to: %s\n", rtc.get_time_string(buffer, 32));
	printf("Enter new time, using 4 numeric chars and press enter to set:\n");
	int ch;
	int value = 0;
	int hours = 0;
	int mins = 0;

	while(1) {
		ch = uart_get_byte(0);
		if (ch < 0) {
			continue;
		}
		if (ch == 0x0d) {
			if ((hours > 23) || (mins > 59)) {
				continue;
			}
			break; // OK
		}
		if ((ch < '0') || (ch > '9')) {
			continue;
		}
		value *= 10;
		value += (ch - '0');
		value -= 10000 * (value / 10000);
		hours = value / 100;
		mins = value % 100;
		printf("%02d:%02d\r", hours, mins);
	}
	int Y,M,D,wd,h,m,s;
	rtc.get_time(Y,M,D,wd,h,m,s);
	h = hours;
	m = mins;
	s = 0;
	rtc.set_time_in_chip(0, Y,M,D,wd,h,m,s);
	printf("\nTime is now set to: %s\n", rtc.get_time_string(buffer, 32));
}

void setDate(void)
{
	char buffer[32];
	printf("The current date is set to: %s\n", rtc.get_date_string(buffer, 32));
	printf("Enter new date, using 6 numeric chars:\n");
	int ch;
	char *input = "_______";
	int Yi,Mi,Di;

	printf("20__-__-__\r20");
	for(int i=0;i<6;) {
		ch = uart_get_byte(0);
		if (ch < 0) {
			continue;
		}
		if ((ch < '0') || (ch > '9')) {
			continue;
		}
		input[i++] = (char)ch;

		printf("\r20%c%c-%c%c-%c%c\r", input[0], input[1], input[2], input[3], input[4], input[5]);
		printf("20");
		for(int j=0;j<i;j++) {
			printf("%c", input[j]);
			if ((j & 1) == 1) {
				printf("-");
			}
		}
	}

/*
	if (input[6] != 0x0D) {
		printf("\nTime not set.\n");
		return;
	}
*/

	Yi = 10*(input[0]-'0') + (input[1] - '0');
	Mi = 10*(input[2]-'0') + (input[3] - '0');
	Di = 10*(input[4]-'0') + (input[5] - '0');

	if ((Mi < 1) || (Mi > 12)) {
		printf("\nInvalid month.\n");
		return;
	}
	if ((Di < 1) || (Di > 31)) {
		printf("\nInvalid date.\n");
		return;
	}
	int Y,M,D,wd,h,m,s;
	rtc.get_time(Y,M,D,wd,h,m,s);
	Y = Yi + 20;
	M = Mi;
	D = Di;
	int month_days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	// 1 jan 2000 - saturday
	int days = Yi * 365 + D + 5;

	days += ((Yi+3) >> 2); // leap years before this one (2000 = 0, 2001 = 1, 2002 = 1, 2003 = 1, 2004 = 1, 2005 = 2
	if ((Yi & 3) == 0) {
		month_days[1] = 29;
	} else {
		month_days[1] = 28;
	}
	for(int i=1;i<M;i++) {
		days += month_days[i-1];
	}
	wd = days % 7;
	rtc.set_time_in_chip(0, Y,M,D,wd,h,m,s);
	printf("\nDate is now set to: %s\n", rtc.get_date_string(buffer, 32));
}

void listenJtag(void)
{
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, (14 << 4)); // setting power on on JIG
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, (1 << 16)); // setting JTAG to input

	alt_msgdma_dev *dma_in = alt_msgdma_open("/dev/jtagdebug_csr");
	if (!dma_in) {
		printf("Cannot open dma input device for JTAG snoop.\n");
		return;
	}

	uint8_t *buffer = (uint8_t *)malloc(8*1024*1024);
	memset(buffer, 0xFF, 8*1024*1024);
	printf("Now 'listening' on JTAG port.\n");

	alt_msgdma_standard_descriptor desc_in;
	desc_in.write_address = (uint32_t*)buffer;
	desc_in.transfer_length = 8*1024*1024 - 4;
	desc_in.control = 0x80000000; // just once
	alt_msgdma_standard_descriptor_async_transfer(dma_in, &desc_in);

	vTaskDelay(200);
	while(1) {
		vTaskDelay(50);
		uint8_t buttons = getButtons();
		if (!buttons) {
			continue;
		}
		if (buttons == 0x02) {
			printf("Middle button stop. Going to write file.\n\n");
			break;
		}
	}
	int i;
	for(i = 8*1024*1024 - 4; i > 0; i--) {
		if(buffer[i] != 0xFF) {
			break;
		}
	}
	printf("Length of capture found: %d\n", i);

	FileManager *fm = FileManager :: getFileManager();
	File *file;
	FRESULT fres = fm->fopen("/Usb?/jtag_trace.bin", FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS, &file);
	if (fres == FR_OK) {
		uint32_t transferred = 0;
		file->write(buffer, i, &transferred);
		if (transferred != i) {
			printf("Warning! Trace file not (completely) written.\n");
		}
		fm->fclose(file);
	} else {
		printf("WARNING! Trace file could not be written!\n");
	}

	free(buffer);
}

void usage()
{
	char date[48];
	char time[48];

	printf("\n** Ultimate 2+ Tester *** V1.5 *** ");
	printf("%s %s ***\n", rtc.get_long_date(date, 40), rtc.get_time_string(time, 40));
	printf("Press 'j' or left button on Tester to run test on JIG.\n");
	printf("Press 's' or right button on Tester to run test in Slot.\n");
	printf("Press 'c' to read the test report from the board.\n");
	printf("Press 'u' to run the USB test only.\n");
	printf("Press 'P' to turn on jig and slot. Press any key to turn off power.\n");
	printf("Press 'D' to set RTC date. (ISO Format: YYYY-MM-DD\n");
	printf("Press 'T' to set RTC time. (24 hr format)\n");
	printf("Press '#' to prepare a board with tester software.\n");
}

extern "C" {
	void main_task(void *context)
	{
		IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, 0xFF);
		IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x08); // green
		configure_adc();
		printf("Welcome to the Ultimate-II+ automated test system.\n");
		printf("Initializing local USB devices...\n");

		// rtc.set_time_in_chip(0, 2016 - 1980, 11, 19, 6, 17, 45, 0);
		custom_outbyte = outbyte_log;

		usb2.initHardware();
		FileManager *fm = FileManager :: getFileManager();

		printf("Waiting for USB storage device to become available.\n");
		FileInfo info(32);
		FRESULT res;
		do {
			vTaskDelay(100);
			res = fm->fstat("/Usb?", info);
			//printf("%s\n", FileSystem :: get_error_string(res));
		} while (res != FR_OK);

		// fm->print_directory("/Usb?");

		for (int i=0; i < 5; i++) {
			load_file(&toBeFlashed[i]);
		}
		for (int i=0; i < 2; i++) {
			load_file(&toBeFlashedExec[i]);
		}

		// Initialize fpga and application structures for DUT
//		dutFpga.buffer = (uint32_t *)&_dut_b_start;
//		dutFpga.size   = (uint32_t)&_dut_b_size;
//		dutAppl.buffer = (uint32_t *)&_dut_application_start;

		load_file(&dutFpga);
		load_file(&dutAppl);

		TestSuite jigSuite("JIG Test Suite", jig_tests);
		TestSuite slotSuite("Slot Test Suite", slot_tests);
		TestSuite checkSuite("Check Suite", checks);
		TestSuite usbSuite("USB Suite", usb_only);
		TestSuite flashSuite("Flash Suite", flash_only);
		TestSuite prepareSuite("Prepare Tester", prepare_testexec);

		printf("Generating Random numbers for testing memory.\n");
		generateRandom();
		printf("Done!\n\n");

		usage();

		while(1) {
			vTaskDelay(1);
			int ub = uart_get_byte(0);
			uint8_t buttons = getButtons();
			if ((!buttons) && (ub < 0)) {
				continue;
			}

			IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, 0x03); // red+green
			if ((buttons == 0x00) && (ub == (int)'P')) {
				IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0xF0); // turn on DUT
				vTaskDelay(200);
				jigSuite.Reset((volatile uint32_t *)JTAG_0_BASE);
				bringupConfigure(jigSuite.getTarget(), 100, NULL);
				printf("JIG & SLOT are now powered... Press any key to turn off.\n");
				do {
					vTaskDelay(1);
					ub = uart_get_byte(0);
					if (ub == 'a') {
						report_analog();
						ub = -1;
					}
				} while(ub < 0);
				IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, 0xF0); // turn off DUT
				printf("Turning off power.\n");
				continue;
			}

			if ((buttons == 0x01) || (ub == (int)'j')) {
				jigSuite.Reset((volatile uint32_t *)JTAG_0_BASE);
				logger.Reset();
				IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, (1 << 16)); // setting JTAG to output (port 0)
				IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x04); // yellow one
				jigSuite.Run();
				jigSuite.Report();
				logger.Stop();
				jigReport.buffer = (uint32_t *)logger.getText();
				jigReport.size = logger.getLength();
				if (jigSuite.getTarget()->dutRunning) {
					printf("Writing report into flash.\n");
					program_flash(jigSuite.getTarget(), &jigReport);
				}
				vTaskDelay(5);
				IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, 0xF4); // turn off
				if (!jigSuite.Passed()) {
					IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x02); // red one
				} else {
					IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x01); // green one
				}
				printf("Writing report into USB.\n");
				writeLog(&logger, "/Usb?/logs/jig_", jigSuite.getDateTime());
				continue;
			}
/*
			if (buttons == 0x02) {
				printf("Middle button pressed. ");
				listenJtag();
			}
*/
			if ((buttons == 0x04) || (ub == (int)'s')) {
				slotSuite.Reset((volatile uint32_t *)JTAG_1_BASE);
				logger.Reset();
				IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x04);
				slotSuite.Run();
				slotSuite.Report();
				logger.Stop();
				slotReport.buffer = (uint32_t *)logger.getText();
				slotReport.size = logger.getLength();
				if (slotSuite.getTarget()->dutRunning) {
					printf("Writing report into flash.\n");
					program_flash(slotSuite.getTarget(), &slotReport);
				}
				vTaskDelay(5);
				IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, 0xF4);
				if (!slotSuite.Passed()) {
					IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x02); // red one
				} else {
					IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x01); // green one
				}
				printf("Writing report into USB.\n");
				writeLog(&logger, "/Usb?/logs/slot_", slotSuite.getDateTime());
			}

			if (ub == (int)'#') {
				prepareSuite.Reset((volatile uint32_t *)JTAG_1_BASE);
				logger.Reset();
				IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x04);
				prepareSuite.Run();
				prepareSuite.Report();
				logger.Stop();
				slotReport.buffer = (uint32_t *)logger.getText();
				slotReport.size = logger.getLength();
				if (prepareSuite.getTarget()->dutRunning) {
					printf("Writing report into flash.\n");
					program_flash(prepareSuite.getTarget(), &slotReport);
				}
				vTaskDelay(5);
				IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, 0xF4);
				if (!prepareSuite.Passed()) {
					IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x02); // red one
				} else {
					IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x01); // green one
				}
				printf("Writing report into USB.\n");
				writeLog(&logger, "/Usb?/logs/prep_", slotSuite.getDateTime());
			}

			if ((buttons == 0x00) && (ub == (int)'c')) {
				checkSuite.Reset((volatile uint32_t *)JTAG_0_BASE);
				logger.Reset();
				IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x04);
				checkSuite.Run();
				checkSuite.Report();
				logger.Stop();
				IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, 0xF4);
				if (!checkSuite.Passed()) {
					IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x02); // red one
				} else {
					IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x01); // green one
				}
			}

			if ((buttons == 0x00) && (ub == (int)'u')) {
				usbSuite.Reset((volatile uint32_t *)JTAG_1_BASE);
				logger.Reset();
				IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x04);
				usbSuite.Run();
				usbSuite.Report();
				logger.Stop();
				IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, 0xF4);
				if (!usbSuite.Passed()) {
					IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x02); // red one
				} else {
					IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x01); // green one
				}
			}

			if ((buttons == 0x00) && (ub == (int)'f')) {
				flashSuite.Reset((volatile uint32_t *)JTAG_1_BASE);
				logger.Reset();
				IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x04);
				flashSuite.Run();
				flashSuite.Report();
				logger.Stop();
				IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, 0xF4);
				if (!flashSuite.Passed()) {
					IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x02); // red one
				} else {
					IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x01); // green one
				}
			}

			if ((buttons == 0x00) && (ub == (int)'D')) {
				setDate();
			} else if ((buttons == 0x00) && (ub == (int)'T')) {
				setTime();
			}
			vTaskDelay(20);

			usage();
		}
		printf("Test has terminated.\n");
		while(1) {
			vTaskDelay(100);
		}
	}
}
