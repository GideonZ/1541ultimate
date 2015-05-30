#include "itu.h"
#include "sampler.h"

Sampler sampler; // global

static void poll_sampler(Event &e)
{
	sampler.poll(e);
}

Sampler :: Sampler()
{
    if(getFpgaCapabilities() & CAPAB_SAMPLER) {
        MainLoop :: addPollFunction(poll_sampler);
    }
}

Sampler :: ~Sampler()
{
    MainLoop :: removePollFunction(poll_sampler);
}
                        
int Sampler :: fetch_task_items(IndexedList<Action *> &item_list)
{
    if(getFpgaCapabilities() & CAPAB_SAMPLER) {
		item_list.append(new ObjectMenuItem(this, "Play 8bit REU sample", MENU_SAMP_PLAY8B));
		item_list.append(new ObjectMenuItem(this, "Play 16bit REU sample", MENU_SAMP_PLAY16B));
		return 2;
    }
    return 0;
}

void Sampler :: poll(Event &e)
{
	if(e.type != e_object_private_cmd) {
		if(e.object == this) {
			switch(e.param) {
                case MENU_SAMP_PLAY8B:
                    for(int i=0;i<8;i++) {
                        ioWrite8(VOICE_CONTROL(i), 0);
                    }
                    ioWrite32(VOICE_START(0),  0x01000000L);
                    ioWrite32(VOICE_LENGTH(0), 0x00800000L);
                    ioWrite32(VOICE_RATE(0),   195);
                    ioWrite8(VOICE_CONTROL(0), (VOICE_CTRL_ENABLE | VOICE_CTRL_8BIT));
                    break;
                case MENU_SAMP_PLAY16B:
                    for(int i=0;i<8;i++) {
                        ioWrite8(VOICE_CONTROL(i), 0);
                    }
                    ioWrite8(VOICE_CONTROL(1), 0);
                    ioWrite32(VOICE_START(1), 0x01001000L);
                    ioWrite32(VOICE_LENGTH(1),0x00F00000L);
                    ioWrite32(VOICE_RATE(1),  194);
                    ioWrite8(VOICE_CONTROL(1), (VOICE_CTRL_ENABLE | VOICE_CTRL_16BIT));
                    wait_ms(5000);
                    ioWrite32(VOICE_RATE(1), 142);
                    break;
				default:
					break;
			}
		}
	}
}
