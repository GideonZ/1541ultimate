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
