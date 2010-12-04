/*
 * sdcard_manager.cc
 *
 *  Created on: Apr 6, 2010
 *      Author: Gideon
 */
extern "C" {
	#include "itu.h"
	#include "dump_hex.h"
}
#include "sdcard_manager.h"

SdCardManager sd_card_manager;
static void poll_sdcard(Event &e)
{
	sd_card_manager.poll();
}

SdCardManager :: SdCardManager(void)
{
	init();
}

SdCardManager :: ~SdCardManager()
{
	root.children.remove(sd_dev);
	sd_dev->detach();
	delete sd_dev;
	delete sd_card;
}

void SdCardManager :: init()
{
	sd_card = new SdCard; // block device
	sd_dev = new FileDevice(&root, sd_card, "SdCard");
	root.children.append(sd_dev);
	sd_dev->attach();
	poll_list.append(&poll_sdcard);
}

void SdCardManager :: poll()
{
	int	sense = sdio_sense(); // doesn't cost much time to do it every time
	switch(sd_card->get_state()) {
		case e_device_unknown: // boot state
		    if(sense & SD_CARD_DETECT) {
				if(sd_card->init()) { // initialize card
					sd_card->set_state(e_device_error);
				} else { // initial insertion
					sd_card->set_state(e_device_ready);
                    sd_dev->attach_disk(512); // block size
				}
				push_event(e_refresh_browser, &root);
		    } else {
		    	sd_card->set_state(e_device_no_media);
				push_event(e_refresh_browser, &root);
		    }
		    break;

		case e_device_no_media:
		    if(sense & SD_CARD_DETECT) { // insertion
		    	sd_card->set_state(e_device_not_ready);
				push_event(e_refresh_browser, &root);
		    }
		    break;

		case e_device_not_ready:
			wait_ms(100);
			if(sd_card->init()) { // initialize card
				sd_card->set_state(e_device_error);
				push_event(e_refresh_browser, &root);
			} else {
				sd_card->set_state(e_device_ready);
                sd_dev->attach_disk(512); // block size
				push_event(e_refresh_browser, &root);
			}
			break;
		
		case e_device_ready:
			if(!(sense & SD_CARD_DETECT)) { // removal
				push_event(e_invalidate, sd_dev);
				push_event(e_detach_disk, sd_dev);
				sd_card->set_state(e_device_no_media);
				push_event(e_refresh_browser, &root);
			}
			break;

		case e_device_error:
			if(!(sense & SD_CARD_DETECT)) { // removal
				push_event(e_invalidate, sd_dev);
				push_event(e_detach_disk, sd_dev);
				sd_card->set_state(e_device_no_media);
				push_event(e_refresh_browser, &root);
			}
			break;
			
		default:
			sd_card->set_state(e_device_unknown);
	}
}
