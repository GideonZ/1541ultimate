/*
 * subsys_command.h
 *
 *  Created on: Jun 18, 2015
 *      Author: Gideon
 */

#ifndef INFRA_SUBSYS_H_
#define INFRA_SUBSYS_H_

#include "mystring.h"
#include "action.h"
#include "managed_array.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdio.h>

class SubsysCommand;
class UserInterface;

#define SUBSYSID_USER_STANDARD   0
#define SUBSYSID_C64             1
#define SUBSYSID_DRIVE_A         2
#define SUBSYSID_DRIVE_B         3
#define SUBSYSID_TAPE_PLAYER     4
#define SUBSYSID_TAPE_RECORDER   5
#define SUBSYSID_IEC             6
#define SUBSYSID_CMD_IF			 7
#define SUBSYSID_U64			 8

class SubSystem  // implements function "executeCommand"
{
	int myID;
	SemaphoreHandle_t myMutex;
	friend class SubsysCommand;

	virtual int executeCommand(SubsysCommand *cmd) { return -2; }
public:
	SubSystem(int id) {
		myID = id;
		myMutex = xSemaphoreCreateMutex();
		SubSystem :: getSubSystems() -> set(myID, this);
	}
	virtual ~SubSystem() {
		SubSystem :: getSubSystems() -> unset(myID);
		vSemaphoreDelete(myMutex);
	}

	static ManagedArray<SubSystem *>* getSubSystems() {
		static ManagedArray<SubSystem *> subsystem_array(16, NULL);
		return &subsystem_array;
	}

	virtual const char *identify(void) { return "Unknown Subsystem"; }

	int  lock(const char *name) {
		int retval = xSemaphoreTake(myMutex, 5000);
		if (!retval) {
			printf("Lock: %s not available.\n", name);
		}
		return retval;
	}
	void unlock() {
		xSemaphoreGive(myMutex);
	}

	int getID() { return myID; }
};

struct SubsysResult
{
	int      resultCode;
	int		 resultDataSize;
	uint8_t *resultData;
};

class SubsysCommand
{
public:
	SubsysCommand(UserInterface *ui, Action *act, const char *p, const char *fn) :
		user_interface(ui),
		subsysID(act->subsys),
		functionID(act->function),
		mode(act->mode),
		direct_call(act->func),
		path(p), filename(fn) {
		buffer = NULL;
		bufferSize = 0;
	}

	SubsysCommand(UserInterface *ui, int subID, int funcID, int mode, const char *p, const char *fn) :
		user_interface(ui),
		subsysID(subID),
		functionID(funcID),
		mode(mode),
		direct_call(0),
		path(p), filename(fn) {
		buffer = NULL;
		bufferSize = 0;
	}

	SubsysCommand(UserInterface *ui, int subID, int funcID, int mode, void *buffer, int bufferSize) :
		user_interface(ui),
		subsysID(subID),
		functionID(funcID),
		mode(mode),
		direct_call(0),
		path(""), filename(""),
		buffer(buffer),
		bufferSize(bufferSize) {
	}

	int execute(void) {
		int retval = -5;
		if(direct_call) {
			retval = direct_call(this);
		} else {
		    SubSystem *subsys;    // filled in by factory
			subsys = (*SubSystem :: getSubSystems())[subsysID];
			if (subsys) {
				printf("About to execute a command in subsys %s (%p)\n", subsys->identify(), subsys->myMutex);
				if (xSemaphoreTake(subsys->myMutex, 1000)) {
					retval = subsys->executeCommand(this);
					//puts("before give");
					xSemaphoreGive(subsys->myMutex);
					//puts("after give");
				} else {
					printf("Could not get lock on %s. Command not executed.\n", subsys->identify());
				}
			}
		}
		delete this;
		printf("Command executed. Returning %d.\n", retval);
		return retval;
	}

	void print(void) {
		printf("SubsysCommand for system %d:\n", subsysID);
		printf("  Function ID: %d\n", functionID);
		printf("  Mode: %d\n", mode);
		printf("  Path: %s\n", path.c_str());
		printf("  Filename: %s\n", filename.c_str());
		printf("  Buffer = %p (size: %d)\n", buffer, bufferSize);
	}

	UserInterface *user_interface;
	int            subsysID;
	int			   functionID;
	int 		   mode;
	actionFunction_t direct_call;
	mstring        path;
	mstring		   filename;
	void 		  *buffer;
	int            bufferSize;
};

#endif /* INFRA_SUBSYS_H_ */
