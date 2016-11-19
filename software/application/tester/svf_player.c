#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

//version variables
int ver_major = 1;
int ver_minor = 4;

volatile uint32_t *host;

#define MAX_STRING_LENGTH (1024*4)
char rbuffer[MAX_STRING_LENGTH];
char command[MAX_STRING_LENGTH];

//possible JTAG TAP states
#define TAP_STATE_TLR 0
#define TAP_STATE_RTI 1
#define TAP_STATE_SELECT_DR_SCAN 2
#define TAP_STATE_SELECT_IR_SCAN 3
#define TAP_STATE_CAPTURE_DR 4
#define TAP_STATE_CAPTURE_IR 5
#define TAP_STATE_SHIFT_DR 6
#define TAP_STATE_SHIFT_IR 7
#define TAP_STATE_EXIT_1_DR 8
#define TAP_STATE_EXIT_1_IR 9
#define TAP_STATE_PAUSE_DR 10
#define TAP_STATE_PAUSE_IR 11
#define TAP_STATE_EXIT_2_DR 12
#define TAP_STATE_EXIT_2_IR 13
#define TAP_STATE_UPDATE_DR 14
#define TAP_STATE_UPDATE_IR 15

int g_def_end_state_dr = TAP_STATE_RTI;
int g_def_end_state_ir = TAP_STATE_RTI;
int g_least_runtest_state = TAP_STATE_RTI;
int g_tap_state = TAP_STATE_RTI;

void send_32_tdi_bits(unsigned int value)
{
	host[1] = value;
}
#define JTAG_LAST   0x40000000
#define JTAG_TMSSEL 0x80000000
#define JTAG_MAXRUN 16384

void send_tdi_bits(int clocks, unsigned int value, int last)
{
	if ((clocks == 32) && (!last)) {
		host[1] = value;
	} else {
		printf("TDI %d => %08x => ", clocks, value);
		int remain = clocks;
		while(remain > 0) {
			unsigned int now = (remain > 16) ? 16 : (unsigned int)remain;
			remain -= now;
			int now_last = (remain ? 0 : last);
			host[0] = (((unsigned int)(now - 1)) << 16) | (value & 0xFFFF) | (now_last ? JTAG_LAST : 0);
			value >>= 16;
		}
		uint32_t readback = host[0];
		printf("%08x\n", readback);
	}
}
// 10.110.100

int send_tdi_bits_cmp(int clocks, unsigned int value, unsigned int check, unsigned int mask, int last)
{
	int err = 0;

	if ((clocks == 32) && (!last)) {
		host[1] = value;
		uint32_t readback = host[0];
		printf("TDI %d => %08x => %08x\n", clocks, value, readback);
		if ((readback & mask) != check)
			return 1;
		else
			return 0;
	}

	int remain = clocks;
	while(remain > 0) {
		unsigned int now = (remain > 16) ? 16 : (unsigned int)remain;
		remain -= now;
		int now_last = (remain ? 0 : last);
		host[0] = (((unsigned int)(now - 1)) << 16) | (value & 0xFFFF) | (now_last ? JTAG_LAST : 0);
		uint32_t readback = host[0] & 0xFFFF;
		printf("TDI %d => %08x (chk: %08x | %08x) Got: %08x\n", now, value, check, mask, readback);
		if ((readback & mask & 0xFFFF) != (check & 0xFFFF)) {
			err++;
		}
		value >>= 16;
		mask >>= 16;
		check >>= 16;
	}
	return err;

}

void send_tms_bits(int clocks, unsigned int value)
{
	if (clocks > 16) {
		printf("Error: Not supported sending more than 16 bits of TMS data.\n");
		return;
	}
	host[0] = (((unsigned int)(clocks - 1)) << 16) | JTAG_TMSSEL | (value & 0xFFFF);
}

void send_tms_zeros(int clocks)
{
	uint32_t value = (((unsigned int)(clocks - 1)) << 16) | JTAG_TMSSEL;
	printf("TMS Zeros: %08x\n", value);
	host[0] = value;
}

// Navigage TMS to desired state with no TDO read back
void tms_command(unsigned char num_shifts, unsigned char pattern, int target_tap_state) {
	g_tap_state = target_tap_state;
	printf("TMS %d => %02x (going to state %d)\n", num_shifts, pattern, target_tap_state);
	send_tms_bits((int)num_shifts, (unsigned int)pattern);
}

void tap_navigate(int start_state, int end_state) {
	if (start_state == TAP_STATE_PAUSE_IR && end_state == TAP_STATE_RTI)
		tms_command(3, 3, TAP_STATE_RTI);
	else if (start_state == TAP_STATE_PAUSE_DR && end_state == TAP_STATE_RTI)
		tms_command(3, 3, TAP_STATE_RTI);
	else
		printf("Not implemented TAP navigate sequence %d %d\n", start_state,
				end_state);
}

void print_buffer(char* name, unsigned char* pbuffer, int len) {
	int i;
	int num = len;
	if (num > 256)
		num = 256;
	printf("%s\n", name);
	for (i = 0; i < num; i++) {
		printf(" %02X", pbuffer[i]);
		if (i > 0 && (i & 0xFF) == 0)
			printf("\n");
	}
	printf("\n");
}

//////////////////////////////////////////////////////////////////////////////
// SVF functions
//////////////////////////////////////////////////////////////////////////////

//go to Test Logic Reset state then to Idle
void goto_tlr() {
	// Navigage TMS to Test-Logic-Reset (TMS=1 more then 4 times) then Idle (TMS=0 once)
	// // Data is shifted LSB first, so: 011111 >>> chip
	tms_command(6, 0x1f, TAP_STATE_TLR);
}

//current state is Idle and stay Idle for some clocks
void Idle() {
	// Idle (TMS=0 any times)
	// Data is shifted LSB first, so the TMS pattern is 000000 >>> chip
	if (g_tap_state != TAP_STATE_RTI) {
		tms_command(6, 0x1F, TAP_STATE_RTI);
	}
	tms_command(6, 0, TAP_STATE_RTI);
}

//define default end state for DR scan operation
//possible commands are:
//ENDDR IRPAUSE;
//ENDDR IDLE;
//ENDDR RESET;
void do_ENDDR() {
	//use simplified "one char" command recognition
	//is it OK?
	if (rbuffer[7] == 'R') {
		//default is PAUSE
		g_def_end_state_dr = TAP_STATE_PAUSE_DR;
	} else if (rbuffer[7] == 'D') {
		//default is IDLE
		g_def_end_state_dr = TAP_STATE_RTI;
	} else if (rbuffer[7] == 'E') {
		//default is RESET
		g_def_end_state_dr = TAP_STATE_TLR;
	} else {
		//oops....
		printf("unknown ENDDR command parameter in line: %s", rbuffer);
	}
}

//define default end state for IR scan operation
//possible commands are:
//ENDIR IRPAUSE;
//ENDIR IDLE;
//ENDIR RESET;
void do_ENDIR() {
	//use simplified "one char" command recognition
	//is it OK?
	if (rbuffer[7] == 'R') {
		//default is PAUSE
		g_def_end_state_ir = TAP_STATE_PAUSE_IR;
	} else if (rbuffer[7] == 'D') {
		//default is IDLE
		g_def_end_state_ir = TAP_STATE_RTI;
	} else if (rbuffer[7] == 'E') {
		//default is RESET
		g_def_end_state_ir = TAP_STATE_TLR;
	} else {
		//oops....
		printf("unknown ENDIR command parameter in line: %s", rbuffer);
	}
}

int g_num_tdo_errors = 0;
int sir(int nclk, unsigned int val) {

	if (nclk == 0 || nclk > 32) {
		printf("error SIR parameters\n");
		return 0;
	}

	//go to idle if not idle now
	if (g_tap_state != TAP_STATE_RTI) {
		tap_navigate(g_tap_state, TAP_STATE_RTI);
	}

	//from Idle enter to state where we SHIFT IR
	tms_command(4, 0x03, TAP_STATE_SHIFT_IR);

	send_tdi_bits(nclk, val, 1);

	//exit from Exit1-IR state depends on target state (defined by ENDIR command)
	//also this should sent least bit from command
	if (g_def_end_state_ir == TAP_STATE_RTI)
		tms_command(2, 0x01, TAP_STATE_RTI); //go to idle
	else if (g_def_end_state_ir == TAP_STATE_PAUSE_IR)
		tms_command(1, 0x00, TAP_STATE_PAUSE_IR); //go to pause
	else if (g_def_end_state_ir == TAP_STATE_TLR)
		tms_command(5, 0x1D, TAP_STATE_TLR); //go to reset '11101 1

	return 1;
}

int runidle(int num) {
	int num_ = num;
	int block_size;

	while (num_ > JTAG_MAXRUN) {
		send_tms_zeros(JTAG_MAXRUN);
		num_ -= JTAG_MAXRUN;
	}
	if (num_) {
		send_tms_zeros(num_);
	}
	return 1;
}

//process typical strings:
//SIR 10 TDI (203);
int do_SIR() {
	//get command parameters
	unsigned int sir_arg = 0;
	unsigned int tdi_arg = 0;
	int n = sscanf(rbuffer, "SIR %d TDI (%X);", &sir_arg, &tdi_arg);
	if (n != 2) {
		printf("error processing SIR\n");
		return 0;
	}
	return sir(sir_arg, tdi_arg);
}

//////////////////////////////////////////////////////////////////////////////
// memory alloc/free functions
//////////////////////////////////////////////////////////////////////////////

//global pointers to sdr tdi, tdo, mask and smask arrays
//we expect that dr data can be very long, but we do not know exact size
//size of arrays allocated will be defined during SVF strings parcing
unsigned char* psdr_tdi_data = NULL;
unsigned char* psdr_tdo_data = NULL;
unsigned char* psdr_mask_data = NULL;
unsigned char* psdr_smask_data = NULL;
unsigned int sdr_data_size = 0; //current size of arrays
unsigned int sdr_tdi_sz;
unsigned int sdr_tdo_sz;
unsigned int sdr_mask_sz;
unsigned int sdr_smask_sz;

//allocate arrays for sdr tdi, tdo and mask
//return 1 if ok or 0 if fail
unsigned int alloc_sdr_data(unsigned int size) {
	//compare new size with size of already allocated buffers
	if (sdr_data_size >= size)
		return 1; //ok, because already allocated enough

	//we need to allocate memory for arrays
	//but first free previously allocated buffers

	//tdi
	if (psdr_tdi_data) {
		free(psdr_tdi_data);
		psdr_tdi_data = NULL;
	}

	//tdo
	if (psdr_tdo_data) {
		free(psdr_tdo_data);
		psdr_tdo_data = NULL;
	}

	//mask
	if (psdr_mask_data) {
		free(psdr_mask_data);
		psdr_mask_data = NULL;
	}

	//smask
	if (psdr_smask_data) {
		free(psdr_smask_data);
		psdr_smask_data = NULL;
	}

	psdr_tdi_data = (unsigned char*) malloc(size);
	if (psdr_tdi_data == NULL) {
		printf("error allocating sdr tdi buffer\n");
		return (0);
	}

	psdr_tdo_data = (unsigned char*) malloc(size);
	if (psdr_tdo_data == NULL) {
		free(psdr_tdi_data);
		psdr_tdi_data = NULL;
		printf("error allocating sdr tdo buffer\n");
		return (0);
	}

	psdr_mask_data = (unsigned char*) malloc(size);
	if (psdr_mask_data == NULL) {
		free(psdr_tdi_data);
		psdr_tdi_data = NULL;
		psdr_tdo_data = NULL;
		printf("error allocating sdr mask buffer\n");
		return (0);
	}

	psdr_smask_data = (unsigned char*) malloc(size);
	if (psdr_smask_data == NULL) {
		free(psdr_tdi_data);
		free(psdr_tdo_data);
		free(psdr_mask_data);
		psdr_tdi_data = NULL;
		psdr_tdo_data = NULL;
		psdr_mask_data = NULL;
		printf("error allocating sdr smask buffer\n");
		return (0);
	}

	//remember that we have allocated some size memory
	sdr_data_size = size;

	//we have successfully allocated buffers for sdr data!
	return 1;
}

//free buffers allocated for sdr tdi, tdo, mask
void free_sdr_data() {
	if (psdr_tdi_data)
		free(psdr_tdi_data);
	if (psdr_tdo_data)
		free(psdr_tdo_data);
	if (psdr_mask_data)
		free(psdr_mask_data);
	if (psdr_smask_data)
		free(psdr_smask_data);
	sdr_data_size = 0;
}

int sdr_nbits(unsigned int nbits, unsigned int has_tdo, unsigned int has_mask,
		unsigned int has_smask) {

	unsigned char val_o, val_i, val_m, m;
	unsigned char cmd_mask;
	unsigned int num_bytes;
	unsigned int offset, i, j;

	if (nbits < 1)
		return 0;

#ifdef DUMP
	if (nbits > 10000) {
		FILE *fo;
		fo = fopen("dump.bin", "wb");
		if (fo) {
			fwrite(psdr_tdi_data, 1, (nbits+7)/8, fo);
			fclose(fo);
		}
	}
#endif


	if (g_tap_state == TAP_STATE_PAUSE_IR) {
		//from pause IR go to state where we can shift DR
		tms_command(5, 0x07, TAP_STATE_SHIFT_DR);
	} else {
		//go to idle if not idle now
		if (g_tap_state != TAP_STATE_RTI) {
			tap_navigate(g_tap_state, TAP_STATE_RTI);
		}

		//from Idle go to state where we can shift DR
		tms_command(3, 0x01, TAP_STATE_SHIFT_DR);
	}

	//calc "offset" - index of least thetrade from array of tdi data
	offset = sdr_tdi_sz - 1;

	printf("Send SDR nbits %u bits (%p), offset: %u, check %u, mask: %u\n", nbits, psdr_tdi_data, offset, has_tdo, has_mask);

	unsigned int *tdi, *tdo, *mask, *smask;
	tdi  = (unsigned int *) psdr_tdi_data;
	tdo  = (unsigned int *) psdr_tdo_data;
	mask = (unsigned int *) psdr_mask_data;
	smask = (unsigned int *)psdr_smask_data;
	int err = 0;

	if (has_mask) {
		while(nbits > 32) {
			nbits -= 32;
			err += send_tdi_bits_cmp(32, *(tdi++), *(tdo++), *(mask++), 0);
		}
		err += send_tdi_bits_cmp(nbits, *(tdi++), *(tdo++), *(mask++), 1);
	} else if(has_tdo) {
		while(nbits > 32) {
			nbits -= 32;
			err += send_tdi_bits_cmp(32, *(tdi++), *(tdo++), 0xFFFFFFFF, 0);
		}
		err += send_tdi_bits_cmp(nbits, *(tdi++), *(tdo++), 0xFFFFFFFF, 1);
	} else {
		while(nbits > 32) {
			nbits -= 32;
			send_32_tdi_bits(*(tdi++));
		}
		send_tdi_bits(nbits, *(tdi++), 1);
	}
	if(err) {
		printf("Compare failed.\n");
	}

	//exit from Exit1-DR state depends on target state (defined by ENDDR command)
	if (g_def_end_state_dr == TAP_STATE_RTI)
		tms_command(2, 0x01, TAP_STATE_RTI);
	else if (g_def_end_state_dr == TAP_STATE_PAUSE_DR)
		tms_command(1, 0x00, TAP_STATE_PAUSE_DR);
	else if (g_def_end_state_dr == TAP_STATE_TLR)
		tms_command(4, 0x0f, TAP_STATE_TLR);
	else
		printf("Error: Unknown target state after SDR\n");
	return 0;
}

//////////////////////////////////////////////////////////////////////////////
//string syntax parcing functions
//////////////////////////////////////////////////////////////////////////////

//return hex number in range 0-0xf or error 0xff if char is not a hexadecimal digit
unsigned char char2hex(unsigned char c) {
	unsigned char v;
	if (c >= '0' && c <= '9')
		v = c - '0';
	else if (c >= 'a' && c <= 'f')
		v = c - 'a' + 10;
	else if (c >= 'A' && c <= 'F')
		v = c - 'A' + 10;
	else
		v = 0xff; //not a hex
	return v;
}

//skip leading space or tab chars in string
unsigned char* skip_space(unsigned char* ptr) {
	while (1) {
		unsigned char c = *ptr;
		if (c == ' ' || c == 9) {
			ptr++;
			continue;
		} else
			break;
	}
	return ptr;
}

//skip from string word which we expect to see
unsigned char* skip_expected_word(unsigned char* ptr, unsigned char* pword,
		unsigned int* presult) {
	//assume error result
	*presult = 0;
	ptr = skip_space(ptr);
	while (1) {
		unsigned char c1, c2;
		c2 = *pword;
		if (c2 == 0) {
			//end of comparing word
			*presult = 1;
			return ptr;
		}
		c1 = *ptr;
		if (c1 == c2) {
			//at this point strings are equal, but compare next chars
			ptr++;
			pword++;
			continue;
		} else
			break; //string has no expected word
	}
	return ptr;
}

//get word from string
unsigned char* get_and_skip_word(unsigned char* ptr, unsigned char* pword,
		unsigned int* presult) {
	int i;
	unsigned char c;
	*presult = 0;

	//assume error result
	*presult = 0;
	ptr = skip_space(ptr);
	for (i = 0; i < 8; i++) {
		c = *ptr;
		if (c == ' ' || c == 9 || c == 0 || c == 0xd || c == 0xa) {
			//end of getting word
			*pword = 0;
			*presult = 1;
			return ptr;
		}
		*pword++ = c;
		ptr++;
	}
	*pword = 0;
	return ptr;
}

//read decimal integer from string
//returned 0xffffffff mean error
unsigned char* get_dec_integer(unsigned char* ptr, unsigned int* presult) {
	unsigned int r = 0;
	ptr = skip_space(ptr);
	while (1) {
		unsigned char c;
		c = *ptr;
		if (c >= '0' && c <= '9') {
			r = r * 10 + (c - '0');
			ptr++;
			continue;
		} else if (c == ' ' || c == 9) {
			*presult = r;
			break;
		} else {
			//unexpected char
			*presult = 0xffffffff;
			break;
		}
	}
	return ptr;
}

// Precondition: text pointer is at the '(' character
// Return value: pointer to char after ).

char *read_hex_array(char *text, unsigned int num_bits, unsigned int *dest)
{
	char *start = text;

	// first find the closing )
	while(*text != ')') {
		if (!(*text)) {
			return NULL; // no trailing ) found
		}
		text ++;
	}
	char *retval = text+1;

	*text = 0;
	text--;

	// now walk backwards and collect 8 nibbles into a word, put the word in the array
	unsigned char c;
	unsigned int val = 0;
	int pos = 0;

	while (num_bits > 0) {
		c = (text == start) ? '0' : (unsigned char)*(text--);
		c = char2hex(c);
		if (c != 0xFF) {
			val |= (((uint32_t)c) << pos);
			pos += 4;
			num_bits -= 4;
			if (pos == 32) {
				*(dest++) = val;
				val = 0;
				pos = 0;
			}
		}
	}
	if (pos > 0) {
		*(dest++) = val;
	}

	return retval;
}

/*
 process typical strings:
 SDR 16 TDI (FFFF) TDO (C0C7) MASK (FFFF);
 SDR 16 TDI (FFFF) TDO (2027);
 SDR 13 TDI (0000);
 return 1 on success, 0 is fail
 note that arguments can be very long and take several lines in a source file
 */
char *do_SDR(char *text) {
	int i, j, k;
	int has_tdi, has_tdo, has_mask, has_smask;
	unsigned int r;
	unsigned char b;
	unsigned char word[16];
	unsigned char* pdst;
	unsigned int num_bits = 0;
	unsigned int num_bytes;
	unsigned char* pdest;
	unsigned int* pdest_count;
	unsigned char* ptr = (unsigned char*) text;

	//at begin of string we expect to see word "SDR"
	ptr = skip_expected_word(ptr, (unsigned char*) "SDR", &r);
	if (r == 0) {
		printf("syntax error for SDR command\n");
		return 0;
	}

	//now we expect to get decimal number of bits for shift
	ptr = get_dec_integer(ptr, &num_bits);
	if (num_bits == 0xffffffff) {
		printf("syntax error for SDR command, cannot get number of bits\n");
		return 0;
	}

	//how many bytes? calculate space required and allocate
	num_bytes = ((num_bits + 31) / 32) << 2;
	if (alloc_sdr_data(num_bytes + 8) == 0) {
		printf("error on SDR command\n");
		return 0;
	}

	//we expect some words like TDI, TDO, MASK or SMASK here
	//order of words can be different
	has_tdi = 0;
	has_tdo = 0;
	has_mask = 0;
	has_smask = 0;
	while (1) {
		ptr = skip_space(ptr);
		ptr = get_and_skip_word(ptr, word, &r);
		if (r == 0) {
			printf("syntax error for SDR command, cannot fetch parameter word\n");
			return 0;
		}

		//analyze words
		if (strcmp((char*) word, "TDI") == 0) {
			has_tdi = 1;
			pdest = psdr_tdi_data;
			pdest_count = &sdr_tdi_sz;
		} else if (strcmp((char*) word, "TDO") == 0) {
			has_tdo = 1;
			pdest = psdr_tdo_data;
			pdest_count = &sdr_tdo_sz;
		} else if (strcmp((char*) word, "MASK") == 0) {
			has_mask = 1;
			pdest = psdr_mask_data;
			pdest_count = &sdr_mask_sz;
		} else if (strcmp((char*) word, "SMASK") == 0) {
			has_smask = 1;
			pdest = psdr_smask_data;
			pdest_count = &sdr_smask_sz;
		} else if (strcmp((char*) word, ";") == 0) {
			//end of string!
			//send bitstream to jtag
			sdr_nbits(num_bits, has_tdo, has_mask, has_smask);
			break;
		} else {
			printf("syntax error for SDR command, unknown parameter word\n");
			return 0;
		}

		//parameter should be in parentheses
		ptr = skip_expected_word(ptr, (unsigned char*) "(", &r);
		if (r == 0) {
			printf("syntax error for SDR command, expected char ( after TDI word\n");
			return 0;
		}
		//now expect to read hexadecimal array of tdi data
		ptr = read_hex_array(ptr-1, num_bits, (unsigned int*)pdest);
	}
	return ptr;
}

/*
 process typical strings:
 RUNTEST 53 TCK;
 */
void do_RUNTEST() {
	int expected_state = -1;

	//get command parameters
	unsigned int tck = 0;
	int n = sscanf(rbuffer, "RUNTEST %d TCK;", &tck);
	if (n == 1) {
		//1 param
		expected_state = g_least_runtest_state;
	} else {
		n = sscanf(rbuffer, "RUNTEST IDLE %d TCK", &tck);
		if (n)
			expected_state = TAP_STATE_RTI;
		else {
			n = sscanf(rbuffer, "RUNTEST DRPAUSE %d TCK", &tck);
			if (n)
				expected_state = TAP_STATE_PAUSE_DR;
			else {
				n = sscanf(rbuffer, "RUNTEST IRPAUSE %d TCK", &tck);
				if (n)
					expected_state = TAP_STATE_PAUSE_IR;
				else {
					printf("error processing RUNTEST\n");
				}
			}
		}
	}

	//check current TAP state aganst expected
	if (g_tap_state != expected_state) {
		g_least_runtest_state = expected_state;
		tap_navigate(g_tap_state, expected_state);
	}

	if (tck)
		runidle(tck);
}

/*
 process typical strings:
 STATE IDLE;
 */
void do_STATE() {
	Idle();
}

//reset JTAG
void do_TRST() {
	goto_tlr();
}

void do_FREQUENCY()
{
	printf("WARNING: Frequency set was ignored\n");
}

//////////////////////////////////////////////////////////////////////////////
// SVF PLAYER IS HERE
//////////////////////////////////////////////////////////////////////////////

int play_svf(char *text, volatile uint32_t *jtag)
{
	//read and process SVF file
	char* pstr = text;

	host = jtag;

	while (1) {
		int len;

		if (!(*pstr)) {
			break;
		}

		while((*pstr) && ((*pstr == 0x0A) || (*pstr == 0x0D))) {
			pstr++;
		}

		if (pstr[0] == '!' /* Altera Quartus marks SVF comment lines with '!' char */
		|| (pstr[0] == '/' && pstr[1] == '/') /*Xilinx marks comment lines with '//' chars*/
		) {
			//line is commented
			while(*pstr != 0x0A) {
				if (!(*pstr))
					break;
				pstr++;
			}
			pstr++;
			continue;
		}

		//analyze 1st word
		len = 0;
		char *s = pstr;
		while((len < MAX_STRING_LENGTH) && isalpha(*s)) {
			command[len++] = *(s++);
		}
		command[len] = 0;

		if (!len)
			continue;

		//real command is here
		if (strcmp(command, "SDR") == 0) {
			pstr = do_SDR(pstr);
		} else {
			len = 0;
			while((len < MAX_STRING_LENGTH) && (*pstr != ';')) {
				rbuffer[len++] = *(pstr++);
			}
			rbuffer[len] = 0;
			while((*pstr) && (*pstr != 0x0A) && (*pstr != 0x0D)) {
				pstr++;
			}
			while((*pstr) && ((*pstr == 0x0A) || (*pstr == 0x0D))) {
				pstr++;
			}

			printf("'%s'\n", rbuffer);

			if (strcmp(command, "SIR") == 0) {
				if (do_SIR() == 0) {
					//probably error on check answer
					return 0;
				}
			}
			else if (strcmp(command, "RUNTEST") == 0)
				do_RUNTEST();
			else if (strcmp(command, "STATE") == 0)
				do_STATE();
			else if (strcmp(command, "TRST") == 0)
				do_TRST();
			else if (strcmp(command, "ENDDR") == 0)
				do_ENDDR();
			else if (strcmp(command, "ENDIR") == 0)
				do_ENDIR();
			else if (strcmp(command, "FREQUENCY") == 0)
				do_FREQUENCY();
			else if (strcmp(command, "HDR") == 0) {
			} else if (strcmp(command, "TDR") == 0) {
			} else if (strcmp(command, "HIR") == 0) {
			} else if (strcmp(command, "TIR") == 0) {
			} else {
				printf("Unknown command in line: %s\n", rbuffer);
				return 1;
			}
		}
	}

	free_sdr_data();
	return 1;
}

#ifdef DUMP
int main(int argc, char **argv)
{
	volatile uint32_t jtag[2];

	FILE *fi = fopen(argv[1], "rb");
	if (!fi)
		return -1;
	fseek(fi, 0, SEEK_END);
	size_t size = ftell(fi);
	printf("Size: %ld\n", size);
	fseek(fi, 0, SEEK_SET);
	char *buffer = malloc(size + 2);
	memset(buffer, 0, size + 2);
	fread(buffer, 1, size, fi);
	fclose(fi);

	play_svf(buffer, jtag);

	free(buffer);
}
#endif
