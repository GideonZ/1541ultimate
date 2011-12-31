/*************************************************************/
/* Tape Recorder Control                                     */
/*************************************************************/
extern "C" {
    #include "itu.h"
}

#include "copper.h"
#include "userinterface.h"

Copper copper; // this global causes us to run!!

#define COPPER_MENU_MEASURE     0x6301
#define COPPER_MENU_SYNC        0x6302
#define COPPER_MENU_BREAK       0x6303
#define COPPER_MENU_TIMED_WRITE 0x6304
#define COPPER_MENU_WRITE_STATE 0x6305
#define COPPER_MENU_SWEEP       0x6306

static void poll_copper(Event &e)
{
	copper.poll(e);
}

Copper :: Copper()
{
    poll_list.append(&poll_copper);
    if(CAPABILITIES & CAPAB_COPPER) {
        printf("** COPPER ENABLED **\n");
        main_menu_objects.append(this);
    }
    synchronized = false;
}

Copper :: ~Copper()
{
	poll_list.remove(&poll_copper);
	main_menu_objects.remove(this);
}
	
int  Copper :: fetch_task_items(IndexedList<PathObject*> &item_list)
{
	item_list.append(new ObjectMenuItem(this, "Copper Measure Frame", COPPER_MENU_MEASURE));
	item_list.append(new ObjectMenuItem(this, "Copper Sync", COPPER_MENU_SYNC));
	item_list.append(new ObjectMenuItem(this, "Copper Break", COPPER_MENU_BREAK));
	item_list.append(new ObjectMenuItem(this, "Copper Timed Write", COPPER_MENU_TIMED_WRITE));
	item_list.append(new ObjectMenuItem(this, "Copper Write State", COPPER_MENU_WRITE_STATE));
	item_list.append(new ObjectMenuItem(this, "Copper Sweep Parameter", COPPER_MENU_SWEEP));
	return 6;
}

void Copper :: poll(Event &e)
{
	if(e.type == e_object_private_cmd) {
		if(e.object == this) {
			switch(e.param) {
				case COPPER_MENU_MEASURE:
					measure();
					break;
				case COPPER_MENU_SYNC:
                    sync();
					break;
                case COPPER_MENU_BREAK:
                    COPPER_BREAK = 1;
                    break;
                case COPPER_MENU_TIMED_WRITE:
                    timed_write();
                    break;
                case COPPER_MENU_WRITE_STATE:
                    write_state();
                    break;
                case COPPER_MENU_SWEEP:
                    sweep();
                    break;
				default:
					break;
			}
		}
	}
	if(e.type == e_copper_capture) {
	    if(synchronized) {
            capture();
	    } else {
	        printf("Sorry, we're not synchronized, so we can't do a capture.\n");
	    }
    }
}

/*
#define COPCODE_WRITE_REG   0x00
#define COPCODE_READ_REG    0x40
#define COPCODE_WAIT_IRQ    0x81
#define COPCODE_WAIT_SYNC   0x82
#define COPCODE_TIMER_CLR   0x83
#define COPCODE_CAPTURE     0x84
#define COPCODE_WAIT_FOR    0x85
#define COPCODE_WAIT_UNTIL  0x86
#define COPCODE_REPEAT      0x87
#define COPCODE_END         0x88
*/

void Copper :: measure()
{
    static BYTE copper_list[] = { // init
                           COPCODE_WRITE_REG + 0x1A, 0x01,
                           COPCODE_WRITE_REG + 0x12, 0x00,
                           COPCODE_WRITE_REG + 0x11, 0x1B,
                           // clear pending interrupts
                           COPCODE_WRITE_REG + 0x19, 0xFF,
                           // actual measure
                           COPCODE_WAIT_IRQ,
                           COPCODE_TIMER_CLR,
                           COPCODE_WRITE_REG + 0x19, 0x01,
                           COPCODE_WAIT_IRQ,
                           COPCODE_CAPTURE,
                           COPCODE_WRITE_REG + 0x19, 0x01,
                           // disable raster irq
                           COPCODE_WRITE_REG + 0x1A, 0x00,
                           COPCODE_END };

    // disable CIA interrupts
    CIA1_ICR = 0x7F;
    printf("Clearing CIA1 interrupt: %b\n", CIA1_ICR);
    
    COPPER_FRAMELEN_H = 0xAB; // far away
    
    // len = 19
    for(int i=0;i<19;i++) {
        COPPER_RAM(i) = copper_list[i];
    }

    wait_ms(200);
    printf("Start!\n");
    
    // start!
    COPPER_COMMAND = COPPER_CMD_PLAY;
    
    while(COPPER_STATUS & 0x01)
        ;

    DWORD cycles = (DWORD(COPPER_MEASURE_H) << 8) | COPPER_MEASURE_L;
    
    printf("Cycles measured: %d.\n", cycles);
    COPPER_FRAMELEN_H = COPPER_MEASURE_H;
    COPPER_FRAMELEN_L = COPPER_MEASURE_L;
}

void Copper :: sync()
{
    static BYTE copper_list[] = { // init
                           COPCODE_WRITE_REG + 0x1A, 0x01,
                           COPCODE_WRITE_REG + 0x12, 0x01,
                           COPCODE_WRITE_REG + 0x11, 0x1B,
                           // clear pending interrupts
                           COPCODE_WRITE_REG + 0x19, 0xFF,
                           // actual measure
                           COPCODE_WAIT_IRQ,
                           COPCODE_TIMER_CLR,
                           COPCODE_END };

    measure();

    // len = 11
    for(int i=0;i<11;i++) {
        COPPER_RAM(i) = copper_list[i];
    }

    printf("Start!\n");
    
    // start!
    COPPER_COMMAND = COPPER_CMD_PLAY;
    
    while(COPPER_STATUS & 0x01)
        ;

    printf("Copper is now in sync; do not clear timer.\n");    
    synchronized = true;
}

int atoi(char *s)
{
    if(*s == '-')
        return -atoi(s+1);
    int res = 0;
    while(*s) {
        if((*s >= '0')&&(*s <= '9')) {
            res *= 10;
            res += (int)(*s - '0');
        } else {
            break;
        }
        s++;
    }
    return res;            
}

void Copper :: capture(void)
{
    COPPER_COMMAND = COPPER_CMD_RECORD;

    while(COPPER_STATUS & 0x01)
        ;
    
    BYTE b1,b2,b3,b4;
    WORD w1;
    int line, cycle;
    for(int i=0;i<COPPER_RAM_SIZE;i+=4) {
        b1 = COPPER_RAM(i);
        b2 = COPPER_RAM(i+1);
        b3 = COPPER_RAM(i+2);
        b4 = COPPER_RAM(i+3);
        if(b2 == 0xFF)
            break;
        w1 = (WORD(b2) << 8) | b1;
        line = 1 + (int(w1) / 63);
        cycle = 1 + (int(w1) % 63);               
        printf("Line %3d Cycle %2d: %b > $D0%b\n", line, cycle, b4, b3);
    }
}

void Copper :: timed_write(void)
{
    static char cycle_string[8];
    static char reg_string[8];
    static char before_string[8];
    static char after_string[8];

    static BYTE copper_list[] = { // init
                           COPCODE_WAIT_SYNC,
                           COPCODE_TRIGGER_2,
                           COPCODE_WAIT_UNTIL, 0xE8, 0x03,
                           COPCODE_WRITE_REG + 0x20, 0x02,
                           COPCODE_TRIGGER_1,
                           COPCODE_WAIT_FOR, 0x05,
                           COPCODE_WRITE_REG + 0x20, 0x00,
                           COPCODE_TRIGGER_1,
                           COPCODE_REPEAT };

    if(! user_interface->string_box("Enter cycle #", cycle_string, 8))
        return;

    if(! user_interface->string_box("Register #", reg_string, 8))
        return;

    if(! user_interface->string_box("Value to store", before_string, 8))
        return;

    if(! user_interface->string_box("Value after", after_string, 8))
        return;

    int cycle = atoi(cycle_string);
    int reg = atoi(reg_string);
    int before = atoi(before_string);
    int after = atoi(after_string);

    VIC_REG(0x20) = 14;
    VIC_REG(0x21) = 6;

    copper_list[3] = BYTE(cycle & 0xFF);
    copper_list[4] = BYTE(cycle >> 8);
    copper_list[5] = BYTE(reg);
    copper_list[6] = BYTE(before);
    copper_list[10] = BYTE(reg);
    copper_list[11] = BYTE(after);
     
    COPPER_BREAK = 1;

    // len = 14
    for(int i=0;i<14;i++) {
        COPPER_RAM(i) = copper_list[i];
        printf("%b ", copper_list[i]);
    }
    printf("\n");

    COPPER_COMMAND = COPPER_CMD_PLAY;

    printf("Copper is now running.. %b\n", COPPER_STATUS);
}

void Copper :: write_state(void)
{
    UINT written;
    PathObject *po = user_interface->get_path();
    File *f = root.fcreate("image.vic", po);
    if(f) {
        printf("Opened file successfully.\n");
        if(!c64->write_vic_state(f)) {
            printf("Write unsuccessful.\n");
        } else {
            f->write((BYTE *)&COPPER_RAM(0), COPPER_RAM_SIZE, &written);
        }
        f->close();
    } else {
        printf("Couldn't open file..\n");
    }
}

void Copper :: sweep(void)
{
    static BYTE copper_list[] = { // init
       COPCODE_WAIT_SYNC, // 0
       COPCODE_WAIT_UNTIL, 0x0F, 0x0C, // 1, 2, 3
//       COPCODE_WRITE_REG + 0x21, 0x0D,
//       COPCODE_WRITE_REG + 0x21, 0x00,
//       COPCODE_WAIT_FOR, 61, // two cycles already used
       COPCODE_WRITE_REG + 0x21, 0x06,
       COPCODE_WRITE_REG + 0x18, 0x02,
       COPCODE_WRITE_REG + 0x18, 0x12,
       COPCODE_WRITE_REG + 0x21, 0x07,
       COPCODE_WRITE_REG + 0x21, 0x00,
       COPCODE_WRITE_REG + 0x20, 0x07,
       COPCODE_WRITE_REG + 0x20, 0x0B,
       COPCODE_REPEAT };

    C64_POKE(1024, 18);
//    for(int i=0;i<40;i++) {
//        C64_POKE(0xD800+i, 0x0e);
//    }

    Keyboard *keyb = c64->get_keyboard();
    
    COPPER_BREAK = 1;

    // len = 14
    for(int i=0;i<40;i++) {
        COPPER_RAM(i) = copper_list[i];
        printf("%b ", copper_list[i]);
    }
    printf("\n");

    COPPER_COMMAND = COPPER_CMD_PLAY;

    BYTE a, b, c;
    int cycle = 3495;
    do {
        c = keyb->getch();
        switch(c) {
            case 0x1D:
                cycle ++;
                break;
            case 0x9D:
                cycle --;
                break;
            case 0x11:
                cycle += 63;
                break;
            case 0x91:
                cycle -= 63;
                break;
            default:
                break;
        }
        if(c != 0) {
            a = BYTE(cycle & 0xFF);
            b = BYTE(cycle >> 8);
            COPPER_RAM(3) = b;
            COPPER_RAM(2) = a;
            printf("Line: %d. Cycle: %d.\n", 1+(cycle/63), 1+(cycle%63));
        }
    } while(c != 0x03);

    COPPER_BREAK = 1;
    C64_POKE(0xD011, 0x1B);
    C64_POKE(0xD016, 0x08);
}
