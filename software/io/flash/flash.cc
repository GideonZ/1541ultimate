#include "flash.h"
#include "at45_flash.h"
#include "at49_flash.h"
#include "w25q_flash.h"
#include "s25fl_flash.h"
#include "itu.h"
#include <stdio.h>


Flash *get_flash(void)
{
#if RUNS_ON_PC
	return new Flash(); // on PC, return stubbed class
#endif

	IndexedList<Flash*> *types = Flash :: getSupportedFlashTypes();
	for(int i=0; i < types->get_elements(); i++) {
		Flash *f = (*types)[i];
		if (f->tester()) {
			return f;
		}
	}

	return new Flash(); // stubbed base class, just never return 0!
}
