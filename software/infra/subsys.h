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

#include "http_codes.h"

class SubsysCommand;
class UserInterface;

#define SUBSYSID_USER_STANDARD   0
#define SUBSYSID_C64             1
#define SUBSYSID_DRIVE_A         2
#define SUBSYSID_DRIVE_B         3
#define SUBSYSID_DRIVE_C         4
#define SUBSYSID_TAPE_PLAYER     5
#define SUBSYSID_TAPE_RECORDER   6
#define SUBSYSID_IEC             7
#define SUBSYSID_CMD_IF			 8
#define SUBSYSID_U64			 9

#define SORT_ORDER_CREATE  10
#define SORT_ORDER_C64     20
#define SORT_ORDER_DRIVES  30
#define SORT_ORDER_SOFTIEC 40
#define SORT_ORDER_TAPE    50
#define SORT_ORDER_PRINTER 60
#define SORT_ORDER_CONFIG  70
#define SORT_ORDER_DEVELOPER 999

typedef enum {
	SSRET_OK = 0,
	SSRET_GENERIC_ERROR,
	SSRET_SUBSYS_NOT_PRESENT,
	SSRET_SUBSYS_NO_EXEC,
	SSRET_NO_LOCK,
	SSRET_DISK_MODIFIED,
	SSRET_NO_DRIVE_ROM,
	SSRET_INVALID_DRIVE_ROM,
	SSRET_ONLY_1541,
	SSRET_WRONG_DRIVE_TYPE,
	SSRET_CANNOT_OPEN_FILE,
	SSRET_WRONG_MOUNT_MODE,
	SSRET_UNDEFINED_COMMAND,
	SSRET_ABORTED_BY_USER,
	SSRET_SAVE_FAILED,
} SubsysResultCode_t;


class SubSystem  // implements function "executeCommand"
{
	int myID;
	SemaphoreHandle_t myMutex;
	friend class SubsysCommand;

	virtual SubsysResultCode_t executeCommand(SubsysCommand *cmd) { return SSRET_SUBSYS_NO_EXEC; }
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
    // Only used in Stream Menu - an obsolete ASC-II menu that was once available on UART. It directly connects to an action
	SubsysCommand(UserInterface *ui, Action *act, const char *p, const char *fn) :
		user_interface(ui),
		subsysID(act->subsys),
		functionID(act->function),
		mode(act->mode),
		direct_call(act->func),
		actionName(act->getName()),
		path(p), filename(fn) {
		buffer = NULL;
		bufferSize = 0;
	}

	// The most common form of Subsys command; one that is called just about anywhere; with a reference to a path and file
	SubsysCommand(UserInterface *ui, int subID, int funcID, int mode, const char *p, const char *fn) :
		user_interface(ui),
		subsysID(subID),
		functionID(funcID),
		mode(mode),
		direct_call(0),
		actionName(""),
		path(p), filename(fn) {
		buffer = NULL;
		bufferSize = 0;
	}

	// A variant of the one above, but then using a pointer to a buffer. This should be obsoleted.
	SubsysCommand(UserInterface *ui, int subID, int funcID, int mode, void *buffer, int bufferSize) :
		user_interface(ui),
		subsysID(subID),
		functionID(funcID),
		mode(mode),
		direct_call(0),
        actionName(""),
		path(""), filename(""),
		buffer(buffer),
		bufferSize(bufferSize) {
	}

	SubsysResultCode_t execute(void) {
		SubsysResultCode_t retval = SSRET_SUBSYS_NOT_PRESENT;
		if(direct_call) {
			retval = (SubsysResultCode_t)direct_call(this);
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
					retval = SSRET_NO_LOCK;
				}
			}
		}
		delete this;
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

    static const char *error_string(SubsysResultCode_t resultCode)
    {
        static const char *error_strings[] = {
            "All Okay",        
            "Generic Error",    
            "SubSystem does not exist", 
            "SubSystem does not implement command executer",
            "Could not obtain lock of subsystem",   
            "Disk has been modified, save first",    
            "Drive ROM not found",       
            "Drive ROM is invalid",
            "This hardware only supports 1541", 
            "Drive is in the wrong mode",
            "Cannot open file",
            "Illegal mount mode / drive type",
            "Undefined subsystem command",
			"Operation aborted by user",
			"Save failed",
        };
        return error_strings[(int)resultCode];
    }

    static int http_response_map(SubsysResultCode_t resultCode)
    {
        static const int codes[] = {
            HTTP_OK, // "All Okay",        
            HTTP_INTERNAL_SERVER_ERROR, // "Generic Error (Unspecified!)",    
            HTTP_SERVICE_UNAVAILABLE, // "SubSystem does not exist", 
            HTTP_METHOD_NOT_ALLOWED, // "SubSystem does not implement command executer",
            HTTP_LOCKED, // "Could not obtain lock of subsystem",   
            HTTP_FORBIDDEN, // "Disk has been modified, save first",    
            HTTP_PRECONDITION_FAILED, // "Drive ROM not found",       
            HTTP_PRECONDITION_FAILED, // "Drive ROM is invalid",
            HTTP_METHOD_NOT_ALLOWED, // "This hardware only supports 1541", 
            HTTP_UNSUPPORTED_MEDIA_TYPE, // "Drive is in the wrong mode",
            HTTP_NOT_FOUND, // "Cannot open file",
            HTTP_INTERNAL_SERVER_ERROR, // "Illegal mount mode / drive type",
            HTTP_INTERNAL_SERVER_ERROR, // "Undefined subsystem command",
			HTTP_INTERNAL_SERVER_ERROR, // "Aborted by user" <-- should not happen from HTTP
			HTTP_FAILED_DEPENDENCY, // "Save failed", not sure what went wrong, but the save was unsuccessful
        }; 
        return codes[(int)resultCode];
    }

    UserInterface *user_interface;
	int            subsysID;
	int			   functionID;
	int 		   mode;
	actionFunction_t direct_call;
	mstring        path;
	mstring		   filename;
	mstring        actionName;
	void 		  *buffer;
	int            bufferSize;
};

#endif /* INFRA_SUBSYS_H_ */
