/*
 * test_loader.cc
 *
 *  Created on: Nov 20, 2016
 *      Author: gideon
 */

#include <stdio.h>
#include "system.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "alt_types.h"
#include "dump_hex.h"
#include "iomap.h"
#include "itu.h"
#include "profiler.h"
#include "u2p.h"
#include "usb_base.h"
#include "filemanager.h"
#include "fpll.h"
#include "chargen.h"
#include "screen.h"
#include "rtc.h"
#include "prog_flash.h"
#include "u64_tester.h"
#include "i2c_drv_sockettest.h"
#include "stream_textlog.h"
#include "product.h"

typedef struct {
	const char *fileName;
	const char *romName;
	const uint32_t flashAddress;
	uint32_t *buffer;
	uint32_t  size;
} BinaryImage_t;

#define DOTESTS 1

BinaryImage_t images[] = {
     { "/Usb?/u64/u64.swp",      "FPGA Binary",         0x000000, 0, 0 },
     { "/Usb?/u64/ultimate.app", "Application Binary",  0x290000, 0, 0 },
     { NULL,                     "Flash Filesystem",    0x400000, 0, 0 },
     { "/Usb?/u64/1541.rom",     NULL,                  0x000000, 0, 0 },
     { "/Usb?/u64/1571.rom",     NULL,                  0x000000, 0, 0 },
     { "/Usb?/u64/1581.rom",     NULL,                  0x000000, 0, 0 },
     { "/Usb?/u64/snds1541.bin", NULL,                  0x000000, 0, 0 },
     { "/Usb?/u64/snds1571.bin", NULL,                  0x000000, 0, 0 },
     { "/Usb?/u64/snds1581.bin", NULL,                  0x000000, 0, 0 },
};

#define NUM_IMAGES (sizeof(images) / sizeof(BinaryImage_t))

void wait_button(void)
{
    while(!U64_POWER_REG) {
        vTaskDelay(1);
    }
}

void jump_run(uint32_t a)
{
	void (*function)();
	uint32_t *dp = (uint32_t *)&function;
    *dp = a;
    function();
}
#define ESP_UART_BASE  0xA0000500

#define ESP_UART_DATA  (ESP_UART_BASE + 0x00)
#define ESP_UART_GET   (ESP_UART_BASE + 0x01)
#define ESP_UART_FLAGS (ESP_UART_BASE + 0x02)
#define ESP_UART_ICTRL (ESP_UART_BASE + 0x03)

int esp32_get_byte(int delay)
{
    uint8_t d;

    ioWrite8(ITU_TIMER, 240);
    while(!(ioRead8(ESP_UART_FLAGS) & UART_RxDataAv)) {
        if (!ioRead8(ITU_TIMER)) {
            ioWrite8(ITU_TIMER, 240);
            delay --;
            if (delay <= 0) {
                return -2;
            }
        }
    }
    d = ioRead8(ESP_UART_DATA);
    ioWrite8(ESP_UART_GET, 0);
    return (int)d;
}

char *esp32_get_string(char *buffer, int buflen)
{
    buffer[buflen - 1] = 0;
    for(int i=0;i<buflen-1;i++) {
        int c = esp32_get_byte(100);
        if ((c == -2) || (c == 0x0a)) {
            buffer[i] = 0;
            break;
        }
        if (c == 0x0d) {
            continue;
        }
        buffer[i] = c;
    }
    return buffer;
}

void esp32_put_byte(uint8_t c)
{
    while(ioRead8(ESP_UART_FLAGS) & UART_TxFifoFull)
        ;
    ioWrite8(ESP_UART_DATA, c);
}

int load_file(BinaryImage_t *flashFile)
{
	FRESULT fres;
	File *file;
	uint32_t transferred;

	if (!flashFile->fileName) {
	    return 0; // not to be loaded but created locally
	}
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
    printf("Successfully read %s.\n", flashFile->fileName);
    fm->fclose(file);
    return 0;
}

Screen *screen;
StreamTextLog textLog(96*1024);

#define CHARGEN_BASE      0xA0040000
#define CHARGEN_TIMING    (CHARGEN_BASE + 0x0000)
#define CHARGEN_REGISTERS (CHARGEN_BASE + 0x4000)
#define CHARGEN_SCREEN    (CHARGEN_BASE + 0x5000)
#define CHARGEN_COLOR     (CHARGEN_BASE + 0x6000)

extern "C" {
    static void screen_outbyte(int c) {
        screen->output(c);
        textLog.charout(c);
    }
}

// -- A0040000 is chargen base
// Mapping:
// 0000 : timing
// 4000 : chargen registers
// 5000 : screen
// 6000 : color ram

int test_esp32(void)
{
    char buffer[256];
    const char message1[] = "rst:0x1 (POWERON_RESET),boot:0x3 (DOWNLOAD_BOOT";
    const char message2[] = "rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLA";
    for(int i=0;i<16;i++) {
        ioWrite8(ESP_UART_GET, 0);
    }

    // First boot in bootloader mode
//    screen->clear();
//    screen->move_cursor(0,0);
    printf("\033\037Listening to ESP32 BOOT...");
    U64_WIFI_CONTROL = 2;
    vTaskDelay(150);
    U64_WIFI_CONTROL = 7;


    int j = 0;
    for (int i=0;i<200000;i++) {
        if (ioRead8(ESP_UART_FLAGS) & UART_RxDataAv) {
            uint8_t c = ioRead8(ESP_UART_DATA);
            ioWrite8(ESP_UART_GET, 0);
            buffer[j++] = c;
            // screen->output(c);
            if (j == 120) {
                break;
            }
        }
    }
    buffer[j] = 0;
    int ok1 = -1;
    for(int i=0;((i<50) && (i<j));i++) {
        if ((buffer[i] == 'r') && (buffer[i+1] == 's')) {
            ok1 = strncmp(&buffer[i], message1, strlen(message1));
            break;
        }
    }
    //printf("#%d#\n", j);
    if (ok1) {
        printf("\033\022FAIL. '%s'\n", buffer);
    } else {
        printf("\033\025OK!\n");
    }

    printf("\033\037Listening to ESP32 RUN...");

    // Then boot in normal mode:
    U64_WIFI_CONTROL = 0;
    vTaskDelay(50);
    U64_WIFI_CONTROL = 5;

    j = 0;
    for (int i=0;i<200000;i++) {
        if (ioRead8(ESP_UART_FLAGS) & UART_RxDataAv) {
            uint8_t c = ioRead8(ESP_UART_DATA);
            ioWrite8(ESP_UART_GET, 0);
            // screen->output(c);
            buffer[j++] = c;
            // screen->output(c);
            if (j == 120) {
                break;
            }
        }
    }
    buffer[j] = 0;
    int ok2 = -1;
    for(int i=0;((i<50) && (i<j));i++) {
        if ((buffer[i] == 'r') && (buffer[i+1] == 's')) {
            ok2 = strncmp(&buffer[i], message2, strlen(message2));
            break;
        }
    }
    //printf("#%d#\n", j);
    if (ok2) {
        printf("\033\022FAIL. %s\n", buffer);
    } else {
        printf("\033\025OK!\n");
    }
    if ((!ok1) && (!ok2)) {
        return 0;
    }
    return 1;
}

int test_memory(void)
{
    uint16_t *random = new uint16_t[4096];

    uint16_t seed = 0x1B7F;
    for(int i=0; i < 4096; i++) {
        if (seed & 0x8000) {
            seed = (seed << 1) ^ 0x8004;
        } else {
            seed = (seed << 1) | 1;
        }
        random[i] = seed;
    }

    uint32_t dest = 0;
    uint16_t *src = random;
    int retval = 0;

    while (dest < (64*1024*1024)) {
        memcpy((void *)dest, src, 256);
        printf(">");
        src += 132;
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
        memcpy(verifyBuffer, (void *)dest, 256);
        printf("<");

        if(memcmp(verifyBuffer, src, 256) != 0) {
            printf("Verify failure. RAM error at address %p.\n", dest);
            dump_hex_verify(src, verifyBuffer, 256);
            retval = -3;
            break;
        }

        src += 132;
        if (!dest) {
            dest = 0x100;
        } else {
            dest <<= 1;
        }
    }
    printf("\n");
    return retval;
}

void do_update(void)
{
/*
    char time_buffer[32];
    printf("\n%s ", rtc.get_long_date(time_buffer, 32));
    printf("%s\n", rtc.get_time_string(time_buffer, 32));
*/

    Flash *flash2 = get_flash();
    printf("\033\024Detected Flash: %s\n", flash2->get_type_string());
    const char *fpgaType = (getFpgaCapabilities() & CAPAB_FPGA_TYPE) ? "5CEBA4" : "5CEBA2";
    printf("Detected FPGA Type: %s.\nBoard Revision: %s\n\033\037\n", fpgaType, getBoardRevision());

    uint32_t test;
    flash2->read_linear_addr(0x1000, 4, &test);
    if (test != 0xFFFFFFFF) {
        printf("Press power button to start programming.\n");
        wait_button();
    }

    flash2->protect_disable();

    for(int i=0;i<NUM_IMAGES;i++) {
        if (!images[i].romName) { // skip those with no name
            continue;
        }
        if (! flash_buffer_length(flash2, screen, images[i].flashAddress, false, images[i].buffer, images[i].size, "?", images[i].romName)) {
            printf("\033\022ERROR!\n\n");
            while(1)
                ;
        }
    }
    printf("\nClearing Configuration Area");
    int cfg_pages = flash2->get_number_of_config_pages();
    for(int i=0; i<cfg_pages; i++) {
        flash2->clear_config_page(i);
        printf(".");
    }
    printf("\nConfiguring Flash write protection..\n");
    flash2->protect_configure();
    flash2->protect_enable();
    printf("Done!                            \n");
}

#define CHARGEN_REGS  ((volatile t_chargen_registers *)CHARGEN_REGISTERS)

void initScreen()
{
    TVideoMode mode = { 01, 25175000,  640, 16,  96,  48, 0,   480, 10, 2, 33, 0, 1, 0 };  // VGA 60
    SetScanModeRegisters((volatile t_video_timing_regs *)CHARGEN_TIMING, &mode);

    CHARGEN_REGS->TRANSPARENCY     = 0;
    CHARGEN_REGS->CHAR_WIDTH       = 8;
    CHARGEN_REGS->CHAR_HEIGHT      = 0x80 | 16;
    CHARGEN_REGS->CHARS_PER_LINE   = 79;
    CHARGEN_REGS->ACTIVE_LINES     = 29;
    CHARGEN_REGS->X_ON_HI          = 0;
    CHARGEN_REGS->X_ON_LO          = 0;
    CHARGEN_REGS->Y_ON_HI          = 0;
    CHARGEN_REGS->Y_ON_LO          = 8;
    CHARGEN_REGS->POINTER_HI       = 0;
    CHARGEN_REGS->POINTER_LO       = 0;
    CHARGEN_REGS->PERFORM_SYNC     = 0;
    CHARGEN_REGS->TRANSPARENCY     = 0x84;

    screen = new Screen_MemMappedCharMatrix((char *)CHARGEN_SCREEN, (char *)CHARGEN_COLOR, 79, 29);
    screen->clear();
    custom_outbyte = screen_outbyte;
    printf("Screen Initialized.\n");
}

#define ROMS_DIRECTORY  "/prep/roms"
#define CARTS_DIRECTORY "/prep/carts"

int prepare_flashdisk_pre(uint8_t *mem, uint32_t mem_size);
int prepare_flashdisk_used(uint32_t mem_size);

static void create_dir(const char *name)
{
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->create_dir(name);
    printf("Creating '%s': %s\n", name, FileSystem :: get_error_string(fres));
}

static FRESULT write_file(const char *name, uint8_t *data, int length)
{
    File *f;
    uint32_t dummy;
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->fopen(ROMS_DIRECTORY, name, FA_CREATE_ALWAYS | FA_WRITE, &f);
    if (fres == FR_OK) {
        fres = f->write(data, length, &dummy);
        printf("Writing %s to /prep: %s\n", name, FileSystem :: get_error_string(fres));
        fm->fclose(f);
    }
    if (fres != FR_OK) {
        printf("Failed to write essentials. Abort!\n");
        while(1)
            ;
    }
    return fres;
}

void copy_roms(void)
{
    create_dir(ROMS_DIRECTORY);
    create_dir(CARTS_DIRECTORY);
    for(int i=3; i<NUM_IMAGES; i++) {
        write_file(images[i].fileName + 10, (uint8_t*)images[i].buffer, (int)images[i].size); // +10 = snoop "/Usb?/u64/"
    }
}

int load_images(void)
{
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

    for(int i=0;i<NUM_IMAGES;i++) {
        if(load_file(&images[i])) {
            printf("\033\022Could not load image. Did not flash.\n");
            return -1;
        }
    }

    const uint32_t flashDiskSize = 0x3E8000;
    uint8_t *flashDiskImage = new uint8_t[flashDiskSize]; // see w25q_flash.cc
    if (prepare_flashdisk_pre(flashDiskImage, flashDiskSize)) {
        printf("\e2Error preparing flash image.\n");
        return -1;
    }
    copy_roms();
    int flashDiskUsed = prepare_flashdisk_used(flashDiskSize);
    if (flashDiskUsed > 0) {
        images[2].fileName = "FlashDisk";
        images[2].buffer = (uint32_t *)flashDiskImage;
        images[2].size = (flashDiskUsed << 12);
    }

    return 0;
}

/*
#define PLD_WR_CTRL1      (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 0x12))
#define PLD_WR_CTRL2      (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 0x13))
#define PLD_JOYSWAP       (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 0x112))
#define PLD_RD_CTRL       (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 0x13)) // not implemented in version < 16
#define PLD_RD_VERSION    (*(volatile uint8_t *)(U64TESTER_PLD_BASE + 0x113))
 */

void read_socket_analog(I2C_Driver_SocketTest& i2c, int& vdd, int& vcc, int& mid)
{
    uint8_t buf[4];
    i2c.read_results(0xC8, buf);
    vdd = (301 * (int)buf[0]) >> 2;
    vcc = 32 * (int)buf[1];
    mid = 32 * (int)buf[2];
}

void read_caps(volatile socket_tester_t *test, int &cap1, int &cap2)
{
    test->capsel = 0;
    wait_ms(5);
    cap1 = (370 * (int)test->capval) / 3 - 616;
    test->capsel = 1;
    wait_ms(5);
    cap2 = (370 * (int)test->capval) / 3 - 616;
}

int test_socket_voltages(I2C_Driver_SocketTest& i2c, volatile uint8_t *ctrl, uint8_t ctrlbyte, int low, int high, bool shunt)
{
    if (ctrl) {
        *ctrl  = ctrlbyte;
    }
    wait_ms(200);
    int vdd, vcc, mid, error = 0;
    read_socket_analog(i2c, vdd, vcc, mid);

    // 4% range for VCC: 0.96*5000 < vcc < 1.04*5000
    if ((vcc < 4800) || (vcc > 5200)) {
        printf("\e2VCC Out Of Range: %d mV  (should be 5V)\n", vcc);
        error |= (1 << 0);
    }

    if ((vdd < low) || (vdd > high)) {
        printf("\e2VDD Out Of Range: %d mV  (should be between %d and %d mV)\n", vdd, low, high);
        error |= (1 << 1);
    }

    if (shunt) {
        vdd *= 68;
        vdd /= 100;
    }
    if (abs(mid * 2 - vdd) > 250) {
        printf("\e2Mid level Out Of Range: %d (Expected %d mV)\n", mid, vdd / 2);
        error |= (1 << 2);
    }
    return error;
}

int test_socket_caps(volatile socket_tester_t *test, volatile uint8_t *ctrl, uint8_t ctrlbyte, int low, int high, int expected)
{
    int cap1, cap2;
    int error = 0;
    if(ctrl) {
        *ctrl  = ctrlbyte;
    }
    wait_ms(50);
    // should be in 22 nF mode now. 5-7% caps
    read_caps(test, cap1, cap2);
    if ((cap1 < low) || (cap1 > high)) {
        printf("\e2CAP1 out of range: %d pF, expected (%d-%d-%d) pF\n", cap1, low, expected, high);
        error |= (1 << 0);
    }
    if ((cap2 < low) || (cap2 > high)) {
        printf("\e2CAP2 out of range: %d pF, expected (%d-%d-%d) pF\n", cap2, low, expected, high);
        error |= (1 << 1);
    }
    return error;
}

int socket_test(volatile socket_tester_t *test, volatile uint8_t *ctrl, uint8_t magic, bool elite)
{
//    PLD_WR_CTRL1 = 0xBF; // all on = 12V, 22 nF, 1K
//    PLD_WR_CTRL2 = 0x5F; // all on = 12V, 22 nF, 1K
    int error = 0;

    if (test->id != 0x34) {
        printf("\e2Socket Tester not found (0x%b != 0x34)\n", test->id);
        return -1;
    }

    // bit 3: Capsel (1 = 470 pF)
    // bit 2: 1K shunt
    // bit 1: Regulator enable
    // bit 0: regulator select (1 = 12V, 0 = 9V)
    I2C_Driver_SocketTest i2c(test);

    // Setup byte: 1.101.0000 (unipolar, internal clock, reference always on)
    // Configuration byte: 0.00.0011.1
    if (i2c.i2c_write_byte(0xC8, 0xD0, 0x07) < 0) {
        printf("\e2Unable to access I2C ADC on tester\n");
        return -2;
    }

    if (elite) {
    	error |= test_socket_voltages(i2c, ctrl, magic | 0,  4800,  5200, false);
        error |= test_socket_voltages(i2c, ctrl, magic | 2,  8640,  9560, false) << 3;
        error |= test_socket_voltages(i2c, ctrl, magic | 3, 11600, 13000, false) << 6;
        error |= test_socket_voltages(i2c, ctrl, magic | 7, 11600, 13000, true) << 9;
        error |= test_socket_caps(test, ctrl, magic | 7, 19500, 25000, 22470) << 12;
        error |= test_socket_caps(test, ctrl, magic | 15, 200, 900, 470) << 14;
    } else {
        error |= test_socket_voltages(i2c, 0, 0, 11600, 12800, true);
        error |= test_socket_caps(test, 0, 0, 200, 900, 470) << 3;
        printf("\e?Place all jumpers for socket %d and press power button.\n", (test == SOCKET1)?1:2);
        wait_button();
        error |= test_socket_voltages(i2c, ctrl, magic | 2,  8640,  9400, true) << 8;
        error |= test_socket_caps(test, 0, 0, 19500, 25000, 22470) << 11;
    }

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

int TestSidSockets(bool elite)
{
    LOCAL_CART = 0x20; // Reset PLD in socket!
    vTaskDelay(10);
    LOCAL_CART = 0x00; // release reset for PLD. This should enable the oscillator

    int error = 0;
    if(socket_test(SOCKET1, &PLD_WR_CTRL2, 0x50, elite) == 0) {
        printf("\e5SID Socket 1 passed.\n");
    } else {
        printf("\e2SID Socket 1 FAILED!\n");
        error = 1;
    }

    if(socket_test(SOCKET2, &PLD_WR_CTRL1, 0xB0, elite) == 0) {
        printf("\e5SID Socket 2 passed.\n");
    } else {
        printf("\e2SID Socket 2 FAILED!\n");
        error = 1;
    }
    return error;
}

void write_log(void)
{
    FRESULT fres;
    File *file;
    uint32_t transferred;
    uint8_t s[8];
    char fn[40];

    FileManager *fm = FileManager :: getFileManager();
    Flash *flash = get_flash();
    flash->read_serial(s);

    for (int attempt = 0; attempt < 10; attempt++) {
        sprintf(fn, "/Usb?/logs/%b%b%b%b%b%b%b%b-%d", s[7], s[6], s[5], s[4], s[3], s[2], s[1], s[0], attempt);

        fres = fm->fopen(fn, FA_CREATE_NEW | FA_WRITE, &file);
        if (fres == FR_OK) {
            printf("\e6Saving %s\n", fn);
            fres = file->write(textLog.getText(), textLog.getLength(), &transferred);
            if (fres == FR_OK) {
                fm->fclose(file);
                break;
            }
        }
    }
}

extern "C" {
    void codec_init(void);

    void main_task(void *context)
	{
	    initScreen();
	    printf("Ultimate-64 - LOADER...\n");
        codec_init();
        bool elite = isEliteBoard();
        bool advanced_joy = elite;
        int errors = 0, joy = 0;
	    if (!test_memory()) {
	        if (!load_images()) {
	            screen->clear();
	            screen->move_cursor(0,0);
#if DOTESTS
                printf("\e4U64 Tester - 26.05.2022 - 14:48\e?\n");
                errors =  test_esp32();
	            errors += U64AudioCodecTest();
                errors += U64PaddleTest();
                errors += U64TestIEC();
                errors += U64TestCartridge();
                errors += U64TestCassette();
                if (!rtc.is_valid()) {
                    errors ++;
                    printf("\e2RTC not valid. Battery inserted?\n\eO");
                }
	            if (elite) {
	                joy = U64EliteTestJoystick();
	                if (joy < 0) {
	                    advanced_joy = false;
	                } else {
	                    errors += joy;
	                }
	            }
	            if (!advanced_joy) {
	                printf("\e?Remove joystick tester boards and press power button.\n");
	                wait_button();
	            }
                errors += U64TestKeyboard();
                errors += U64TestUserPort();
                errors += TestSidSockets(elite);

                if (errors) {
                    printf("\n\e2** BOARD FAILED **\n");
                    write_log();
                } else {
                    printf("\n\e5-> Passed!\n\e?");
                    write_log();
                    do_update();
                }
#else
                printf("\e4U64 Programmer - 26.05.2022 - 12:44\e?\n");
                printf("\n\e5Tests skipped.\n\e?");
                do_update();
#endif
	        }
	    }
        printf("\n\n\033\023Press power button to turn off the machine..\n");
        wait_button();
        while(1) {
            U64_POWER_REG = 0x2B;
            wait_ms(1);
            U64_POWER_REG = 0xB2;
            wait_ms(1);
        }
	}
}

