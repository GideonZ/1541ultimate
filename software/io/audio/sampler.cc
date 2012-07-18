extern "C" {
    #include "itu.h"
}
#include "sampler.h"

Sampler sampler; // global

static void poll_sampler(Event &e)
{
	sampler.poll(e);
}

Sampler :: Sampler()
{
    if(ITU_CAPABILITIES & CAPAB_SAMPLER) {
		main_menu_objects.append(this);
        poll_list.append(&poll_sampler);
    }
}

Sampler :: ~Sampler()
{
	main_menu_objects.remove(this);
    poll_list.remove(&poll_sampler);
}
                        
int Sampler :: fetch_task_items(IndexedList<PathObject*> &item_list)
{
    item_list.append(new ObjectMenuItem(this, "Play 8bit REU sample", MENU_SAMP_PLAY8B));
    item_list.append(new ObjectMenuItem(this, "Play 16bit REU sample", MENU_SAMP_PLAY16B));
    return 2;
}

void Sampler :: poll(Event &e)
{
	if(e.type != e_object_private_cmd) {
		if(e.object == this) {
			switch(e.param) {
                case MENU_SAMP_PLAY8B:
                    for(int i=0;i<8;i++) {
                        VOICE_CONTROL(i) = 0;
                    }
                    VOICE_START(0)   = 0x01000000L;
                    VOICE_LENGTH(0)  = 0x00800000L;
                    VOICE_RATE(0)    = 195;
                    VOICE_CONTROL(0) = (VOICE_CTRL_ENABLE | VOICE_CTRL_8BIT);
                    break;
                case MENU_SAMP_PLAY16B:
                    for(int i=0;i<8;i++) {
                        VOICE_CONTROL(i) = 0;
                    }
                    VOICE_CONTROL(1) = 0;
                    VOICE_START(1)   = 0x01001000L;
                    VOICE_LENGTH(1)  = 0x00F00000L;
                    VOICE_RATE(1)    = 194;
                    VOICE_CONTROL(1) = (VOICE_CTRL_ENABLE | VOICE_CTRL_16BIT);
                    wait_ms(5000);
                    VOICE_RATE(1)    = 142;
                    break;
				default:
					break;
			}
		}
	}
}
