/******************************************************************************
*                                                                             *
* License Agreement                                                           *
*                                                                             *
* Copyright (c) 2004 Altera Corporation, San Jose, California, USA.           *
* All rights reserved.                                                        *
*                                                                             *
* Permission is hereby granted, free of charge, to any person obtaining a     *
* copy of this software and associated documentation files (the "Software"),  *
* to deal in the Software without restriction, including without limitation   *
* the rights to use, copy, modify, merge, publish, distribute, sublicense,    *
* and/or sell copies of the Software, and to permit persons to whom the       *
* Software is furnished to do so, subject to the following conditions:        *
*                                                                             *
* The above copyright notice and this permission notice shall be included in  *
* all copies or substantial portions of the Software.                         *
*                                                                             *
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR  *
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,    *
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE *
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER      *
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING     *
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER         *
* DEALINGS IN THE SOFTWARE.                                                   *
*                                                                             *
* This agreement shall be governed in all respects by the laws of the State   *
* of California and by the laws of the United States of America.              *
*                                                                             *
* Altera does not recommend, suggest or require that this reference design    *
* file be used in conjunction or combination with any other product.          *
******************************************************************************/

#include "system.h"

/*
 * These are the malloc lock/unlock stubs required by newlib. These are
 * used to make accesses to the heap thread safe. Note that
 * this implementation requires that the heap is never manipulated
 * by an interrupt service routine.
 */

#include <reent.h>

#include "FreeRTOS.h"
#include "projdefs.h"
#include "semphr.h"
#include "task.h"

void __malloc_lock ( struct _reent *_r )
{
	if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED)
		return;

	vTaskSuspendAll();

	return;
}

/* __malloc_unlock needs to provide recursive mutex unlocking */

void __malloc_unlock ( struct _reent *_r )
{
	if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED)
		return;

	xTaskResumeAll();
}
