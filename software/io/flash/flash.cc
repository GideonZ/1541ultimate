#include "flash.h"
#include "at45_flash.h"
#include "at49_flash.h"
#include "w25q_flash.h"
#include "itu.h"

Flash *get_flash(void)
{
	// we test all known flashes here..
	// Because we don't have a 'new' in the 1st boot, we simply
	// instatiate all possible flash classes and return the pointer to the right one..
	Flash *ret_val = NULL;

	ret_val = at45_flash.tester();
	if(!ret_val) {
		ret_val = w25q_flash.tester();
	}
	if(!ret_val) {
		ret_val = at49_flash.tester();
	}
	return ret_val;
}

//Flash *flash = get_flash();

