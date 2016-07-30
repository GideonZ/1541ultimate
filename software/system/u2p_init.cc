/*
 * u2p_init.cc
 *
 *  Created on: May 8, 2016
 *      Author: gideon
 */

#include "u2p_init.h"
#include <stdio.h>

#include "system.h"
#include "u2p.h"

U2P_Init hardwareInitializer;

U2P_Init :: U2P_Init()
{
	puts("** U2+ NiosII System **");
	REMOTE_FLASHSEL_1;
    REMOTE_FLASHSELCK_0;
    REMOTE_FLASHSELCK_1;
}

extern "C" {
void _exit(int a)
{
	while(1)
		;
}
}
